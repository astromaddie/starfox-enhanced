#include "starfox/vr/vulkan_scene_textures.hpp"
#include "starfox/vr/backdrop_texture.hpp"
#include <cstring>
#include <stdexcept>
namespace starfox::vr {
namespace {
template<class T> T entry(PFN_vkGetDeviceProcAddr get,VkDevice device,const char* name) {
    auto fn=reinterpret_cast<T>(get(device,name));
    if(!fn) throw std::runtime_error(std::string("Missing Vulkan entry point: ")+name);
    return fn;
}
void check(VkResult result,const char* name) {if(result!=VK_SUCCESS) throw std::runtime_error(std::string(name)+": "+std::to_string(result));}
}
VulkanSceneTextures::~VulkanSceneTextures() {close();}
void VulkanSceneTextures::close() noexcept {
    if(!device_) return;
    if(pool_) reinterpret_cast<PFN_vkDestroyDescriptorPool>(get_(device_,"vkDestroyDescriptorPool"))(device_,pool_,nullptr);
    if(layout_) reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(get_(device_,"vkDestroyDescriptorSetLayout"))(device_,layout_,nullptr);
    if(sampler_) reinterpret_cast<PFN_vkDestroySampler>(get_(device_,"vkDestroySampler"))(device_,sampler_,nullptr);
    if(view_) reinterpret_cast<PFN_vkDestroyImageView>(get_(device_,"vkDestroyImageView"))(device_,view_,nullptr);
    if(image_) reinterpret_cast<PFN_vkDestroyImage>(get_(device_,"vkDestroyImage"))(device_,image_,nullptr);
    if(image_memory_) reinterpret_cast<PFN_vkFreeMemory>(get_(device_,"vkFreeMemory"))(device_,image_memory_,nullptr);
    if(buffer_) reinterpret_cast<PFN_vkDestroyBuffer>(get_(device_,"vkDestroyBuffer"))(device_,buffer_,nullptr);
    if(memory_) reinterpret_cast<PFN_vkFreeMemory>(get_(device_,"vkFreeMemory"))(device_,memory_,nullptr);
    device_={};buffer_={};memory_={};pool_={};layout_={};set_={};get_={};
    image_={};image_memory_={};view_={};sampler_={};copies_.clear();uploaded_=false;barrier_={};copy_={};
}
bool VulkanSceneTextures::initialize(VkDevice device,PFN_vkGetDeviceProcAddr get,
    const VkPhysicalDeviceMemoryProperties& properties,std::span<const uint32_t> texels,bool backdrop) {
    close();
    try {
        if(!device || !get || texels.empty() || texels.size()>backdrop_texture_word_limit || properties.memoryTypeCount>VK_MAX_MEMORY_TYPES)
            throw std::runtime_error("Invalid scene texture upload");
        // Resolve cleanup operations before creating anything.
        entry<PFN_vkDestroyDescriptorPool>(get,device,"vkDestroyDescriptorPool");
        entry<PFN_vkDestroyDescriptorSetLayout>(get,device,"vkDestroyDescriptorSetLayout");
        entry<PFN_vkDestroyBuffer>(get,device,"vkDestroyBuffer");
        entry<PFN_vkFreeMemory>(get,device,"vkFreeMemory");
        device_=device;get_=get;
#define FN(name) entry<PFN_##name>(get,device,#name)
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size=texels.size_bytes();info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|(backdrop?VK_BUFFER_USAGE_TRANSFER_SRC_BIT:0);info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        check(FN(vkCreateBuffer)(device,&info,nullptr,&buffer_),"Create texture buffer");
        VkMemoryRequirements required{};FN(vkGetBufferMemoryRequirements)(device,buffer_,&required);
        if(required.size<info.size) throw std::runtime_error("Invalid texture allocation size");
        uint32_t selected=VK_MAX_MEMORY_TYPES;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)
            if((required.memoryTypeBits&(1U<<i)) && (properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                selected=i;
                if(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
            }
        if(selected==VK_MAX_MEMORY_TYPES) throw std::runtime_error("No host-visible texture memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize=required.size;allocation.memoryTypeIndex=selected;
        check(FN(vkAllocateMemory)(device,&allocation,nullptr,&memory_),"Allocate texture memory");
        check(FN(vkBindBufferMemory)(device,buffer_,memory_,0),"Bind texture memory");
        const auto unmap=FN(vkUnmapMemory);const auto flush=FN(vkFlushMappedMemoryRanges);
        void* mapped{};check(FN(vkMapMemory)(device,memory_,0,VK_WHOLE_SIZE,0,&mapped),"Map texture memory");
        std::memcpy(mapped,texels.data(),texels.size_bytes());
        VkResult flushed=VK_SUCCESS;
        if(!(properties.memoryTypes[selected].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=memory_;range.size=VK_WHOLE_SIZE;
            flushed=flush(device,1,&range);
        }
        unmap(device,memory_);check(flushed,"Flush texture memory");
        if(backdrop) create_backdrop(properties,texels);
        bind_storage({buffer_,0,info.size});
#undef FN
        status_="Scene texels uploaded";return true;
    } catch(const std::exception& e) {status_=e.what();close();return false;}
}
void VulkanSceneTextures::create_backdrop(const VkPhysicalDeviceMemoryProperties& properties,std::span<const uint32_t> texels) {
    const auto levels=backdrop_mip_uploads(texels);
    const auto device=device_;const auto get=get_;
#define FN(name) entry<PFN_##name>(get,device,#name)
    FN(vkDestroyImage);FN(vkDestroyImageView);FN(vkDestroySampler);
    barrier_=FN(vkCmdPipelineBarrier);copy_=FN(vkCmdCopyBufferToImage);
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType=VK_IMAGE_TYPE_2D;image.format=VK_FORMAT_R8G8B8A8_UNORM;
    image.extent={levels[0].width,levels[0].height,1};image.mipLevels=uint32_t(levels.size());image.arrayLayers=1;
    image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    check(FN(vkCreateImage)(device,&image,nullptr,&image_),"Create backdrop image");
    VkMemoryRequirements required{};FN(vkGetImageMemoryRequirements)(device,image_,&required);
    uint32_t selected=VK_MAX_MEMORY_TYPES;
    for(uint32_t i=0;i<properties.memoryTypeCount;++i) if(required.memoryTypeBits&(1U<<i)) {
        if(selected==VK_MAX_MEMORY_TYPES) selected=i;
        if(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {selected=i;break;}
    }
    if(selected==VK_MAX_MEMORY_TYPES) throw std::runtime_error("No backdrop image memory");
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize=required.size;allocation.memoryTypeIndex=selected;
    check(FN(vkAllocateMemory)(device,&allocation,nullptr,&image_memory_),"Allocate backdrop image memory");
    check(FN(vkBindImageMemory)(device,image_,image_memory_,0),"Bind backdrop image memory");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image_;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=image.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,image.mipLevels,0,1};
    check(FN(vkCreateImageView)(device,&view,nullptr,&view_),"Create backdrop image view");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU=texels[2]?VK_SAMPLER_ADDRESS_MODE_REPEAT:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod=float(image.mipLevels-1);
    check(FN(vkCreateSampler)(device,&sampler,nullptr,&sampler_),"Create backdrop sampler");
    copies_.reserve(levels.size());
    for(uint32_t level=0;level<levels.size();++level) {
        VkBufferImageCopy copy{};copy.bufferOffset=levels[level].byte_offset;
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1};
        copy.imageExtent={levels[level].width,levels[level].height,1};copies_.push_back(copy);
    }
#undef FN
}
bool VulkanSceneTextures::record_upload(VkCommandBuffer command) const {
    if(!image_ || uploaded_) return true;
    if(!command || !buffer_ || copies_.empty()) return false;
    VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;image.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;image.srcQueueFamilyIndex=image.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    image.image=image_;image.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,uint32_t(copies_.size()),0,1};
    barrier_(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&image);
    copy_(command,buffer_,image_,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(copies_.size()),copies_.data());
    image.oldLayout=image.newLayout;image.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;image.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    barrier_(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&image);
    uploaded_=true;return true;
}
void VulkanSceneTextures::bind_storage(const VkDescriptorBufferInfo& storage) {
        const auto device=device_;const auto get=get_;
#define FN(name) entry<PFN_##name>(get,device,#name)
        const VkDescriptorSetLayoutBinding bindings[]{
            {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount=image_?2:1;layout.pBindings=bindings;
        check(FN(vkCreateDescriptorSetLayout)(device,&layout,nullptr,&layout_),"Create texture layout");
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=image_?2:1;pool.pPoolSizes=sizes;
        check(FN(vkCreateDescriptorPool)(device,&pool,nullptr,&pool_),"Create texture pool");
        VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sets.descriptorPool=pool_;sets.descriptorSetCount=1;sets.pSetLayouts=&layout_;
        check(FN(vkAllocateDescriptorSets)(device,&sets,&set_),"Allocate texture set");
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set_;write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&storage;
        FN(vkUpdateDescriptorSets)(device,1,&write,0,nullptr);
        if(image_) {
            const VkDescriptorImageInfo sampled{sampler_,view_,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            write.dstBinding=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pBufferInfo=nullptr;write.pImageInfo=&sampled;
            FN(vkUpdateDescriptorSets)(device,1,&write,0,nullptr);
        }
#undef FN
}
bool VulkanSceneTextures::initialize_external(VkDevice device,PFN_vkGetDeviceProcAddr get,VkBuffer buffer,VkDeviceSize bytes) {
    close();
    try {
        if(!device || !get || !buffer || !bytes || bytes%4 || bytes>256ULL*1024*1024)
            throw std::runtime_error("Invalid external scene storage");
        entry<PFN_vkDestroyDescriptorPool>(get,device,"vkDestroyDescriptorPool");
        entry<PFN_vkDestroyDescriptorSetLayout>(get,device,"vkDestroyDescriptorSetLayout");
        device_=device;get_=get;
        // buffer_ and memory_ intentionally remain empty: close() owns only
        // the descriptor resources, never the producer's buffer/allocation.
        bind_storage({buffer,0,bytes});
        status_="External scene storage bound";return true;
    } catch(const std::exception& e) {status_=e.what();close();return false;}
}
}

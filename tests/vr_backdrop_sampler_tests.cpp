#include "starfox/vr/vulkan_scene_textures.hpp"
#include "starfox/vr/backdrop_texture.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace starfox::vr;
template<class T> T handle(uintptr_t value) {return reinterpret_cast<T>(value);}
void check(bool value,std::source_location where=std::source_location::current()) {
    if(!value) throw std::runtime_error("Backdrop sampler assertion failed at line "+std::to_string(where.line()));
}
std::array<uint32_t,64> mapped{};
uint32_t next_handle=1;
bool image_apis_allowed=true;
bool fail_pool{};
unsigned buffer_destroys{},memory_frees{},image_destroys{},view_destroys{},sampler_destroys{},layout_destroys{},pool_destroys{};
VkImageCreateInfo seen_image{};
VkSamplerCreateInfo seen_sampler{};
VkBufferUsageFlags seen_buffer_usage{};
std::vector<VkDescriptorSetLayoutBinding> seen_bindings;
std::vector<VkBufferImageCopy> seen_copies;
std::vector<VkImageMemoryBarrier> seen_barriers;
std::vector<std::array<VkPipelineStageFlags,2>> seen_barrier_stages;
unsigned descriptor_updates{};
VkDescriptorImageInfo seen_descriptor_image{};

VKAPI_ATTR VkResult VKAPI_CALL create_buffer(VkDevice,const VkBufferCreateInfo* info,const VkAllocationCallbacks*,VkBuffer* out) {
    seen_buffer_usage=info->usage;
    check(info->size<=sizeof(mapped) && (info->usage&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    check((info->usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)!=0 || info->usage==VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    *out=handle<VkBuffer>(next_handle++);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_buffer(VkDevice,VkBuffer,const VkAllocationCallbacks*) {++buffer_destroys;}
VKAPI_ATTR void VKAPI_CALL buffer_requirements(VkDevice,VkBuffer,VkMemoryRequirements* out) {*out={sizeof(mapped),16,1};}
VKAPI_ATTR VkResult VKAPI_CALL allocate_memory(VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory* out) {
    check(info->memoryTypeIndex==0);*out=handle<VkDeviceMemory>(next_handle++);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice,VkDeviceMemory,const VkAllocationCallbacks*) {++memory_frees;}
VKAPI_ATTR VkResult VKAPI_CALL bind_buffer(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize) {return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL bind_image(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize) {return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL map_memory(VkDevice,VkDeviceMemory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void** out) {*out=mapped.data();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL unmap_memory(VkDevice,VkDeviceMemory) {}
VKAPI_ATTR VkResult VKAPI_CALL flush_memory(VkDevice,uint32_t count,const VkMappedMemoryRange*) {check(count==1);return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL create_image(VkDevice,const VkImageCreateInfo* info,const VkAllocationCallbacks*,VkImage* out) {
    seen_image=*info;*out=handle<VkImage>(next_handle++);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_image(VkDevice,VkImage,const VkAllocationCallbacks*) {++image_destroys;}
VKAPI_ATTR void VKAPI_CALL image_requirements(VkDevice,VkImage,VkMemoryRequirements* out) {*out={4096,16,1};}
VKAPI_ATTR VkResult VKAPI_CALL create_view(VkDevice,const VkImageViewCreateInfo* info,const VkAllocationCallbacks*,VkImageView* out) {
    check(info->viewType==VK_IMAGE_VIEW_TYPE_2D && info->subresourceRange.levelCount==seen_image.mipLevels);
    *out=handle<VkImageView>(next_handle++);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_view(VkDevice,VkImageView,const VkAllocationCallbacks*) {++view_destroys;}
VKAPI_ATTR VkResult VKAPI_CALL create_sampler(VkDevice,const VkSamplerCreateInfo* info,const VkAllocationCallbacks*,VkSampler* out) {
    seen_sampler=*info;*out=handle<VkSampler>(next_handle++);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_sampler(VkDevice,VkSampler,const VkAllocationCallbacks*) {++sampler_destroys;}
VKAPI_ATTR VkResult VKAPI_CALL create_layout(VkDevice,const VkDescriptorSetLayoutCreateInfo* info,const VkAllocationCallbacks*,VkDescriptorSetLayout* out) {
    seen_bindings.assign(info->pBindings,info->pBindings+info->bindingCount);*out=handle<VkDescriptorSetLayout>(next_handle++);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_layout(VkDevice,VkDescriptorSetLayout,const VkAllocationCallbacks*) {++layout_destroys;}
VKAPI_ATTR VkResult VKAPI_CALL create_pool(VkDevice,const VkDescriptorPoolCreateInfo*,const VkAllocationCallbacks*,VkDescriptorPool* out) {
    if(fail_pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *out=handle<VkDescriptorPool>(next_handle++);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_pool(VkDevice,VkDescriptorPool,const VkAllocationCallbacks*) {++pool_destroys;}
VKAPI_ATTR VkResult VKAPI_CALL allocate_sets(VkDevice,const VkDescriptorSetAllocateInfo*,VkDescriptorSet* out) {*out=handle<VkDescriptorSet>(next_handle++);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL update_sets(VkDevice,uint32_t count,const VkWriteDescriptorSet* writes,uint32_t,const VkCopyDescriptorSet*) {
    for(uint32_t i=0;i<count;++i) if(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        ++descriptor_updates;seen_descriptor_image=*writes[i].pImageInfo;
    }
}
VKAPI_ATTR void VKAPI_CALL barriers(VkCommandBuffer,VkPipelineStageFlags source_stage,VkPipelineStageFlags destination_stage,VkDependencyFlags,
    uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier* images) {
    check(count==1);seen_barriers.push_back(*images);seen_barrier_stages.push_back({source_stage,destination_stage});
}
VKAPI_ATTR void VKAPI_CALL copy_image(VkCommandBuffer,VkBuffer,VkImage,VkImageLayout,uint32_t count,const VkBufferImageCopy* regions) {
    seen_copies.assign(regions,regions+count);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name) {
#define ENTRY(n,f) if(std::strcmp(name,n)==0) return reinterpret_cast<PFN_vkVoidFunction>(f)
    ENTRY("vkCreateBuffer",create_buffer);ENTRY("vkDestroyBuffer",destroy_buffer);ENTRY("vkGetBufferMemoryRequirements",buffer_requirements);
    ENTRY("vkAllocateMemory",allocate_memory);ENTRY("vkFreeMemory",free_memory);ENTRY("vkBindBufferMemory",bind_buffer);
    ENTRY("vkMapMemory",map_memory);ENTRY("vkUnmapMemory",unmap_memory);ENTRY("vkFlushMappedMemoryRanges",flush_memory);
    if(image_apis_allowed) {
        ENTRY("vkCreateImage",create_image);ENTRY("vkDestroyImage",destroy_image);ENTRY("vkGetImageMemoryRequirements",image_requirements);
        ENTRY("vkBindImageMemory",bind_image);ENTRY("vkCreateImageView",create_view);ENTRY("vkDestroyImageView",destroy_view);
        ENTRY("vkCreateSampler",create_sampler);ENTRY("vkDestroySampler",destroy_sampler);
    }
    ENTRY("vkCreateDescriptorSetLayout",create_layout);ENTRY("vkDestroyDescriptorSetLayout",destroy_layout);
    ENTRY("vkCreateDescriptorPool",create_pool);ENTRY("vkDestroyDescriptorPool",destroy_pool);
    ENTRY("vkAllocateDescriptorSets",allocate_sets);ENTRY("vkUpdateDescriptorSets",update_sets);
    ENTRY("vkCmdPipelineBarrier",barriers);ENTRY("vkCmdCopyBufferToImage",copy_image);
#undef ENTRY
    return nullptr;
}
std::vector<uint32_t> pyramid(bool wrap=true) {
    // Two-by-one master plus one-by-one tail; the helper validates header and offsets.
    return {backdrop_texture_magic,2,wrap?1U:0U,0,10,2,1,12,1,1,0xff112233U,0xff445566U,0xff334455U};
}
}

void test_backdrop_failure_cleanup() {
    VkPhysicalDeviceMemoryProperties properties{};properties.memoryTypeCount=1;
    properties.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    {
        const auto before=std::array<unsigned,5>{buffer_destroys,memory_frees,image_destroys,view_destroys,sampler_destroys};
        fail_pool=true;VulkanSceneTextures textures;auto words=pyramid();
        check(!textures.initialize(handle<VkDevice>(1),get,properties,words,true));
        fail_pool=false;
        check(!textures.sampled_backdrop() && !textures.descriptor());
        check(buffer_destroys==before[0]+1 && memory_frees==before[1]+2 && image_destroys==before[2]+1
            && view_destroys==before[3]+1 && sampler_destroys==before[4]+1);
    }
}
void test_backdrop_sampler() {
    using namespace starfox::vr;
    {
        auto words=pyramid();const auto levels=backdrop_mip_uploads(words);
        check(levels.size()==2 && levels[0].byte_offset==40 && levels[0].width==2 && levels[0].height==1
            && levels[1].byte_offset==48 && levels[1].width==1 && levels[1].height==1);
        for(unsigned index:{0U,1U,2U,3U,4U,5U,6U,7U}) {
            auto invalid=words;invalid[index]=UINT32_MAX;bool rejected=false;
            try {(void)backdrop_mip_uploads(invalid);} catch(const std::invalid_argument&) {rejected=true;}
            check(rejected);
        }
        auto invalid=words;invalid.pop_back();bool rejected=false;
        try {(void)backdrop_mip_uploads(invalid);} catch(const std::invalid_argument&) {rejected=true;}
        check(rejected);
        for(const auto dimensions:{std::array<unsigned,2>{7,5},{1,9},{9,1},{1,1},{320,128},{1024,1536}}) {
            starfox::render::BackdropImage image;image.width=dimensions[0];image.height=dimensions[1];
            image.pixels.resize(size_t(image.width)*image.height,0x80402010U);
            const auto texture=make_backdrop_texture(image,false,true,true);
            const auto uploads=backdrop_mip_uploads(*texture);unsigned width=image.width,height=image.height;
            uint64_t offset=uint64_t((*texture)[4])*4;
            for(const auto& level:uploads) {
                check(level.width==width && level.height==height && level.byte_offset==offset && offset%4==0);
                offset+=uint64_t(width)*height*4;width=std::max(1U,width/2);height=std::max(1U,height/2);
            }
            check(uploads.back().width==1 && uploads.back().height==1 && offset==texture->size()*4);
            auto damaged=*texture;damaged[5]+=1;rejected=false;
            try {(void)backdrop_mip_uploads(damaged);} catch(const std::invalid_argument&) {rejected=true;}
            check(rejected);
        }
        rejected=false;
        try {(void)backdrop_mip_uploads({});} catch(const std::invalid_argument&) {rejected=true;}
        check(rejected);
    }
    VkPhysicalDeviceMemoryProperties properties{};properties.memoryTypeCount=1;
    properties.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    {
        VulkanSceneTextures textures;auto words=pyramid(true);
        check(textures.initialize(handle<VkDevice>(1),get,properties,words,true));
        check(seen_buffer_usage==(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
            && std::equal(words.begin(),words.end(),mapped.begin()));
        check(textures.sampled_backdrop() && !textures.ready());
        check(seen_image.format==VK_FORMAT_R8G8B8A8_UNORM && seen_image.tiling==VK_IMAGE_TILING_OPTIMAL
            && seen_image.usage==(VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT)
            && seen_image.extent.width==2 && seen_image.extent.height==1 && seen_image.mipLevels==2 && seen_image.arrayLayers==1);
        check(seen_sampler.magFilter==VK_FILTER_LINEAR && seen_sampler.minFilter==VK_FILTER_LINEAR
            && seen_sampler.mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR && seen_sampler.addressModeU==VK_SAMPLER_ADDRESS_MODE_REPEAT
            && seen_sampler.addressModeV==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE && seen_sampler.addressModeW==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            && seen_sampler.maxLod==1.f);
        check(seen_bindings.size()==2 && seen_bindings[0].binding==0 && seen_bindings[1].binding==1
            && seen_bindings[1].descriptorType==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        check(descriptor_updates==1 && seen_descriptor_image.sampler && seen_descriptor_image.imageView
            && seen_descriptor_image.imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        seen_copies.clear();seen_barriers.clear();seen_barrier_stages.clear();
        check(!textures.ready() && textures.record_upload(handle<VkCommandBuffer>(1)) && textures.ready());
        check(seen_copies.size()==2 && seen_copies[0].bufferOffset==40 && seen_copies[0].imageSubresource.mipLevel==0
            && seen_copies[0].imageExtent.width==2 && seen_copies[0].imageExtent.height==1
            && seen_copies[1].bufferOffset==48 && seen_copies[1].imageSubresource.mipLevel==1
            && seen_copies[1].imageExtent.width==1 && seen_copies[1].imageExtent.height==1);
        check(seen_barriers.size()==2 && seen_barriers[0].oldLayout==VK_IMAGE_LAYOUT_UNDEFINED
            && seen_barriers[0].newLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            && seen_barriers[0].srcAccessMask==0 && seen_barriers[0].dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT
            && seen_barriers[0].subresourceRange.levelCount==2 && seen_barriers[1].oldLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            && seen_barriers[1].newLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            && seen_barriers[1].srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT && seen_barriers[1].dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
        check(seen_barrier_stages.size()==2 && seen_barrier_stages[0][0]==VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            && seen_barrier_stages[0][1]==VK_PIPELINE_STAGE_TRANSFER_BIT
            && seen_barrier_stages[1][0]==VK_PIPELINE_STAGE_TRANSFER_BIT
            && seen_barrier_stages[1][1]==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        check(textures.ready() && textures.record_upload(handle<VkCommandBuffer>(1)) && seen_barriers.size()==2 && seen_copies.size()==2);
    }
    check(image_destroys==1 && view_destroys==1 && sampler_destroys==1 && buffer_destroys==1 && memory_frees==2);
    {
        VulkanSceneTextures textures;auto words=pyramid(false);
        check(textures.initialize(handle<VkDevice>(1),get,properties,words,true));
        check(seen_sampler.addressModeU==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    }
    {
        VulkanSceneTextures textures;auto words=pyramid();
        image_apis_allowed=false;
        check(textures.initialize(handle<VkDevice>(1),get,properties,std::span<const uint32_t>(words.data()+10,3)));
        check(!textures.sampled_backdrop() && textures.ready() && seen_bindings.size()==1 && descriptor_updates==2
            && seen_buffer_usage==VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        image_apis_allowed=true;
    }
}

int main() {
    try {
        test_backdrop_sampler();test_backdrop_failure_cleanup();
        std::puts("Backdrop sampler uploads passed");
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}

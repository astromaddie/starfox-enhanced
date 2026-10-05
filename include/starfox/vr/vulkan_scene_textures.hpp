#pragma once
#include "starfox/vr/vulkan_scene_pipeline.hpp"
#include <span>
#include <vector>
namespace starfox::vr {
// Immutable packed RGBA texels. Finish all GPU use before replacing/closing.
class VulkanSceneTextures {
public:
    ~VulkanSceneTextures();
    VulkanSceneTextures()=default;
    VulkanSceneTextures(const VulkanSceneTextures&)=delete;
    VulkanSceneTextures& operator=(const VulkanSceneTextures&)=delete;
    bool initialize(VkDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceMemoryProperties&,std::span<const uint32_t>,bool backdrop=false);
    // Record once outside a render pass, before sampling on the same queue.
    // Keep the staging/storage buffer alive until all GPU use completes.
    bool record_upload(VkCommandBuffer) const;
    bool sampled_backdrop() const {return image_!=VK_NULL_HANDLE;}
    bool ready() const {return !image_ || uploaded_;}
    // Bind a caller-owned storage buffer without uploading or taking ownership.
    // Caller supplies the actual byte size, synchronizes producer writes, and
    // keeps the buffer alive until all draws and this descriptor are finished.
    bool initialize_external(VkDevice,PFN_vkGetDeviceProcAddr,VkBuffer,VkDeviceSize bytes);
    void close() noexcept;
    VkDescriptorSetLayout layout() const {return layout_;}
    VkDescriptorSet descriptor() const {return set_;}
    const std::string& status() const {return status_;}
private:
    void bind_storage(const VkDescriptorBufferInfo&);
    void create_backdrop(const VkPhysicalDeviceMemoryProperties&,std::span<const uint32_t>);
    VkDevice device_{};VkBuffer buffer_{};VkDeviceMemory memory_{};
    VkImage image_{};VkDeviceMemory image_memory_{};VkImageView view_{};VkSampler sampler_{};
    std::vector<VkBufferImageCopy> copies_;
    mutable bool uploaded_{};
    PFN_vkCmdPipelineBarrier barrier_{};PFN_vkCmdCopyBufferToImage copy_{};
    VkDescriptorPool pool_{};VkDescriptorSetLayout layout_{};VkDescriptorSet set_{};
    PFN_vkGetDeviceProcAddr get_{};
    std::string status_;
};
}

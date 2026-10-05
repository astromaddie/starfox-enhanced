#pragma once
#include "starfox/vr/vulkan_scene_buffer.hpp"
#include <array>
#include <span>
namespace starfox::vr {
class VulkanPipelineCache;
// SFX_VR_VISIBILITY_MASK: draws one eye's hidden-area triangles at depth 0
// with no colour, first in the eye pass, so every later eye draw is rejected
// there (see VulkanScenePipeline::set_visibility_mask_pass). Points are tangent
// space on z=-1, placed with the eye's projection alone.
class VulkanVisibilityMask {
public:
    bool initialize(VkDevice,PFN_vkGetDeviceProcAddr,VkRenderPass,VulkanPipelineCache* cache=nullptr);
    // Replace one eye's mesh; empty clears it. Only while no eye work using
    // the previous mesh is in flight.
    bool upload(unsigned eye,const VkPhysicalDeviceMemoryProperties&,std::span<const XrVector2f> triangles);
    // Records nothing for an eye without a mesh.
    bool record(VkCommandBuffer,VkExtent2D,unsigned eye,const Matrix4& projection) const;
    bool has_mesh(unsigned eye) const noexcept {return eye<2 && meshes_[eye].buffer();}
    void close() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    VkDevice device_{};PFN_vkGetDeviceProcAddr get_{};
    VulkanScenePipeline pipeline_;
    std::array<VulkanSceneBuffer,2> meshes_;
    std::string status_{"Visibility mask not initialized"};
};
}

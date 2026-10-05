#include "starfox/vr/vulkan_visibility_mask.hpp"
#include <cmath>
#include <vector>
namespace starfox::vr {
bool VulkanVisibilityMask::initialize(VkDevice device,PFN_vkGetDeviceProcAddr get,VkRenderPass pass,VulkanPipelineCache* cache) {
    close();
    if(!pipeline_.initialize(device,get,pass,true,SceneTopology::triangles,VK_NULL_HANDLE,SceneBlend::visibility_mask,cache)) {
        status_=pipeline_.status();return false;
    }
    device_=device;get_=get;status_="Visibility mask pipeline ready";return true;
}
bool VulkanVisibilityMask::upload(unsigned eye,const VkPhysicalDeviceMemoryProperties& memory,std::span<const XrVector2f> triangles) {
    if(eye>=2 || !device_ || triangles.size()%3) {status_="Invalid visibility mask upload";return false;}
    meshes_[eye].close();
    if(triangles.empty()) return true;
    std::vector<SceneVertex> vertices(triangles.size());
    for(std::size_t i=0;i<triangles.size();++i) {
        if(!std::isfinite(triangles[i].x) || !std::isfinite(triangles[i].y)) {status_="Non-finite visibility mask vertex";return false;}
        vertices[i].position[0]=triangles[i].x;vertices[i].position[1]=triangles[i].y;vertices[i].position[2]=-1;
        vertices[i].color[3]=1;
    }
    if(!meshes_[eye].initialize(device_,get_,memory,vertices)) {status_=meshes_[eye].status();return false;}
    return true;
}
bool VulkanVisibilityMask::record(VkCommandBuffer command,VkExtent2D extent,unsigned eye,const Matrix4& projection) const {
    if(eye>=2 || !meshes_[eye].buffer()) return true;
    // Identity view: the mesh is already in the eye's view space.
    const EyeCamera camera{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},projection};
    return pipeline_.record(command,extent,meshes_[eye].buffer(),meshes_[eye].count(),camera);
}
void VulkanVisibilityMask::close() noexcept {
    for(auto& mesh:meshes_) mesh.close();
    pipeline_.close();device_={};get_={};
}
}

#include "starfox/vr/visibility_mask.hpp"
#include <cmath>

namespace starfox::vr {
VisibilityMaskApi VisibilityMaskApi::from_instance(XrInstance instance) {
    VisibilityMaskApi api;
    if(instance==XR_NULL_HANDLE) return api;
    PFN_xrVoidFunction function{};
    if(XR_SUCCEEDED(xrGetInstanceProcAddr(instance,"xrGetVisibilityMaskKHR",&function)))
        api.get=reinterpret_cast<PFN_xrGetVisibilityMaskKHR>(function);
    return api;
}

bool VisibilityMask::refresh(XrSession session) {
    if(!api_.available() || session==XR_NULL_HANDLE || (!stale_[0] && !stale_[1])) return false;
    std::string report;
    for(std::uint32_t eye=0;eye<2;++eye) {
        if(!stale_[eye]) continue;
        stale_[eye]=false;
        const bool fetched=fetch(session,eye);
        report+=(report.empty()?"":", ")+std::string("eye ")+std::to_string(eye)+": "
            +(fetched?std::to_string(triangles_[eye].size()/3)+" hidden triangles":status_);
    }
    status_="Visibility mask "+report;
    return true;
}

bool VisibilityMask::fetch(XrSession session,std::uint32_t eye) {
    triangles_[eye].clear();
    constexpr auto view=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    constexpr auto type=XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR;
    XrVisibilityMaskKHR mask{XR_TYPE_VISIBILITY_MASK_KHR};
    auto result=api_.get(session,view,eye,type,&mask);
    if(XR_FAILED(result)) {status_="xrGetVisibilityMaskKHR failed ("+std::to_string(result)+")";return false;}
    if(!mask.vertexCountOutput || !mask.indexCountOutput) {status_="no hidden area";return false;}
    if(mask.vertexCountOutput>max_vertices || mask.indexCountOutput>max_indices || mask.indexCountOutput%3) {
        status_="invalid hidden mesh size";return false;
    }
    std::vector<XrVector2f> vertices(mask.vertexCountOutput);
    std::vector<std::uint32_t> indices(mask.indexCountOutput);
    mask.vertexCapacityInput=static_cast<std::uint32_t>(vertices.size());mask.vertices=vertices.data();
    mask.indexCapacityInput=static_cast<std::uint32_t>(indices.size());mask.indices=indices.data();
    result=api_.get(session,view,eye,type,&mask);
    if(XR_FAILED(result)) {status_="xrGetVisibilityMaskKHR failed ("+std::to_string(result)+")";return false;}
    // The mesh may have changed between the calls; a shrunken one is still whole.
    if(mask.vertexCountOutput>vertices.size() || mask.indexCountOutput>indices.size() || mask.indexCountOutput%3) {
        status_="hidden mesh changed while reading";return false;
    }
    vertices.resize(mask.vertexCountOutput);indices.resize(mask.indexCountOutput);
    for(const auto& vertex:vertices) if(!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) {
        status_="non-finite hidden mesh vertex";return false;
    }
    for(const auto index:indices) if(index>=vertices.size()) {status_="hidden mesh index out of range";return false;}
    triangles_[eye].reserve(indices.size());
    for(const auto index:indices) triangles_[eye].push_back(vertices[index]);
    return true;
}
}

// XR_KHR_visibility_mask fetch through an injected xrGetVisibilityMaskKHR.
#include "starfox/vr/visibility_mask.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>
using namespace starfox::vr;
namespace {
void require(bool value,const std::source_location where=std::source_location::current()) {
    if(!value) throw std::runtime_error("Visibility mask assertion failed at line "+std::to_string(where.line()));
}
const XrSession session=reinterpret_cast<XrSession>(std::uintptr_t(7));
std::vector<std::uint32_t> asked;
XrResult fail_with=XR_SUCCESS;
// Eye 0: a quad as two triangles. Eye 1: one triangle, replaced by tests.
std::vector<std::vector<XrVector2f>> fake_vertices{{{-1,-1},{1,-1},{1,1},{-1,1}},{{0,0},{.5F,0},{0,.5F}}};
std::vector<std::vector<std::uint32_t>> fake_indices{{0,1,2,0,2,3},{2,1,0}};
XRAPI_ATTR XrResult XRAPI_CALL fake_get(XrSession given,XrViewConfigurationType view,std::uint32_t eye,
    XrVisibilityMaskTypeKHR type,XrVisibilityMaskKHR* mask) {
    require(given==session && view==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && eye<2
        && type==XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR && mask && mask->type==XR_TYPE_VISIBILITY_MASK_KHR);
    asked.push_back(eye);
    if(XR_FAILED(fail_with)) return fail_with;
    const auto& vertices=fake_vertices[eye];const auto& indices=fake_indices[eye];
    mask->vertexCountOutput=std::uint32_t(vertices.size());mask->indexCountOutput=std::uint32_t(indices.size());
    // Two-call idiom: sizes first, then both arrays.
    if(!mask->vertexCapacityInput && !mask->indexCapacityInput) return XR_SUCCESS;
    if(mask->vertexCapacityInput<vertices.size() || mask->indexCapacityInput<indices.size()) return XR_ERROR_SIZE_INSUFFICIENT;
    std::copy(vertices.begin(),vertices.end(),mask->vertices);std::copy(indices.begin(),indices.end(),mask->indices);
    return XR_SUCCESS;
}
bool same(XrVector2f a,XrVector2f b) {return a.x==b.x && a.y==b.y;}
}
int main() try {
    {
        // Extension absent: an empty table, and nothing is ever called.
        require(!VisibilityMaskApi::from_instance(XR_NULL_HANDLE).available());
        VisibilityMask absent;
        require(!absent.refresh(session) && absent.triangles(0).empty() && absent.triangles(1).empty());
        absent.invalidate(0);require(!absent.refresh(session));
    }
    VisibilityMask mask(VisibilityMaskApi{fake_get});
    require(!mask.refresh(XR_NULL_HANDLE) && asked.empty());
    // Both eyes on the first refresh, each with a size query then the read.
    require(mask.refresh(session) && asked==std::vector<std::uint32_t>({0,0,1,1}));
    require(!mask.stale(0) && !mask.stale(1));
    const auto left=mask.triangles(0),right=mask.triangles(1);
    require(left.size()==6 && same(left[0],{-1,-1}) && same(left[2],{1,1}) && same(left[3],{-1,-1}) && same(left[5],{-1,1}));
    require(right.size()==3 && same(right[0],{0,.5F}) && same(right[2],{0,0}));
    require(mask.status()=="Visibility mask eye 0: 2 hidden triangles, eye 1: 1 hidden triangles");
    // Nothing stale: no calls.
    asked.clear();require(!mask.refresh(session) && asked.empty());
    // The change event refetches only that eye.
    fake_vertices[1]={{.25F,.25F},{.75F,.25F},{.75F,.75F},{.25F,.75F}};fake_indices[1]={0,1,2,2,3,0};
    mask.invalidate(1);mask.invalidate(2); // Not a stereo view: ignored.
    require(mask.stale(1) && !mask.stale(0));
    require(mask.refresh(session) && asked==std::vector<std::uint32_t>({1,1}));
    require(mask.triangles(0).size()==6 && mask.triangles(1).size()==6 && same(mask.triangles(1)[5],{.25F,.25F}));
    // Bad meshes and failures leave that eye without a mask until the next event.
    asked.clear();fake_indices[1]={0,1,9};mask.invalidate(1);
    require(mask.refresh(session) && mask.triangles(1).empty() && mask.status().find("out of range")!=std::string::npos);
    fake_indices[1]={0,1};mask.invalidate(1);
    require(mask.refresh(session) && mask.triangles(1).empty() && asked.size()==3);
    fake_indices[1]={};mask.invalidate(1);
    require(mask.refresh(session) && mask.triangles(1).empty() && mask.status().find("no hidden area")!=std::string::npos);
    fail_with=XR_ERROR_RUNTIME_FAILURE;mask.invalidate(0);
    require(mask.refresh(session) && mask.triangles(0).empty() && mask.status().find("failed")!=std::string::npos);
    asked.clear();require(!mask.refresh(session) && asked.empty());
    fail_with=XR_SUCCESS;mask.invalidate(0);
    require(mask.refresh(session) && mask.triangles(0).size()==6);
    std::cout<<"Visibility mask fetch per eye, refetch on change and failure handling passed (injected runtime)\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}

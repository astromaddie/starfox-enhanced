#include "starfox/vr/vulkan_stereo_draw.hpp"
namespace starfox::vr {
StereoRenderer::EyeResult VulkanStereoDraw::draw(unsigned eye,uint32_t image,
    const EyeCamera& camera,XrTime time,const VkClearColorValue& clear,const Record& record,const Record& before_render,
    const Record& after_render,const VulkanEyeCommands::TimelineWait* wait,bool overlap) {
    using Result=StereoRenderer::EyeResult;
    auto& slot=slots_[eye==1 && slots_[1].commands?1:0];
    auto& commands=*slot.commands;
    if(slot.submitted && (eye!=slot.eye || image!=slot.image)) return Result::fatal;
    if(!slot.submitted) {
        slot.submitted_at=std::chrono::steady_clock::now();
        if(!commands.submit(targets_,eye,image,clear,[&](VkCommandBuffer command,VkExtent2D extent) {
            if(record) record(command,extent,camera,time);
        },[&](VkCommandBuffer command,VkExtent2D extent) {
            if(before_render) before_render(command,extent,camera,time);
        },[&](VkCommandBuffer command,VkExtent2D extent) {
            if(after_render) after_render(command,extent,camera,time);
        },wait)) {
            return commands.poll()==VulkanEyeCommands::Completion::complete?Result::failed:Result::fatal;
        }
        ++timing_.submissions;
        timing_.submit_ms+=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-slot.submitted_at).count();
        slot.submitted=true;slot.eye=eye;slot.image=image;
        if(overlap && slots_[1].commands) return Result::submitted;
    }
    switch(commands.poll(1000000)) {
    case VulkanEyeCommands::Completion::pending:
        ++timing_.pending_polls;
        return Result::pending;
    case VulkanEyeCommands::Completion::error:return Result::fatal;
    case VulkanEyeCommands::Completion::complete: {
        const auto completed=std::chrono::steady_clock::now();
        const auto elapsed=std::chrono::duration<double,std::milli>(completed-slot.submitted_at).count();
        ++timing_.eyes;timing_.total_ms+=elapsed;
        if(elapsed>timing_.maximum_ms) timing_.maximum_ms=elapsed;
        last_eye_timing_.submit_to_fence_cpu_ms=elapsed;
        last_eye_timing_.gpu_timestamp_ms=commands.take_gpu_duration_ms();
        last_eye_timing_.pre_pass_gpu_ms=commands.take_pre_pass_ms();
        last_eye_timing_.submitted_at=slot.submitted_at;last_eye_timing_.completed_at=completed;
        slot.submitted=false;return Result::complete;
    }
    }
    return Result::fatal;
}
}

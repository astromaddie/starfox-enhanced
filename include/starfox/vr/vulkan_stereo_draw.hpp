#pragma once
#include "starfox/vr/stereo_renderer.hpp"
#include "starfox/vr/vulkan_eye_commands.hpp"
#include <array>
#include <chrono>
#include <optional>
namespace starfox::vr {
// Adapter retained for the lifetime of the frame driver. Commands/targets
// outlive it. Only this adapter may submit work on its command objects.
// With a second command object, eye 1 records into it, so both eyes can be
// in flight at once (SFX_VR_OVERLAP_EYES); without one every eye shares the
// first and must complete before the next is drawn.
class VulkanStereoDraw {
public:
    struct CompletionTiming {
        uint64_t eyes{},pending_polls{},submissions{};
        double total_ms{},maximum_ms{},submit_ms{};
    };
    struct EyeTiming {
        double submit_to_fence_cpu_ms{};
        std::optional<double> gpu_timestamp_ms;
        std::optional<double> pre_pass_gpu_ms;
        std::optional<VulkanEyeCommands::PassDurations> pass_gpu_ms;
        std::chrono::steady_clock::time_point submitted_at{},completed_at{};
    };
    // CPU elapsed submission-to-fence-observation time, including polling
    // delays. This is intentionally not labelled GPU timestamp time.
    CompletionTiming take_completion_timing() noexcept {auto result=timing_;timing_={};return result;}
    EyeTiming take_last_eye_timing() noexcept {auto result=last_eye_timing_;last_eye_timing_={};return result;}
    bool pending() const noexcept {return slots_[0].submitted || slots_[1].submitted;}
    // Ends a GPU timing segment of the eye being recorded (see VulkanEyeCommands::mark).
    void mark(VkCommandBuffer command,unsigned index) noexcept {if(recording_) recording_->mark(command,index);}
    using Record=std::function<void(VkCommandBuffer,VkExtent2D,const EyeCamera&,XrTime)>;
    VulkanStereoDraw(VulkanEyeCommands& commands,const VulkanEyeTargets& targets,VulkanEyeCommands* right=nullptr)
        :targets_(targets),slots_{{{&commands},{right}}} {}
    // overlap (needs the second command object) returns submitted straight
    // after queueing instead of waiting; later calls poll that eye's fence.
    StereoRenderer::EyeResult draw(unsigned eye,uint32_t image,const EyeCamera&,XrTime,
        const VkClearColorValue&,const Record& record={},const Record& before_render={},
        const Record& after_render={},const VulkanEyeCommands::TimelineWait* wait=nullptr,bool overlap=false);
private:
    struct Slot {
        VulkanEyeCommands* commands{};
        bool submitted{};
        unsigned eye{};uint32_t image{};
        std::chrono::steady_clock::time_point submitted_at{};
    };
    const VulkanEyeTargets& targets_;
    std::array<Slot,2> slots_;
    CompletionTiming timing_{};
    EyeTiming last_eye_timing_{};
    VulkanEyeCommands* recording_{};
};
}

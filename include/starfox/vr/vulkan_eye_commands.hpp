#pragma once
#include "starfox/vr/vulkan_eye_targets.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
namespace starfox::vr {
// One in-flight eye submission. Caller serializes access to the shared queue.
// Never release an XR image until poll() returns complete. Errors require
// device/session teardown, not reuse of an image with uncertain completion.
class VulkanEyeCommands {
public:
    enum class Completion {complete,pending,error};
    using Record=std::function<void(VkCommandBuffer,VkExtent2D)>;
    struct TimelineWait {VkSemaphore semaphore{};uint64_t value{};VkPipelineStageFlags stage{VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};};
    // passes adds in-pass timestamps for per-segment GPU time (SFX_VR_TIMING_GPU).
    struct TimestampConfig {std::uint32_t valid_bits{};double period_ns{};bool passes{};};
    // Eye pass segments: sky, models, sprites, cockpit. The pass start, the
    // pass_segments-1 mark()s and the end of record bound them.
    static constexpr unsigned pass_segments=4;
    using PassDurations=std::array<double,pass_segments>;
    VulkanEyeCommands()=default;
    ~VulkanEyeCommands();
    VulkanEyeCommands(const VulkanEyeCommands&)=delete;
    VulkanEyeCommands& operator=(const VulkanEyeCommands&)=delete;
    bool initialize(VkDevice,VkQueue,uint32_t queue_family,PFN_vkGetDeviceProcAddr,
        std::optional<TimestampConfig> timestamps=std::nullopt);
    // Standalone producer work, without a render pass or XR image. Poll before
    // handing geometry to another API or reusing this command allocation.
    bool submit_work(VkExtent2D,const Record&,const TimelineWait* wait=nullptr);
    bool submit(const VulkanEyeTargets&,unsigned eye,unsigned image,
        const VkClearColorValue&,const Record& record={},const Record& before_render={},
        const Record& after_render={},const TimelineWait* wait=nullptr);
    // Optional wait requires a timeline-enabled device. Keep semaphore alive
    // until completion. after_render runs outside the pass for ownership release.
    // before_render runs after command begin, outside the render pass. It
    // owns compute-to-graphics barriers; both callbacks share one submission.
    // Optional bounded wait wakes on completion instead of a fixed host sleep.
    Completion poll(uint64_t timeout_ns=0);
    bool gpu_timestamps_available() const noexcept {return query_pool_!=VK_NULL_HANDLE && timestamps_active_;}
    const std::string& timestamp_status() const noexcept {return timestamp_status_;}
    std::optional<double> take_gpu_duration_ms() noexcept {
        auto result=gpu_duration_ms_;gpu_duration_ms_.reset();return result;
    }
    // GPU time from command start to the end of before_render, when it ran.
    std::optional<double> take_pre_pass_ms() noexcept {
        auto result=pre_pass_ms_;pre_pass_ms_.reset();return result;
    }
    // End segment `index` (0..pass_segments-2) inside submit()'s record
    // callback. A no-op unless pass timestamps are active, so nothing extra is
    // recorded without them. Durations exist only when every mark was written.
    void mark(VkCommandBuffer,unsigned index) noexcept;
    std::optional<PassDurations> take_pass_ms() noexcept {
        auto result=pass_ms_;pass_ms_.reset();return result;
    }
    static std::optional<double> timestamp_duration_ms(std::uint64_t begin,std::uint64_t end,
        std::uint32_t valid_bits,double period_ns) noexcept;
    // Consecutive boundary differences. Tilers may report a boundary slightly
    // before the previous one; such a segment counts as zero, not a wrap.
    static std::optional<PassDurations> pass_durations_ms(std::span<const std::uint64_t,pass_segments+1>,
        std::uint32_t valid_bits,double period_ns) noexcept;
    void close() noexcept;
    const std::string& status() const noexcept {return status_;}
private:
    VkDevice device_{};VkQueue queue_{};VkCommandPool pool_{};
    VkCommandBuffer command_{};VkFence fence_{};
    VkQueryPool query_pool_{};
    bool pending_{},failed_{};
    bool timestamps_active_{};
    std::uint32_t timestamp_valid_bits_{};
    double timestamp_period_ns_{};
    std::optional<double> gpu_duration_ms_,pre_pass_ms_;
    std::optional<PassDurations> pass_ms_;
    bool pre_pass_marked_{},pass_timestamps_{},in_pass_{};
    unsigned pass_marks_{};
    PFN_vkDestroyCommandPool destroy_pool_{};
    PFN_vkDestroyFence destroy_fence_{};
    PFN_vkQueueWaitIdle idle_{};
    PFN_vkResetCommandPool reset_pool_{};
    PFN_vkResetFences reset_fences_{};
    PFN_vkBeginCommandBuffer begin_{};
    PFN_vkEndCommandBuffer end_{};
    PFN_vkCmdBeginRenderPass begin_pass_{};
    PFN_vkCmdEndRenderPass end_pass_{};
    PFN_vkQueueSubmit submit_{};
    PFN_vkGetFenceStatus fence_status_{};
    PFN_vkWaitForFences wait_fences_{};
    PFN_vkDestroyQueryPool destroy_query_pool_{};
    PFN_vkCmdResetQueryPool reset_query_pool_{};
    PFN_vkCmdWriteTimestamp write_timestamp_{};
    PFN_vkGetQueryPoolResults get_query_results_{};
    std::string status_{"Eye commands not initialized"};
    std::string timestamp_status_{"GPU timestamps not requested"};
};
// [vr-perf] passes=: each segment's per-eye mean over the window, summed over
// both eyes. Nothing until both eyes have a sample.
inline std::optional<VulkanEyeCommands::PassDurations> eye_pass_means(
    const std::array<VulkanEyeCommands::PassDurations,2>& sums,const std::array<unsigned,2>& frames) noexcept {
    if(!frames[0] || !frames[1]) return std::nullopt;
    VulkanEyeCommands::PassDurations result{};
    for(unsigned segment=0;segment<result.size();++segment)
        result[segment]=sums[0][segment]/frames[0]+sums[1][segment]/frames[1];
    return result;
}
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rocket::performance {

struct DistributionSummary {
    std::size_t sampleCount = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double average = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

DistributionSummary summarizeDistribution(std::span<const double> samples);

struct FrameTimingSample {
    double frameMilliseconds = 0.0;
    double cpuMilliseconds = 0.0;
};

struct FrameTimingSummary {
    DistributionSummary frame;
    DistributionSummary cpu;
};

// The in-game overlay publishes four times per second. Its immediate update is
// observed by the next start-to-start frame interval, while work realized by the
// following RmlUi render is observed by that frame's CPU measurement and by the
// subsequent start-to-start interval. Frame and CPU quarantine therefore advance
// independently: ordinary CPU work is still sampled when only the frame interval
// carries deferred instrumentation time.
class RollingFrameTimingSamples {
public:
    static constexpr std::size_t capacity = 120;

    void reset();
    void quarantineNextSample();
    void quarantineOverlayPublication();
    bool record(const FrameTimingSample& sample);

    FrameTimingSummary summarize() const;
    std::size_t sampleCount() const;
    std::size_t frameSampleCount() const;
    std::size_t cpuSampleCount() const;
    bool nextSampleQuarantined() const;

private:
    std::array<double, capacity> frameSamples_ {};
    std::array<double, capacity> cpuSamples_ {};
    std::size_t frameSampleCount_ = 0;
    std::size_t cpuSampleCount_ = 0;
    std::size_t frameSampleCursor_ = 0;
    std::size_t cpuSampleCursor_ = 0;
    std::uint8_t frameSamplesToQuarantine_ = 0;
    std::uint8_t cpuSamplesToQuarantine_ = 0;
};

struct BenchmarkRunDescriptor {
    std::string scenario;
    std::string renderer;
    std::string platform;
    std::string gpu;
    std::uint64_t seed = 0;
    int width = 0;
    int height = 0;
    double simulationFramesPerSecond = 0.0;
    double targetFramesPerSecond = 0.0;
    double activeRefreshRateHz = 0.0;
    std::string presentIntervalSource = "queue-present-return";
    double warmupSeconds = 0.0;
    double captureSeconds = 0.0;
    std::optional<std::uint64_t> initialGameplayStateHash;
    std::optional<std::uint64_t> finalGameplayStateHash;
};

struct BenchmarkFrameSample {
    double cpuMilliseconds = 0.0;
    double gpuMilliseconds = 0.0;
    double presentIntervalMilliseconds = 0.0;
    double limiterIdleMilliseconds = 0.0;
    std::uint32_t sceneDrawCalls = 0;
    std::uint64_t uploadedBytes = 0;
    std::uint32_t pipelineEvents = 0;
    std::uint64_t deviceMemoryBytes = 0;
    bool missedRefresh = false;
};

struct BenchmarkReport {
    static constexpr int schemaVersion = 2;

    BenchmarkRunDescriptor run;
    std::size_t acceptedSampleCount = 0;
    std::size_t rejectedSampleCount = 0;
    std::size_t missedRefreshCount = 0;
    DistributionSummary cpuMilliseconds;
    DistributionSummary gpuMilliseconds;
    DistributionSummary presentIntervalMilliseconds;
    DistributionSummary limiterIdleMilliseconds;
    DistributionSummary sceneDrawCalls;
    DistributionSummary uploadedBytes;
    DistributionSummary pipelineEvents;
    DistributionSummary deviceMemoryBytes;
};

class BenchmarkAccumulator {
public:
    explicit BenchmarkAccumulator(
        BenchmarkRunDescriptor descriptor,
        std::size_t expectedSampleCount = 0);

    bool addSample(const BenchmarkFrameSample& sample);
    void setFinalGameplayStateHash(std::optional<std::uint64_t> hash);
    BenchmarkReport report() const;
    std::string reportJson() const;

private:
    BenchmarkRunDescriptor descriptor_;
    std::vector<BenchmarkFrameSample> samples_;
    std::size_t rejectedSampleCount_ = 0;
};

std::string serializeBenchmarkReportJson(const BenchmarkReport& report);

} // namespace rocket::performance

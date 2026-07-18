#include "performance/PerformanceMetrics.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace {

bool approximately(double lhs, double rhs)
{
    return std::abs(lhs - rhs) < 0.000001;
}

} // namespace

int main()
{
    using namespace rocket::performance;

    const std::array<double, 5> values {5.0, 1.0, 100.0, 3.0, 2.0};
    const DistributionSummary distribution = summarizeDistribution(values);
    assert(distribution.sampleCount == 5);
    assert(approximately(distribution.minimum, 1.0));
    assert(approximately(distribution.maximum, 100.0));
    assert(approximately(distribution.average, 22.2));
    assert(approximately(distribution.median, 3.0));
    assert(approximately(distribution.p95, 100.0));
    assert(approximately(distribution.p99, 100.0));

    // A frame that contains the overlay's immediate/deferred update work must
    // not enter either rolling distribution. Sampling resumes on the next frame.
    RollingFrameTimingSamples rolling;
    assert(rolling.record({16.0, 4.0}));
    assert(rolling.record({17.0, 5.0}));
    rolling.quarantineNextSample();
    assert(rolling.nextSampleQuarantined());
    assert(!rolling.record({250.0, 200.0}));
    assert(!rolling.nextSampleQuarantined());
    assert(rolling.record({15.0, 3.0}));
    const FrameTimingSummary rollingSummary = rolling.summarize();
    assert(rolling.sampleCount() == 3);
    assert(approximately(rollingSummary.frame.p95, 17.0));
    assert(approximately(rollingSummary.cpu.p95, 5.0));
    rolling.reset();
    assert(rolling.sampleCount() == 0);
    assert(!rolling.nextSampleQuarantined());

    // Overlay publication affects two start-to-start intervals but only the
    // next CPU measurement. Preserve the ordinary CPU sample from the second
    // interval instead of hiding the complete frame.
    assert(rolling.record({16.0, 4.0}));
    assert(rolling.record({17.0, 5.0}));
    rolling.quarantineOverlayPublication();
    assert(!rolling.record({66.0, 80.0}));
    assert(rolling.nextSampleQuarantined());
    assert(rolling.record({96.0, 3.0}));
    assert(!rolling.nextSampleQuarantined());
    assert(rolling.record({15.0, 4.0}));
    const FrameTimingSummary overlaySummary = rolling.summarize();
    assert(rolling.frameSampleCount() == 3);
    assert(rolling.cpuSampleCount() == 4);
    assert(rolling.sampleCount() == 3);
    assert(approximately(overlaySummary.frame.p95, 17.0));
    assert(approximately(overlaySummary.cpu.p95, 5.0));

    BenchmarkRunDescriptor descriptor;
    descriptor.scenario = "mining \"stress\"\nfixed";
    descriptor.renderer = "vulkan";
    descriptor.platform = "windows";
    descriptor.gpu = "test gpu";
    descriptor.seed = 71575;
    descriptor.width = 1280;
    descriptor.height = 800;
    descriptor.simulationFramesPerSecond = 60.0;
    descriptor.targetFramesPerSecond = 60.0;
    descriptor.activeRefreshRateHz = 240.0;
    descriptor.warmupSeconds = 10.0;
    descriptor.captureSeconds = 60.0;
    descriptor.initialGameplayStateHash = 123U;

    BenchmarkAccumulator benchmark(std::move(descriptor), 3);
    assert(benchmark.addSample({4.0, 2.0, 16.0, 10.0, 20, 1000, 0, 5000, false}));
    assert(benchmark.addSample({8.0, 4.0, 17.0, 8.0, 30, 2000, 1, 6000, true}));
    assert(benchmark.addSample({6.0, 3.0, 15.0, 9.0, 25, 1500, 0, 5500, false}));
    BenchmarkFrameSample invalid;
    invalid.cpuMilliseconds = std::numeric_limits<double>::quiet_NaN();
    assert(!benchmark.addSample(invalid));
    benchmark.setFinalGameplayStateHash(456U);

    const BenchmarkReport report = benchmark.report();
    assert(report.acceptedSampleCount == 3);
    assert(report.rejectedSampleCount == 1);
    assert(report.missedRefreshCount == 1);
    assert(approximately(report.cpuMilliseconds.average, 6.0));
    assert(approximately(report.cpuMilliseconds.median, 6.0));
    assert(approximately(report.cpuMilliseconds.p95, 8.0));
    assert(approximately(report.uploadedBytes.median, 1500.0));
    assert(approximately(report.pipelineEvents.maximum, 1.0));

    const std::string json = benchmark.reportJson();
    assert(json.find("\"schemaVersion\": 2") != std::string::npos);
    assert(json.find("mining \\\"stress\\\"\\nfixed") != std::string::npos);
    assert(json.find("\"samples\": {\"accepted\": 3, \"rejected\": 1}") != std::string::npos);
    assert(json.find("\"queuePresentDeadlineMisses\": {\"count\": 1") != std::string::npos);
    assert(json.find("\"queuePresentReturnIntervalMilliseconds\"") != std::string::npos);
    assert(json.find("\"activeRefreshRateHz\": 240") != std::string::npos);
    assert(json.find("\"simulationFramesPerSecond\": 60") != std::string::npos);
    assert(json.find("\"cpuMilliseconds\"") != std::string::npos);
    assert(json.find("\"p95\": 8") != std::string::npos);
    assert(json.find("\"initialGameplayStateHash\": \"0x000000000000007b\"") != std::string::npos);
    assert(json.find("\"finalGameplayStateHash\": \"0x00000000000001c8\"") != std::string::npos);

    return 0;
}

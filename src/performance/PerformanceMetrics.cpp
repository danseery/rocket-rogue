#include "performance/PerformanceMetrics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string_view>
#include <utility>

namespace rocket::performance {
namespace {

double nearestRankPercentile(const std::vector<double>& sorted, double quantile)
{
    const std::size_t index = std::min(
        sorted.size() - 1,
        static_cast<std::size_t>(std::ceil(static_cast<double>(sorted.size()) * quantile)) - 1);
    return sorted[index];
}

bool validBenchmarkSample(const BenchmarkFrameSample& sample)
{
    const auto validDuration = [](double value) {
        return std::isfinite(value) && value >= 0.0;
    };
    return validDuration(sample.cpuMilliseconds)
        && validDuration(sample.gpuMilliseconds)
        && validDuration(sample.presentIntervalMilliseconds)
        && validDuration(sample.limiterIdleMilliseconds);
}

std::string escapeJson(std::string_view value)
{
    std::ostringstream out;
    for (const unsigned char character : value) {
        switch (character) {
        case '\"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (character < 0x20) {
                out << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(character)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(character);
            }
            break;
        }
    }
    return out.str();
}

void writeDistribution(
    std::ostringstream& out,
    std::string_view name,
    const DistributionSummary& summary,
    bool trailingComma)
{
    out << "    \"" << name << "\": {"
        << "\"count\": " << summary.sampleCount
        << ", \"minimum\": " << summary.minimum
        << ", \"maximum\": " << summary.maximum
        << ", \"average\": " << summary.average
        << ", \"p50\": " << summary.median
        << ", \"p95\": " << summary.p95
        << ", \"p99\": " << summary.p99
        << "}";
    if (trailingComma) out << ',';
    out << '\n';
}

void writeGameplayHash(
    std::ostringstream& out,
    const std::optional<std::uint64_t>& hash)
{
    if (!hash) {
        out << "null";
        return;
    }
    out << "\"0x" << std::hex << std::setw(16) << std::setfill('0') << *hash
        << std::dec << std::setfill(' ') << '\"';
}

template <typename Projection>
DistributionSummary summarizeBenchmarkMetric(
    const std::vector<BenchmarkFrameSample>& samples,
    Projection projection)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const BenchmarkFrameSample& sample : samples) {
        values.push_back(static_cast<double>(projection(sample)));
    }
    return summarizeDistribution(values);
}

} // namespace

DistributionSummary summarizeDistribution(std::span<const double> samples)
{
    DistributionSummary result;
    if (samples.empty()) {
        return result;
    }

    std::vector<double> sorted;
    sorted.reserve(samples.size());
    for (const double sample : samples) {
        if (std::isfinite(sample)) {
            sorted.push_back(sample);
        }
    }
    if (sorted.empty()) {
        return result;
    }

    result.sampleCount = sorted.size();
    result.average = std::accumulate(sorted.begin(), sorted.end(), 0.0) /
        static_cast<double>(sorted.size());
    std::sort(sorted.begin(), sorted.end());
    result.minimum = sorted.front();
    result.maximum = sorted.back();
    result.median = nearestRankPercentile(sorted, 0.50);
    result.p95 = nearestRankPercentile(sorted, 0.95);
    result.p99 = nearestRankPercentile(sorted, 0.99);
    return result;
}

void RollingFrameTimingSamples::reset()
{
    frameSamples_.fill(0.0);
    cpuSamples_.fill(0.0);
    frameSampleCount_ = 0;
    cpuSampleCount_ = 0;
    frameSampleCursor_ = 0;
    cpuSampleCursor_ = 0;
    frameSamplesToQuarantine_ = 0;
    cpuSamplesToQuarantine_ = 0;
}

void RollingFrameTimingSamples::quarantineNextSample()
{
    frameSamplesToQuarantine_ = std::max<std::uint8_t>(frameSamplesToQuarantine_, 1);
    cpuSamplesToQuarantine_ = std::max<std::uint8_t>(cpuSamplesToQuarantine_, 1);
}

void RollingFrameTimingSamples::quarantineOverlayPublication()
{
    // Publication happens after the current sample. Its immediate work lands
    // in the next start-to-start interval. Deferred RmlUi realization lands in
    // the next CPU sample and in the interval that follows that render.
    frameSamplesToQuarantine_ = std::max<std::uint8_t>(frameSamplesToQuarantine_, 2);
    cpuSamplesToQuarantine_ = std::max<std::uint8_t>(cpuSamplesToQuarantine_, 1);
}

bool RollingFrameTimingSamples::record(const FrameTimingSample& sample)
{
    bool frameQuarantined = false;
    if (frameSamplesToQuarantine_ > 0) {
        --frameSamplesToQuarantine_;
        frameQuarantined = true;
    }
    bool cpuQuarantined = false;
    if (cpuSamplesToQuarantine_ > 0) {
        --cpuSamplesToQuarantine_;
        cpuQuarantined = true;
    }

    bool frameRecorded = false;
    if (!frameQuarantined
        && std::isfinite(sample.frameMilliseconds)
        && sample.frameMilliseconds > 0.0
        && sample.frameMilliseconds <= 1000.0) {
        frameSamples_[frameSampleCursor_] = sample.frameMilliseconds;
        frameSampleCursor_ = (frameSampleCursor_ + 1) % capacity;
        frameSampleCount_ = std::min(frameSampleCount_ + 1, capacity);
        frameRecorded = true;
    }

    bool cpuRecorded = false;
    if (!cpuQuarantined
        && std::isfinite(sample.cpuMilliseconds)
        && sample.cpuMilliseconds >= 0.0) {
        cpuSamples_[cpuSampleCursor_] = sample.cpuMilliseconds;
        cpuSampleCursor_ = (cpuSampleCursor_ + 1) % capacity;
        cpuSampleCount_ = std::min(cpuSampleCount_ + 1, capacity);
        cpuRecorded = true;
    }

    return frameRecorded || cpuRecorded;
}

FrameTimingSummary RollingFrameTimingSamples::summarize() const
{
    return {
        summarizeDistribution(std::span(frameSamples_.data(), frameSampleCount_)),
        summarizeDistribution(std::span(cpuSamples_.data(), cpuSampleCount_)),
    };
}

std::size_t RollingFrameTimingSamples::sampleCount() const
{
    return std::min(frameSampleCount_, cpuSampleCount_);
}

std::size_t RollingFrameTimingSamples::frameSampleCount() const
{
    return frameSampleCount_;
}

std::size_t RollingFrameTimingSamples::cpuSampleCount() const
{
    return cpuSampleCount_;
}

bool RollingFrameTimingSamples::nextSampleQuarantined() const
{
    return frameSamplesToQuarantine_ > 0 || cpuSamplesToQuarantine_ > 0;
}

BenchmarkAccumulator::BenchmarkAccumulator(
    BenchmarkRunDescriptor descriptor,
    std::size_t expectedSampleCount)
    : descriptor_(std::move(descriptor))
{
    samples_.reserve(expectedSampleCount);
}

bool BenchmarkAccumulator::addSample(const BenchmarkFrameSample& sample)
{
    if (!validBenchmarkSample(sample)) {
        ++rejectedSampleCount_;
        return false;
    }
    samples_.push_back(sample);
    return true;
}

void BenchmarkAccumulator::setFinalGameplayStateHash(std::optional<std::uint64_t> hash)
{
    descriptor_.finalGameplayStateHash = hash;
}

BenchmarkReport BenchmarkAccumulator::report() const
{
    BenchmarkReport result;
    result.run = descriptor_;
    result.acceptedSampleCount = samples_.size();
    result.rejectedSampleCount = rejectedSampleCount_;
    result.missedRefreshCount = static_cast<std::size_t>(std::count_if(
        samples_.begin(), samples_.end(),
        [](const BenchmarkFrameSample& sample) { return sample.missedRefresh; }));
    result.cpuMilliseconds = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.cpuMilliseconds; });
    result.gpuMilliseconds = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.gpuMilliseconds; });
    result.presentIntervalMilliseconds = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.presentIntervalMilliseconds; });
    result.limiterIdleMilliseconds = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.limiterIdleMilliseconds; });
    result.sceneDrawCalls = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.sceneDrawCalls; });
    result.uploadedBytes = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.uploadedBytes; });
    result.pipelineEvents = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.pipelineEvents; });
    result.deviceMemoryBytes = summarizeBenchmarkMetric(samples_,
        [](const BenchmarkFrameSample& sample) { return sample.deviceMemoryBytes; });
    return result;
}

std::string BenchmarkAccumulator::reportJson() const
{
    return serializeBenchmarkReportJson(report());
}

std::string serializeBenchmarkReportJson(const BenchmarkReport& report)
{
    std::ostringstream out;
    out << std::setprecision(15);
    const double missedRefreshRate = report.acceptedSampleCount == 0
        ? 0.0
        : static_cast<double>(report.missedRefreshCount) /
            static_cast<double>(report.acceptedSampleCount);
    out << "{\n"
        << "  \"schemaVersion\": " << BenchmarkReport::schemaVersion << ",\n"
        << "  \"run\": {\n"
        << "    \"scenario\": \"" << escapeJson(report.run.scenario) << "\",\n"
        << "    \"renderer\": \"" << escapeJson(report.run.renderer) << "\",\n"
        << "    \"platform\": \"" << escapeJson(report.run.platform) << "\",\n"
        << "    \"gpu\": \"" << escapeJson(report.run.gpu) << "\",\n"
        << "    \"seed\": " << report.run.seed << ",\n"
        << "    \"width\": " << report.run.width << ",\n"
        << "    \"height\": " << report.run.height << ",\n"
        << "    \"simulationFramesPerSecond\": " << report.run.simulationFramesPerSecond << ",\n"
        << "    \"targetFramesPerSecond\": " << report.run.targetFramesPerSecond << ",\n"
        << "    \"activeRefreshRateHz\": " << report.run.activeRefreshRateHz << ",\n"
        << "    \"presentIntervalSource\": \"" << escapeJson(report.run.presentIntervalSource) << "\",\n"
        << "    \"warmupSeconds\": " << report.run.warmupSeconds << ",\n"
        << "    \"captureSeconds\": " << report.run.captureSeconds << ",\n"
        << "    \"initialGameplayStateHash\": ";
    writeGameplayHash(out, report.run.initialGameplayStateHash);
    out << ",\n    \"finalGameplayStateHash\": ";
    writeGameplayHash(out, report.run.finalGameplayStateHash);
    out << "\n"
        << "  },\n"
        << "  \"samples\": {\"accepted\": " << report.acceptedSampleCount
        << ", \"rejected\": " << report.rejectedSampleCount << "},\n"
        << "  \"queuePresentDeadlineMisses\": {\"count\": " << report.missedRefreshCount
        << ", \"rate\": " << missedRefreshRate << "},\n"
        << "  \"metrics\": {\n";
    writeDistribution(out, "cpuMilliseconds", report.cpuMilliseconds, true);
    writeDistribution(out, "gpuMilliseconds", report.gpuMilliseconds, true);
    writeDistribution(out, "queuePresentReturnIntervalMilliseconds", report.presentIntervalMilliseconds, true);
    writeDistribution(out, "limiterIdleMilliseconds", report.limiterIdleMilliseconds, true);
    writeDistribution(out, "sceneDrawCalls", report.sceneDrawCalls, true);
    writeDistribution(out, "uploadedBytes", report.uploadedBytes, true);
    writeDistribution(out, "pipelineEvents", report.pipelineEvents, true);
    writeDistribution(out, "deviceMemoryBytes", report.deviceMemoryBytes, false);
    out << "  }\n"
        << "}\n";
    return out.str();
}

} // namespace rocket::performance

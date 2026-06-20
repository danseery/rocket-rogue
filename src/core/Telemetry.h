#pragma once

#include "core/GameText.h"
#include "core/GameTypes.h"

#include <array>
#include <algorithm>
#include <cstddef>
#include <string_view>

namespace rocket {

enum class TelemetryChannel {
    Heat,
    Pressure,
    Vibration,
    Guidance,
    FuelMix,
    AbortRisk
};

struct TelemetryChannelDefinition {
    TelemetryChannel channel;
    std::string_view label;
    text::telemetry::WarningCopy warningCopy;
};

inline constexpr std::array<TelemetryChannelDefinition, 6> telemetryChannelDefinitions {{
    {TelemetryChannel::Heat, text::labels::temp, text::telemetry::heat},
    {TelemetryChannel::Pressure, text::labels::press, text::telemetry::pressure},
    {TelemetryChannel::Vibration, text::labels::vibration, text::telemetry::vibration},
    {TelemetryChannel::Guidance, text::labels::nav, text::telemetry::guidance},
    {TelemetryChannel::FuelMix, text::labels::mix, text::telemetry::fuelMix},
    {TelemetryChannel::AbortRisk, text::labels::abort, text::telemetry::abortRisk}
}};

struct TelemetryChannelSample {
    TelemetryChannel channel;
    std::string_view label;
    text::telemetry::WarningCopy warningCopy;
    double value = 0.0;
};

inline double telemetryValue(const TelemetryEvent& event, TelemetryChannel channel)
{
    switch (channel) {
    case TelemetryChannel::Heat:
        return event.heat;
    case TelemetryChannel::Pressure:
        return event.pressure;
    case TelemetryChannel::Vibration:
        return event.vibration;
    case TelemetryChannel::Guidance:
        return event.guidance;
    case TelemetryChannel::FuelMix:
        return event.fuelMix;
    case TelemetryChannel::AbortRisk:
        return event.abortRisk;
    }
    return 0.0;
}

inline std::array<TelemetryChannelSample, telemetryChannelDefinitions.size()> telemetrySamples(const TelemetryEvent& event)
{
    std::array<TelemetryChannelSample, telemetryChannelDefinitions.size()> samples {};
    for (std::size_t i = 0; i < telemetryChannelDefinitions.size(); ++i) {
        const TelemetryChannelDefinition& definition = telemetryChannelDefinitions[i];
        samples[i] = {
            definition.channel,
            definition.label,
            definition.warningCopy,
            telemetryValue(event, definition.channel)
        };
    }
    return samples;
}

inline TelemetryChannelSample strongestTelemetrySample(const TelemetryEvent& event)
{
    const auto samples = telemetrySamples(event);
    return *std::max_element(samples.begin(), samples.end(), [](const TelemetryChannelSample& lhs, const TelemetryChannelSample& rhs) {
        return lhs.value < rhs.value;
    });
}

inline double telemetryChannelLoad(const TelemetryEvent& event)
{
    const auto samples = telemetrySamples(event);
    double load = 0.0;
    for (const TelemetryChannelSample& sample : samples) {
        load += sample.value;
    }
    return load;
}

inline double strongestNonHeatTelemetryValue(const TelemetryEvent& event)
{
    const auto samples = telemetrySamples(event);
    double strongest = 0.0;
    for (const TelemetryChannelSample& sample : samples) {
        if (sample.channel != TelemetryChannel::Heat) {
            strongest = std::max(strongest, sample.value);
        }
    }
    return strongest;
}

} // namespace rocket

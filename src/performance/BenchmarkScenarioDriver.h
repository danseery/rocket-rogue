#pragma once

#include "core/GameTypes.h"
#include "performance/NativeBenchmarkOptions.h"

#include <cstdint>
#include <optional>
#include <string>

namespace rocket {
class RocketGameApp;
}

namespace rocket::performance {

struct BenchmarkScenarioSetupResult {
    NativeBenchmarkScenario scenario = NativeBenchmarkScenario::Mining;
    Screen screen = Screen::Hangar;
    std::optional<std::uint64_t> gameplayStateHash;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

// Enters deterministic, save-suppressed debug sandboxes after RocketGameApp
// has initialized. The caller is still responsible for selecting an isolated
// benchmark profile before constructing the app's platform services.
class BenchmarkScenarioDriver {
public:
    BenchmarkScenarioSetupResult setup(
        RocketGameApp& app,
        const NativeBenchmarkOptions& options) const;

    // Uses RocketGameApp's canonical save-payload hash so renderer changes can
    // be compared without exposing or duplicating private gameplay state.
    std::optional<std::uint64_t> sampleGameplayStateHash(
        const RocketGameApp& app) const noexcept;
};

} // namespace rocket::performance

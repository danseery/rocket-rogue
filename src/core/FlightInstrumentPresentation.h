#pragma once

#include "core/GameFormat.h"
#include "core/GameTypes.h"
#include "core/LaunchSimulation.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rocket {

struct FlightInstrumentPresentation {
    bool visible = false;
    double speed = 0.0;
    double temperature = 0.0;
    double fuel = 0.0;
    std::string speedValue;
    std::string temperatureValue;
    std::string fuelValue;
    bool temperatureCritical = false;
    bool offCourse = false;
    bool courseCritical = false;
    double throttle = 0.0;
    std::string throttleValue;
};

inline double normalizedGaugeValue(double value, double minimum, double maximum)
{
    return std::clamp(
        (value - minimum) / std::max(0.0001, maximum - minimum),
        0.0,
        1.0);
}

inline FlightInstrumentPresentation launchFlightInstruments(
    const PreparedLaunch& launch,
    const LaunchFlightState& flight)
{
    const double maximumMultiplier = std::max(
        1.5,
        launch.config.burnGoalMultiplier);
    const double fuelShare = flight.fuelRemaining /
        std::max(0.01, flight.fuelCapacity);
    const double courseMagnitude = std::abs(flight.courseOffset);
    return {
        true,
        normalizedGaugeValue(flight.currentMultiplier, 1.0, maximumMultiplier),
        std::clamp(flight.heat, 0.0, 1.0),
        std::clamp(fuelShare, 0.0, 1.0),
        display::fixed(flight.currentMultiplier * 100.0, 0) + " m/s",
        display::percent(std::clamp(flight.heat, 0.0, 1.0)),
        display::fixed(std::max(0.0, flight.fuelRemaining), 0) + " / " +
            display::fixed(flight.fuelCapacity, 0),
        flight.heat > 0.80,
        launch.manualControlsEnabled &&
            courseMagnitude >= tuning::launch::pilotingCourseCaution,
        launch.manualControlsEnabled &&
            courseMagnitude >= std::max(0.01, launchCourseLimit(launch)),
        std::clamp(flight.selectedThrottle, 0.0, 1.0),
        display::percent(std::clamp(flight.selectedThrottle, 0.0, 1.0))
    };
}

inline FlightInstrumentPresentation flybyFlightInstruments(const FlybyRunState& flyby)
{
    const double throttle = std::clamp(flyby.selectedThrottle, 0.0, 1.0);
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double speedShare = normalizedGaugeValue(
        speed,
        tuning::flyby::minSpeed,
        tuning::flyby::maxSpeed);
    const double temperature = std::clamp(
        0.18 + throttle * 0.50 + speedShare * 0.18,
        0.0,
        1.0);
    const double fuel = std::clamp(
        1.0 - flyby.elapsedSeconds / std::max(0.01, flyby.durationSeconds),
        0.0,
        1.0);
    return {
        flyby.active && !flyby.completed,
        speedShare,
        temperature,
        fuel,
        display::fixed(speed * 100.0, 0) + " m/s",
        display::percent(temperature),
        display::percent(fuel),
        temperature > 0.80,
        flyby.currentZone <= 0,
        false,
        throttle,
        "Throttle " + display::percent(throttle)
    };
}

inline FlightInstrumentPresentation orbitFlightInstruments(const OrbitRunState& orbit)
{
    const double throttle = std::clamp(orbit.selectedThrottle, 0.0, 1.0);
    const double speed = std::hypot(orbit.velocityX, orbit.velocityY);
    const double speedShare = normalizedGaugeValue(
        speed,
        tuning::orbit::minSpeed,
        tuning::orbit::maxSpeed);
    const double maneuverInput = std::clamp(
        std::hypot(orbit.inputX, throttle),
        0.0,
        1.0);
    const double temperature = std::clamp(
        0.18 + maneuverInput * 0.42 + speedShare * 0.18,
        0.0,
        1.0);
    const double fuel = std::clamp(
        1.0 - orbit.elapsedSeconds / std::max(0.01, orbit.durationSeconds),
        0.0,
        1.0);
    return {
        orbit.active && !orbit.completed,
        speedShare,
        temperature,
        fuel,
        display::fixed(speed * 100.0, 0) + " m/s",
        display::percent(temperature),
        display::percent(fuel),
        temperature > 0.80,
        orbit.currentZone <= 0,
        false,
        throttle,
        "Throttle " + display::percent(throttle)
    };
}

} // namespace rocket

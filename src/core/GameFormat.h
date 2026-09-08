#pragma once

#include "core/GameText.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

namespace rocket::display {

inline std::string fixed(double value, int precision)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

inline std::string signedFixed(double value, int precision)
{
    return (value > 0.0 ? "+" : "") + fixed(value, precision);
}

inline std::string money(double value)
{
    return fixed(value, 0);
}

inline std::string signedMoney(double value)
{
    return signedFixed(value, 0);
}

inline std::string multiplier(double value)
{
    return "x" + fixed(value, 2);
}

inline std::string percent(double value)
{
    return fixed(std::clamp(value, 0.0, 1.0) * 100.0, 0) + "%";
}

inline std::string signedPercent(double value)
{
    return signedFixed(value * 100.0, 0) + "%";
}

inline std::string wholePercent(int value)
{
    return std::to_string(value) + "%";
}

inline std::string fraction(int current, int required)
{
    return std::to_string(current) + "/" + std::to_string(required);
}

inline std::string credits(double value)
{
    return text::panel::credits(money(value));
}

inline std::string needCredits(double value)
{
    return text::panel::needCredits(money(value));
}

inline std::string damage(int value)
{
    return wholePercent(value) + " " + std::string(text::units::damage);
}

} // namespace rocket::display

#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace rocket {

struct DetailPresentationRow {
    std::string label;
    std::string value;
    bool heading = false;
};

inline DetailPresentationRow detailPresentationRow(std::string_view label, std::string value)
{
    return {std::string(label), std::move(value), false};
}

inline DetailPresentationRow detailPresentationRow(std::string_view label, std::string_view value)
{
    return {std::string(label), std::string(value), false};
}

inline DetailPresentationRow detailPresentationHeader(std::string_view label)
{
    return {std::string(label), {}, true};
}

} // namespace rocket

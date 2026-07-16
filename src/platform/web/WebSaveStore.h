#pragma once

#include "platform/AppServices.h"

#include <string>

namespace rocket {

class WebSaveStore final : public ISaveStore {
public:
    std::string load() override;
    bool storeAtomic(std::string_view data) override;
    bool clear() override;
    std::string lastError() const override;
    std::string_view description() const override { return "Browser localStorage (save_v1)"; }

private:
    std::string lastError_;
};

} // namespace rocket

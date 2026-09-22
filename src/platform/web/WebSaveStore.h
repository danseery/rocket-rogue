#pragma once

#include "platform/AppServices.h"

#include <string>

namespace rocket {

class WebSaveStore final : public ISaveStore {
public:
    std::string load() override;
    bool storeAtomic(std::string_view data) override;
    bool storeMilestoneAtomic(std::string_view data) override;
    bool clear() override;
    std::string loadCheckpoint() override;
    bool storeCheckpointAtomic(std::string_view data) override;
    bool clearCheckpoint() override;
    std::string lastError() const override;
    // Web writes acknowledge staging. The shell tracks asynchronous IndexedDB
    // commit/verification and owns the persistent pending/failure recovery UI.
    std::string_view description() const override { return "Browser IndexedDB (verified save queue)"; }

private:
    std::string lastError_;
};

} // namespace rocket

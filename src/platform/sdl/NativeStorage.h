#pragma once

#include "platform/AppServices.h"

#include <filesystem>

namespace rocket {

class AtomicTextFile {
public:
    explicit AtomicTextFile(std::filesystem::path path);

    std::string load();
    bool store(std::string_view data);
    bool clear();
    const std::string& lastError() const;
    const std::filesystem::path& path() const;

private:
    std::filesystem::path path_;
    std::string lastError_;
};

class NativeSaveStore final : public ISaveStore {
public:
    explicit NativeSaveStore(const std::filesystem::path& preferenceDirectory);

    std::string load() override;
    bool storeAtomic(std::string_view data) override;
    bool clear() override;
    std::string lastError() const override;
    std::string_view description() const override { return "Native profile (save_v1.txt)"; }
    const std::filesystem::path& path() const;

private:
    AtomicTextFile file_;
};

class NativePreferenceStore final : public IPreferenceStore {
public:
    explicit NativePreferenceStore(const std::filesystem::path& preferenceDirectory);

    AppPreferences load() override;
    bool store(const AppPreferences& preferences) override;
    std::string lastError() const override;
    const std::filesystem::path& path() const;

private:
    void ensureLoaded();

    AtomicTextFile file_;
    AppPreferences cached_;
    bool loaded_ = false;
};

} // namespace rocket

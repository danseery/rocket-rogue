#pragma once

#include "platform/AppServices.h"

#include <filesystem>
#include <unordered_map>

namespace rocket {

class NativeTextureSource final : public ITextureSource {
public:
    explicit NativeTextureSource(std::filesystem::path assetRoot);

    void request(std::string_view key, std::string_view relativePath) override;
    TextureStatus status(std::string_view key) const override;
    bool uploadToOpenGl(std::string_view key, unsigned int texture, int& width, int& height) override;
    std::string lastError() const override;
    TextureDiagnostics diagnostics() const override;

private:
    struct TextureRecord {
        TextureStatus status = TextureStatus::Pending;
        std::filesystem::path path;
        std::vector<unsigned char> rgba;
        int width = 0;
        int height = 0;
        bool uploaded = false;
    };

    std::filesystem::path assetRoot_;
    std::unordered_map<std::string, TextureRecord> records_;
    std::string lastError_;
    TextureDiagnostics diagnostics_;
};

} // namespace rocket

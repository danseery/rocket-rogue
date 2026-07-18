#include "platform/sdl/NativeTextureSource.h"

#include "lodepng.h"

#include <chrono>
#include <utility>

namespace rocket {

NativeTextureSource::NativeTextureSource(std::filesystem::path assetRoot)
    : assetRoot_(std::move(assetRoot))
{
}

void NativeTextureSource::request(std::string_view key, std::string_view relativePath)
{
    const std::string keyCopy(key);
    if (records_.contains(keyCopy)) return;

    TextureRecord record;
    record.path = (assetRoot_ / std::filesystem::path(relativePath)).lexically_normal();
    unsigned width = 0, height = 0;
    const auto decodeStarted = std::chrono::steady_clock::now();
    const unsigned error = lodepng::decode(record.rgba, width, height, record.path.string());
    diagnostics_.decodeMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - decodeStarted).count();
    if (error != 0 || width == 0 || height == 0) {
        record.status = TextureStatus::Failed;
        lastError_ = "Required asset is missing or corrupt: " + record.path.string();
        if (error != 0) lastError_ += " (LodePNG " + std::to_string(error) + ": " + lodepng_error_text(error) + ")";
    } else {
        record.status = TextureStatus::Ready;
        record.width = static_cast<int>(width);
        record.height = static_cast<int>(height);
        ++diagnostics_.decodedTextures;
        diagnostics_.decodedBytes += record.rgba.size();
    }
    records_.emplace(keyCopy, std::move(record));
}

TextureStatus NativeTextureSource::status(std::string_view key) const
{
    const auto found = records_.find(std::string(key));
    return found == records_.end() ? TextureStatus::Pending : found->second.status;
}

std::optional<DecodedImageView> NativeTextureSource::decodedImage(std::string_view key) const
{
    const auto found = records_.find(std::string(key));
    if (found == records_.end() || found->second.status != TextureStatus::Ready
        || found->second.rgba.empty()) {
        return std::nullopt;
    }
    const TextureRecord& record = found->second;
    return DecodedImageView {record.width, record.height, record.rgba};
}

void NativeTextureSource::releaseDecodedImage(std::string_view key)
{
    const auto found = records_.find(std::string(key));
    if (found == records_.end()) return;
    found->second.rgba.clear();
    found->second.rgba.shrink_to_fit();
    found->second.uploaded = true;
}

std::string NativeTextureSource::lastError() const { return lastError_; }
TextureDiagnostics NativeTextureSource::diagnostics() const { return diagnostics_; }

} // namespace rocket

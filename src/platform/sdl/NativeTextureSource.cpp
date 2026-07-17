#include "platform/sdl/NativeTextureSource.h"

#include "render/OpenGlApi.h"
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

bool NativeTextureSource::uploadToOpenGl(std::string_view key, unsigned int texture, int& width, int& height)
{
    auto found = records_.find(std::string(key));
    if (found == records_.end() || found->second.status != TextureStatus::Ready) return false;
    TextureRecord& record = found->second;
    width = record.width;
    height = record.height;
    if (record.uploaded) return true;
    if (record.rgba.empty()) return false;

    const auto uploadStarted = std::chrono::steady_clock::now();
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, record.rgba.data());
    diagnostics_.uploadMilliseconds += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - uploadStarted).count();
    diagnostics_.uploadedBytes += record.rgba.size();
    ++diagnostics_.uploadedTextures;
    record.rgba.clear();
    record.rgba.shrink_to_fit();
    record.uploaded = true;
    return true;
}

std::string NativeTextureSource::lastError() const { return lastError_; }
TextureDiagnostics NativeTextureSource::diagnostics() const { return diagnostics_; }

} // namespace rocket

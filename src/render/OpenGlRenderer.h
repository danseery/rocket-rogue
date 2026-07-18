#pragma once

#include "platform/AppServices.h"
#include "render/SceneAtlas.h"
#include "render/SceneComposer.h"

#include <array>

namespace rocket {

// Web-only WebGL2 graphics backend. Native targets do not compile this source.
class WebGlGraphicsBackend final : public IGameRenderer {
public:
    WebGlGraphicsBackend(IPlatformHost& host, ITextureSource& textures);

    bool initialize() override;
    void render(const RenderSnapshot& snapshot) override;
    void shutdown() override;
    void setPreferences(const AppPreferences& preferences) override;
    RendererDiagnostics diagnostics() const override;
    std::string_view description() const override;

private:
    struct TextureAsset {
        const char* key = nullptr;
        const char* path = nullptr;
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool requested = false;
        bool ready = false;
        bool failed = false;
    };

    bool textureReady(std::size_t pageIndex);
    void warmTextures();
    void flushCommands(const ScenePacket& packet);

    IPlatformHost& host_;
    ITextureSource& textures_;
    SceneComposer composer_;
    unsigned int program_ = 0;
    unsigned int instanceProgram_ = 0;
    unsigned int vao_ = 0;
    unsigned int instanceVao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int miningTerrainVbo_ = 0;
    unsigned int instanceVbo_ = 0;
    unsigned int miningTerrainInstanceVbo_ = 0;
    std::uint64_t miningTerrainRevision_ = 0;
    int useTextureUniform_ = -1;
    int samplerUniform_ = -1;
    int effectModeUniform_ = -1;
    int effectColorUniform_ = -1;
    int effectParamsUniform_ = -1;
    int effectSizeUniform_ = -1;
    int positionScaleUniform_ = -1;
    int positionOffsetUniform_ = -1;
    int instanceSamplerUniform_ = -1;
    int instancePositionScaleUniform_ = -1;
    int instancePositionOffsetUniform_ = -1;
    std::array<TextureAsset, kSceneAtlasPages.size()> assets_ {};
    RendererDiagnostics diagnostics_;
    bool initialized_ = false;
};

} // namespace rocket

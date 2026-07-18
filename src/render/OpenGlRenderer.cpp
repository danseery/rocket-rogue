#include "render/OpenGlRenderer.h"

#include "render/OpenGlApi.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rocket {

namespace {

GLuint compileShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) {
        return shader;
    }
    glDeleteShader(shader);
    return 0;
}

GLuint createWebGl2Program()
{
    constexpr std::string_view shaderHeader = "#version 300 es\n";
    const std::string vertexSource = std::string(shaderHeader) + R"(layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
out vec4 v_color;
out vec2 v_uv;
uniform vec2 u_positionScale;
uniform vec2 u_positionOffset;
void main()
{
    gl_Position = vec4(a_pos * u_positionScale + u_positionOffset, 0.0, 1.0);
    v_color = a_color;
    v_uv = a_uv;
}
)";

    const std::string fragmentSource = std::string(shaderHeader)
        + "precision mediump float;\n"
        + R"(
in vec4 v_color;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_useTexture;
uniform float u_effectMode;
uniform vec4 u_effectColor;
uniform vec4 u_effectParams;
uniform vec2 u_effectSize;
out vec4 out_color;
void main()
{
    if (u_effectMode > 0.5) {
        float gradientWidth = u_effectParams.x;
        float frameWidth = u_effectParams.y;
        float feather = u_effectParams.z;
        float radius = u_effectParams.w;
        vec2 point = (v_uv - vec2(0.5)) * u_effectSize;
        vec2 halfSize = u_effectSize * 0.5;
        vec2 rounded = abs(point) - (halfSize - vec2(radius));
        float signedDistance = length(max(rounded, vec2(0.0)))
            + min(max(rounded.x, rounded.y), 0.0)
            - radius;
        float insideDistance = max(-signedDistance, 0.0);
        float insideMask = 1.0 - smoothstep(0.0, feather, signedDistance);
        float vignette = 1.0 - smoothstep(0.0, gradientWidth, insideDistance);
        float frame = 1.0 - smoothstep(frameWidth, frameWidth + feather, insideDistance);
        float alpha = u_effectColor.a * max(vignette * 0.42, frame) * insideMask;
        out_color = vec4(u_effectColor.rgb, alpha);
        return;
    }
    vec4 sprite = texture(u_texture, v_uv) * v_color;
    out_color = mix(v_color, sprite, u_useTexture);
}
)";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource.c_str());
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource.c_str());
    if (vertex == 0 || fragment == 0) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

GLuint createWebGl2InstanceProgram()
{
    constexpr std::string_view shaderHeader = "#version 300 es\n";
    const std::string vertexSource = std::string(shaderHeader) + R"(
layout(location = 0) in vec2 a_center;
layout(location = 1) in vec2 a_axisX;
layout(location = 2) in vec2 a_axisY;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec2 a_uvMin;
layout(location = 5) in vec2 a_uvMax;
layout(location = 6) in uvec2 a_shape;
out vec4 v_color;
out vec2 v_uv;
out vec2 v_unitPosition;
flat out uint v_shape;
flat out uint v_segments;
uniform vec2 u_positionScale;
uniform vec2 u_positionOffset;
const vec2 unitQuad[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main()
{
    vec2 local = unitQuad[gl_VertexID];
    vec2 position = a_center + a_axisX * local.x + a_axisY * local.y;
    gl_Position = vec4(position * u_positionScale + u_positionOffset, 0.0, 1.0);
    v_color = a_color;
    v_uv = vec2(
        mix(a_uvMin.x, a_uvMax.x, local.x * 0.5 + 0.5),
        mix(a_uvMax.y, a_uvMin.y, local.y * 0.5 + 0.5));
    v_unitPosition = local;
    v_shape = a_shape.x;
    v_segments = a_shape.y;
}
)";

    const std::string fragmentSource = std::string(shaderHeader)
        + "precision mediump float;\nprecision highp int;\n"
        + R"(
in vec4 v_color;
in vec2 v_uv;
in vec2 v_unitPosition;
flat in uint v_shape;
flat in uint v_segments;
uniform sampler2D u_texture;
out vec4 out_color;
const float pi = 3.14159265358979323846;
void main()
{
    uint shape = v_shape & 0x7fu;
    bool textured = (v_shape & 0x80u) != 0u;
    vec4 color = v_color;
    if (textured) {
        color = texture(u_texture, v_uv) * v_color;
    }
    if (shape != 0u) {
        float segments = max(float(v_segments), 3.0);
        float sector = 2.0 * pi / segments;
        float centeredAngle = mod(atan(v_unitPosition.y, v_unitPosition.x), sector)
            - sector * 0.5;
        float polygonRadius = cos(sector * 0.5) / cos(centeredAngle);
        float normalizedRadius = length(v_unitPosition) / polygonRadius;
        if (normalizedRadius > 1.0) {
            discard;
        }
        if (shape == 2u) {
            color.a *= max(0.0, 1.0 - normalizedRadius);
        }
    }
    out_color = color;
}
)";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource.c_str());
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource.c_str());
    if (vertex == 0 || fragment == 0) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

} // namespace

WebGlGraphicsBackend::WebGlGraphicsBackend(IPlatformHost& host, ITextureSource& textures)
    : host_(host), textures_(textures)
{
}

bool WebGlGraphicsBackend::initialize()
{
    for (std::size_t pageIndex = 0; pageIndex < kSceneAtlasPages.size(); ++pageIndex) {
        const SceneAtlasPage& descriptor = kSceneAtlasPages[pageIndex];
        assets_[pageIndex] = {descriptor.key, descriptor.relativePath};
    }

    program_ = createWebGl2Program();
    instanceProgram_ = createWebGl2InstanceProgram();
    if (program_ == 0 || instanceProgram_ == 0) {
        host_.log(PlatformLogLevel::Error, "Failed to compile the WebGL2 scene shader program.");
        shutdown();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glGenVertexArrays(1, &instanceVao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &miningTerrainVbo_);
    glGenBuffers(1, &instanceVbo_);
    glGenBuffers(1, &miningTerrainInstanceVbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneVertex),
        reinterpret_cast<void*>(offsetof(PackedSceneVertex, x)));
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedSceneVertex),
        reinterpret_cast<void*>(offsetof(PackedSceneVertex, r)));
    glVertexAttribPointer(2, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneVertex),
        reinterpret_cast<void*>(offsetof(PackedSceneVertex, u)));
    glUseProgram(program_);
    useTextureUniform_ = glGetUniformLocation(program_, "u_useTexture");
    samplerUniform_ = glGetUniformLocation(program_, "u_texture");
    effectModeUniform_ = glGetUniformLocation(program_, "u_effectMode");
    effectColorUniform_ = glGetUniformLocation(program_, "u_effectColor");
    effectParamsUniform_ = glGetUniformLocation(program_, "u_effectParams");
    effectSizeUniform_ = glGetUniformLocation(program_, "u_effectSize");
    positionScaleUniform_ = glGetUniformLocation(program_, "u_positionScale");
    positionOffsetUniform_ = glGetUniformLocation(program_, "u_positionOffset");
    glUniform1i(samplerUniform_, 0);
    glUniform1f(effectModeUniform_, 0.0F);
    glUniform2f(positionScaleUniform_, 1.0F, 1.0F);
    glUniform2f(positionOffsetUniform_, 0.0F, 0.0F);

    glBindVertexArray(instanceVao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(0, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneInstance),
        reinterpret_cast<void*>(offsetof(PackedSceneInstance, centerX)));
    glVertexAttribPointer(1, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneInstance),
        reinterpret_cast<void*>(offsetof(PackedSceneInstance, axisXx)));
    glVertexAttribPointer(2, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneInstance),
        reinterpret_cast<void*>(offsetof(PackedSceneInstance, axisYx)));
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedSceneInstance),
        reinterpret_cast<void*>(offsetof(PackedSceneInstance, r)));
    glVertexAttribPointer(4, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(PackedSceneInstance),
        reinterpret_cast<void*>(offsetof(PackedSceneInstance, u0)));
    glVertexAttribPointer(5, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(PackedSceneInstance),
        reinterpret_cast<void*>(offsetof(PackedSceneInstance, u1)));
    glVertexAttribIPointer(6, 2, GL_UNSIGNED_BYTE, sizeof(PackedSceneInstance),
        reinterpret_cast<void*>(offsetof(PackedSceneInstance, shape)));
    for (GLuint attribute = 0; attribute <= 6; ++attribute) {
        glVertexAttribDivisor(attribute, 1);
    }
    glUseProgram(instanceProgram_);
    instanceSamplerUniform_ = glGetUniformLocation(instanceProgram_, "u_texture");
    instancePositionScaleUniform_ = glGetUniformLocation(instanceProgram_, "u_positionScale");
    instancePositionOffsetUniform_ = glGetUniformLocation(instanceProgram_, "u_positionOffset");
    glUniform1i(instanceSamplerUniform_, 0);
    glUniform2f(instancePositionScaleUniform_, 1.0F, 1.0F);
    glUniform2f(instancePositionOffsetUniform_, 0.0F, 0.0F);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::vector<GLuint> textureIds(assets_.size());
    glGenTextures(static_cast<GLsizei>(textureIds.size()), textureIds.data());
    for (std::size_t i = 0; i < assets_.size(); ++i) {
        assets_[i].texture = textureIds[i];
        TextureAsset& asset = assets_[i];
        textures_.request(asset.key, asset.path);
        asset.requested = true;
        if (textures_.status(asset.key) == TextureStatus::Failed) {
            host_.log(PlatformLogLevel::Error, textures_.lastError());
            shutdown();
            return false;
        }
    }

    initialized_ = true;
    return true;
}

void WebGlGraphicsBackend::render(const RenderSnapshot& snapshot)
{
    if (!initialized_) {
        return;
    }

    diagnostics_ = {};
    warmTextures();
    const ViewportMetrics metrics = host_.viewportMetrics();
    composer_.setViewport({
        metrics.logicalWidth,
        metrics.logicalHeight,
        metrics.drawableWidth,
        metrics.drawableHeight,
        metrics.densityRatio,
        metrics.sceneLeftNdc
    });
    composer_.setPresentationTime(host_.monotonicSeconds());
    const ScenePacket& packet = composer_.compose(snapshot);

    glViewport(0, 0, std::max(1, metrics.drawableWidth), std::max(1, metrics.drawableHeight));
    glDisable(GL_SCISSOR_TEST);
    glClearColor(packet.clearColor.r, packet.clearColor.g, packet.clearColor.b, packet.clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT);
    flushCommands(packet);
}

bool WebGlGraphicsBackend::textureReady(std::size_t pageIndex)
{
    if (pageIndex >= assets_.size()) {
        return false;
    }
    TextureAsset& asset = assets_[pageIndex];
    if (asset.failed) {
        return false;
    }
    if (!asset.ready && textures_.status(asset.key) == TextureStatus::Ready) {
        const std::optional<DecodedImageView> image = textures_.decodedImage(asset.key);
        if (image && image->valid()) {
            const SceneAtlasPage& page = kSceneAtlasPages[pageIndex];
            if (image->width != page.width || image->height != page.height) {
                asset.failed = true;
                host_.log(
                    PlatformLogLevel::Error,
                    "Generated WebGL scene atlas page dimensions do not match its compiled manifest.");
                return false;
            }
            glBindTexture(GL_TEXTURE_2D, asset.texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                image->width,
                image->height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                image->rgba.data());
            asset.width = image->width;
            asset.height = image->height;
            asset.ready = true;
            textures_.releaseDecodedImage(asset.key);
        }
    }
    return asset.ready;
}

void WebGlGraphicsBackend::warmTextures()
{
    for (std::size_t pageIndex = 0; pageIndex < assets_.size(); ++pageIndex) {
        const bool ready = textureReady(pageIndex);
        if (ready) {
            ++diagnostics_.texturesReady;
        } else if (assets_[pageIndex].failed
            || textures_.status(assets_[pageIndex].key) == TextureStatus::Failed) {
            ++diagnostics_.texturesFailed;
        } else {
            ++diagnostics_.texturesPending;
        }
    }
    for (std::size_t index = 1; index < textureIndex(TextureId::Count); ++index) {
        const TextureId texture = static_cast<TextureId>(index);
        const SceneAtlasTexture& atlasTexture = kSceneAtlasTextures[index];
        bool ready = atlasTexture.frameCount > 0;
        for (std::size_t frame = 0; ready && frame < atlasTexture.frameCount; ++frame) {
            const std::size_t frameIndex = atlasTexture.firstFrame + frame;
            ready = frameIndex < kSceneAtlasFrames.size()
                && kSceneAtlasFrames[frameIndex].page < assets_.size()
                && assets_[kSceneAtlasFrames[frameIndex].page].ready;
        }
        composer_.setTextureReady(texture, ready);
    }
}

void WebGlGraphicsBackend::flushCommands(const ScenePacket& packet)
{
    if (packet.draws.empty()) {
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    if (!packet.vertices.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(packet.vertices.size_bytes()),
            packet.vertices.data(),
            GL_DYNAMIC_DRAW);
        ++diagnostics_.bufferUploads;
        diagnostics_.uploadedBytes += packet.vertices.size_bytes();
    }
    if (!packet.instances.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(packet.instances.size_bytes()),
            packet.instances.data(),
            GL_DYNAMIC_DRAW);
        ++diagnostics_.bufferUploads;
        diagnostics_.uploadedBytes += packet.instances.size_bytes();
    }
    const bool terrainUploadRequired = miningTerrainRevision_ != packet.miningTerrainRevision;
    if (terrainUploadRequired) {
        if (!packet.miningTerrainVertices.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, miningTerrainVbo_);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(packet.miningTerrainVertices.size_bytes()),
                packet.miningTerrainVertices.data(),
                GL_DYNAMIC_DRAW);
            ++diagnostics_.bufferUploads;
            diagnostics_.uploadedBytes += packet.miningTerrainVertices.size_bytes();
        }
        if (!packet.miningTerrainInstances.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, miningTerrainInstanceVbo_);
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(packet.miningTerrainInstances.size_bytes()),
                packet.miningTerrainInstances.data(),
                GL_DYNAMIC_DRAW);
            ++diagnostics_.bufferUploads;
            diagnostics_.uploadedBytes += packet.miningTerrainInstances.size_bytes();
        }
        if (!packet.miningTerrainVertices.empty() || !packet.miningTerrainInstances.empty()) {
            miningTerrainRevision_ = packet.miningTerrainRevision;
        }
    }

    const auto bindVertexStream = [&](SceneVertexStream stream) {
        glBindBuffer(GL_ARRAY_BUFFER,
            stream == SceneVertexStream::MiningTerrain ? miningTerrainVbo_ : vbo_);
        glVertexAttribPointer(0, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneVertex),
            reinterpret_cast<void*>(offsetof(PackedSceneVertex, x)));
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedSceneVertex),
            reinterpret_cast<void*>(offsetof(PackedSceneVertex, r)));
        glVertexAttribPointer(2, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneVertex),
            reinterpret_cast<void*>(offsetof(PackedSceneVertex, u)));
    };

    const auto bindInstanceStream = [&](SceneInstanceStream stream, std::uint32_t firstInstance) {
        glBindBuffer(GL_ARRAY_BUFFER,
            stream == SceneInstanceStream::MiningTerrain
                ? miningTerrainInstanceVbo_
                : instanceVbo_);
        const std::uintptr_t base = static_cast<std::uintptr_t>(firstInstance)
            * sizeof(PackedSceneInstance);
        glVertexAttribPointer(0, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneInstance),
            reinterpret_cast<void*>(base + offsetof(PackedSceneInstance, centerX)));
        glVertexAttribPointer(1, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneInstance),
            reinterpret_cast<void*>(base + offsetof(PackedSceneInstance, axisXx)));
        glVertexAttribPointer(2, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(PackedSceneInstance),
            reinterpret_cast<void*>(base + offsetof(PackedSceneInstance, axisYx)));
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PackedSceneInstance),
            reinterpret_cast<void*>(base + offsetof(PackedSceneInstance, r)));
        glVertexAttribPointer(4, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(PackedSceneInstance),
            reinterpret_cast<void*>(base + offsetof(PackedSceneInstance, u0)));
        glVertexAttribPointer(5, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(PackedSceneInstance),
            reinterpret_cast<void*>(base + offsetof(PackedSceneInstance, u1)));
        glVertexAttribIPointer(6, 2, GL_UNSIGNED_BYTE, sizeof(PackedSceneInstance),
            reinterpret_cast<void*>(base + offsetof(PackedSceneInstance, shape)));
    };

    CoordinateSpace lastCoordinateSpace = CoordinateSpace::World;
    bool transformInitialized = false;
    PipelineClass lastPipeline = PipelineClass::Solid;
    bool pipelineInitialized = false;
    std::size_t lastTexturePage = kSceneAtlasPages.size();
    SceneVertexStream lastVertexStream = SceneVertexStream::Frame;
    bool vertexStreamInitialized = false;
    SceneDrawType lastDrawType = SceneDrawType::Triangles;
    bool drawTypeInitialized = false;
    for (const SceneDraw& command : packet.draws) {
        if (!drawTypeInitialized || command.drawType != lastDrawType) {
            const bool instanced = command.drawType == SceneDrawType::InstancedQuad;
            glUseProgram(instanced ? instanceProgram_ : program_);
            glBindVertexArray(instanced ? instanceVao_ : vao_);
            lastDrawType = command.drawType;
            drawTypeInitialized = true;
            transformInitialized = false;
            pipelineInitialized = false;
        }
        if (command.drawType == SceneDrawType::InstancedQuad) {
            // WebGL2 has no core base-instance draw. Advance the per-instance
            // attribute bases to preserve the packet's ordered range.
            bindInstanceStream(command.instanceStream, command.firstInstance);
        } else if (!vertexStreamInitialized || command.vertexStream != lastVertexStream) {
            bindVertexStream(command.vertexStream);
            lastVertexStream = command.vertexStream;
            vertexStreamInitialized = true;
        }
        if (!transformInitialized || command.coordinateSpace != lastCoordinateSpace) {
            const bool instanced = command.drawType == SceneDrawType::InstancedQuad;
            const GLint scaleUniform = instanced
                ? instancePositionScaleUniform_
                : positionScaleUniform_;
            const GLint offsetUniform = instanced
                ? instancePositionOffsetUniform_
                : positionOffsetUniform_;
            if (command.coordinateSpace == CoordinateSpace::World) {
                glUniform2f(
                    scaleUniform,
                    packet.transform.worldUnitX * 2.0F / std::max(1.0F, packet.transform.cssWidth),
                    packet.transform.worldUnitY * 2.0F / std::max(1.0F, packet.transform.cssHeight));
                glUniform2f(
                    offsetUniform,
                    packet.transform.pixelCenterX * 2.0F / std::max(1.0F, packet.transform.cssWidth) - 1.0F,
                    packet.transform.pixelCenterY * 2.0F / std::max(1.0F, packet.transform.cssHeight) - 1.0F);
            } else {
                glUniform2f(scaleUniform, 1.0F, 1.0F);
                glUniform2f(offsetUniform, 0.0F, 0.0F);
            }
            lastCoordinateSpace = command.coordinateSpace;
            transformInitialized = true;
        }

        if (!pipelineInitialized || command.pipeline != lastPipeline) {
            const bool instanced = command.drawType == SceneDrawType::InstancedQuad;
            if (!instanced) {
                glUniform1f(
                    useTextureUniform_,
                    command.pipeline == PipelineClass::Textured ? 1.0F : 0.0F);
                glUniform1f(
                    effectModeUniform_,
                    command.pipeline == PipelineClass::RoundedFrame ? 1.0F : 0.0F);
            }
            lastPipeline = command.pipeline;
            pipelineInitialized = true;
        }
        if (command.atlasPage != kNoSceneAtlasPage) {
            const std::size_t texturePage = command.atlasPage;
            if (texturePage < assets_.size() && texturePage != lastTexturePage) {
                glBindTexture(GL_TEXTURE_2D, assets_[texturePage].texture);
                lastTexturePage = texturePage;
            }
        }
        if (command.pipeline == PipelineClass::RoundedFrame) {
            glUniform4f(
                effectColorUniform_,
                command.effectColor.r,
                command.effectColor.g,
                command.effectColor.b,
                command.effectColor.a);
            glUniform4f(
                effectParamsUniform_,
                command.effectParams[0],
                command.effectParams[1],
                command.effectParams[2],
                command.effectParams[3]);
            glUniform2f(effectSizeUniform_, command.effectSize[0], command.effectSize[1]);
        }
        if (command.drawType == SceneDrawType::InstancedQuad) {
            glDrawArraysInstanced(
                GL_TRIANGLES,
                0,
                6,
                static_cast<GLsizei>(command.instanceCount));
        } else {
            glDrawArrays(
                GL_TRIANGLES,
                static_cast<GLint>(command.firstVertex),
                static_cast<GLsizei>(command.vertexCount));
        }
    }

    diagnostics_.sceneDrawCalls = static_cast<int>(packet.draws.size());
    diagnostics_.sceneVertices = static_cast<int>(
        packet.vertices.size() + packet.miningTerrainVertices.size()
        + (packet.instances.size() + packet.miningTerrainInstances.size()) * 6U);
}

void WebGlGraphicsBackend::shutdown()
{
    if (!initialized_ && program_ == 0 && instanceProgram_ == 0
        && vao_ == 0 && instanceVao_ == 0
        && vbo_ == 0 && miningTerrainVbo_ == 0
        && instanceVbo_ == 0 && miningTerrainInstanceVbo_ == 0) {
        return;
    }
    std::vector<GLuint> textureIds;
    textureIds.reserve(assets_.size());
    for (std::size_t index = 0; index < assets_.size(); ++index) {
        if (assets_[index].texture != 0) {
            textureIds.push_back(assets_[index].texture);
        }
        assets_[index] = {};
    }
    if (!textureIds.empty()) {
        glDeleteTextures(static_cast<GLsizei>(textureIds.size()), textureIds.data());
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (miningTerrainVbo_ != 0) {
        glDeleteBuffers(1, &miningTerrainVbo_);
    }
    if (instanceVbo_ != 0) {
        glDeleteBuffers(1, &instanceVbo_);
    }
    if (miningTerrainInstanceVbo_ != 0) {
        glDeleteBuffers(1, &miningTerrainInstanceVbo_);
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
    if (instanceVao_ != 0) {
        glDeleteVertexArrays(1, &instanceVao_);
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
    if (instanceProgram_ != 0) {
        glDeleteProgram(instanceProgram_);
    }
    program_ = 0;
    instanceProgram_ = 0;
    vao_ = 0;
    instanceVao_ = 0;
    vbo_ = 0;
    miningTerrainVbo_ = 0;
    instanceVbo_ = 0;
    miningTerrainInstanceVbo_ = 0;
    miningTerrainRevision_ = 0;
    composer_.reset();
    initialized_ = false;
    diagnostics_ = {};
}

void WebGlGraphicsBackend::setPreferences(const AppPreferences& preferences)
{
    composer_.setCameraShakeEnabled(!preferences.cameraShakeDisabled);
}

RendererDiagnostics WebGlGraphicsBackend::diagnostics() const
{
    return diagnostics_;
}

std::string_view WebGlGraphicsBackend::description() const
{
    return "WebGL2 / Emscripten";
}

} // namespace rocket

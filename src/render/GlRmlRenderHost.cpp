#include "render/GlRmlRenderHost.h"

#include "platform/AppServices.h"
#include "render/OpenGlApi.h"

#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>

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

    char log[1024] = {};
    GLsizei length = 0;
    glGetShaderInfoLog(shader, sizeof(log), &length, log);
    Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi shader compile failed: %s", log);
    glDeleteShader(shader);
    return 0;
}

GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_colour");
    glBindAttribLocation(program, 2, "a_tex_coord");
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE) {
        return program;
    }

    char log[1024] = {};
    GLsizei length = 0;
    glGetProgramInfoLog(program, sizeof(log), &length, log);
    Rml::Log::Message(Rml::Log::LT_ERROR, "RmlUi shader link failed: %s", log);
    glDeleteProgram(program);
    return 0;
}

struct RmlGeometry {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei indexCount = 0;
};

} // namespace

class WebGlRmlRenderHost::Impl final : public Rml::RenderInterface {
public:
    ~Impl() override
    {
        shutdown();
    }

    bool initialize()
    {
        if (program_ != 0) {
            return true;
        }

        constexpr std::string_view shaderHeader = "#version 300 es\nprecision highp float;\n";
        const std::string vertexShaderSource = std::string(shaderHeader) + R"(
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec4 a_colour;
layout(location = 2) in vec2 a_tex_coord;
uniform mat4 u_projection;
uniform vec2 u_translation;
out vec4 v_colour;
out vec2 v_tex_coord;
void main() {
    v_colour = a_colour;
    v_tex_coord = a_tex_coord;
    gl_Position = u_projection * vec4(a_position + u_translation, 0.0, 1.0);
}
)";

        const std::string fragmentShaderSource = std::string(shaderHeader) + R"(
in vec4 v_colour;
in vec2 v_tex_coord;
uniform sampler2D u_texture;
uniform int u_has_texture;
out vec4 frag_colour;
void main() {
    vec4 colour = v_colour;
    if (u_has_texture != 0) {
        colour *= texture(u_texture, v_tex_coord);
    }
    frag_colour = colour;
}
)";

        const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
        const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource.c_str());
        if (vertexShader == 0 || fragmentShader == 0) {
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            return false;
        }

        program_ = linkProgram(vertexShader, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        if (program_ == 0) {
            return false;
        }

        projectionLocation_ = glGetUniformLocation(program_, "u_projection");
        translationLocation_ = glGetUniformLocation(program_, "u_translation");
        textureLocation_ = glGetUniformLocation(program_, "u_texture");
        hasTextureLocation_ = glGetUniformLocation(program_, "u_has_texture");
        return true;
    }

    void shutdown()
    {
        if (program_ != 0) {
            glDeleteProgram(program_);
            program_ = 0;
        }
        projectionWidth_ = 0;
        projectionHeight_ = 0;
        frameActive_ = false;
    }

    void setViewport(const RmlRenderViewport& viewport)
    {
        logicalWidth_ = std::max(1, viewport.logicalWidth);
        logicalHeight_ = std::max(1, viewport.logicalHeight);
        drawingWidth_ = std::max(1, viewport.drawableWidth);
        drawingHeight_ = std::max(1, viewport.drawableHeight);
    }

    void setRootClip(const RmlRenderClip& clip)
    {
        rootClip_ = clip.valid()
            ? Rml::Rectanglei::FromCorners({clip.left, clip.top}, {clip.right, clip.bottom})
            : Rml::Rectanglei::MakeInvalid();
    }

    bool beginFrame()
    {
        if (program_ == 0) {
            return false;
        }

        glGetIntegerv(GL_VIEWPORT, previousViewport_);
        previousBlend_ = glIsEnabled(GL_BLEND);
        previousScissor_ = glIsEnabled(GL_SCISSOR_TEST);
        previousDepth_ = glIsEnabled(GL_DEPTH_TEST);
        previousCull_ = glIsEnabled(GL_CULL_FACE);

        glViewport(0, 0, drawingWidth_, drawingHeight_);
        if (previousDepth_) {
            glDisable(GL_DEPTH_TEST);
        }
        if (previousCull_) {
            glDisable(GL_CULL_FACE);
        }
        if (!previousBlend_) {
            glEnable(GL_BLEND);
        }
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(program_);
        if (projectionWidth_ != logicalWidth_ || projectionHeight_ != logicalHeight_) {
            const GLfloat projection[16] = {
                2.0F / static_cast<GLfloat>(logicalWidth_), 0.0F, 0.0F, 0.0F,
                0.0F, -2.0F / static_cast<GLfloat>(logicalHeight_), 0.0F, 0.0F,
                0.0F, 0.0F, -1.0F, 0.0F,
                -1.0F, 1.0F, 0.0F, 1.0F,
            };
            glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, projection);
            projectionWidth_ = logicalWidth_;
            projectionHeight_ = logicalHeight_;
        }
        glUniform1i(textureLocation_, 0);
        glActiveTexture(GL_TEXTURE0);

        frameActive_ = true;
        compiledGeometryThisFrame_ = compiledGeometryPending_;
        compiledGeometryPending_ = 0;
        renderedGeometryThisFrame_ = 0;
        boundTexture_ = std::numeric_limits<GLuint>::max();
        boundVertexArray_ = std::numeric_limits<GLuint>::max();
        hasTextureUniform_ = -1;
        translationX_ = std::numeric_limits<float>::quiet_NaN();
        translationY_ = std::numeric_limits<float>::quiet_NaN();
        scissorStateKnown_ = false;
        scissorRectangleValid_ = false;

        const bool useScissor = scissorEnabled_ || rootClip_.Valid();
        setScissorTest(useScissor);
        if (useScissor) {
            applyScissor();
        }
        return true;
    }

    void endFrame()
    {
        if (!frameActive_) {
            return;
        }

        if (boundVertexArray_ != 0) {
            glBindVertexArray(0);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        if (boundTexture_ != 0) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUseProgram(0);

        setScissorTest(previousScissor_ != GL_FALSE);
        if (!previousBlend_) {
            glDisable(GL_BLEND);
        }
        if (previousDepth_) {
            glEnable(GL_DEPTH_TEST);
        }
        if (previousCull_) {
            glEnable(GL_CULL_FACE);
        }
        glViewport(previousViewport_[0], previousViewport_[1], previousViewport_[2], previousViewport_[3]);
        frameActive_ = false;
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
    {
        if (frameActive_) {
            ++compiledGeometryThisFrame_;
        } else {
            ++compiledGeometryPending_;
        }
        auto geometry = std::make_unique<RmlGeometry>();
        geometry->indexCount = static_cast<GLsizei>(indices.size());

        glGenVertexArrays(1, &geometry->vao);
        glBindVertexArray(geometry->vao);

        glGenBuffers(1, &geometry->vbo);
        glBindBuffer(GL_ARRAY_BUFFER, geometry->vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Rml::Vertex), vertices.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &geometry->ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(int), indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, colour)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), reinterpret_cast<void*>(offsetof(Rml::Vertex, tex_coord)));

        glBindVertexArray(0);
        boundVertexArray_ = 0;
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override
    {
        auto* geometry = reinterpret_cast<RmlGeometry*>(handle);
        if (!geometry || geometry->indexCount == 0) {
            return;
        }

        ++renderedGeometryThisFrame_;
        if (translationX_ != translation.x || translationY_ != translation.y) {
            glUniform2f(translationLocation_, translation.x, translation.y);
            translationX_ = translation.x;
            translationY_ = translation.y;
        }

        const GLuint requestedTexture = static_cast<GLuint>(texture);
        if (boundTexture_ != requestedTexture) {
            glBindTexture(GL_TEXTURE_2D, requestedTexture);
            boundTexture_ = requestedTexture;
        }
        const int requestedHasTexture = requestedTexture != 0 ? 1 : 0;
        if (hasTextureUniform_ != requestedHasTexture) {
            glUniform1i(hasTextureLocation_, requestedHasTexture);
            hasTextureUniform_ = requestedHasTexture;
        }

        if (boundVertexArray_ != geometry->vao) {
            glBindVertexArray(geometry->vao);
            boundVertexArray_ = geometry->vao;
        }
        glDrawElements(GL_TRIANGLES, geometry->indexCount, GL_UNSIGNED_INT, nullptr);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
    {
        auto* geometry = reinterpret_cast<RmlGeometry*>(handle);
        if (!geometry) {
            return;
        }
        glDeleteBuffers(1, &geometry->ibo);
        glDeleteBuffers(1, &geometry->vbo);
        glDeleteVertexArrays(1, &geometry->vao);
        if (boundVertexArray_ == geometry->vao) {
            boundVertexArray_ = std::numeric_limits<GLuint>::max();
        }
        delete geometry;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override
    {
        (void)textureDimensions;
        Rml::Log::Message(Rml::Log::LT_WARNING, "RmlUi texture file loading is not used by Rocket Rogue: %s", source.c_str());
        return 0;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> sourceData, Rml::Vector2i sourceDimensions) override
    {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sourceDimensions.x, sourceDimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, sourceData.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        boundTexture_ = 0;
        return static_cast<Rml::TextureHandle>(texture);
    }

    void ReleaseTexture(Rml::TextureHandle textureHandle) override
    {
        const GLuint texture = static_cast<GLuint>(textureHandle);
        glDeleteTextures(1, &texture);
        if (boundTexture_ == texture) {
            boundTexture_ = std::numeric_limits<GLuint>::max();
        }
    }

    void EnableScissorRegion(bool enable) override
    {
        scissorEnabled_ = enable;
        const bool useScissor = enable || rootClip_.Valid();
        setScissorTest(useScissor);
        if (useScissor) {
            applyScissor();
        }
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        scissorRegion_ = region;
        if (scissorEnabled_) {
            applyScissor();
        }
    }

    UiDiagnostics diagnostics() const
    {
        UiDiagnostics diagnostics;
        diagnostics.compiledGeometry = compiledGeometryThisFrame_;
        diagnostics.renderedGeometry = renderedGeometryThisFrame_;
        return diagnostics;
    }

private:
    void setScissorTest(bool enabled)
    {
        if (scissorStateKnown_ && scissorTestActive_ == enabled) {
            return;
        }
        if (enabled) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        scissorTestActive_ = enabled;
        scissorStateKnown_ = true;
    }

    void applyScissor()
    {
        Rml::Rectanglei region = scissorEnabled_ && scissorRegion_.Valid()
            ? scissorRegion_
            : Rml::Rectanglei::FromSize({logicalWidth_, logicalHeight_});
        if (rootClip_.Valid()) {
            region = region.Intersect(rootClip_);
        }
        const double scaleX = static_cast<double>(drawingWidth_) / static_cast<double>(logicalWidth_);
        const double scaleY = static_cast<double>(drawingHeight_) / static_cast<double>(logicalHeight_);
        const int x = std::clamp(static_cast<int>(std::floor(static_cast<double>(region.p0.x) * scaleX)), 0, drawingWidth_);
        const int right = std::clamp(static_cast<int>(std::ceil(static_cast<double>(region.p1.x) * scaleX)), x, drawingWidth_);
        const int y = std::clamp(static_cast<int>(std::floor(static_cast<double>(logicalHeight_ - region.p1.y) * scaleY)), 0, drawingHeight_);
        const int top = std::clamp(static_cast<int>(std::ceil(static_cast<double>(logicalHeight_ - region.p0.y) * scaleY)), y, drawingHeight_);
        const int width = right - x;
        const int height = top - y;
        if (!scissorRectangleValid_
            || scissorX_ != x || scissorY_ != y
            || scissorWidth_ != width || scissorHeight_ != height) {
            glScissor(x, y, width, height);
            scissorX_ = x;
            scissorY_ = y;
            scissorWidth_ = width;
            scissorHeight_ = height;
            scissorRectangleValid_ = true;
        }
    }

    GLuint program_ = 0;
    GLint projectionLocation_ = -1;
    GLint translationLocation_ = -1;
    GLint textureLocation_ = -1;
    GLint hasTextureLocation_ = -1;
    int logicalWidth_ = 1;
    int logicalHeight_ = 1;
    int drawingWidth_ = 1;
    int drawingHeight_ = 1;
    int projectionWidth_ = 0;
    int projectionHeight_ = 0;
    int compiledGeometryPending_ = 0;
    int compiledGeometryThisFrame_ = 0;
    int renderedGeometryThisFrame_ = 0;
    bool scissorEnabled_ = false;
    bool frameActive_ = false;
    bool scissorStateKnown_ = false;
    bool scissorTestActive_ = false;
    bool scissorRectangleValid_ = false;
    int scissorX_ = 0;
    int scissorY_ = 0;
    int scissorWidth_ = 0;
    int scissorHeight_ = 0;
    GLuint boundTexture_ = std::numeric_limits<GLuint>::max();
    GLuint boundVertexArray_ = std::numeric_limits<GLuint>::max();
    int hasTextureUniform_ = -1;
    float translationX_ = std::numeric_limits<float>::quiet_NaN();
    float translationY_ = std::numeric_limits<float>::quiet_NaN();
    Rml::Rectanglei scissorRegion_ = Rml::Rectanglei::MakeInvalid();
    Rml::Rectanglei rootClip_ = Rml::Rectanglei::MakeInvalid();
    GLboolean previousBlend_ = GL_FALSE;
    GLboolean previousScissor_ = GL_FALSE;
    GLboolean previousDepth_ = GL_FALSE;
    GLboolean previousCull_ = GL_FALSE;
    GLint previousViewport_[4] = {};
};

WebGlRmlRenderHost::WebGlRmlRenderHost()
    : impl_(std::make_unique<Impl>())
{
}

WebGlRmlRenderHost::~WebGlRmlRenderHost() = default;

bool WebGlRmlRenderHost::initialize()
{
    return impl_->initialize();
}

Rml::RenderInterface& WebGlRmlRenderHost::renderInterface()
{
    return *impl_;
}

void WebGlRmlRenderHost::setViewport(const RmlRenderViewport& viewport)
{
    impl_->setViewport(viewport);
}

void WebGlRmlRenderHost::setRootClip(const RmlRenderClip& clip)
{
    impl_->setRootClip(clip);
}

bool WebGlRmlRenderHost::beginFrame()
{
    return impl_->beginFrame();
}

void WebGlRmlRenderHost::endFrame()
{
    impl_->endFrame();
}

UiDiagnostics WebGlRmlRenderHost::diagnostics() const
{
    return impl_->diagnostics();
}

void WebGlRmlRenderHost::shutdown()
{
    impl_->shutdown();
}

} // namespace rocket

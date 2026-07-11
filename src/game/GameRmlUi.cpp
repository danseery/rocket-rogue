#include "game/GameRmlUi.h"

#include <utility>

#ifdef __EMSCRIPTEN__

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include "FontSource.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <emscripten.h>
#include <GLES3/gl3.h>

namespace rocket {
namespace {

constexpr int kPanelWidth = 448;
constexpr int kPanelInset = 16;
constexpr int kPhaseBoardFrameWidth = 736;
constexpr int kPhaseContentLaneWidth = 704;
constexpr int kPhaseLaneInset = 16;
constexpr int kPhaseCardSlotWidth = 170;
constexpr int kPhaseCardGap = 8;
constexpr int kPhaseCommonButtonWidth = 132;
constexpr int kPhaseCommonChipSlotWidth = 132;

EM_JS(double, rr_rml_now_seconds, (), {
    return performance.now() / 1000.0;
});

EM_JS(int, rr_rml_viewport_width, (), {
    const viewport = globalThis.visualViewport;
    return Math.max(1, Math.round((viewport && viewport.width) || globalThis.innerWidth || 1));
});

EM_JS(int, rr_rml_viewport_height, (), {
    const viewport = globalThis.visualViewport;
    return Math.max(1, Math.round((viewport && viewport.height) || globalThis.innerHeight || 1));
});

EM_JS(int, rr_rml_drawing_width, (), {
    const canvas = document.getElementById("canvas");
    return Math.max(1, (canvas && canvas.width) || rr_rml_viewport_width());
});

EM_JS(int, rr_rml_drawing_height, (), {
    const canvas = document.getElementById("canvas");
    return Math.max(1, (canvas && canvas.height) || rr_rml_viewport_height());
});

EM_JS(double, rr_rml_density_ratio, (), {
    const canvas = document.getElementById("canvas");
    const cssWidth = Math.max(1, (canvas && canvas.clientWidth) || rr_rml_viewport_width());
    const drawingWidth = Math.max(1, (canvas && canvas.width) || cssWidth);
    return Math.max(1.0, drawingWidth / cssWidth);
});

EM_JS(void, rr_rml_set_enabled, (int enabled), {
    document.body.classList.toggle("rmlui-enabled", enabled !== 0);
});

EM_JS(void, rr_rml_set_modal_open, (int enabled), {
    const open = enabled !== 0;
    document.body.classList.toggle("rmlui-modal-open", open);
    const launchControl = document.getElementById("scene-launch-control");
    if (launchControl) {
        if (open) {
            launchControl.classList.remove("is-visible");
            launchControl.hidden = true;
            launchControl.setAttribute("aria-hidden", "true");
        } else {
            launchControl.removeAttribute("aria-hidden");
            if (window.RocketBridge && typeof window.RocketBridge.syncSceneOverlays === "function") {
                window.RocketBridge.syncSceneOverlays();
            }
        }
    }
});

EM_JS(double, rr_rml_game_speed_multiplier, (), {
    try {
        const raw = Number(window.localStorage.getItem("rocket_rogue_game_speed") || "1");
        if (!Number.isFinite(raw)) {
            return 1.0;
        }
        return Math.min(8.0, Math.max(0.25, raw));
    } catch (error) {
        return 1.0;
    }
});

EM_JS(void, rr_rml_set_game_speed_multiplier, (const char* value), {
    const raw = UTF8ToString(value || 0);
    const options = ["0.5", "1", "1.5", "2", "3", "5", "8"];
    const numeric = Number(raw);
    const normalized = options.find((candidate) => Number(candidate) === numeric) || "1";
    try {
        if (normalized === "1") {
            window.localStorage.removeItem("rocket_rogue_game_speed");
        } else {
            window.localStorage.setItem("rocket_rogue_game_speed", normalized);
        }
    } catch (error) {
        // Ignore storage failures. The game will fall back to 1x.
    }
});

EM_JS(int, rr_rml_debug_tools_enabled, (), {
    try {
        const debugTools = document.getElementById("debug-tools");
        return window.localStorage.getItem("rocket_rogue_debug_tools") === "1"
            || (debugTools && debugTools.classList.contains("is-visible")) ? 1 : 0;
    } catch (error) {
        return 0;
    }
});

EM_JS(void, rr_rml_set_debug_tools_enabled, (int enabled), {
    try {
        if (enabled) {
            window.localStorage.setItem("rocket_rogue_debug_tools", "1");
        } else {
            window.localStorage.removeItem("rocket_rogue_debug_tools");
        }
    } catch (error) {
        // Ignore storage failures. The overlay will just stay in its current session state.
    }
    const debugTools = document.getElementById("debug-tools");
    if (debugTools) {
        debugTools.classList.toggle("is-visible", !!enabled);
    }
    if (window.RocketBridge && typeof window.RocketBridge.syncDebugToolsControls === "function") {
        window.RocketBridge.syncDebugToolsControls();
    }
});

EM_JS(int, rr_rml_help_disabled, (), {
    try {
        return window.localStorage.getItem("rocket_rogue_help_disabled") === "1" ? 1 : 0;
    } catch (error) {
        return 0;
    }
});

EM_JS(void, rr_rml_set_help_disabled, (int disabled), {
    try {
        if (disabled) {
            window.localStorage.setItem("rocket_rogue_help_disabled", "1");
        } else {
            window.localStorage.removeItem("rocket_rogue_help_disabled");
        }
    } catch (error) {
        // Ignore storage failures. The setting will return to its default next load.
    }
    if (window.RocketBridge && typeof window.RocketBridge.syncSettingsControls === "function") {
        window.RocketBridge.syncSettingsControls();
    }
});

EM_JS(int, rr_rml_camera_shake_disabled, (), {
    try {
        return window.localStorage.getItem("rocket_rogue_camera_shake_disabled") === "1" ? 1 : 0;
    } catch (error) {
        return 0;
    }
});

EM_JS(void, rr_rml_set_camera_shake_disabled, (int disabled), {
    try {
        if (disabled) {
            window.localStorage.setItem("rocket_rogue_camera_shake_disabled", "1");
        } else {
            window.localStorage.removeItem("rocket_rogue_camera_shake_disabled");
        }
    } catch (error) {
        // Ignore storage failures. The setting will return to its default next load.
    }
    if (window.RocketBridge && typeof window.RocketBridge.syncSettingsControls === "function") {
        window.RocketBridge.syncSettingsControls();
    }
});

EM_JS(int, rr_rml_help_topic_hidden, (const char* value), {
    const topic = UTF8ToString(value || 0);
    try {
        if (window.localStorage.getItem("rocket_rogue_help_disabled") === "1") {
            return 1;
        }
        const parsed = JSON.parse(window.localStorage.getItem("rocket_rogue_help_dismissed_v1") || "[]");
        return Array.isArray(parsed) && parsed.includes(topic) ? 1 : 0;
    } catch (error) {
        return 0;
    }
});

EM_JS(void, rr_rml_dismiss_help_topic, (const char* value), {
    const topic = UTF8ToString(value || 0);
    if (!topic) {
        return;
    }
    try {
        const parsed = JSON.parse(window.localStorage.getItem("rocket_rogue_help_dismissed_v1") || "[]");
        const topics = new Set(Array.isArray(parsed) ? parsed.filter((item) => typeof item === "string") : []);
        topics.add(topic);
        window.localStorage.setItem("rocket_rogue_help_dismissed_v1", JSON.stringify(Array.from(topics)));
    } catch (error) {
        // Ignore storage failures. The help card will simply stay visible.
    }
});

class RmlSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override
    {
        return rr_rml_now_seconds();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        (void)type;
        emscripten_log(EM_LOG_CONSOLE, "RmlUi: %s", message.c_str());
        return true;
    }
};

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

class RocketRmlRenderInterface final : public Rml::RenderInterface {
public:
    ~RocketRmlRenderInterface() override
    {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
    }

    bool initialize()
    {
        static constexpr const char* kVertexShader = R"(#version 300 es
precision highp float;
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

        static constexpr const char* kFragmentShader = R"(#version 300 es
precision highp float;
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

        const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
        const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
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

    explicit operator bool() const { return program_ != 0; }

    void setViewport(int width, int height)
    {
        viewportWidth_ = std::max(1, width);
        viewportHeight_ = std::max(1, height);
    }

    void setRootClip(Rml::Rectanglei clip)
    {
        rootClip_ = clip;
    }

    void beginFrame()
    {
        glGetIntegerv(GL_VIEWPORT, previousViewport_);
        previousBlend_ = glIsEnabled(GL_BLEND);
        previousScissor_ = glIsEnabled(GL_SCISSOR_TEST);
        previousDepth_ = glIsEnabled(GL_DEPTH_TEST);
        previousCull_ = glIsEnabled(GL_CULL_FACE);

        glViewport(0, 0, viewportWidth_, viewportHeight_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (scissorEnabled_ || rootClip_.Valid()) {
            glEnable(GL_SCISSOR_TEST);
            applyScissor();
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    void endFrame()
    {
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);

        glDisable(GL_SCISSOR_TEST);
        if (previousBlend_) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        if (previousScissor_) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
        if (previousDepth_) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        if (previousCull_) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
        glViewport(previousViewport_[0], previousViewport_[1], previousViewport_[2], previousViewport_[3]);
    }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
    {
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
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override
    {
        auto* geometry = reinterpret_cast<RmlGeometry*>(handle);
        if (!geometry || geometry->indexCount == 0) {
            return;
        }

        const GLfloat projection[16] = {
            2.0F / static_cast<GLfloat>(viewportWidth_), 0.0F, 0.0F, 0.0F,
            0.0F, -2.0F / static_cast<GLfloat>(viewportHeight_), 0.0F, 0.0F,
            0.0F, 0.0F, -1.0F, 0.0F,
            -1.0F, 1.0F, 0.0F, 1.0F,
        };

        glUseProgram(program_);
        glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, projection);
        glUniform2f(translationLocation_, translation.x, translation.y);
        glUniform1i(textureLocation_, 0);
        glActiveTexture(GL_TEXTURE0);

        if (texture != 0) {
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
            glUniform1i(hasTextureLocation_, 1);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(hasTextureLocation_, 0);
        }

        if (rootClip_.Valid() || scissorEnabled_) {
            glEnable(GL_SCISSOR_TEST);
            applyScissor();
        }

        glBindVertexArray(geometry->vao);
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
        delete geometry;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override
    {
        (void)textureDimensions;
        Rml::Log::Message(Rml::Log::LT_WARNING, "RmlUi texture file loading is not available in the web build: %s", source.c_str());
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
        return static_cast<Rml::TextureHandle>(texture);
    }

    void ReleaseTexture(Rml::TextureHandle textureHandle) override
    {
        const GLuint texture = static_cast<GLuint>(textureHandle);
        glDeleteTextures(1, &texture);
    }

    void EnableScissorRegion(bool enable) override
    {
        scissorEnabled_ = enable;
        if (enable || rootClip_.Valid()) {
            glEnable(GL_SCISSOR_TEST);
            applyScissor();
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        scissorRegion_ = region;
        if (scissorEnabled_) {
            applyScissor();
        }
    }

private:
    void applyScissor() const
    {
        Rml::Rectanglei region = scissorEnabled_ && scissorRegion_.Valid()
            ? scissorRegion_
            : Rml::Rectanglei::FromSize({viewportWidth_, viewportHeight_});
        if (rootClip_.Valid()) {
            region = region.Intersect(rootClip_);
        }
        const int x = region.p0.x;
        const int y = viewportHeight_ - region.p1.y;
        const int width = region.Width();
        const int height = region.Height();
        glScissor(x, y, width, height);
    }

    GLuint program_ = 0;
    GLint projectionLocation_ = -1;
    GLint translationLocation_ = -1;
    GLint textureLocation_ = -1;
    GLint hasTextureLocation_ = -1;
    int viewportWidth_ = 1;
    int viewportHeight_ = 1;
    bool scissorEnabled_ = false;
    Rml::Rectanglei scissorRegion_ = Rml::Rectanglei::MakeInvalid();
    Rml::Rectanglei rootClip_ = Rml::Rectanglei::MakeInvalid();
    GLboolean previousBlend_ = GL_FALSE;
    GLboolean previousScissor_ = GL_FALSE;
    GLboolean previousDepth_ = GL_FALSE;
    GLboolean previousCull_ = GL_FALSE;
    GLint previousViewport_[4] = {};
};

struct ModalTemplate {
    std::string id;
    std::string title;
    std::string body;
    bool autoOpen = false;
};

struct ElementButtonBinding {
    Rml::Element* element = nullptr;
    RmlButtonBinding binding;
};

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string attributeValue(std::string_view tag, std::string_view name)
{
    const std::string needle = std::string(name) + "=\"";
    const std::size_t start = tag.find(needle);
    if (start == std::string_view::npos) {
        return {};
    }
    const std::size_t valueStart = start + needle.size();
    const std::size_t valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string_view::npos) {
        return {};
    }
    return std::string(tag.substr(valueStart, valueEnd - valueStart));
}

std::vector<ModalTemplate> extractModals(const std::string& html)
{
    std::vector<ModalTemplate> modals;
    std::size_t search = 0;
    while (true) {
        const std::size_t open = html.find("<template", search);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t tagEnd = html.find('>', open);
        const std::size_t close = tagEnd == std::string::npos ? std::string::npos : html.find("</template>", tagEnd + 1);
        if (tagEnd == std::string::npos || close == std::string::npos) {
            break;
        }

        const std::string_view tag(html.data() + open, tagEnd - open + 1);
        ModalTemplate modal;
        modal.id = attributeValue(tag, "data-modal");
        modal.title = attributeValue(tag, "data-title");
        modal.autoOpen = attributeValue(tag, "data-auto-modal") == "1";
        modal.body = html.substr(tagEnd + 1, close - tagEnd - 1);
        if (!modal.id.empty()) {
            modals.push_back(std::move(modal));
        }
        search = close + std::string_view("</template>").size();
    }
    return modals;
}

std::string removeTemplates(std::string html)
{
    std::size_t search = 0;
    while (true) {
        const std::size_t open = html.find("<template", search);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t close = html.find("</template>", open);
        if (close == std::string::npos) {
            html.erase(open);
            break;
        }
        html.erase(open, close + std::string_view("</template>").size() - open);
        search = open;
    }
    return html;
}

std::string removeHiddenElements(std::string html)
{
    std::size_t hiddenSearch = 0;
    while ((hiddenSearch = html.find(" hidden", hiddenSearch)) != std::string::npos) {
        const std::size_t tagStart = html.rfind('<', hiddenSearch);
        const std::size_t tagEnd = html.find('>', hiddenSearch);
        if (tagStart == std::string::npos || tagEnd == std::string::npos || html.compare(tagStart, 2, "</") == 0) {
            hiddenSearch += 7;
            continue;
        }

        const std::size_t tagNameStart = tagStart + 1;
        std::size_t tagNameEnd = tagNameStart;
        while (tagNameEnd < tagEnd && !std::isspace(static_cast<unsigned char>(html[tagNameEnd])) && html[tagNameEnd] != '>') {
            ++tagNameEnd;
        }

        const std::string tagName = html.substr(tagNameStart, tagNameEnd - tagNameStart);
        const std::string closeTag = "</" + tagName + ">";
        const std::size_t closeStart = html.find(closeTag, tagEnd + 1);
        if (closeStart == std::string::npos) {
            html.erase(tagStart, tagEnd - tagStart + 1);
            hiddenSearch = tagStart;
            continue;
        }

        html.erase(tagStart, closeStart + closeTag.size() - tagStart);
        hiddenSearch = tagStart;
    }
    return html;
}

std::string removeDismissedHelpCards(std::string html)
{
    std::size_t search = 0;
    while ((search = html.find("data-help-topic", search)) != std::string::npos) {
        const std::size_t tagStart = html.rfind('<', search);
        const std::size_t tagEnd = html.find('>', search);
        if (tagStart == std::string::npos || tagEnd == std::string::npos || html.compare(tagStart, 2, "</") == 0) {
            search += std::string_view("data-help-topic").size();
            continue;
        }

        const std::string_view tag(html.data() + tagStart, tagEnd - tagStart + 1);
        const std::string topic = attributeValue(tag, "data-help-topic");
        if (topic.empty() || rr_rml_help_topic_hidden(topic.c_str()) == 0) {
            search = tagEnd + 1;
            continue;
        }

        const std::size_t tagNameStart = tagStart + 1;
        std::size_t tagNameEnd = tagNameStart;
        while (tagNameEnd < tagEnd && !std::isspace(static_cast<unsigned char>(html[tagNameEnd])) && html[tagNameEnd] != '>') {
            ++tagNameEnd;
        }

        const std::string tagName = html.substr(tagNameStart, tagNameEnd - tagNameStart);
        const std::string closeTag = "</" + tagName + ">";
        const std::size_t closeStart = html.find(closeTag, tagEnd + 1);
        if (closeStart == std::string::npos) {
            html.erase(tagStart, tagEnd - tagStart + 1);
        } else {
            html.erase(tagStart, closeStart + closeTag.size() - tagStart);
        }
        search = tagStart;
    }
    return html;
}

std::string normalizeBooleanAttributes(std::string html)
{
    static constexpr std::string_view names[] = {
        "disabled", "checked", "selected", "data-preflight-launch", "data-arrival-fanfare",
        "data-flyby-run", "data-flyby-stamp", "data-orbit-run", "data-orbit-stamp",
        "data-help-settings", "data-help-toggle", "data-camera-shake-settings", "data-camera-shake-toggle", "data-game-speed-settings", "data-game-speed-select",
        "data-debug-tools-settings", "data-debug-tools-toggle"
    };

    for (const std::string_view name : names) {
        replaceAll(html, std::string(" ") + std::string(name) + " ", std::string(" ") + std::string(name) + "=\"1\" ");
        replaceAll(html, std::string(" ") + std::string(name) + ">", std::string(" ") + std::string(name) + "=\"1\">");
        replaceAll(html, std::string(" ") + std::string(name) + "/>", std::string(" ") + std::string(name) + "=\"1\"/>");
    }

    replaceAll(html, "<input ", "<input ");
    replaceAll(html, "checked><span>", "checked=\"1\"/><span>");
    replaceAll(html, "checked><", "checked=\"1\"/><");
    return html;
}

std::string sanitizeRml(std::string html)
{
    html = normalizeBooleanAttributes(std::move(html));
    html = removeHiddenElements(std::move(html));
    html = removeDismissedHelpCards(std::move(html));

    replaceAll(html, "<section", "<div");
    replaceAll(html, "</section>", "</div>");
    replaceAll(html, "<article", "<div");
    replaceAll(html, "</article>", "</div>");
    replaceAll(html, "<small", "<span");
    replaceAll(html, "</small>", "</span>");
    replaceAll(html, "<ul", "<div");
    replaceAll(html, "</ul>", "</div>");
    replaceAll(html, "<ol", "<div");
    replaceAll(html, "</ol>", "</div>");
    replaceAll(html, "<li", "<p");
    replaceAll(html, "</li>", "</p>");
    replaceAll(html, "<label", "<div");
    replaceAll(html, "</label>", "</div>");
    replaceAll(html, "<input", "<span");
    return html;
}

std::string currentGameSpeedOptionValue()
{
    const double speed = rr_rml_game_speed_multiplier();
    struct Option {
        double numeric;
        const char* value;
    };
    static constexpr Option options[] = {
        {0.5, "0.5"},
        {1.0, "1"},
        {1.5, "1.5"},
        {2.0, "2"},
        {3.0, "3"},
        {5.0, "5"},
        {8.0, "8"},
    };

    for (const Option& option : options) {
        if (std::abs(speed - option.numeric) < 0.01) {
            return option.value;
        }
    }
    return "1";
}

std::string selectCurrentGameSpeed(std::string html)
{
    if (html.find("data-game-speed-select") == std::string::npos) {
        return html;
    }

    const std::string value = currentGameSpeedOptionValue();
    const std::string needle = "<option value=\"" + value + "\">";
    const std::size_t optionStart = html.find(needle);
    if (optionStart != std::string::npos) {
        html.insert(optionStart + needle.size() - 1, " selected=\"1\"");
    }
    return html;
}

std::string syncCurrentDebugToolsToggle(std::string html)
{
    if (html.find("data-debug-tools-toggle") == std::string::npos) {
        return html;
    }

    const std::string needle = "data-debug-tools-toggle";
    const std::size_t attrStart = html.find(needle);
    if (attrStart == std::string::npos) {
        return html;
    }
    const std::size_t tagStart = html.rfind('<', attrStart);
    const std::size_t tagEnd = html.find('>', attrStart);
    const std::size_t closeStart = html.find("</button>", tagEnd == std::string::npos ? attrStart : tagEnd);
    if (tagStart != std::string::npos && tagEnd != std::string::npos && closeStart != std::string::npos) {
        const bool enabled = rr_rml_debug_tools_enabled() != 0;
        const std::string label = enabled ? "Hide debug tools" : "Show debug tools";
        html.replace(tagEnd + 1, closeStart - tagEnd - 1, label);
    }
    return html;
}

std::string syncCurrentHelpToggle(std::string html)
{
    if (html.find("data-help-toggle") == std::string::npos) {
        return html;
    }

    const std::string needle = "data-help-toggle";
    const std::size_t attrStart = html.find(needle);
    if (attrStart == std::string::npos) {
        return html;
    }
    const std::size_t tagEnd = html.find('>', attrStart);
    const std::size_t closeStart = html.find("</button>", tagEnd == std::string::npos ? attrStart : tagEnd);
    if (tagEnd != std::string::npos && closeStart != std::string::npos) {
        const bool enabled = rr_rml_help_disabled() == 0;
        const std::string label = enabled ? "Hide mission help" : "Show mission help";
        html.replace(tagEnd + 1, closeStart - tagEnd - 1, label);
    }
    return html;
}

std::string syncCurrentCameraShakeToggle(std::string html)
{
    if (html.find("data-camera-shake-toggle") == std::string::npos) {
        return html;
    }

    const std::string needle = "data-camera-shake-toggle";
    const std::size_t attrStart = html.find(needle);
    if (attrStart == std::string::npos) {
        return html;
    }
    const std::size_t tagEnd = html.find('>', attrStart);
    const std::size_t closeStart = html.find("</button>", tagEnd == std::string::npos ? attrStart : tagEnd);
    if (tagEnd != std::string::npos && closeStart != std::string::npos) {
        const bool disabled = rr_rml_camera_shake_disabled() != 0;
        const std::string label = disabled ? "Enable camera shake" : "Disable camera shake";
        html.replace(tagEnd + 1, closeStart - tagEnd - 1, label);
    }
    return html;
}

std::string syncSettingsControls(std::string html)
{
    return syncCurrentCameraShakeToggle(syncCurrentHelpToggle(syncCurrentDebugToolsToggle(selectCurrentGameSpeed(std::move(html)))));
}

std::string collapsedText(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace && !out.empty()) {
            out.push_back(' ');
        }
        pendingSpace = false;
        out.push_back(ch);
    }
    return out;
}

std::string textFromMarkup(std::string_view markup)
{
    std::string text;
    text.reserve(markup.size());
    bool inTag = false;
    for (const char ch : markup) {
        if (ch == '<') {
            inTag = true;
            text.push_back(' ');
            continue;
        }
        if (ch == '>') {
            inTag = false;
            text.push_back(' ');
            continue;
        }
        if (!inTag) {
            text.push_back(ch);
        }
    }
    return collapsedText(text);
}

std::string encodeClassToken(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
            out.push_back(ch);
        } else if (ch == ':') {
            out += "_COLON_";
        }
    }
    return out;
}

std::string decodeClassToken(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (std::size_t index = 0; index < value.size();) {
        if (value.substr(index, 7) == "_COLON_") {
            out.push_back(':');
            index += 7;
        } else {
            out.push_back(value[index]);
            ++index;
        }
    }
    return out;
}

std::string classTokenValue(std::string_view classes, std::string_view prefix)
{
    std::size_t start = 0;
    while (start < classes.size()) {
        while (start < classes.size() && std::isspace(static_cast<unsigned char>(classes[start]))) {
            ++start;
        }
        std::size_t end = start;
        while (end < classes.size() && !std::isspace(static_cast<unsigned char>(classes[end]))) {
            ++end;
        }
        const std::string_view token = classes.substr(start, end - start);
        if (startsWith(token, prefix)) {
            return decodeClassToken(token.substr(prefix.size()));
        }
        start = end;
    }
    return {};
}

std::string idTokenValue(std::string_view id, std::string_view prefix)
{
    if (!startsWith(id, prefix)) {
        return {};
    }
    std::string_view value = id.substr(prefix.size());
    const std::size_t suffix = value.rfind("__");
    if (suffix != std::string_view::npos) {
        value = value.substr(0, suffix);
    }
    return decodeClassToken(value);
}

void appendTextBlock(std::string& out, std::string_view tag, std::string_view text)
{
    const std::string collapsed = collapsedText(text);
    if (collapsed.empty()) {
        return;
    }

    const std::string element = (tag == "h1" || tag == "h2" || tag == "h3") ? std::string(tag) : std::string("p");
    out += "<" + element + ">";
    out += collapsed;
    out += "</" + element + ">";
}

std::string linearizeRml(std::string html)
{
    html = removeHiddenElements(normalizeBooleanAttributes(std::move(html)));

    std::string out = "<div class=\"rml-stack\">";
    std::string textBuffer;
    std::string blockTag = "p";
    std::size_t pos = 0;
    int buttonIndex = 0;

    auto flush = [&]() {
        appendTextBlock(out, blockTag, textBuffer);
        textBuffer.clear();
        blockTag = "p";
    };

    while (pos < html.size()) {
        const std::size_t tagStart = html.find('<', pos);
        if (tagStart == std::string::npos) {
            textBuffer += html.substr(pos);
            break;
        }

        textBuffer += html.substr(pos, tagStart - pos);
        const std::size_t tagEnd = html.find('>', tagStart);
        if (tagEnd == std::string::npos) {
            break;
        }

        const std::string_view tag(html.data() + tagStart, tagEnd - tagStart + 1);
        const bool closing = tag.size() > 2 && tag[1] == '/';
        const std::size_t nameStart = tagStart + (closing ? 2 : 1);
        std::size_t nameEnd = nameStart;
        while (nameEnd < tagEnd && !std::isspace(static_cast<unsigned char>(html[nameEnd])) && html[nameEnd] != '>' && html[nameEnd] != '/') {
            ++nameEnd;
        }
        const std::string name = html.substr(nameStart, nameEnd - nameStart);

        if (!closing && name == "button") {
            flush();
            const std::size_t closeStart = html.find("</button>", tagEnd + 1);
            const std::size_t contentEnd = closeStart == std::string::npos ? tagEnd : closeStart;
            std::string cssClass = attributeValue(tag, "class");
            const std::string action = attributeValue(tag, "data-rr-action");
            const std::string modal = attributeValue(tag, "data-ui-modal");
            const std::string closeModal = attributeValue(tag, "data-ui-close-modal");
            const std::string helpDismiss = attributeValue(tag, "data-help-dismiss");
            const std::string label = textFromMarkup(std::string_view(html.data() + tagEnd + 1, contentEnd - tagEnd - 1));
            if (!action.empty()) {
                cssClass += " rr_action_" + encodeClassToken(action);
            }
            if (!modal.empty()) {
                cssClass += " rr_modal_" + encodeClassToken(modal);
            }
            if (!closeModal.empty()) {
                cssClass += " rr_close";
            }
            if (!helpDismiss.empty()) {
                cssClass += " rr_help_dismiss";
            }
            std::string id;
            if (!action.empty()) {
                id = "rr_action_" + encodeClassToken(action);
            } else if (!modal.empty()) {
                id = "rr_modal_" + encodeClassToken(modal);
            } else if (!closeModal.empty()) {
                id = "rr_close";
            } else if (!helpDismiss.empty()) {
                id = "rr_help_dismiss";
            }
            if (!id.empty()) {
                id += "__" + std::to_string(buttonIndex++);
            }
            out += "<button";
            if (!id.empty()) {
                out += " id=\"" + id + "\"";
            }
            out += " class=\"" + cssClass + "\"";
            if (!action.empty()) {
                out += " rr_action=\"" + action + "\"";
                out += " data-rr-action=\"" + action + "\"";
            }
            if (!modal.empty()) {
                out += " rr_modal=\"" + modal + "\"";
                out += " data-ui-modal=\"" + modal + "\"";
            }
            if (!closeModal.empty()) {
                out += " rr_close=\"1\"";
                out += " data-ui-close-modal=\"" + closeModal + "\"";
            }
            if (!helpDismiss.empty()) {
                out += " rr_help_dismiss=\"" + helpDismiss + "\"";
                out += " data-help-dismiss=\"" + helpDismiss + "\"";
            }
            out += ">";
            out += label.empty() ? std::string("Continue") : label;
            out += "</button>";
            pos = closeStart == std::string::npos ? tagEnd + 1 : closeStart + std::string_view("</button>").size();
            continue;
        }

        if (!closing && (name == "h1" || name == "h2" || name == "h3")) {
            flush();
            blockTag = name;
        } else if (closing && (name == "h1" || name == "h2" || name == "h3" || name == "p" || name == "li" || name == "div" ||
                                 name == "section" || name == "article")) {
            flush();
        } else {
            textBuffer.push_back(' ');
        }

        pos = tagEnd + 1;
    }

    flush();
    out += "</div>";
    return out;
}

std::vector<RmlButtonBinding> extractButtonBindings(const std::string& html)
{
    std::vector<RmlButtonBinding> bindings;
    std::size_t pos = 0;
    while (true) {
        const std::size_t tagStart = html.find("<button", pos);
        if (tagStart == std::string::npos) {
            break;
        }
        const std::size_t tagEnd = html.find('>', tagStart);
        if (tagEnd == std::string::npos) {
            break;
        }
        const std::size_t closeStart = html.find("</button>", tagEnd + 1);
        if (closeStart == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }

        const std::string_view tag(html.data() + tagStart, tagEnd - tagStart + 1);
        RmlButtonBinding binding;
        binding.label = textFromMarkup(std::string_view(html.data() + tagEnd + 1, closeStart - tagEnd - 1));
        binding.action = attributeValue(tag, "data-rr-action");
        binding.modal = attributeValue(tag, "data-ui-modal");
        binding.helpDismiss = attributeValue(tag, "data-help-dismiss");
        binding.close = !attributeValue(tag, "data-ui-close-modal").empty();
        binding.helpToggle = !attributeValue(tag, "data-help-toggle").empty();
        binding.cameraShakeToggle = !attributeValue(tag, "data-camera-shake-toggle").empty();
        binding.debugToolsToggle = !attributeValue(tag, "data-debug-tools-toggle").empty();
        bindings.push_back(std::move(binding));
        pos = closeStart + std::string_view("</button>").size();
    }
    return bindings;
}

RmlPanelMode panelModeForHtml(std::string_view html)
{
    if (html.find("data-panel-mode=\"mining-fullscreen\"") != std::string_view::npos) {
        return RmlPanelMode::MiningFullscreen;
    }
    if (html.find("data-panel-mode=\"arrival-fanfare\"") != std::string_view::npos) {
        return RmlPanelMode::ArrivalFanfare;
    }
    if (html.find("data-panel-mode=\"phase-board\"") != std::string_view::npos) {
        return RmlPanelMode::PhaseBoard;
    }
    return RmlPanelMode::Control;
}

bool panelUsesPhaseBoard(RmlPanelMode mode)
{
    return mode == RmlPanelMode::PhaseBoard;
}

bool panelUsesMiningFullscreen(RmlPanelMode mode)
{
    return mode == RmlPanelMode::MiningFullscreen;
}

bool panelUsesArrivalFanfare(RmlPanelMode mode)
{
    return mode == RmlPanelMode::ArrivalFanfare;
}

bool panelUsesSurfaceOps(std::string_view html)
{
    return html.find("surface-ops-screen") != std::string_view::npos;
}

bool panelUsesDroneOps(std::string_view html)
{
    return html.find("phase-board-drone-ops") != std::string_view::npos;
}

bool panelUsesNavigation(std::string_view html)
{
    return html.find("phase-board-navigation") != std::string_view::npos;
}

bool panelUsesDraftRoom(std::string_view html)
{
    return html.find("phase-board-draft-room") != std::string_view::npos;
}

Rml::Rectanglei panelBounds(RmlPanelMode mode)
{
    const int viewportWidth = rr_rml_viewport_width();
    const int viewportHeight = rr_rml_viewport_height();
    if (mode == RmlPanelMode::MiningFullscreen) {
        return Rml::Rectanglei::FromPositionSize({0, 0}, {std::max(1, viewportWidth), std::max(1, viewportHeight)});
    }
    if (mode == RmlPanelMode::ArrivalFanfare) {
        const int width = std::clamp(viewportWidth - 48, 320, 520);
        const int height = std::clamp(viewportHeight - 48, 210, 258);
        const int left = std::max(16, (viewportWidth - width) / 2);
        const int top = std::max(16, (viewportHeight - height) / 2 - 24);
        return Rml::Rectanglei::FromPositionSize({left, top}, {width, height});
    }
    const bool phaseBoard = panelUsesPhaseBoard(mode);
    const int left = phaseBoard ? 16 : kPanelInset;
    const int top = phaseBoard ? 16 : kPanelInset;
    const int width = phaseBoard ? std::clamp(viewportWidth - 64, 560, kPhaseBoardFrameWidth) : kPanelWidth + 34;
    const int height = phaseBoard
        ? std::max(420, viewportHeight - 32)
        : std::max(1, viewportHeight - 56);
    return Rml::Rectanglei::FromPositionSize({left, top}, {width, height});
}

Rml::Rectanglei expandedPanelClip(RmlPanelMode mode)
{
    const Rml::Rectanglei bounds = panelBounds(mode);
    if (mode == RmlPanelMode::MiningFullscreen) {
        return bounds;
    }
    return Rml::Rectanglei::FromPositionSize(
        {std::max(0, bounds.Left() - 4), std::max(0, bounds.Top() - 4)},
        {bounds.Width() + 40, bounds.Height() + 40});
}

const ModalTemplate* findModal(const std::vector<ModalTemplate>& modals, std::string_view id)
{
    const auto it = std::find_if(modals.begin(), modals.end(), [id](const ModalTemplate& modal) {
        return modal.id == id;
    });
    return it == modals.end() ? nullptr : &*it;
}

std::string panelRcss(RmlPanelMode mode)
{
    const Rml::Rectanglei bounds = panelBounds(mode);
    const int panelWidth = bounds.Width();
    const int panelHeight = std::max(180, bounds.Height());
    const int arrivalTitleSize = panelWidth < 420 ? 34 : 46;
    const int left = bounds.Left();
    const int top = bounds.Top();
    const bool compactMining = panelWidth < 1220;
    const int miningInset = compactMining ? 10 : 18;
    const int miningRailWidth = std::max(1, panelWidth - miningInset * 2);
    const int miningTopHeight = compactMining ? 118 : 62;
    const int miningBottomHeight = compactMining ? 164 : 126;
    const int miningBottomTop = std::max(miningTopHeight + 18, panelHeight - miningBottomHeight - miningInset);
    const int miningUtilityWidth = compactMining ? 104 : 108;
    const int miningTitleWidth = compactMining ? std::max(1, miningRailWidth - miningUtilityWidth - 12) : 246;
    const int miningVitalWidth = compactMining ? 78 : 86;
    const int miningVitalGap = 5;
    const int miningVitalsWidth = compactMining ? miningRailWidth : (miningVitalWidth * 6 + miningVitalGap * 5 + 84);
    const int miningVitalsLeft = compactMining ? 0 : std::max(0, (miningRailWidth - miningVitalsWidth) / 2);
    const int miningPlayfieldTop = miningTopHeight + miningInset + (compactMining ? 30 : 40);
    const int miningPayloadWidth = compactMining ? miningRailWidth : std::max(260, miningRailWidth - 378);
    const int miningActionWidth = compactMining ? 126 : 154;
    const int miningCommandWidth = compactMining ? miningRailWidth : std::min(380, miningRailWidth);
    const int miningCommandLeft = compactMining ? 0 : std::max(0, miningRailWidth - miningCommandWidth - 4);
    const int miningPrimaryActionWidth = compactMining ? std::min(180, miningRailWidth) : 170;
    const int miningPrimaryActionLeft = std::max(0, (miningRailWidth - miningPrimaryActionWidth) / 2);
    const int miningPrimaryActionTop = std::max(8, miningBottomHeight - 56);

    return R"(
body {
    width: 100%;
    height: 100%;
    overflow: hidden;
    color: #edf4f8;
    font-family: rmlui-debugger-font;
    font-size: 14px;
}
div, p, h1, h2, h3, aside {
    display: block;
}
span, strong, small {
    display: block;
}
scrollbarvertical,
scrollbarhorizontal,
scrollbarcorner {
    display: none;
    visibility: hidden;
    width: 0px;
    height: 0px;
    background-color: transparent;
    border-width: 0px;
}
scrollbarvertical slidertrack,
scrollbarhorizontal slidertrack,
scrollbarvertical sliderbar,
scrollbarhorizontal sliderbar {
    display: none;
    visibility: hidden;
    width: 0px;
    height: 0px;
    background-color: transparent;
    border-width: 0px;
}
#rr-panel {
    position: absolute;
    left: )" + std::to_string(left) + R"(px;
    top: )" + std::to_string(top) + R"(px;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow: hidden;
    padding: 14px;
    background-color: #0b1118;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 8px;
}
#rr-panel.surface-ops-panel {
    height: 704px;
    overflow: hidden;
}
#rr-panel.navigation-panel {
    height: 576px;
    overflow: hidden;
}
#rr-panel.arrival-fanfare-panel-mode {
    box-sizing: border-box;
    overflow: hidden;
    padding: 10px;
    background-color: #0b101a;
    border-width: 1px;
    border-color: #8a7131;
    border-radius: 12px;
}
#rr-panel.arrival-fanfare-panel-mode > .panel-head,
#rr-panel.arrival-fanfare-panel-mode > .status,
#rr-panel.arrival-fanfare-panel-mode > .panel-kpis {
    display: none;
}
.arrival-fanfare-panel {
    box-sizing: border-box;
    display: block;
    width: 100%;
    height: 100%;
    padding: 10px 8px;
    background-color: #0d1320;
    border-width: 1px;
    border-color: #31566b;
    border-radius: 9px;
}
.arrival-stamp-kicker {
    color: #ffd166;
    font-size: 13px;
    font-weight: bold;
    line-height: 1;
    text-transform: uppercase;
}
.arrival-stamp-title {
    width: 100%;
    margin-top: 7px;
    margin-bottom: 5px;
    color: #f8fbff;
    font-size: )" + std::to_string(arrivalTitleSize) + R"(px;
    line-height: 0.95;
    text-transform: uppercase;
}
.arrival-stamp-destination {
    color: #dceaf2;
    font-size: 15px;
    line-height: 1.15;
}
.arrival-stamp-tags {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 100%;
    margin-top: 12px;
}
.arrival-stamp-tags span {
    min-height: 24px;
    margin-right: 7px;
    margin-bottom: 5px;
    padding: 5px 8px;
    color: #dff7ff;
    font-size: 13px;
    font-weight: bold;
    line-height: 1.1;
    background-color: #102835;
    border-width: 1px;
    border-color: #34566a;
    border-radius: 14px;
}
.arrival-stamp-tags span.gold {
    color: #ffe6a5;
    background-color: #332b15;
    border-color: #8a7131;
}
.arrival-stamp-continue {
    width: 320px;
    min-height: 28px;
    height: 28px;
    margin-top: 3px;
    margin-right: 0px;
    padding: 0px;
    color: #9eafbc;
    font-size: 13px;
    line-height: 28px;
    text-align: left;
    background-color: transparent;
    border-width: 0px;
}
#rr-panel.mining-fullscreen-panel {
    left: 0px;
    top: 0px;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow: hidden;
    padding: 0px;
    background-color: transparent;
    border-width: 0px;
    border-radius: 0px;
}
#rr-panel.mining-fullscreen-panel .panel-head,
#rr-panel.mining-fullscreen-panel .status,
#rr-panel.mining-fullscreen-panel .panel-kpis {
    display: none;
}
.mining-fullscreen {
    position: relative;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
}
.mining-top-rail,
.mining-bottom-rail,
.mining-failure-banner {
    position: absolute;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.34);
    border-radius: 8px;
    background-color: rgba(5, 13, 18, 0.86);
}
.mining-top-rail {
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningInset) + R"(px;
    width: )" + std::to_string(miningRailWidth) + R"(px;
    min-height: )" + std::to_string(miningTopHeight) + R"(px;
    padding: 5px 8px;
    display: block;
}
.mining-run-title {
    width: )" + std::to_string(miningTitleWidth) + R"(px;
    margin-right: 10px;
}
.mining-run-title span,
.mining-utility-cluster span,
.mining-command-dock span,
.mining-payload-strip > span {
    color: rgba(154, 230, 244, 0.80);
    font-size: 12px;
    text-transform: uppercase;
}
.mining-run-title strong {
    color: #f1fbff;
    font-size: 23px;
    line-height: 1.05;
}
.mining-run-title small {
    color: rgba(215, 232, 234, 0.78);
    font-size: 12px;
    line-height: 1.12;
}
.mining-vitals {
    position: )" + std::string(compactMining ? "relative" : "absolute") + R"(;
    left: )" + std::to_string(miningVitalsLeft) + R"(px;
    top: )" + std::string(compactMining ? "0px" : "5px") + R"(;
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: )" + std::to_string(miningVitalsWidth) + R"(px;
    margin-top: )" + std::string(compactMining ? "8px" : "0px") + R"(;
    margin-right: 0px;
}
.mining-vitals .metric {
    width: )" + std::to_string(miningVitalWidth) + R"(px;
    height: 42px;
    min-height: 42px;
    margin-right: )" + std::to_string(miningVitalGap) + R"(px;
    margin-bottom: 5px;
    padding: 3px 5px;
    background-color: rgba(8, 30, 39, 0.94);
    border-width: 1px;
    border-color: #2f7d99;
}
.mining-vitals .metric strong {
    font-size: 24px;
    line-height: 0.98;
}
.mining-vitals .metric span {
    font-size: 12px;
    line-height: 1.0;
}
.mining-vitals .metric.mining-alert-caution {
    border-color: #c8a446;
}
.mining-vitals .metric.mining-alert-caution strong,
.mining-vitals .metric.mining-alert-caution span {
    color: #ffe6a5;
}
.mining-vitals .metric.mining-alert-caution.mining-alert-pulse-1 {
    background-color: rgba(31, 38, 29, 0.92);
}
.mining-vitals .metric.mining-alert-caution.mining-alert-pulse-2 {
    background-color: rgba(42, 45, 29, 0.94);
}
.mining-vitals .metric.mining-alert-caution.mining-alert-pulse-3 {
    background-color: rgba(53, 53, 29, 0.96);
}
.mining-vitals .metric.mining-alert-critical {
    border-color: #c95d50;
}
.mining-vitals .metric.mining-alert-critical strong,
.mining-vitals .metric.mining-alert-critical span {
    color: #ffd8d3;
}
.mining-vitals .metric.mining-alert-critical.mining-alert-pulse-1 {
    background-color: rgba(37, 27, 30, 0.92);
}
.mining-vitals .metric.mining-alert-critical.mining-alert-pulse-2 {
    background-color: rgba(50, 30, 32, 0.94);
}
.mining-vitals .metric.mining-alert-critical.mining-alert-pulse-3 {
    background-color: rgba(64, 34, 35, 0.96);
}
.mining-vitals .metric.mining-vital-broken strong {
    color: #b8322b;
    font-weight: bold;
    text-transform: uppercase;
}
.mining-vitals .metric.mining-vital-broken.mining-alert-pulse-1 strong {
    color: #d94439;
}
.mining-vitals .metric.mining-vital-broken.mining-alert-pulse-2 strong {
    color: #f45b4d;
}
.mining-vitals .metric.mining-vital-broken.mining-alert-pulse-3 strong {
    color: #ff8278;
}
.mining-health-strip {
    width: )" + std::to_string(std::max(1, miningTitleWidth - 4)) + R"(px;
    margin-top: 5px;
}
.mining-health-strip > div {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
}
.mining-health-strip span {
    color: rgba(169, 231, 246, 0.88);
    font-size: 12px;
    text-transform: uppercase;
}
.mining-health-strip strong {
    color: #e7fbff;
    font-size: 17px;
}
.mining-health-bar {
    width: )" + std::to_string(std::max(1, miningTitleWidth - 6)) + R"(px;
    height: 9px;
    margin-top: 3px;
    background-color: rgba(1, 8, 13, 0.92);
    border-width: 1px;
    border-color: rgba(127, 236, 255, 0.18);
}
.mining-health-bar i {
    display: block;
    height: 9px;
    background-color: #4be7ff;
}
.mining-health-bar .health-fill-0 { width: 0px; }
.mining-health-bar .health-fill-10 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 10) / 100) + R"(px; }
.mining-health-bar .health-fill-20 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 20) / 100) + R"(px; }
.mining-health-bar .health-fill-30 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 30) / 100) + R"(px; }
.mining-health-bar .health-fill-40 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 40) / 100) + R"(px; }
.mining-health-bar .health-fill-50 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 50) / 100) + R"(px; }
.mining-health-bar .health-fill-60 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 60) / 100) + R"(px; }
.mining-health-bar .health-fill-70 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 70) / 100) + R"(px; }
.mining-health-bar .health-fill-80 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 80) / 100) + R"(px; }
.mining-health-bar .health-fill-90 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 90) / 100) + R"(px; }
.mining-health-bar .health-fill-100 { width: )" + std::to_string(std::max(1, miningTitleWidth - 6)) + R"(px; }
.mining-utility-cluster {
    position: absolute;
    right: 8px;
    top: 6px;
    width: )" + std::to_string(miningUtilityWidth) + R"(px;
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    justify-content: flex-end;
}
.mining-utility-cluster button {
    width: 102px;
    min-height: 24px;
    height: auto;
    line-height: 1.15;
    margin-left: 4px;
    margin-bottom: 5px;
    padding: 4px 4px;
    font-size: 12px;
}
.mining-playfield-space {
    position: absolute;
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningPlayfieldTop) + R"(px;
    width: )" + std::to_string(miningRailWidth) + R"(px;
    height: )" + std::to_string(std::max(1, miningBottomTop - miningPlayfieldTop - 12)) + R"(px;
}
.mining-bottom-rail {
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningBottomTop) + R"(px;
    width: )" + std::to_string(miningRailWidth) + R"(px;
    min-height: )" + std::to_string(miningBottomHeight) + R"(px;
    padding: 10px;
    display: block;
}
.mining-payload-strip {
    width: )" + std::to_string(miningPayloadWidth) + R"(px;
    margin-right: 12px;
}
.mining-payload-strip .stat-grid,
.mining-artifact-strip .stat-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
}
.mining-payload-strip .stat-chip,
.mining-artifact-strip .stat-chip {
    width: 112px;
    min-height: 18px;
    padding: 4px 6px;
    margin-top: 5px;
    margin-right: 6px;
    font-size: 11px;
}
.mining-artifact-strip {
    width: )" + std::to_string(miningPayloadWidth) + R"(px;
    margin-top: 6px;
}
.mining-command-dock {
    position: absolute;
    left: )" + std::to_string(miningCommandLeft) + R"(px;
    top: 10px;
    width: )" + std::to_string(miningCommandWidth) + R"(px;
}
.mining-command-dock strong {
    color: #f1fbff;
    font-size: 17px;
    margin-bottom: 6px;
}
.mining-command-dock .system-actions {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    justify-content: flex-end;
}
.mining-command-dock .system-actions button {
    width: )" + std::to_string(miningActionWidth) + R"(px;
    min-height: 40px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
    margin-bottom: 7px;
    padding: 7px 4px;
    font-size: 13px;
}
.mining-ship-service-marker {
    display: none;
}
.mining-recall-dock {
    position: absolute;
    left: )" + std::to_string(miningPrimaryActionLeft) + R"(px;
    top: )" + std::to_string(miningPrimaryActionTop) + R"(px;
    width: )" + std::to_string(miningPrimaryActionWidth) + R"(px;
}
.mining-recall-dock .system-actions button {
    width: )" + std::to_string(miningPrimaryActionWidth) + R"(px;
    min-height: 40px;
    height: auto;
    line-height: 1.15;
    padding: 7px 4px;
    font-size: 13px;
}
.mining-failure-banner {
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningPlayfieldTop) + R"(px;
    width: )" + std::to_string(std::min(520, miningRailWidth)) + R"(px;
    padding: 10px;
    border-color: rgba(255, 95, 87, 0.52);
    background-color: rgba(42, 10, 13, 0.90);
}
.mining-failure-banner strong {
    color: #ffe1df;
    font-size: 17px;
}
.mining-failure-banner span {
    color: rgba(255, 226, 222, 0.82);
    font-size: 12px;
    line-height: 1.22;
}
.phase-lane,
.phase-title-row,
.phase-footer-lane {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.phase-row,
.phase-title-row,
.phase-action-grid,
.phase-footer-lane {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
}
.phase-title-row {
    justify-content: space-between;
}
.phase-action-grid {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    align-items: stretch;
}
.phase-card-slot {
    display: block;
    flex: none;
    width: )" + std::to_string(kPhaseCardSlotWidth) + R"(px;
    margin-right: )" + std::to_string(kPhaseCardGap) + R"(px;
}
.phase-card-slot.is-last {
    margin-right: 0px;
}
.phase-card-slot > .ops-card,
.phase-card-slot > .pilot-card,
.phase-card-slot > .upgrade-draft-card {
    margin-right: 0px;
}
.phase-common-button {
    width: )" + std::to_string(kPhaseCommonButtonWidth) + R"(px;
}
.phase-chip-slot {
    width: )" + std::to_string(kPhaseCommonChipSlotWidth) + R"(px;
}
.panel-head, .phase-titlebar, .card-footer, .draft-card-footer, .utility-row, .utility-actions, .pilot-card-top, .card-topline, .card-kicker, .slot-topline, .recipe-topline {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
}
.action-row {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    align-items: center;
}
.phase-board, .board-primary, .draft-hero, .draft-board, .surface-command, .surface-quickbar, .cockpit-hud, .arrival-fanfare-panel {
    display: block;
    width: 100%;
}
.summary-grid, .metric-grid, .metric-strip, .panel-kpis, .ops-grid, .pilot-card-grid, .inventory-grid, .stat-grid, .chip-strip, .actions, .action-row, .warning-grid, .surface-kpi-grid, .surface-quickbar, .draft-context, .result-grid, .achievement-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
}
.ops-card, .pilot-card, .upgrade-draft-card, .arrival-card, .nav-card, .inventory-item, .achievement-card, .crew-fate-card, .result-group, .drone-recipe-card, .drone-loadout-slot, .drone-control-card, .modal-body {
    display: flex;
    flex-direction: column;
}
.card-topline, .card-kicker, .pilot-card-top, .slot-topline, .recipe-topline, .card-title, .card-copy, .metric-strip, .chip-strip, .utility-actions {
    flex: none;
}
.ops-card h3, .pilot-card h3, .upgrade-draft-card h3, .arrival-card h3, .nav-card h3,
.ops-card p, .pilot-card p, .upgrade-draft-card p, .arrival-card p, .nav-card p,
.module-impact, .drone-build-hook, .drone-upgrade-summary, .inventory-art, .inventory-copy, .card-title, .card-copy {
    flex: none;
}
.stat-grid, .chip-strip, .metric-strip {
    flex: none;
    align-content: flex-start;
    align-items: flex-start;
    gap: 5px;
    row-gap: 5px;
    column-gap: 5px;
}
.stat-grid .stat-chip {
    flex: none;
}
.card-footer, .draft-card-footer {
    flex: none;
    margin-top: auto;
    gap: 8px;
    row-gap: 8px;
    column-gap: 8px;
}
.action-row {
    flex: none;
    gap: 8px;
    row-gap: 8px;
    column-gap: 8px;
}
.card-footer button, .draft-card-footer button, .actions button, .action-row button, .utility-row button, .compact-tools button, .utility-actions button {
    flex: none;
    white-space: normal;
}
.modal-body {
    flex: auto;
    min-height: 0px;
}
.panel-head {
    margin-bottom: 10px;
}
.phase-board-panel .panel-head {
    width: 736px;
}
.phase-board-panel .panel-title {
    width: 240px;
}
.phase-board-panel .panel-head-actions {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 314px;
    margin-right: 0px;
    justify-content: flex-end;
}
.control-panel .panel-head {
    width: 100%;
}
.control-panel .panel-title {
    width: 120px;
}
.control-panel .panel-head-actions {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 260px;
    margin-top: 0px;
    justify-content: flex-end;
}
.panel-head-actions button {
    min-width: 92px;
    width: 118px;
    margin-top: 0px;
}
.phase-board-panel .panel-head-actions button {
    min-width: 0px;
    width: 96px;
    margin-right: 8px;
}
.control-panel .panel-head-actions button {
    min-width: 0px;
    width: 100px;
    margin-top: 0px;
    margin-right: 8px;
}
.game-mark, .card-topline span, .card-kicker span, .pilot-card-top span {
    color: #7e90a0;
    font-size: 12px;
}
.panel-kpis {
    margin-top: 10px;
    margin-bottom: 8px;
}
.panel-kpis .metric {
    width: 150px;
}
.panel-kpis .metric-chapter span {
    display: none;
}
.phase-board-panel .status,
.phase-board-panel .phase-status,
.phase-board-panel .panel-kpis {
    width: 704px;
}
.phase-board-panel .panel-kpis .metric {
    width: 150px;
}
.phase-board-panel.surface-ops-panel .panel-kpis {
    display: none;
}
.phase-board-panel.surface-ops-panel .panel-head {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.phase-board-panel.surface-ops-panel .panel-title {
    flex: 1 1 auto;
    min-width: 0px;
}
.phase-board-panel.surface-ops-panel .panel-head-actions {
    flex: 0 0 auto;
    width: auto;
    margin-left: auto;
}
.phase-board-panel.surface-ops-panel .panel-head-actions button {
    margin-left: 8px;
    margin-right: 0px;
}
.phase-board-panel.drone-ops-panel .panel-kpis {
    display: none;
}
.phase-board-panel.navigation-panel .panel-kpis {
    display: none;
}
.phase-board-panel.navigation-panel .panel-head {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-bottom: 6px;
}
.phase-board-panel.navigation-panel .panel-title {
    flex: 1 1 auto;
    min-width: 0px;
}
.phase-board-panel.navigation-panel .panel-head-actions {
    flex: 0 0 auto;
    width: auto;
    margin-left: auto;
}
.phase-board-panel.navigation-panel .panel-head-actions button {
    margin-left: 8px;
    margin-right: 0px;
}
.phase-board-panel.navigation-panel .status {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-bottom: 6px;
}
.phase-board-panel .phase-titlebar {
    width: 704px;
    margin-top: 8px;
    margin-bottom: 10px;
}
.phase-board-panel .phase-titlebar > div {
    width: 520px;
}
.phase-board-panel .phase-titlebar h2 {
    margin-top: 0px;
}
.phase-board-panel .phase-titlebar p {
    width: 510px;
    line-height: 1.32;
}
.phase-board-panel .compact-tools {
    width: 150px;
    justify-content: flex-end;
}
.phase-board-panel .compact-tools button {
    width: 118px;
    margin-right: 0px;
}
.phase-board-arrival,
.phase-board-research,
.phase-board-drone-ops {
    width: 736px;
}
.phase-board-drone-ops {
    padding-bottom: 8px;
}
.phase-board-panel.drone-ops-panel .panel-head {
    margin-bottom: 8px;
}
.phase-board-panel.drone-ops-panel .status {
    margin-bottom: 6px;
}
.phase-board-panel.drone-ops-panel .phase-titlebar {
    margin-top: 4px;
    margin-bottom: 6px;
}
.phase-board-panel.drone-ops-panel .phase-titlebar > div {
    width: 430px;
}
.phase-board-panel.drone-ops-panel .phase-titlebar p {
    width: 410px;
    line-height: 1.18;
}
.phase-board-panel.drone-ops-panel .compact-tools {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 238px;
    justify-content: flex-end;
}
.phase-board-panel.drone-ops-panel .compact-tools button {
    width: 106px;
    min-height: 30px;
    margin-left: 6px;
    margin-right: 0px;
    padding: 5px 7px;
}
.phase-board-arrival {
    padding-bottom: 0px;
}
.phase-board-arrival .result-grid,
.phase-board-results .result-grid,
.phase-board-arrival .achievement-grid,
.phase-board-results .achievement-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 736px;
    margin-top: 6px;
}
.phase-board-arrival .crew-fate-card,
.phase-board-results .crew-fate-card {
    width: 704px;
    margin-top: 8px;
    margin-right: 0px;
    padding: 11px 12px;
}
.phase-board-arrival .crew-fate-card p,
.phase-board-results .crew-fate-card p {
    width: 646px;
    margin-top: 4px;
    line-height: 1.35;
}
.phase-board-arrival .crew-fate-signal,
.phase-board-results .crew-fate-signal {
    display: none;
}
.phase-board-arrival .result-group,
.phase-board-results .result-group,
.phase-board-arrival .achievement-card,
.phase-board-results .achievement-card {
    padding: 8px 10px;
}
.phase-board-arrival .result-group,
.phase-board-results .result-group {
    width: 334px;
    min-height: 88px;
    margin-top: 6px;
    margin-right: 8px;
}
.phase-board-arrival .result-group.primary,
.phase-board-results .result-group.primary {
    width: 704px;
    margin-right: 0px;
}
.phase-board-arrival .result-group h3,
.phase-board-results .result-group h3 {
    margin-bottom: 5px;
    padding-bottom: 4px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-arrival .result-row,
.phase-board-results .result-row {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    width: 100%;
    min-height: 24px;
    margin-top: 3px;
    padding-top: 3px;
    border-top-width: 1px;
    border-top-color: #263b4c;
}
.phase-board-arrival .result-group h3 + .result-row,
.phase-board-results .result-group h3 + .result-row {
    border-top-width: 0px;
}
.phase-board-arrival .result-row span,
.phase-board-results .result-row span {
    width: 126px;
    color: #8295a5;
    font-size: 12px;
}
.phase-board-arrival .result-row strong,
.phase-board-results .result-row strong {
    width: 160px;
    margin-top: 0px;
}
.phase-board-arrival .result-group.primary .result-row span,
.phase-board-results .result-group.primary .result-row span {
    width: 178px;
}
.phase-board-arrival .result-group.primary .result-row strong,
.phase-board-results .result-group.primary .result-row strong {
    width: 486px;
}
.phase-board-arrival .board-note,
.phase-board-results .board-note {
    width: 704px;
    margin-top: 8px;
    padding: 8px 12px;
    color: #d7c276;
    background-color: #101923;
    border-width: 1px;
    border-color: #4c4728;
    border-radius: 6px;
}
.phase-board-arrival .achievement-card,
.phase-board-results .achievement-card {
    width: 704px;
    margin-right: 0px;
}
.phase-board-arrival .achievement-card p,
.phase-board-results .achievement-card p {
    width: 670px;
}
.phase-board-arrival .tutorial-card {
    width: 704px;
    padding: 11px 12px;
    margin-top: 10px;
    margin-bottom: 12px;
    background-color: #101923;
    border-color: #2f4354;
}
.phase-board-arrival .tutorial-card div {
    width: 680px;
}
.phase-board-arrival .tutorial-card p {
    width: 680px;
    line-height: 1.32;
}
.phase-board-arrival .tutorial-card button {
    width: 140px;
    height: 34px;
    line-height: 34px;
    margin-top: 8px;
}
.phase-board-arrival > .metric-grid,
.phase-board-research .focus-metrics,
.phase-board-drone-ops .focus-metrics {
    width: 704px;
    margin-top: 4px;
    margin-bottom: 6px;
}
.phase-board-arrival .approach-metrics {
    width: 704px;
    margin-top: 4px;
    margin-bottom: 6px;
    flex-wrap: nowrap;
}
.phase-board-arrival .approach-metrics .surface-kpi {
    width: 154px;
    min-height: 30px;
    margin-top: 4px;
    margin-right: 8px;
    padding: 5px 9px;
}
.phase-board-arrival .approach-metrics .surface-kpi span {
    font-size: 11px;
    line-height: 1.1;
}
.phase-board-arrival .approach-metrics .surface-kpi strong {
    font-size: 13px;
    line-height: 1.1;
}
.phase-board-arrival > .metric-grid .metric,
.phase-board-research .focus-metrics .metric,
.phase-board-drone-ops .focus-metrics .metric {
    width: 154px;
    margin-right: 8px;
}
.phase-board-arrival .ops-grid,
.phase-board-research .ops-grid,
.phase-board-drone-ops .ops-grid {
    width: 704px;
    margin-top: 6px;
}
.phase-board-arrival .ops-grid {
    width: 704px;
    flex-wrap: nowrap;
    margin-top: 4px;
    margin-bottom: 6px;
}
.phase-board-arrival .arrival-card,
.phase-board-research .ops-card,
.phase-board-drone-ops .drone-card {
    width: 204px;
    min-height: 172px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 8px;
}
.phase-board-research .ops-card {
    width: 204px;
}
.phase-board-research .ops-card {
    min-height: 238px;
}
.phase-board-drone-ops .ops-grid {
    width: 704px;
    margin-top: 4px;
}
.phase-board-drone-ops .drone-card {
    width: 205px;
    min-height: 330px;
    padding: 8px;
    margin-top: 6px;
    margin-right: 7px;
}
.phase-board-arrival .arrival-card {
    width: 209px;
    min-height: 250px;
    margin-right: 8px;
    padding: 10px 9px;
}
.phase-board-arrival .arrival-card .card-topline {
    width: 191px;
    min-height: 34px;
    overflow: hidden;
}
.phase-board-arrival .arrival-card .card-topline span {
    width: 88px;
    min-height: 34px;
    line-height: 1.15;
    overflow: hidden;
}
.phase-board-arrival .arrival-card h3 {
    min-height: 20px;
    overflow: hidden;
}
.phase-board-arrival .arrival-card p,
.phase-board-research .ops-card p,
.phase-board-drone-ops .drone-card p {
    width: 194px;
    min-height: 52px;
    margin-top: 5px;
    margin-bottom: 7px;
    line-height: 1.28;
}
.phase-board-research .ops-card p {
    min-height: 54px;
    overflow: hidden;
}
.phase-board-drone-ops .drone-card p {
    width: 189px;
    min-height: 38px;
    margin-top: 3px;
    margin-bottom: 4px;
    line-height: 1.16;
    overflow: hidden;
}
.phase-board-arrival .arrival-card p {
    width: 191px;
    max-height: 100px;
    overflow: hidden;
}
.phase-board-research .ops-card .module-impact {
    display: block;
    width: 194px;
    min-height: 30px;
    margin-top: 4px;
    margin-bottom: 4px;
    font-size: 13px;
    line-height: 1.25;
    overflow: hidden;
}
.phase-board-research .ops-card .stat-grid,
.phase-board-drone-ops .drone-card .stat-grid {
    width: 194px;
    min-height: 50px;
    margin-top: 4px;
    justify-content: center;
}
.phase-board-drone-ops .drone-card .stat-grid {
    width: 189px;
    min-height: 54px;
    height: auto;
    margin-top: 2px;
}
.phase-board-drone-ops .drone-card .drone-build-hook {
    width: 177px;
    min-height: 52px;
    margin-top: 5px;
    margin-bottom: 5px;
    padding: 6px;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.18);
    background-color: rgba(71, 220, 255, 0.06);
    color: #bfeef7;
    font-size: 11px;
    line-height: 1.20;
    overflow: hidden;
}
.phase-board-drone-ops .drone-card .drone-upgrade-summary {
    width: 189px;
    min-height: 40px;
    margin-top: 2px;
    margin-bottom: 5px;
    color: #ffd166;
    font-size: 11px;
    line-height: 1.15;
    overflow: hidden;
}
.phase-board-drone-ops .drone-card .stat-chip {
    max-width: 92px;
    min-height: 22px;
    padding: 4px 6px;
    font-size: 11px;
    line-height: 1.15;
}
.phase-board-drone-ops .drone-card .stat-chip.wide {
    max-width: 118px;
}
.phase-board-drone-ops .drone-card .module-art {
    width: 194px;
    height: 44px;
    margin-top: 4px;
    margin-bottom: 6px;
    background-color: #0c141d;
    border-width: 1px;
    border-color: #3c596a;
    border-radius: 6px;
}
.phase-board-drone-ops .drone-card .module-art {
    width: 189px;
    height: 34px;
    margin-top: 3px;
    margin-bottom: 4px;
}
.phase-board-drone-ops .drone-card .module-art span {
    width: 100%;
    color: #8fd8f0;
    font-size: 22px;
    line-height: 44px;
    text-align: center;
}
.phase-board-drone-ops .drone-card .module-art span {
    font-size: 19px;
    line-height: 34px;
}
.phase-board-drone-ops .drone-card h3 {
    margin-top: 2px;
    margin-bottom: 0px;
}
.phase-board-arrival .arrival-card .card-footer,
.phase-board-research .ops-card .card-footer,
.phase-board-drone-ops .drone-card .card-footer {
    min-height: 48px;
    height: auto;
    margin-top: auto;
    padding-top: 9px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    align-items: center;
}
.phase-board-drone-ops .drone-card .card-footer {
    width: 189px;
    min-height: 34px;
    height: auto;
    margin-top: auto;
    padding-top: 6px;
}
.phase-board-arrival .arrival-card .card-footer {
    width: 191px;
    min-height: 48px;
    height: auto;
    margin-top: auto;
}
.phase-board-arrival .arrival-card .card-footer span,
.phase-board-research .ops-card .card-footer span,
.phase-board-drone-ops .drone-card .card-footer span {
    width: 76px;
    color: #d7c276;
    font-size: 12px;
    line-height: 1.15;
}
.phase-board-drone-ops .drone-card .card-footer span {
    width: 45px;
    font-size: 11px;
}
.phase-board-arrival .arrival-card .card-footer span {
    width: 42px;
    min-height: 34px;
    overflow: hidden;
}
.phase-board-arrival .arrival-card .card-footer button,
.phase-board-research .ops-card .card-footer button,
.phase-board-drone-ops .drone-card .card-footer button {
    width: 104px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 5px;
    padding-right: 5px;
    font-size: 12px;
}
.phase-board-drone-ops .drone-card .card-footer button {
    width: 63px;
    min-height: 28px;
    height: auto;
    line-height: 1.15;
    font-size: 11px;
}
.phase-board-arrival .arrival-card .card-footer button {
    width: 140px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    white-space: normal;
    overflow-wrap: anywhere;
}
.phase-board-research .phase-advisory,
.phase-board-drone-ops .resource-bank {
    width: 704px;
}
.phase-board-drone-ops .drone-top-row {
    display: flex;
    flex-direction: row;
    width: 704px;
    margin-top: 6px;
    margin-bottom: 6px;
    gap: 8px;
}
.phase-board-drone-ops .drone-top-row .resource-bank {
    margin-top: 0px;
    margin-bottom: 0px;
}
.phase-board-drone-ops .drone-build-guidance {
    width: 240px;
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    align-items: flex-start;
    padding: 7px;
    margin-top: 0px;
    margin-bottom: 0px;
    background-color: rgba(33, 28, 46, 0.76);
    border-color: rgba(183, 132, 255, 0.24);
}
.phase-board-drone-ops .drone-build-guidance .drone-guidance-copy {
    width: 94px;
}
.phase-board-drone-ops .drone-build-guidance h2 {
    color: #d8c7ff;
    margin-top: 0px;
    margin-bottom: 2px;
}
.phase-board-drone-ops .drone-build-guidance p {
    width: 92px;
    margin-top: 0px;
    margin-bottom: 0px;
    font-size: 12px;
    line-height: 1.14;
}
.phase-board-drone-ops .drone-build-guidance .drone-guidance-stats {
    width: 136px;
    margin-top: 0px;
    justify-content: flex-end;
}
.phase-board-drone-ops .drone-build-guidance .stat-chip {
    width: 126px;
    min-height: 16px;
    padding: 2px 5px;
    margin-left: 0px;
    margin-bottom: 3px;
    font-size: 10px;
}
.phase-board-drone-ops .drone-build-guidance .stat-chip.wide {
    width: 126px;
}
.phase-board-drone-ops .drone-loadout-bench {
    width: 704px;
    margin-top: 6px;
    margin-bottom: 8px;
    padding: 8px 10px;
}
.phase-board-drone-ops .drone-loadout-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 684px;
}
.phase-board-drone-ops .drone-loadout-slot {
    width: 196px;
    min-height: 96px;
    padding: 6px;
    margin-right: 7px;
    margin-bottom: 7px;
    border-width: 1px;
    border-color: rgba(137, 178, 211, 0.16);
    background-color: rgba(255, 255, 255, 0.03);
}
.phase-board-drone-ops .drone-loadout-slot.filled {
    border-color: rgba(71, 220, 255, 0.28);
    background-color: rgba(19, 48, 58, 0.58);
}
.phase-board-drone-ops .drone-loadout-slot.open {
    border-color: rgba(112, 224, 168, 0.28);
    background-color: rgba(20, 54, 39, 0.52);
}
.phase-board-drone-ops .drone-loadout-slot.locked {
    border-color: rgba(137, 178, 211, 0.12);
    background-color: rgba(255, 255, 255, 0.018);
}
.phase-board-drone-ops .drone-loadout-slot.role-attack {
    border-color: rgba(71, 220, 255, 0.38);
}
.phase-board-drone-ops .drone-loadout-slot.role-defense {
    border-color: rgba(183, 255, 241, 0.36);
}
.phase-board-drone-ops .drone-loadout-slot.role-mining {
    border-color: rgba(112, 224, 168, 0.34);
}
.phase-board-drone-ops .drone-loadout-slot.role-resource {
    border-color: rgba(255, 209, 102, 0.34);
}
.phase-board-drone-ops .drone-loadout-slot.role-survey {
    border-color: rgba(183, 132, 255, 0.34);
}
.phase-board-drone-ops .drone-loadout-slot.role-stabilizer {
    border-color: rgba(137, 178, 211, 0.34);
}
.phase-board-drone-ops .drone-loadout-slot .slot-topline {
    width: 196px;
    min-height: 16px;
    color: #89b2d3;
    font-size: 11px;
}
.phase-board-drone-ops .drone-loadout-slot .slot-topline span {
    width: 74px;
}
.phase-board-drone-ops .drone-loadout-slot .slot-topline strong {
    width: 114px;
    color: #ffd166;
    text-align: right;
}
.phase-board-drone-ops .drone-loadout-slot h3 {
    margin-top: 2px;
    margin-bottom: 1px;
    font-size: 13px;
}
.phase-board-drone-ops .drone-loadout-slot p {
    max-height: 18px;
    margin-top: 2px;
    margin-bottom: 0px;
    line-height: 1.15;
    overflow: hidden;
}
.phase-board-drone-ops .drone-loadout-slot .slot-role {
    min-height: 14px;
    color: #bfeef7;
}
.phase-board-drone-ops .drone-loadout-slot .stat-grid {
    min-height: 16px;
    margin-top: 2px;
}
.phase-board-drone-ops .drone-loadout-slot .stat-chip {
    width: 58px;
    min-height: 15px;
    padding: 2px 5px;
    margin-right: 4px;
    margin-bottom: 3px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-loadout-slot .stat-chip.wide {
    width: 88px;
}
.phase-board-drone-ops .drone-combat-forecast {
    width: 704px;
    padding: 8px 10px;
    margin-top: 6px;
    margin-bottom: 6px;
    background-color: rgba(37, 32, 17, 0.72);
    border-color: rgba(255, 209, 102, 0.26);
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-copy {
    width: 180px;
}
.phase-board-drone-ops .drone-combat-forecast h2 {
    color: #ffd166;
    margin-top: 0px;
}
.phase-board-drone-ops .drone-combat-forecast p {
    width: 170px;
    font-size: 12px;
    line-height: 1.18;
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-stats {
    width: 482px;
    margin-top: 0px;
    justify-content: flex-end;
}
.phase-board-drone-ops .drone-combat-forecast .stat-chip {
    width: 84px;
    min-height: 24px;
    padding: 4px 5px;
    margin-left: 5px;
    margin-bottom: 5px;
}
.phase-board-drone-ops .drone-combat-forecast .stat-chip.wide {
    width: 112px;
}
.phase-board-research .board-primary,
.phase-board-drone-ops .board-primary {
    width: 704px;
    margin-top: 10px;
    padding: 8px 10px;
}
.phase-board-research .board-primary h2,
.phase-board-drone-ops .board-primary h2 {
    margin-top: 0px;
}
.phase-board-research .actions,
.phase-board-drone-ops .actions {
    width: 704px;
    margin-top: 12px;
    justify-content: center;
}
.phase-board-research .actions button,
.phase-board-drone-ops .actions button {
    width: 210px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
}
.phase-board-drone-ops .drone-combat-forecast {
    padding: 7px 9px;
    margin-top: 5px;
    margin-bottom: 5px;
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-copy {
    width: 188px;
}
.phase-board-drone-ops .drone-combat-forecast p {
    width: 180px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-stats {
    width: 480px;
}
.phase-board-drone-ops .drone-combat-forecast {
    display: none;
}
.phase-board-drone-ops .drone-loadout-bench,
.phase-board-drone-ops .drone-roster {
    margin-top: 6px;
    margin-bottom: 6px;
    padding: 7px 9px;
}
.phase-board-drone-ops .section-heading {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: baseline;
    width: 684px;
    margin-bottom: 4px;
}
.phase-board-drone-ops .section-heading h2 {
    margin-top: 0px;
    margin-bottom: 0px;
}
.phase-board-drone-ops .section-heading p {
    width: 360px;
    margin-top: 0px;
    margin-bottom: 0px;
    color: #9eaebe;
    font-size: 11px;
    line-height: 1.1;
    text-align: right;
}
.phase-board-drone-ops .drone-loadout-slot {
    min-height: 96px;
    width: 196px;
}
.phase-board-drone-ops .drone-loadout-slot p {
    max-height: 26px;
}
.phase-board-drone-ops .drone-control-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 684px;
}
.phase-board-drone-ops .drone-control-card {
    width: 196px;
    min-height: 92px;
    padding: 6px;
    margin-right: 7px;
    margin-bottom: 7px;
    border-width: 1px;
    border-color: rgba(137, 178, 211, 0.16);
    border-radius: 6px;
    background-color: rgba(255, 255, 255, 0.03);
}
.phase-board-drone-ops .drone-control-card h3 {
    margin-top: 2px;
    margin-bottom: 1px;
    font-size: 12px;
}
.phase-board-drone-ops .drone-control-status {
    width: 188px;
    min-height: 12px;
    max-height: 14px;
    margin-top: 0px;
    margin-bottom: 2px;
    color: #9eaebe;
    font-size: 10px;
    line-height: 1.12;
    overflow: hidden;
}
.phase-board-drone-ops .drone-control-card .stat-grid {
    width: 188px;
    min-height: 28px;
    margin-top: 1px;
}
.phase-board-drone-ops .drone-control-card .stat-chip {
    width: 84px;
    min-height: 15px;
    padding: 2px 5px;
    font-size: 10px;
    line-height: 1.1;
}
.phase-board-drone-ops .drone-control-card .card-footer {
    width: 188px;
    min-height: 24px;
    margin-top: auto;
    padding-top: 3px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    gap: 5px;
}
.phase-board-drone-ops .drone-control-card .card-footer button {
    width: 82px;
    min-height: 22px;
    padding: 3px 5px;
    font-size: 9px;
    line-height: 1.1;
}
.phase-board-navigation {
    width: 736px;
    padding-bottom: 12px;
}
.phase-board-navigation .ark-status,
.phase-board-navigation .navigation-map {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-top: 6px;
    padding: 8px 10px;
}
.phase-board-navigation .navigation-map {
    padding-right: 0px;
}
.phase-board-navigation .ark-status {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
}
.phase-board-navigation .ark-status > div {
    width: 338px;
}
.phase-board-navigation .ark-status .stat-grid {
    width: 300px;
    margin-top: 0px;
    justify-content: flex-end;
}
.phase-board-navigation .nav-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 100%;
    margin-top: 4px;
    align-items: stretch;
    gap: 8px;
}
.phase-board-navigation .nav-card {
    box-sizing: border-box;
    flex: 1 1 0px;
    width: 0px;
    min-width: 0px;
    min-height: 168px;
    margin-top: 6px;
    margin-right: 0px;
    padding: 8px 10px;
}
.phase-board-navigation .nav-card.selected {
    background-color: #10241f;
    border-color: #72e0a8;
}
.phase-board-navigation .nav-card .card-kicker span {
    width: 140px;
    height: 14px;
    font-size: 10px;
    overflow: hidden;
}
.phase-board-navigation .nav-card h3 {
    min-height: 18px;
    margin-top: 4px;
    margin-bottom: 2px;
    font-size: 14px;
    overflow: hidden;
}
.phase-board-navigation .nav-card p {
    width: auto;
    align-self: stretch;
    min-height: 26px;
    margin-top: 2px;
    margin-bottom: 5px;
    font-size: 12px;
    line-height: 1.12;
    overflow: hidden;
}
.phase-board-navigation .nav-card .stat-grid {
    width: auto;
    align-self: stretch;
    min-height: 38px;
    margin-top: 0px;
    align-content: flex-start;
    align-items: flex-start;
}
.phase-board-navigation .nav-card .stat-chip {
    width: 124px;
    min-height: 16px;
    line-height: 1.08;
    margin-top: 0px;
    margin-right: 5px;
    margin-bottom: 4px;
    padding: 2px 5px;
    font-size: 10px;
}
.phase-board-navigation .nav-card .stat-chip.wide {
    width: 136px;
}
.phase-board-navigation .nav-card .card-footer {
    width: auto;
    align-self: stretch;
    min-height: 32px;
    height: auto;
    margin-top: auto;
    padding-top: 5px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    align-items: center;
}
.phase-board-navigation .nav-card .card-footer span {
    flex: 1 1 auto;
    width: auto;
    min-height: 24px;
    color: #d7c276;
    font-size: 12px;
    line-height: 1.15;
    overflow: hidden;
}
.phase-board-navigation .nav-card .card-footer button {
    width: 140px;
    min-height: 30px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 11px;
}
.phase-board-drone-ops .drone-bay-strip {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    width: 412px;
    padding: 7px;
    margin-top: 0px;
}
.phase-board-drone-ops .drone-bay-strip .drone-bay-copy {
    width: 150px;
}
.phase-board-drone-ops .drone-bay-strip h2 {
    margin-top: 0px;
    margin-bottom: 2px;
}
.phase-board-drone-ops .drone-bay-strip p {
    width: 146px;
    margin-top: 0px;
    margin-bottom: 0px;
    line-height: 1.18;
}
.phase-board-drone-ops .drone-bay-strip .stat-grid {
    margin-top: 0px;
}
.phase-board-drone-ops .drone-bay-stats {
    width: 86px;
}
.phase-board-drone-ops .drone-bay-materials {
    width: 86px;
}
.phase-board-drone-ops .drone-bay-strip .stat-chip {
    max-width: 84px;
    min-height: 22px;
    padding: 4px 6px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-bay-strip .stat-chip.wide {
    max-width: 116px;
}
.phase-board-drone-ops .drone-bay-strip button {
    width: 78px;
    min-height: 28px;
    height: auto;
    line-height: 1.15;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 10px;
    white-space: normal;
}
.phase-board-drone-ops .drone-roster {
    margin-top: 6px;
    padding: 8px 10px;
}
.phase-board-drone-ops .board-primary h2 {
    margin-bottom: 2px;
}
.phase-board-surface {
    width: )" + std::to_string(kPhaseBoardFrameWidth) + R"(px;
    padding-bottom: 44px;
}
.phase-board-surface .phase-titlebar,
.phase-board-surface .surface-command,
.phase-board-surface .surface-quickbar,
.phase-board-surface .surface-kpi-grid,
.phase-board-surface .drone-ops-callout,
.phase-board-surface .surface-primary-action,
.phase-board-surface .board-primary {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.phase-board-surface .phase-titlebar {
    margin-bottom: 4px;
}
.phase-board-surface .phase-titlebar > div,
.phase-board-surface .phase-titlebar p {
    width: 330px;
}
.phase-board-surface .compact-tools {
    width: 374px;
    justify-content: flex-end;
}
.phase-board-surface.surface-ops-screen .compact-tools {
    width: 374px;
    margin-right: 0px;
}
.phase-board-surface .compact-tools button {
    width: 96px;
    margin-left: 6px;
    margin-right: 0px;
}
.phase-board-surface .surface-command {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    padding: 0px;
    margin-top: 8px;
}
.phase-board-surface .surface-site-card,
.phase-board-surface .surface-posture {
    min-height: 76px;
    padding: 10px;
    background-color: #131c26;
    border-width: 1px;
    border-color: #35495a;
    border-radius: 6px;
}
.phase-board-surface .surface-site-card {
    width: 326px;
    margin-right: 8px;
}
.phase-board-surface .surface-site-card span {
    color: #7f8f9f;
    font-size: 12px;
}
.phase-board-surface .surface-site-card strong {
    display: block;
    margin-top: 4px;
    color: #dce6ee;
    font-size: 15px;
}
.phase-board-surface .surface-site-card p {
    margin-top: 6px;
    line-height: 1.25;
}
.phase-board-surface .surface-posture {
    width: 326px;
    margin-top: 0px;
    margin-right: 0px;
}
.phase-board-surface .surface-kpi-grid {
    margin-top: 8px;
    margin-bottom: 6px;
}
.phase-board-surface .surface-quickbar {
    flex-wrap: nowrap;
    margin-top: 4px;
    margin-bottom: 4px;
}
.phase-board-surface.surface-ops-screen .phase-titlebar,
.phase-board-surface.surface-ops-screen .surface-quickbar,
.phase-board-surface.surface-ops-screen .surface-kpi-grid,
.phase-board-surface.surface-ops-screen .drone-ops-callout,
.phase-board-surface.surface-ops-screen .surface-primary-action,
.phase-board-surface.surface-ops-screen .board-primary {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.phase-board-surface.surface-ops-screen > .phase-titlebar {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    align-items: flex-start;
    justify-content: space-between;
}
.phase-board-surface.surface-ops-screen > .phase-titlebar > div {
    flex: 1 1 auto;
    width: auto;
    min-width: 0px;
}
.phase-board-surface.surface-ops-screen > .phase-titlebar > .compact-tools {
    flex: 0 0 auto;
    width: auto;
    margin-left: auto;
}
.phase-board-surface .surface-kpi {
    width: 128px;
    min-height: 40px;
    padding: 7px 8px;
}
.phase-board-surface .surface-quickbar .surface-kpi {
    width: 74px;
    min-height: 32px;
    padding: 4px 6px;
    margin-top: 0px;
    margin-right: 6px;
}
.phase-board-surface.surface-ops-screen .surface-quickbar .surface-kpi {
    box-sizing: border-box;
    flex: 1 1 74px;
    width: auto;
}
.phase-board-surface .surface-quickbar .surface-quick-item.is-last {
    margin-right: 0px;
}
.phase-board-surface .surface-quickbar .surface-quick-site {
    width: 104px;
}
.phase-board-surface.surface-ops-screen .surface-quickbar .surface-quick-site {
    flex: 1 1 104px;
    width: auto;
}
.phase-board-surface .surface-quickbar .surface-quick-next {
    width: 184px;
}
.phase-board-surface.surface-ops-screen .surface-quickbar .surface-quick-next {
    flex: 2 1 184px;
    width: auto;
}
.phase-board-surface .surface-kpi strong {
    font-size: 15px;
    margin-top: 3px;
}
.phase-board-surface .surface-quickbar .surface-kpi span {
    font-size: 11px;
}
.phase-board-surface .surface-quickbar .surface-kpi strong {
    font-size: 13px;
    line-height: 1.15;
    margin-top: 2px;
}
.phase-board-surface .resource-bank {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    padding: 8px 10px;
    margin-top: 4px;
}
.phase-board-surface .resource-bank > div {
    width: 520px;
}
.phase-board-surface .resource-bank button {
    width: )" + std::to_string(kPhaseCommonButtonWidth) + R"(px;
    min-height: 32px;
    height: auto;
    line-height: 1.15;
    margin-top: 3px;
    margin-right: 0px;
}
.phase-board-surface.surface-ops-screen .drone-ops-callout > div {
    flex: 1 1 auto;
    width: auto;
    min-width: 0px;
}
.phase-board-surface.surface-ops-screen .drone-ops-callout button {
    flex: 0 0 140px;
    width: 140px;
    margin-left: auto;
    margin-right: 0px;
}
.phase-board-surface .surface-primary-action {
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    padding: 9px;
    margin-top: 7px;
    border-color: #4c6d5a;
    background-color: #111f22;
}
.phase-board-surface .surface-primary-copy {
    width: 496px;
    margin-right: 18px;
}
.phase-board-surface .surface-action-topline {
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    margin-bottom: 5px;
}
.phase-board-surface .surface-action-topline span {
    width: 188px;
    color: #d7c276;
    font-size: 12px;
    line-height: 1.15;
}
.phase-board-surface .surface-primary-action p {
    width: 482px;
    min-height: 26px;
    margin-top: 5px;
    line-height: 1.25;
}
.phase-board-surface .surface-primary-action .stat-grid {
    width: 482px;
    min-height: 28px;
    margin-top: 5px;
}
.phase-board-surface .surface-primary-control {
    width: 132px;
    margin-top: 8px;
}
.phase-board-surface .surface-primary-control span {
    display: block;
    width: 132px;
    margin-bottom: 8px;
    color: #d7c276;
    font-size: 12px;
    text-align: center;
}
.phase-board-surface .surface-primary-action button {
    width: 132px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
}
.phase-board-surface .board-primary {
    padding: 2px 0px 24px 0px;
    margin-top: 2px;
}
.phase-board-surface .surface-actions .phase-titlebar {
    width: 704px;
    margin-bottom: 0px;
}
.phase-board-surface.surface-ops-screen .surface-actions .phase-titlebar {
    width: 704px;
}
.phase-board-surface .surface-actions .phase-titlebar h2 {
    margin-top: 6px;
    margin-bottom: 4px;
}
.phase-board-surface .surface-actions .ops-grid {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
}
.phase-board-surface.surface-ops-screen .surface-actions .ops-grid {
    box-sizing: border-box;
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 100%;
}
.phase-board-surface .surface-action-slot {
    width: )" + std::to_string(kPhaseCardSlotWidth) + R"(px;
    margin-right: )" + std::to_string(kPhaseCardGap) + R"(px;
}
.phase-board-surface.surface-ops-screen .surface-action-slot {
    flex: 1 1 0px;
    width: auto;
}
.phase-board-surface .surface-action-slot.is-last {
    margin-right: 0px;
}
.phase-board-surface .surface-action-slot .surface-action-card {
    margin-right: 0px;
}
.phase-board-surface.surface-ops-screen .surface-action-slot .surface-action-card {
    box-sizing: border-box;
    width: 100%;
}
.phase-board-surface .surface-action-card {
    display: flex;
    flex-direction: column;
    width: 154px;
    min-height: 292px;
    padding: 7px;
    margin-right: 6px;
}
.phase-board-surface.surface-ops-screen .surface-action-card {
    width: 154px;
    min-height: 344px;
}
.phase-board-surface .surface-action-card .card-topline,
.phase-board-surface .surface-action-card h3,
.phase-board-surface .surface-action-card p,
.phase-board-surface .surface-action-card .stat-grid {
    flex: none;
}
.phase-board-surface .surface-action-card.featured-action {
    border-color: #4c6d5a;
    background-color: #111f22;
}
.phase-board-surface .surface-action-card h3 {
    margin-top: 3px;
    margin-bottom: 3px;
    font-size: 14px;
}
.phase-board-surface .surface-action-card p {
    width: 140px;
    min-height: 62px;
    margin-top: 5px;
    margin-bottom: 6px;
    font-size: 12px;
    line-height: 1.22;
    overflow: hidden;
}
.phase-board-surface.surface-ops-screen .surface-action-card p {
    width: 140px;
    min-height: 72px;
}
.phase-board-surface .surface-action-card .stat-grid {
    width: 140px;
    min-height: 82px;
    height: auto;
    align-content: flex-start;
    align-items: flex-start;
    flex-wrap: wrap;
}
.phase-board-surface.surface-ops-screen .surface-action-card .stat-grid {
    width: 140px;
    min-height: 0px;
    height: auto;
}
.phase-board-surface .surface-action-card .stat-grid .stat-chip {
    flex: none;
    width: 62px;
    min-height: 16px;
    margin-top: 0px;
    margin-bottom: 3px;
    padding: 0px 4px;
    font-size: 11px;
    line-height: 16px;
    overflow: hidden;
}
.phase-board-surface .surface-action-card .stat-grid .stat-chip.wide {
    width: 132px;
}
.phase-board-surface.surface-ops-screen .surface-action-card .stat-grid .stat-chip.wide {
    width: 132px;
}
.phase-board-surface .surface-action-card .card-footer {
    width: 140px;
    min-height: 52px;
    margin-top: auto;
    padding-top: 6px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    display: flex;
    flex-direction: column;
    align-items: center;
}
.phase-board-surface.surface-ops-screen .surface-action-card .card-footer {
    width: 140px;
    min-height: 60px;
    margin-top: auto;
}
.phase-board-surface .surface-action-card .card-footer span {
    width: 140px;
    min-height: 16px;
    color: #d7c276;
    font-size: 11px;
    line-height: 16px;
    overflow: hidden;
}
.phase-board-surface.surface-ops-screen .surface-action-card .card-footer span {
    width: 140px;
    min-height: 18px;
    line-height: 18px;
}
.phase-board-surface .surface-action-card .card-footer button {
    width: 104px;
    min-height: 27px;
    height: auto;
    line-height: 1.15;
    margin-top: 3px;
    margin-left: 0px;
    margin-right: 0px;
    padding-left: 3px;
    padding-right: 3px;
    font-size: 11px;
}
.phase-board-surface.surface-ops-screen .surface-action-card .card-footer button {
    width: 120px;
    min-height: 30px;
    height: auto;
    line-height: 1.15;
    margin-left: 0px;
}
.phase-board-surface-minigame .surface-minigame,
.phase-board-surface-minigame .minigame-readout,
.phase-board-surface-minigame .minigame-callout,
.phase-board-surface-minigame .minigame-actions {
    width: 704px;
}
.phase-board-surface-minigame .surface-minigame {
    margin-top: 6px;
}
.phase-board-surface-minigame .phase-titlebar {
    margin-bottom: 6px;
}
.phase-board-surface-minigame .minigame-readout {
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    margin-top: 2px;
}
.phase-board-surface-minigame .minigame-metrics {
    width: 456px;
    flex-shrink: 0;
}
.phase-board-surface-minigame .minigame-metric-row {
    display: flex;
    flex-direction: row;
    width: 456px;
}
.phase-board-surface-minigame .minigame-metrics .metric {
    width: 204px;
    min-height: 52px;
    margin-right: 8px;
    margin-bottom: 8px;
}
.phase-board-surface-minigame .minigame-rewards {
    width: 210px;
    flex-shrink: 0;
    margin-left: 16px;
    justify-content: flex-end;
    align-content: flex-start;
}
.phase-board-surface-minigame .minigame-rewards .stat-chip {
    width: 132px;
    height: 28px;
    min-height: 28px;
    margin-left: 6px;
    margin-bottom: 6px;
    line-height: 28px;
    text-align: center;
    white-space: nowrap;
    overflow: hidden;
}
.phase-board-surface-minigame .minigame-callout {
    min-height: 78px;
    border-color: #5a4e25;
    background-color: #111c21;
}
.phase-board-surface-minigame .minigame-callout > div {
    width: 650px;
}
.phase-board-surface-minigame .minigame-callout h2 {
    color: #f2d15f;
}
.phase-board-surface-minigame .minigame-actions {
    display: flex;
    flex-direction: row;
    justify-content: center;
    margin-top: 10px;
}
.phase-board-surface-minigame .minigame-actions button {
    width: 176px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
}
.phase-board-surface-upgrade {
    width: 736px;
    padding-bottom: 24px;
}
.phase-board-surface-upgrade .draft-card-grid {
    width: 736px;
}
.phase-board-surface-upgrade .upgrade-draft-card {
    width: 206px;
    min-height: 368px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 8px;
    background-color: #151c24;
    border-width: 2px;
    border-color: #4f8e6b;
}
.phase-board-surface-upgrade .upgrade-draft-card .pilot-card-top {
    margin-bottom: 6px;
    padding-bottom: 4px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-art {
    height: 48px;
    margin-top: 4px;
    margin-bottom: 6px;
    background-color: #102019;
    border-width: 1px;
    border-color: #4f8e6b;
    border-radius: 6px;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-art span {
    width: 100%;
    color: #8de0b3;
    font-size: 24px;
    line-height: 48px;
    text-align: center;
}
.phase-board-surface-upgrade .upgrade-draft-card h3,
.phase-board-refit .upgrade-draft-card h3 {
    min-height: 34px;
    height: auto;
    margin-bottom: 5px;
    line-height: 1.16;
    overflow: hidden;
}
.phase-board-surface-upgrade .upgrade-draft-card p {
    min-height: 48px;
    margin-bottom: 6px;
    line-height: 1.28;
    overflow: hidden;
}
.phase-board-surface-upgrade .upgrade-draft-card .stat-grid {
    width: 186px;
    min-height: 0px;
    height: auto;
    margin-top: 5px;
    justify-content: center;
    align-content: flex-start;
    align-items: flex-start;
}
.phase-board-surface-upgrade .upgrade-draft-card .stat-chip {
    width: 70px;
    min-height: 20px;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 5px;
    margin-bottom: 4px;
    padding: 0px 4px;
    font-size: 11px;
    overflow: hidden;
}
.phase-board-surface-upgrade .upgrade-draft-card .stat-chip.wide {
    width: 146px;
    margin-right: 0px;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-card-footer {
    min-height: 52px;
    height: auto;
    margin-top: auto;
    padding-top: 8px;
    border-top-width: 1px;
    border-top-color: #34485a;
    align-items: center;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-card-footer span {
    width: 78px;
    color: #d7c276;
    font-size: 12px;
    line-height: 12px;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-card-footer button {
    width: 98px;
    min-height: 34px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 12px;
}
.phase-board-surface-upgrade .draft-actions {
    flex-wrap: nowrap;
    width: 704px;
    margin-left: 0px;
    margin-top: 12px;
    margin-bottom: 28px;
    justify-content: center;
}
.phase-board-surface-upgrade .draft-actions button {
    width: 224px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
}
.phase-board-hangar {
    width: 736px;
    padding-bottom: 28px;
}
.phase-board-hangar .summary-grid,
.phase-board-hangar .ops-grid {
    width: 704px;
}
.phase-board-hangar .summary-card {
    width: 202px;
}
.phase-board-hangar .ops-card {
    width: 202px;
    min-height: 182px;
    padding: 10px;
    margin-top: 10px;
    margin-right: 8px;
}
.phase-board-hangar .ops-card h3 {
    margin-bottom: 7px;
}
.phase-board-hangar .ops-card .ops-detail {
    color: #9aabba;
    min-height: 78px;
    margin-top: 0px;
    margin-bottom: 10px;
    line-height: 1.28;
    overflow: hidden;
}
.phase-board-hangar .ops-card .card-footer {
    display: flex;
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    min-height: 48px;
    height: auto;
    margin-top: auto;
    padding-top: 10px;
    border-top-width: 1px;
    border-top-color: #263b4c;
}
.phase-board-hangar .ops-card .ops-cost {
    color: #d7c276;
    width: 86px;
    padding: 0px;
    font-size: 13px;
    line-height: 1.2;
    overflow: hidden;
}
.phase-board-hangar .ops-card .card-footer button {
    width: 92px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
}
.phase-board-hangar .hangar-actions {
    width: 704px;
    margin-top: 12px;
    margin-bottom: 18px;
    justify-content: center;
}
.phase-board-hangar .hangar-actions button {
    width: 196px;
    margin-right: 8px;
}
.phase-board-draft-room .draft-hero {
    width: 708px;
    margin-top: 8px;
    margin-bottom: 10px;
    padding: 12px;
    background-color: #111b23;
    border-width: 1px;
    border-color: #6d5d35;
    border-radius: 8px;
}
.phase-board-draft-room .draft-hero span,
.phase-board-draft-room .draft-card-footer span {
    color: #d7c276;
}
.phase-board-draft-room .draft-hero h2 {
    margin-top: 3px;
    margin-bottom: 5px;
    color: #edf4f8;
}
.phase-board-draft-room .draft-board {
    width: 736px;
    margin-top: 8px;
}
.phase-board-refit {
    width: 736px;
    padding-bottom: 24px;
}
.phase-board-panel.draft-room-panel .panel-kpis {
    margin-top: 6px;
    margin-bottom: 6px;
}
.phase-board-panel.draft-room-panel .panel-kpis .metric {
    height: 54px;
    padding: 7px 9px;
}
.phase-board-panel.draft-room-panel .panel-kpis .metric strong {
    line-height: 17px;
}
.phase-board-panel.draft-room-panel .panel-kpis .metric span {
    line-height: 12px;
}
.phase-board-refit .draft-card-grid {
    width: 736px;
}
.phase-board-refit .upgrade-draft-card {
    width: 206px;
    min-height: 368px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 8px;
    background-color: #151c24;
    border-width: 2px;
    border-color: #6d5d35;
}
.phase-board-refit .upgrade-draft-card .pilot-card-top {
    margin-bottom: 6px;
    padding-bottom: 4px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-refit .upgrade-draft-card .draft-art {
    height: 48px;
    margin-top: 4px;
    margin-bottom: 6px;
    background-color: #0c141d;
    border-width: 1px;
    border-color: #3c596a;
    border-radius: 6px;
}
.phase-board-refit .upgrade-draft-card .draft-art span {
    width: 100%;
    color: #8fd8f0;
    font-size: 24px;
    line-height: 48px;
    text-align: center;
}
.phase-board-refit .upgrade-draft-card p {
    min-height: 46px;
    margin-bottom: 5px;
    line-height: 1.25;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .module-impact {
    color: #edf4f8;
    font-size: 13px;
    min-height: 30px;
    line-height: 14px;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .stat-grid {
    width: 186px;
    min-height: 0px;
    height: auto;
    margin-top: 5px;
    justify-content: center;
    align-content: flex-start;
    align-items: flex-start;
}
.phase-board-refit .upgrade-draft-card .stat-chip {
    width: 70px;
    min-height: 20px;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 5px;
    margin-bottom: 4px;
    padding: 0px 4px;
    font-size: 11px;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .stat-chip.wide {
    width: 146px;
    margin-right: 0px;
}
.phase-board-refit .upgrade-draft-card .draft-card-footer {
    min-height: 52px;
    height: auto;
    margin-top: auto;
    padding-top: 8px;
    border-top-width: 1px;
    border-top-color: #34485a;
    align-items: center;
}
.phase-board-refit .upgrade-draft-card .draft-card-footer span {
    width: 78px;
    min-height: 44px;
    font-size: 12px;
    line-height: 12px;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .draft-card-footer button {
    width: 98px;
    min-height: 34px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 12px;
}
.phase-board-refit .draft-actions {
    flex-wrap: nowrap;
    width: 704px;
    margin-left: 0px;
    margin-top: 8px;
    margin-bottom: 28px;
    justify-content: center;
}
.phase-board-refit .draft-actions button {
    width: 224px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
}
.phase-board-results {
    width: 736px;
    padding-bottom: 28px;
}
.phase-board-results .debrief-hero {
    width: 704px;
    margin-top: 8px;
    margin-bottom: 12px;
    padding: 14px;
    background-color: #111b23;
    border-width: 1px;
    border-color: #6d5d35;
    border-radius: 8px;
}
.phase-board-results .debrief-hero span {
    color: #d7c276;
    font-size: 12px;
}
.phase-board-results .debrief-hero h2 {
    margin-top: 4px;
    margin-bottom: 6px;
    color: #edf4f8;
    font-size: 20px;
}
.phase-board-results .debrief-hero p {
    width: 672px;
    line-height: 1.35;
}
.phase-board-results .debrief-handoff {
    display: flex;
    flex-direction: row;
    align-items: center;
    width: 704px;
    min-height: 112px;
    margin-top: 8px;
    margin-bottom: 10px;
    padding: 10px 12px;
    background-color: #101923;
    border-width: 1px;
    border-color: #31566b;
    border-radius: 8px;
}
.phase-board-results .debrief-handoff-copy {
    width: 184px;
    margin-right: 14px;
}
.phase-board-results .debrief-handoff-copy span {
    color: #d7c276;
    font-size: 11px;
}
.phase-board-results .debrief-handoff-copy h3 {
    margin-top: 4px;
    margin-bottom: 5px;
    color: #edf4f8;
    font-size: 16px;
}
.phase-board-results .debrief-handoff-copy p {
    width: 180px;
    margin-top: 0px;
    margin-bottom: 0px;
    color: #9eaebe;
    font-size: 12px;
    line-height: 1.22;
}
.phase-board-results .debrief-handoff-plan {
    width: 360px;
    margin-right: 12px;
}
.phase-board-results .debrief-phase-track {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 360px;
    margin-top: 0px;
    margin-bottom: 0px;
    padding: 0px;
}
.phase-board-results .phase-step-card {
    display: flex;
    flex-direction: column;
    width: 76px;
    min-height: 42px;
    margin-right: 7px;
    margin-bottom: 6px;
    padding: 6px 7px;
    background-color: #0d1d27;
    border-width: 1px;
    border-color: #31566b;
    border-radius: 6px;
}
.phase-board-results .phase-step-card.active {
    border-color: #70e0a8;
    background-color: #0d2119;
}
.phase-board-results .phase-step-card.done {
    border-color: #3d6174;
    background-color: #102332;
}
.phase-board-results .phase-step-card.pending {
    border-color: #34485a;
}
.phase-board-results .phase-step-card span {
    color: #9eaebe;
    font-size: 10px;
}
.phase-board-results .phase-step-card strong {
    margin-top: 3px;
    color: #edf4f8;
    font-size: 11px;
}
.phase-board-results .phase-step-card.active strong {
    color: #70e0a8;
}
.phase-board-results .debrief-handoff-actions {
    display: flex;
    flex-direction: column;
    width: 108px;
}
.phase-board-results .debrief-handoff-actions button {
    width: 106px;
    min-height: 34px;
    margin-right: 0px;
}
.phase-board-results .result-grid,
.phase-board-results .achievement-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 736px;
    margin-top: 8px;
}
.phase-board-results .crew-fate-card,
.phase-board-results .result-group,
.phase-board-results .achievement-card {
    padding: 11px 12px;
}
.phase-board-results .crew-fate-card {
    width: 704px;
    margin-top: 8px;
    margin-right: 0px;
}
.phase-board-results .crew-fate-card p {
    width: 646px;
    margin-top: 4px;
    line-height: 1.35;
}
.phase-board-results .crew-fate-signal {
    display: none;
}
.phase-board-results .result-group {
    width: 334px;
    min-height: 104px;
    margin-top: 8px;
    margin-right: 8px;
}
.phase-board-results .result-group.primary {
    width: 704px;
    margin-right: 0px;
}
.phase-board-results .result-group h3 {
    margin-bottom: 8px;
    padding-bottom: 6px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-results .result-row {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    width: 100%;
    min-height: 30px;
    margin-top: 5px;
    padding-top: 5px;
    border-top-width: 1px;
    border-top-color: #263b4c;
}
.phase-board-results .result-group h3 + .result-row {
    border-top-width: 0px;
}
.phase-board-results .result-row span {
    width: 126px;
    color: #8295a5;
    font-size: 12px;
}
.phase-board-results .result-row strong {
    width: 160px;
    margin-top: 0px;
}
.phase-board-results .result-group.primary .result-row span {
    width: 178px;
}
.phase-board-results .result-group.primary .result-row strong {
    width: 486px;
}
.phase-board-results .board-note {
    width: 704px;
    margin-top: 8px;
    padding: 8px 12px;
    color: #d7c276;
    background-color: #101923;
    border-width: 1px;
    border-color: #4c4728;
    border-radius: 6px;
}
.phase-board-results .achievement-card {
    width: 704px;
    margin-right: 0px;
}
.phase-board-results .achievement-card p {
    width: 670px;
}
.phase-board-results .actions {
    width: 704px;
    margin-left: 16px;
    margin-right: 16px;
    margin-top: 16px;
    margin-bottom: 22px;
    justify-content: center;
}
.phase-board-results .actions button {
    width: 230px;
    min-height: 42px;
    height: auto;
    line-height: 1.15;
    margin-right: 0px;
}
.metric, .summary-card, .ops-card, .pilot-card, .inventory-item, .resource-bank, .phase-advisory, .cockpit-hud, .surface-primary-action, .achievement-card, .crew-fate-card, .tutorial-card, .result-group {
    margin-top: 8px;
    margin-right: 8px;
    padding: 9px 10px;
    background-color: #131c26;
    border-width: 1px;
    border-color: #35495a;
    border-radius: 6px;
}
.inventory-modal {
    width: 724px;
    margin-top: 10px;
}
.inventory-layout {
    display: flex;
    flex-direction: row;
    width: 724px;
}
.inventory-side-column {
    width: 164px;
    margin-right: 10px;
}
.inventory-main-column {
    width: 550px;
}
.inventory-modal.inventory-main-only,
.inventory-layout-main-only,
.inventory-layout-main-only .inventory-main-column,
.inventory-layout-main-only .inventory-section,
.inventory-layout-main-only .inventory-section-head,
.inventory-layout-main-only .inventory-grid {
    width: 724px;
}
.inventory-layout-main-only .inventory-section-head p {
    width: 696px;
}
.inventory-side-column .inventory-section {
    width: 164px;
    margin-top: 0px;
    padding-top: 0px;
    border-top-width: 0px;
}
.inventory-side-column .inventory-section-head {
    width: 164px;
}
.inventory-side-column .inventory-section-head p {
    width: 150px;
}
.inventory-side-column .inventory-grid {
    width: 164px;
}
.inventory-summary {
    width: 550px;
    margin-top: 4px;
    margin-bottom: 8px;
}
.inventory-layout-main-only .inventory-summary {
    width: 724px;
}
.inventory-summary .metric {
    width: 154px;
    height: 42px;
    padding: 8px 10px;
    margin-right: 10px;
    margin-top: 8px;
    background-color: #101c27;
    border-color: #46677d;
}
.inventory-layout-main-only .inventory-summary .metric {
    width: 184px;
}
.inventory-summary .metric span {
    width: 134px;
    white-space: nowrap;
}
.inventory-layout-main-only .inventory-summary .metric span {
    width: 164px;
}
.inventory-section {
    width: 550px;
    margin-top: 8px;
    padding-top: 8px;
    border-top-width: 1px;
    border-top-color: #26394a;
}
.inventory-section-head {
    width: 550px;
    margin-bottom: 6px;
}
.inventory-section-head h3 {
    margin-top: 0px;
    color: #edf4f8;
    font-size: 16px;
}
.inventory-section-head p {
    width: 520px;
    margin-top: 4px;
    line-height: 1.32;
}
.inventory-grid {
    width: 550px;
}
.inventory-item {
    width: 110px;
    min-height: 88px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 10px;
    background-color: #101923;
    border-color: #46677d;
}
.inventory-art {
    width: 42px;
    height: 34px;
    padding-top: 8px;
    margin-bottom: 8px;
    background-color: #0b131c;
    border-width: 1px;
    border-color: #40596d;
    border-radius: 6px;
}
.inventory-art span {
    color: #8de0ff;
    font-size: 15px;
    text-align: center;
}
.inventory-copy {
    width: 110px;
}
.inventory-copy h3 {
    margin-top: 0px;
    color: #edf4f8;
    font-size: 15px;
    line-height: 1.18;
}
.inventory-copy p {
    width: 106px;
    margin-top: 5px;
    color: #9eb0bf;
    font-size: 13px;
    line-height: 1.28;
}
.inventory-count {
    width: 100px;
    min-height: 20px;
    margin-top: 9px;
    padding: 5px 8px;
    background-color: #0b131c;
    border-width: 1px;
    border-color: #40596d;
    border-radius: 5px;
    color: #edf4f8;
    font-size: 13px;
    text-align: center;
}
.inventory-section-resources .inventory-item {
    width: 118px;
    min-height: 88px;
    padding: 10px;
    margin-right: 8px;
}
.inventory-layout-main-only .inventory-section-resources .inventory-item {
    width: 148px;
}
.inventory-section-resources .inventory-copy {
    width: 118px;
}
.inventory-layout-main-only .inventory-section-resources .inventory-copy {
    width: 148px;
}
.inventory-section-resources .inventory-copy h3 {
    font-size: 15px;
    overflow: hidden;
    white-space: nowrap;
}
.inventory-section-resources .inventory-copy p {
    width: 112px;
    font-size: 13px;
}
.inventory-layout-main-only .inventory-section-resources .inventory-copy p {
    width: 142px;
}
.inventory-section-resources .inventory-art {
    width: 42px;
    height: 34px;
    padding-top: 8px;
}
.inventory-section-resources .inventory-count {
    width: 100px;
}
.inventory-side-column .inventory-item {
    width: 140px;
    min-height: 58px;
    padding: 6px 8px;
    margin-top: 5px;
    margin-right: 0px;
}
.inventory-side-column .inventory-art {
    width: 30px;
    height: 24px;
    padding-top: 5px;
    margin-bottom: 4px;
}
.inventory-side-column .inventory-copy {
    width: 184px;
}
.inventory-side-column .inventory-copy h3 {
    font-size: 13px;
}
.inventory-side-column .inventory-copy p {
    width: 178px;
    margin-top: 2px;
    font-size: 11px;
    line-height: 1.1;
}
.inventory-side-column .inventory-count {
    width: 70px;
    min-height: 16px;
    margin-top: 3px;
    padding: 3px 5px;
    font-size: 11px;
}
.inventory-item.drone-slot.equipped {
    border-color: #5ed993;
}
.inventory-item.drone-slot.open {
    background-color: #102432;
    border-color: #4aa9d8;
}
.inventory-item.drone-slot.open .inventory-art,
.inventory-item.drone-slot.open .inventory-count {
    background-color: #153247;
    border-color: #4aa9d8;
    color: #8de0ff;
}
.inventory-item.drone-slot.locked {
    background-color: #101923;
    border-color: #35495a;
}
.inventory-item.drone-slot.locked .inventory-art,
.inventory-item.drone-slot.locked .inventory-count {
    background-color: #0b131c;
    border-color: #35495a;
    color: #8295a5;
}
.inventory-item.blueprint {
    background-color: #112638;
    border-color: #4aa9d8;
}
.inventory-item.blueprint .inventory-art,
.inventory-item.blueprint .inventory-count {
    background-color: #153247;
    border-color: #4aa9d8;
    color: #8de0ff;
}
.inventory-item.blueprint .inventory-art span {
    color: #8de0ff;
}
.inventory-item.common {
    background-color: #171d23;
    border-color: #647382;
}
.inventory-item.common .inventory-art,
.inventory-item.common .inventory-count {
    background-color: #111820;
    border-color: #647382;
    color: #c2cbd3;
}
.inventory-item.common .inventory-art span {
    color: #c2cbd3;
}
.inventory-item.uncommon {
    background-color: #102b25;
    border-color: #52d990;
}
.inventory-item.uncommon .inventory-art,
.inventory-item.uncommon .inventory-count {
    background-color: #163d31;
    border-color: #52d990;
    color: #8bf0bd;
}
.inventory-item.uncommon .inventory-art span {
    color: #8bf0bd;
}
.inventory-item.rare {
    background-color: #2b2412;
    border-color: #d7aa3a;
}
.inventory-item.rare .inventory-art,
.inventory-item.rare .inventory-count {
    background-color: #3c3014;
    border-color: #d7aa3a;
    color: #ffd166;
}
.inventory-item.rare .inventory-art span {
    color: #ffd166;
}
.inventory-item.exotic {
    background-color: #2a1734;
    border-color: #e05aaf;
}
.inventory-item.exotic .inventory-art,
.inventory-item.exotic .inventory-count {
    background-color: #3b1c47;
    border-color: #e05aaf;
    color: #ffaad9;
}
.inventory-item.exotic .inventory-art span {
    color: #ffaad9;
}
.inventory-item.artifact {
    background-color: #241b38;
    border-color: #a974ff;
}
.inventory-item.artifact .inventory-art,
.inventory-item.artifact .inventory-count {
    background-color: #30224d;
    border-color: #a974ff;
    color: #d2b7ff;
}
.inventory-item.artifact .inventory-art span {
    color: #d2b7ff;
}
.inventory-item.module {
    background-color: #112638;
    border-color: #4aa9d8;
}
.inventory-item.module .inventory-art,
.inventory-item.module .inventory-count {
    background-color: #153247;
    border-color: #4aa9d8;
}
.inventory-item.module .inventory-art span {
    color: #8de0ff;
}
.inventory-item.drone {
    background-color: #123026;
    border-color: #5ed993;
}
.inventory-item.drone .inventory-art,
.inventory-item.drone .inventory-count {
    background-color: #173d2f;
    border-color: #5ed993;
}
.inventory-item.drone .inventory-art span {
    color: #8bf0bd;
}
.upgrade-draft-card.rarity-common,
.ops-card.rarity-common,
.inventory-item.rarity-common {
    background-color: #171d23;
    border-color: #647382;
}
.upgrade-draft-card.rarity-common .draft-art,
.ops-card.rarity-common .module-art,
.inventory-item.rarity-common .inventory-art,
.inventory-item.rarity-common .inventory-count {
    background-color: #111820;
    border-color: #647382;
    color: #c2cbd3;
}
.upgrade-draft-card.rarity-common .draft-art span,
.ops-card.rarity-common .module-art span,
.inventory-item.rarity-common .inventory-art span {
    color: #c2cbd3;
}
.upgrade-draft-card.rarity-uncommon,
.ops-card.rarity-uncommon,
.inventory-item.rarity-uncommon {
    background-color: #102b25;
    border-color: #52d990;
}
.upgrade-draft-card.rarity-uncommon .draft-art,
.ops-card.rarity-uncommon .module-art,
.inventory-item.rarity-uncommon .inventory-art,
.inventory-item.rarity-uncommon .inventory-count {
    background-color: #163d31;
    border-color: #52d990;
    color: #8bf0bd;
}
.upgrade-draft-card.rarity-uncommon .draft-art span,
.ops-card.rarity-uncommon .module-art span,
.inventory-item.rarity-uncommon .inventory-art span {
    color: #8bf0bd;
}
.upgrade-draft-card.rarity-rare,
.ops-card.rarity-rare,
.inventory-item.rarity-rare {
    background-color: #2b2412;
    border-color: #d7aa3a;
}
.upgrade-draft-card.rarity-rare .draft-art,
.ops-card.rarity-rare .module-art,
.inventory-item.rarity-rare .inventory-art,
.inventory-item.rarity-rare .inventory-count {
    background-color: #3c3014;
    border-color: #d7aa3a;
    color: #ffd166;
}
.upgrade-draft-card.rarity-rare .draft-art span,
.ops-card.rarity-rare .module-art span,
.inventory-item.rarity-rare .inventory-art span {
    color: #ffd166;
}
.upgrade-draft-card.rarity-prototype,
.ops-card.rarity-prototype,
.inventory-item.rarity-prototype {
    background-color: #241b38;
    border-color: #a974ff;
}
.upgrade-draft-card.rarity-prototype .draft-art,
.ops-card.rarity-prototype .module-art,
.inventory-item.rarity-prototype .inventory-art,
.inventory-item.rarity-prototype .inventory-count {
    background-color: #30224d;
    border-color: #a974ff;
    color: #d2b7ff;
}
.upgrade-draft-card.rarity-prototype .draft-art span,
.ops-card.rarity-prototype .module-art span,
.inventory-item.rarity-prototype .inventory-art span {
    color: #d2b7ff;
}
.upgrade-draft-card.rarity-exotic,
.ops-card.rarity-exotic,
.inventory-item.rarity-exotic {
    background-color: #2a1734;
    border-color: #e05aaf;
}
.upgrade-draft-card.rarity-exotic .draft-art,
.ops-card.rarity-exotic .module-art,
.inventory-item.rarity-exotic .inventory-art,
.inventory-item.rarity-exotic .inventory-count {
    background-color: #3b1c47;
    border-color: #e05aaf;
    color: #ffaad9;
}
.upgrade-draft-card.rarity-exotic .draft-art span,
.ops-card.rarity-exotic .module-art span,
.inventory-item.rarity-exotic .inventory-art span {
    color: #ffaad9;
}
.resource-bank, .phase-advisory, .cockpit-hud, .surface-primary-action, .tutorial-card, .draft-hero, .draft-board, .board-primary {
    width: 100%;
}
.metric, .surface-kpi {
    width: 128px;
}
.summary-card {
    width: 184px;
}
.ops-card, .pilot-card, .upgrade-card, .upgrade-draft-card, .surface-action-card {
    width: 292px;
}
.flight-readout .metric, .focus-metrics .metric {
    width: 132px;
}
.control-panel .metric-grid {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .phase-board-surface-minigame .minigame-readout {
    width: 704px;
}
.control-panel .phase-board-surface-minigame .minigame-metrics {
    width: 456px;
    flex-shrink: 0;
}
.control-panel .phase-board-surface-minigame .minigame-metric-row {
    width: 456px;
}
.control-panel .phase-board-surface-minigame .minigame-rewards {
    width: 210px;
    flex-shrink: 0;
    margin-left: 16px;
}
.control-panel .panel-kpis {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .panel-kpis .metric,
.control-panel .flight-readout .metric,
.control-panel .focus-metrics .metric {
    width: 160px;
    padding: 7px 9px;
    margin-top: 6px;
}
.control-panel .telemetry .metric,
.control-panel .mining-metrics .metric {
    width: 168px;
}
.control-panel .phase-board-mining {
    width: 430px;
    padding-bottom: 12px;
}
.control-panel .phase-board-mining .phase-titlebar {
    width: 420px;
    margin-top: 8px;
    margin-bottom: 8px;
}
.control-panel .phase-board-mining .phase-titlebar > div {
    width: 282px;
}
.control-panel .phase-board-mining .phase-titlebar h2 {
    margin-top: 0px;
}
.control-panel .phase-board-mining .phase-titlebar p {
    width: 278px;
    line-height: 1.24;
}
.control-panel .phase-board-mining .compact-tools {
    width: 126px;
    justify-content: flex-end;
}
.control-panel .phase-board-mining .compact-tools button {
    width: 112px;
    height: 34px;
    line-height: 34px;
    margin-top: 0px;
    margin-right: 0px;
}
.control-panel .phase-board-mining .tutorial-card,
.control-panel .phase-board-mining .phase-advisory,
.control-panel .phase-board-mining .mining-health-strip,
.control-panel .phase-board-mining .mining-combat-strip,
.control-panel .phase-board-mining .mining-payload,
.control-panel .phase-board-mining .mining-hud {
    width: 400px;
}
.control-panel .phase-board-mining .mining-health-strip {
    padding: 8px 10px;
    margin-top: 8px;
    margin-bottom: 4px;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.28);
    background-color: rgba(7, 24, 32, 0.86);
}
.control-panel .phase-board-mining .mining-health-strip > div {
    width: 380px;
    height: 22px;
}
.control-panel .phase-board-mining .mining-health-strip span {
    display: inline-block;
    width: 250px;
    color: rgba(169, 231, 246, 0.88);
    font-size: 12px;
    text-transform: uppercase;
}
.control-panel .phase-board-mining .mining-health-strip strong {
    display: inline-block;
    width: 118px;
    color: #e7fbff;
    font-size: 22px;
    text-align: right;
}
.control-panel .phase-board-mining .mining-health-bar {
    width: 378px;
    height: 12px;
    margin-top: 4px;
    margin-bottom: 0px;
    background-color: rgba(1, 8, 13, 0.92);
}
.control-panel .phase-board-mining .mining-health-bar i {
    display: block;
    height: 12px;
    background-color: #4be7ff;
}
.control-panel .phase-board-mining .mining-health-bar .health-fill-0 { width: 0px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-10 { width: 38px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-20 { width: 76px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-30 { width: 113px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-40 { width: 151px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-50 { width: 189px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-60 { width: 227px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-70 { width: 265px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-80 { width: 302px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-90 { width: 340px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-100 { width: 378px; }
.control-panel .phase-board-mining .mining-health-strip p {
    width: 380px;
    margin-top: 0px;
    margin-bottom: 0px;
    color: rgba(199, 224, 226, 0.76);
    font-size: 12px;
    line-height: 1.22;
}
.control-panel .phase-board-mining .mining-combat-strip {
    padding: 10px;
    margin-top: 8px;
    margin-bottom: 6px;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.26);
    background-color: rgba(9, 28, 36, 0.84);
}
.control-panel .phase-board-mining .mining-combat-strip > div {
    width: 382px;
}
.control-panel .phase-board-mining .mining-combat-strip h2 {
    margin-top: 0px;
    margin-bottom: 4px;
    color: #dffbff;
    font-size: 15px;
}
.control-panel .phase-board-mining .mining-combat-strip p {
    width: 382px;
    margin-top: 0px;
    margin-bottom: 6px;
    color: rgba(199, 224, 226, 0.76);
    font-size: 12px;
    line-height: 1.20;
}
.control-panel .phase-board-mining .mining-combat-strip .stat-grid {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .phase-board-mining .mining-combat-strip .stat-chip {
    width: 104px;
    min-height: 17px;
    padding: 4px 6px;
    margin-top: 4px;
    margin-right: 5px;
    font-size: 11px;
    line-height: 1.12;
}
.control-panel .phase-board-mining .mining-payload {
    padding: 8px 10px;
    margin-top: 6px;
    margin-bottom: 0px;
}
.control-panel .phase-board-mining .mining-payload > div {
    width: 382px;
}
.control-panel .phase-board-mining .mining-payload h2 {
    margin-top: 0px;
    margin-bottom: 4px;
}
.control-panel .phase-board-mining .mining-payload p {
    width: 382px;
    margin-bottom: 5px;
    line-height: 1.24;
}
.control-panel .phase-board-mining .mining-payload .stat-grid {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .phase-board-mining .mining-payload .stat-chip {
    width: 104px;
    min-height: 17px;
    padding: 4px 6px;
    margin-top: 4px;
    margin-right: 5px;
    font-size: 11px;
    line-height: 1.12;
}
.control-panel .phase-board-mining .mining-metrics {
    width: 420px;
    margin-top: 4px;
    margin-bottom: 4px;
    justify-content: flex-start;
}
.control-panel .phase-board-mining .mining-metrics .metric {
    width: 112px;
    min-height: 36px;
    margin-top: 4px;
    margin-right: 4px;
    padding: 5px 6px;
}
.control-panel .phase-board-mining .mining-metrics .metric strong {
    font-size: 16px;
    line-height: 1.05;
}
.control-panel .phase-board-mining .mining-metrics .metric span {
    font-size: 11px;
    line-height: 1.12;
}
.control-panel .phase-board-mining .mining-hud {
    margin-top: 6px;
    margin-bottom: 6px;
    padding: 10px;
}
.control-panel .phase-board-mining .mining-hud .system-actions {
    width: 390px;
    justify-content: flex-start;
    flex-wrap: wrap;
    margin-top: 8px;
}
.control-panel .phase-board-mining .mining-hud .system-actions button {
    min-width: 0px;
    width: 174px;
    height: 42px;
    line-height: 42px;
    margin-top: 0px;
    margin-right: 8px;
    margin-bottom: 6px;
    padding-top: 0px;
    padding-left: 4px;
    padding-right: 4px;
    padding-bottom: 0px;
    font-size: 13px;
}
.control-panel h2 {
    margin-top: 10px;
    margin-bottom: 5px;
}
.control-panel .warning-grid {
    width: 390px;
    justify-content: center;
    margin-top: 6px;
    margin-bottom: 8px;
}
.control-panel .warning-button {
    min-width: 0px;
    width: 104px;
    height: 46px;
    line-height: 1.1;
    padding: 4px 5px;
    margin-top: 4px;
    margin-right: 6px;
    text-align: left;
}
.control-panel .warning-button strong {
    display: block;
    width: 100%;
    color: #9eb0bd;
    font-size: 11px;
    text-align: center;
}
.control-panel .warning-button span {
    display: block;
    width: 100%;
    margin-top: 2px;
    color: #edf4f8;
    font-size: 23px;
    line-height: 1.05;
    text-align: center;
}
.control-panel .tutorial-card,
.control-panel .phase-advisory {
    width: 370px;
}
.control-panel .tutorial-card {
    display: block;
    margin-top: 8px;
    margin-bottom: 12px;
    padding: 10px;
    background-color: #101923;
    border-color: #2f4354;
}
.control-panel .tutorial-card div {
    display: block;
    width: 344px;
}
.control-panel .tutorial-card span {
    color: #7f91a0;
    font-size: 12px;
}
.control-panel .tutorial-card strong {
    margin-top: 2px;
    margin-bottom: 4px;
}
.control-panel .tutorial-card p {
    width: 344px;
    line-height: 1.25;
}
.control-panel .tutorial-card button {
    display: block;
    min-width: 0px;
    width: 128px;
    height: 32px;
    line-height: 32px;
    margin-top: 8px;
    margin-right: 0px;
}
.control-panel .telemetry-status,
.control-panel .phase-copy,
.control-panel .cockpit-hold-copy {
    width: 390px;
}
.control-panel .telemetry-status {
    margin-top: 6px;
    margin-bottom: 8px;
    line-height: 1.25;
}
.control-panel .cockpit-hud {
    width: 390px;
    margin-bottom: 12px;
    padding: 9px;
}
.control-panel .actions {
    width: 372px;
    justify-content: center;
}
.control-panel .actions button {
    width: 176px;
    height: 42px;
    line-height: 42px;
    margin-right: 8px;
}
.control-panel .flight-hud .primary-actions {
    flex-wrap: nowrap;
    width: 372px;
}
.control-panel .flight-hud .primary-actions button {
    width: 176px;
    height: 42px;
    line-height: 42px;
}
.control-panel .flight-hud .system-actions {
    flex-wrap: nowrap;
    width: 372px;
    margin-top: 8px;
}
.stat-chip, .telemetry-legend-chip, .surface-kpi {
    margin-top: 5px;
    margin-right: 5px;
    padding: 5px 7px;
    background-color: #102835;
    border-width: 1px;
    border-color: #34566a;
    border-radius: 5px;
    overflow: hidden;
}
.stat-chip, .telemetry-legend-chip {
    white-space: nowrap;
}
.stat-grid .stat-chip {
    width: 108px;
}
.phase-board-drone-ops .drone-card .stat-grid .stat-chip {
    width: 82px;
    min-height: 20px;
    padding: 4px 5px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-card .stat-grid .stat-chip.wide {
    width: 112px;
}
.phase-board-drone-ops .drone-bay-strip .stat-grid .stat-chip {
    width: 76px;
    min-height: 20px;
    padding: 4px 5px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-bay-strip .stat-grid .stat-chip.wide {
    width: 106px;
}
h1 {
    font-size: 21px;
    margin-bottom: 4px;
}
h2 {
    font-size: 17px;
    color: #99a9b8;
    margin-top: 12px;
    margin-bottom: 6px;
}
h3 {
    font-size: 15px;
    margin-bottom: 4px;
}
p, span {
    color: #99a9b8;
    line-height: 1.35;
}
strong {
    color: #edf4f8;
}
.metric span, .stat-chip span, .summary-card span, .surface-kpi span {
    font-size: 12px;
}
.metric strong, .stat-chip strong, .summary-card strong, .surface-kpi strong {
    margin-top: 2px;
}
.card-footer span, .draft-card-footer span, .card-topline span, .card-kicker span, .pilot-card-top span {
    display: inline-block;
}
.detail-stack {
    display: flex;
    flex-direction: column;
    width: 100%;
    margin-top: 8px;
}
.detail-row {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    width: 572px;
    margin-top: 7px;
    padding: 7px 8px;
    background-color: #111a24;
    border-width: 1px;
    border-color: #243749;
    border-radius: 5px;
}
.detail-row span, .detail-row strong {
    display: inline-block;
}
.detail-row span {
    width: 128px;
    color: #7f91a0;
}
.detail-row strong {
    width: 408px;
    line-height: 1.28;
}
.detail-section {
    margin-top: 10px;
    margin-bottom: 4px;
    color: #edf4f8;
}
button {
    display: inline-block;
    min-width: 92px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    margin-top: 6px;
    margin-right: 6px;
    padding: 7px 9px;
    color: #edf4f8;
    text-align: center;
    background-color: #153244;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 6px;
}
.actions button {
    width: 184px;
}
.control-panel .flight-hud .system-actions button {
    width: 116px;
    box-sizing: border-box;
    padding: 0 6px;
    height: 38px;
    min-height: 38px;
    line-height: 38px;
    margin-right: 8px;
    font-size: 12px;
    white-space: nowrap;
    overflow-wrap: normal;
    overflow: hidden;
    text-overflow: ellipsis;
    text-align: center;
}
.card-footer button, .draft-card-footer button, .summary-card button, .resource-bank button {
    width: 116px;
}
.phase-board-drone-ops .drone-control-card .card-footer button {
    width: 82px;
    min-width: 82px;
    height: 18px;
    min-height: 18px;
    margin-top: 0px;
    margin-right: 0px;
    margin-bottom: 0px;
    padding: 2px 5px;
    font-size: 9px;
    line-height: 1.1;
}
.phase-board-drone-ops .drone-bay-strip button {
    width: 78px;
    min-width: 78px;
    min-height: 28px;
    margin-top: 0px;
    margin-right: 0px;
    padding: 4px 5px;
    font-size: 10px;
    line-height: 1.1;
}
.phase-board-drone-ops .drone-control-card,
.phase-board-drone-ops .drone-loadout-slot {
    border-radius: 8px;
    border-width: 2px;
    border-color: #31566b;
    background-color: #0d1d27;
}
.phase-board-drone-ops .drone-top-row {
    width: 704px;
    margin-top: 8px;
    margin-bottom: 8px;
}
.phase-board-drone-ops .drone-bay-strip {
    width: 684px;
    min-height: 116px;
    padding: 10px;
    justify-content: flex-start;
}
.phase-board-drone-ops .drone-bay-strip .drone-bay-copy {
    width: 198px;
    margin-right: 18px;
}
.phase-board-drone-ops .drone-bay-strip p {
    width: 190px;
    line-height: 1.2;
}
.phase-board-drone-ops .drone-bay-stats,
.phase-board-drone-ops .drone-bay-materials {
    width: 132px;
    margin-right: 12px;
}
.phase-board-drone-ops .drone-bay-materials {
    margin-right: 24px;
}
.phase-board-drone-ops .drone-bay-strip .stat-chip {
    width: 122px;
    max-width: 122px;
    min-height: 24px;
    padding: 4px 6px;
}
.phase-board-drone-ops .drone-bay-strip button {
    width: 100px;
    min-width: 100px;
    min-height: 34px;
}
.phase-board-drone-ops .drone-control-card {
    height: 142px;
    min-height: 142px;
    padding: 8px;
}
.phase-board-drone-ops .drone-control-card.rarity-common {
    border-color: #31566b;
}
.phase-board-drone-ops .drone-control-card.rarity-uncommon {
    border-color: #3a735c;
    background-color: #0d2222;
}
.phase-board-drone-ops .drone-control-card.rarity-rare {
    border-color: #806b2c;
    background-color: #201c12;
}
.phase-board-drone-ops .drone-card-head {
    display: flex;
    flex-direction: row;
    align-items: flex-start;
    width: 184px;
    min-height: 32px;
}
.phase-board-drone-ops .drone-role-mark {
    display: block;
    width: 22px;
    height: 22px;
    margin-right: 7px;
    padding-top: 3px;
    color: #dffbff;
    text-align: center;
    font-size: 12px;
    line-height: 1;
    border-width: 1px;
    border-color: #4a7f96;
    border-radius: 6px;
    background-color: #15384a;
}
.phase-board-drone-ops .drone-card-id {
    display: block;
    width: 155px;
}
.phase-board-drone-ops .drone-card-id .card-topline {
    width: 155px;
    min-height: 11px;
    font-size: 9px;
    color: #89b2d3;
}
.phase-board-drone-ops .drone-card-id .card-title {
    width: 155px;
    margin-top: 2px;
    margin-bottom: 0px;
    font-size: 12px;
}
.phase-board-drone-ops .drone-control-status {
    width: 184px;
    min-height: 15px;
    max-height: 15px;
    margin-top: 3px;
    padding-top: 2px;
    color: #bfeef7;
    font-size: 10px;
    border-top-width: 1px;
    border-top-color: #243b4c;
}
.phase-board-drone-ops .drone-control-card .stat-grid {
    width: 184px;
    min-height: 42px;
    margin-top: 5px;
}
.phase-board-drone-ops .drone-control-card .stat-chip {
    width: 104px;
    min-height: 15px;
    padding: 2px 5px;
}
.phase-board-drone-ops .drone-control-card .card-footer {
    width: 184px;
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    min-height: 24px;
    padding-top: 5px;
    border-top-color: #2d5265;
    justify-content: space-between;
}
.phase-board-drone-ops .drone-loadout-slot {
    height: 118px;
    min-height: 118px;
    padding: 8px;
    background-color: #0b1720;
}
.phase-board-drone-ops .drone-loadout-slot.filled {
    border-color: #367486;
    background-color: #0d252d;
}
.phase-board-drone-ops .drone-loadout-slot.open {
    border-color: #397459;
    background-color: #0d2119;
}
.phase-board-drone-ops .drone-loadout-slot.locked {
    border-color: #2a3845;
    background-color: #0e141a;
}
.phase-board-drone-ops .slot-card-head {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    width: 184px;
    min-height: 22px;
}
.phase-board-drone-ops .slot-number {
    display: block;
    width: 24px;
    min-height: 16px;
    padding-top: 2px;
    color: #dffbff;
    text-align: center;
    font-size: 10px;
    border-width: 1px;
    border-color: #456b80;
    border-radius: 5px;
    background-color: #132f3d;
}
.phase-board-drone-ops .slot-state {
    width: 142px;
    color: #ffd166;
    text-align: right;
    font-size: 10px;
}
.phase-board-drone-ops .slot-card-head button {
    width: 78px;
    min-width: 78px;
    height: 17px;
    min-height: 17px;
    margin-top: 0px;
    margin-right: 0px;
    margin-bottom: 0px;
    padding: 1px 5px;
    font-size: 9px;
    line-height: 1;
}
.phase-board-drone-ops .slot-card-body {
    width: 184px;
    min-height: 32px;
    margin-top: 3px;
}
.phase-board-drone-ops .slot-card-body .card-title {
    margin-top: 0px;
    margin-bottom: 1px;
    font-size: 12px;
}
.phase-board-drone-ops .slot-card-body .slot-role {
    min-height: 11px;
    margin-top: 0px;
    color: #bfeef7;
    font-size: 10px;
}
.phase-board-drone-ops .drone-loadout-slot .stat-grid {
    width: 184px;
    min-height: 17px;
    margin-top: 4px;
}
.phase-board-drone-ops .drone-loadout-slot .stat-chip {
    width: 76px;
    min-height: 14px;
    padding: 2px 5px;
    font-size: 9px;
}
.phase-board-drone-ops .drone-loadout-slot .stat-chip.wide {
    width: 92px;
}
button:hover {
    background-color: #1f4b62;
}
button.disabled,
button:disabled {
    color: #8a939c;
    background-color: #252b31;
    border-color: #47515a;
}
button.disabled:hover,
button:disabled:hover {
    color: #8a939c;
    background-color: #252b31;
    border-color: #47515a;
}
button.ok {
    background-color: #1d4a39;
    border-color: #5ba77f;
}
button.warn {
    background-color: #4c3f1c;
    border-color: #b99a3f;
}
button.rare {
    color: #ffd166;
    background-color: #3c3014;
    border-color: #d7aa3a;
}
button.danger {
    background-color: #4a2421;
    border-color: #b85b51;
}
button.ghost {
    color: #99a9b8;
    background-color: #1a222d;
}
button.settings-toggle {
    width: 184px;
    min-height: 36px;
    color: #edf4f8;
    background-color: #173044;
    border-color: #4aa9d8;
}
button.settings-toggle:hover {
    color: #edf4f8;
    background-color: #1f4b62;
    border-color: #67d2ff;
}
.settings-control button.settings-toggle {
    margin-top: 6px;
}
.settings-control {
    margin-top: 12px;
}
.settings-control h3 {
    margin-bottom: 4px;
}
.settings-control p {
    width: 560px;
}
.settings-control select {
    display: block;
    width: 184px;
    height: 36px;
    margin-top: 6px;
}
.settings-checkbox {
    display: block;
    width: 240px;
    min-height: 26px;
    margin-top: 6px;
    color: #edf4f8;
}
.settings-checkbox input {
    display: inline-block;
    width: 18px;
    height: 18px;
    margin-right: 8px;
    background-color: #0c141d;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 4px;
}
.settings-checkbox input:checked {
    background-color: #1d4a39;
    border-color: #72e0a8;
}
.settings-checkbox span {
    display: inline-block;
    height: 22px;
    line-height: 22px;
}
.settings-control select selectvalue {
    width: auto;
    height: 28px;
    margin-right: 0px;
    padding: 8px 10px 0px 10px;
    color: #edf4f8;
    background-color: #131c26;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 6px;
}
.settings-control select selectarrow {
    width: 0px;
    height: 0px;
    margin: 0px;
    padding: 0px;
    border-width: 0px;
    background-color: transparent;
}
.settings-control select:hover selectvalue {
    background-color: #1f4b62;
}
.settings-control select selectbox {
    width: 184px;
    margin-top: 2px;
    padding: 4px;
    background-color: #0c121a;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 6px;
}
.settings-control select selectbox option {
    display: block;
    width: auto;
    padding: 6px 8px;
    color: #edf4f8;
    background-color: #131c26;
}
.settings-control select selectbox option:hover,
.settings-control select selectbox option:checked {
    background-color: #1f4b62;
}
.status, .phase-status {
    color: #ffd166;
    margin-top: 10px;
}
.danger, .critical {
    border-color: #c95d50;
}
.warn, .caution {
    border-color: #c8a446;
}
.ok {
    border-color: #5ba77f;
}
#rr-modal-scrim {
    position: absolute;
    left: 0px;
    top: 0px;
    width: 100%;
    height: 100%;
    background-color: #05070a;
}
#rr-modal {
    position: absolute;
    left: 50%;
    top: 9%;
    width: 640px;
    height: 78%;
    overflow: visible;
    margin-left: -320px;
    padding: 16px;
    background-color: #0c121a;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 8px;
    overflow: hidden;
}
#rr-modal.modal-inventory {
    top: 4%;
    width: 760px;
    height: 88%;
    margin-left: -400px;
    padding: 18px;
}
#rr-modal.modal-phase_briefing {
    top: 14%;
    height: 340px;
    padding: 20px;
}
#rr-modal.modal-surface {
    top: 8%;
    height: 680px;
    padding: 20px;
}
#rr-modal.modal-mission_log {
    top: 12%;
    height: 300px;
    padding: 20px;
}
.modal-head {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    margin-bottom: 8px;
}
)";
}

std::string buildDocumentRml(const std::string& panelHtml, const std::string& openModalId)
{
    const std::vector<ModalTemplate> modals = extractModals(panelHtml);
    std::string activeModalId = openModalId;
    if (activeModalId.empty()) {
        const auto autoModal = std::find_if(modals.begin(), modals.end(), [](const ModalTemplate& modal) {
            return modal.autoOpen;
        });
        if (autoModal != modals.end()) {
            activeModalId = autoModal->id;
        }
    }

    const RmlPanelMode panelMode = panelModeForHtml(panelHtml);
    const bool phaseBoard = panelUsesPhaseBoard(panelMode);
    const bool miningFullscreen = panelUsesMiningFullscreen(panelMode);
    const bool arrivalFanfare = panelUsesArrivalFanfare(panelMode);
    const bool surfaceOps = panelUsesSurfaceOps(panelHtml);
    const bool droneOps = panelUsesDroneOps(panelHtml);
    const bool navigation = panelUsesNavigation(panelHtml);
    const bool draftRoom = panelUsesDraftRoom(panelHtml);
    std::string body = syncSettingsControls(sanitizeRml(removeTemplates(panelHtml)));

    std::string document = "<rml><head><style>" + panelRcss(panelMode) + "</style></head><body>";
    document += "<div id=\"rr-panel\" class=\"" +
        std::string(miningFullscreen
                ? "mining-fullscreen-panel"
                : (arrivalFanfare ? "arrival-fanfare-panel-mode" : (phaseBoard ? "phase-board-panel" : "control-panel")));
    if (surfaceOps) {
        document += " surface-ops-panel";
    }
    if (droneOps) {
        document += " drone-ops-panel";
    }
    if (navigation) {
        document += " navigation-panel";
    }
    if (draftRoom) {
        document += " draft-room-panel";
    }
    document += "\">";
    document += body;
    document += "</div>";

    if (const ModalTemplate* modal = findModal(modals, activeModalId)) {
        document += "<div id=\"rr-modal-scrim\"></div>";
        document += "<div id=\"rr-modal\" class=\"modal-" + activeModalId + "\"><div class=\"modal-head\"><h2>";
        document += modal->title;
        document += "</h2><button class=\"ghost\" data-ui-close-modal=\"1\">Close</button></div>";
        document += syncSettingsControls(sanitizeRml(modal->body));
        document += "</div>";
    }

    document += "</body></rml>";
    return document;
}

RmlSystemInterface g_systemInterface;
std::unique_ptr<RocketRmlRenderInterface> g_renderInterface;
Rml::Context* g_context = nullptr;
Rml::ElementDocument* g_document = nullptr;
std::vector<ElementButtonBinding> g_elementButtonBindings;

class RmlSettingsEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override
    {
        Rml::Element* target = event.GetTargetElement();
        if (auto* control = dynamic_cast<Rml::ElementFormControl*>(target)) {
            if (control->GetTagName() == "select" && control->HasAttribute("data-game-speed-select")) {
                rr_rml_set_game_speed_multiplier(control->GetValue().c_str());
                return;
            }
        }

        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(target)) {
            if (input->HasAttribute("data-camera-shake-toggle")) {
                rr_rml_set_camera_shake_disabled(input->HasAttribute("checked") ? 0 : 1);
                return;
            }
            if (input->HasAttribute("data-debug-tools-toggle")) {
                rr_rml_set_debug_tools_enabled(input->HasAttribute("checked") ? 1 : 0);
                return;
            }
        }
    }
};

RmlSettingsEventListener g_settingsEventListener;

bool dispatchButtonBinding(GameRmlUi& owner, const RmlButtonBinding& binding)
{
    if (binding.close) {
        owner.closeModal();
        return true;
    }
    if (!binding.helpDismiss.empty()) {
        owner.dismissHelp(binding.helpDismiss);
        return true;
    }
    if (!binding.modal.empty()) {
        owner.openModal(binding.modal);
        return true;
    }
    if (!binding.action.empty()) {
        owner.dispatchAction(binding.action);
        return true;
    }
    if (binding.helpToggle) {
        rr_rml_set_help_disabled(rr_rml_help_disabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    if (binding.cameraShakeToggle) {
        rr_rml_set_camera_shake_disabled(rr_rml_camera_shake_disabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    if (binding.debugToolsToggle) {
        rr_rml_set_debug_tools_enabled(rr_rml_debug_tools_enabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    return false;
}

void collectButtonElements(Rml::Element* element, std::vector<Rml::Element*>& buttons)
{
    if (!element) {
        return;
    }
    if (element->GetTagName() == "button") {
        buttons.push_back(element);
    }
    const int children = element->GetNumChildren(true);
    for (int index = 0; index < children; ++index) {
        collectButtonElements(element->GetChild(index), buttons);
    }
}

void bindLoadedButtons(const std::string& documentRml)
{
    g_elementButtonBindings.clear();
    if (!g_document) {
        return;
    }

    std::vector<Rml::Element*> elements;
    collectButtonElements(g_document, elements);
    const std::vector<RmlButtonBinding> bindings = extractButtonBindings(documentRml);
    const std::size_t count = std::min(elements.size(), bindings.size());
    g_elementButtonBindings.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        g_elementButtonBindings.push_back({elements[index], bindings[index]});
    }
}

bool activateButtonElement(GameRmlUi& owner, Rml::Element* target)
{
    if (!target) {
        return false;
    }

    Rml::Element* button = target->GetTagName() == "button" ? target : target->Closest("button");
    if (!button) {
        return false;
    }

    const auto bound = std::find_if(g_elementButtonBindings.begin(), g_elementButtonBindings.end(), [&](const ElementButtonBinding& entry) {
        return entry.element == button;
    });
    if (bound != g_elementButtonBindings.end() && dispatchButtonBinding(owner, bound->binding)) {
        return true;
    }

    const Rml::String classNames = button->GetClassNames();
    const Rml::String id = button->GetId();
    const Rml::String closeModal = button->GetAttribute<Rml::String>("rr_close", button->GetAttribute<Rml::String>("data-ui-close-modal", ""));
    if (!closeModal.empty() || button->IsClassSet("rr_close") || startsWith(id, "rr_close")) {
        owner.closeModal();
        return true;
    }

    const Rml::String helpDismiss = button->GetAttribute<Rml::String>("rr_help_dismiss", button->GetAttribute<Rml::String>("data-help-dismiss", ""));
    if (!helpDismiss.empty()) {
        owner.dismissHelp(helpDismiss);
        return true;
    }

    std::string modalId = idTokenValue(id, "rr_modal_");
    if (modalId.empty()) {
        modalId = classTokenValue(classNames, "rr_modal_");
    }
    if (modalId.empty()) {
        modalId = button->GetAttribute<Rml::String>("rr_modal", button->GetAttribute<Rml::String>("data-ui-modal", ""));
    }
    if (!modalId.empty()) {
        owner.openModal(modalId);
        return true;
    }

    std::string action = idTokenValue(id, "rr_action_");
    if (action.empty()) {
        action = classTokenValue(classNames, "rr_action_");
    }
    if (action.empty()) {
        action = button->GetAttribute<Rml::String>("rr_action", button->GetAttribute<Rml::String>("data-rr-action", ""));
    }
    if (!action.empty()) {
        owner.dispatchAction(action);
        return true;
    }

    std::string label;
    std::vector<Rml::Element*> stack {button};
    while (!stack.empty()) {
        Rml::Element* element = stack.back();
        stack.pop_back();
        if (auto* text = dynamic_cast<Rml::ElementText*>(element)) {
            label += text->GetText();
            label.push_back(' ');
            continue;
        }
        for (int index = element->GetNumChildren(true) - 1; index >= 0; --index) {
            stack.push_back(element->GetChild(index));
        }
    }
    if (label.empty()) {
        label = textFromMarkup(button->GetInnerRML());
    }
    return owner.activateButtonLabel(label);
}

Rml::Element* buttonElementAtPoint(Rml::Context& context, const Rml::Vector2f& point)
{
    Rml::Element* element = context.GetElementAtPoint(point);
    if (element && element->GetTagName() == "sliderbar") {
        element = context.GetElementAtPoint(point, element);
    }
    return element && element->GetTagName() == "button" ? element : (element ? element->Closest("button") : nullptr);
}

} // namespace

bool GameRmlUi::initialize(ActionHandler actionHandler)
{
    actionHandler_ = std::move(actionHandler);

    Rml::SetSystemInterface(&g_systemInterface);

    g_renderInterface = std::make_unique<RocketRmlRenderInterface>();
    if (!g_renderInterface->initialize()) {
        return false;
    }
    if (!*g_renderInterface) {
        return false;
    }
    Rml::SetRenderInterface(g_renderInterface.get());

    if (!Rml::Initialise()) {
        return false;
    }

    const auto normal = Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code), sizeof(courier_prime_code));
    const auto italic = Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code_italic), sizeof(courier_prime_code_italic));
    Rml::LoadFontFace(normal, "rmlui-debugger-font", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
    Rml::LoadFontFace(italic, "rmlui-debugger-font", Rml::Style::FontStyle::Italic, Rml::Style::FontWeight::Normal);

    g_context = Rml::CreateContext("rocket-ui", {rr_rml_drawing_width(), rr_rml_drawing_height()});
    if (!g_context) {
        return false;
    }
    g_context->SetDensityIndependentPixelRatio(static_cast<float>(rr_rml_density_ratio()));

    rr_rml_set_enabled(1);
    initialized_ = true;
    return true;
}

void GameRmlUi::setPanelHtml(const std::string& html)
{
    if (panelHtml_ == html) {
        return;
    }
    panelHtml_ = html;
    panelMode_ = panelModeForHtml(panelHtml_);
    buttonBindings_ = extractButtonBindings(panelHtml_);
    rebuildDocument();
}

void GameRmlUi::render()
{
    if (!initialized_ || !g_context || !g_renderInterface) {
        return;
    }

    const int width = rr_rml_drawing_width();
    const int height = rr_rml_drawing_height();
    g_context->SetDimensions({width, height});
    g_context->SetDensityIndependentPixelRatio(static_cast<float>(rr_rml_density_ratio()));
    g_renderInterface->setViewport(width, height);
    if (!openModalId_.empty()) {
        g_renderInterface->setRootClip(Rml::Rectanglei::FromSize({width, height}));
    } else {
        g_renderInterface->setRootClip(expandedPanelClip(panelMode_));
    }
    g_context->Update();
    g_renderInterface->beginFrame();
    g_context->Render();
    g_renderInterface->endFrame();
}

bool GameRmlUi::mouseMove(int x, int y)
{
    if (!initialized_ || !g_context) {
        return false;
    }
    const double ratio = rr_rml_density_ratio();
    g_context->ProcessMouseMove(static_cast<int>(std::round(static_cast<double>(x) * ratio)), static_cast<int>(std::round(static_cast<double>(y) * ratio)), 0);
    return hitTest(x, y);
}

bool GameRmlUi::mouseDown(int x, int y, int button)
{
    if (!initialized_ || !g_context) {
        return false;
    }
    pressedButton_ = nullptr;
    const double ratio = rr_rml_density_ratio();
    const int scaledX = static_cast<int>(std::round(static_cast<double>(x) * ratio));
    const int scaledY = static_cast<int>(std::round(static_cast<double>(y) * ratio));
    g_context->ProcessMouseMove(scaledX, scaledY, 0);
    const bool overUi = hitTest(x, y);
    if (overUi) {
        g_context->ProcessMouseButtonDown(std::max(0, button), 0);
    }
    if (button == 0 && overUi) {
        pressedButton_ = buttonElementAtPoint(*g_context, {static_cast<float>(scaledX), static_cast<float>(scaledY)});
    }
    return overUi;
}

bool GameRmlUi::mouseUp(int x, int y, int button)
{
    if (!initialized_ || !g_context) {
        return false;
    }
    const double ratio = rr_rml_density_ratio();
    const int scaledX = static_cast<int>(std::round(static_cast<double>(x) * ratio));
    const int scaledY = static_cast<int>(std::round(static_cast<double>(y) * ratio));
    g_context->ProcessMouseMove(scaledX, scaledY, 0);
    g_context->ProcessMouseButtonUp(std::max(0, button), 0);
    if (!hitTest(x, y)) {
        pressedButton_ = nullptr;
        return false;
    }
    const Rml::Vector2f point {static_cast<float>(scaledX), static_cast<float>(scaledY)};
    Rml::Element* releasedButton = buttonElementAtPoint(*g_context, point);
    Rml::Element* pressedButton = pressedButton_;
    pressedButton_ = nullptr;
    if (button != 0 || !pressedButton || releasedButton != pressedButton) {
        return true;
    }
    activateButtonElement(*this, pressedButton);
    return true;
}

bool GameRmlUi::mouseWheel(int x, int y, double deltaY)
{
    if (!initialized_ || !g_context || !hitTest(x, y)) {
        return false;
    }
    const double ratio = rr_rml_density_ratio();
    g_context->ProcessMouseMove(static_cast<int>(std::round(static_cast<double>(x) * ratio)), static_cast<int>(std::round(static_cast<double>(y) * ratio)), 0);
    g_context->ProcessMouseWheel(static_cast<float>(-deltaY / 90.0), 0);
    return true;
}

bool GameRmlUi::hitTest(int x, int y) const
{
    if (!initialized_) {
        return false;
    }
    if (!openModalId_.empty()) {
        return true;
    }
    if (panelMode_ == RmlPanelMode::MiningFullscreen && g_context) {
        const double ratio = rr_rml_density_ratio();
        const Rml::Vector2f point {
            static_cast<float>(std::round(static_cast<double>(x) * ratio)),
            static_cast<float>(std::round(static_cast<double>(y) * ratio))
        };
        return buttonElementAtPoint(*g_context, point) != nullptr;
    }
    const Rml::Rectanglei bounds = panelBounds(panelMode_);
    return x >= bounds.Left() && y >= bounds.Top() && x <= bounds.Right() && y <= bounds.Bottom();
}

void GameRmlUi::openModal(const std::string& id)
{
    openModalId_ = id;
    rebuildDocument();
}

void GameRmlUi::closeModal()
{
    openModalId_.clear();
    rebuildDocument();
}

void GameRmlUi::dismissHelp(const std::string& topic)
{
    rr_rml_dismiss_help_topic(topic.c_str());
    rebuildDocument();
}

void GameRmlUi::dispatchAction(const std::string& action)
{
    if (!openModalId_.empty()) {
        openModalId_.clear();
        rebuildDocument();
    }
    if (actionHandler_) {
        actionHandler_(action);
    }
}

void GameRmlUi::refresh()
{
    rebuildDocument();
}

bool GameRmlUi::activateButtonLabel(const std::string& label)
{
    const std::string collapsed = collapsedText(label);
    const auto it = std::find_if(buttonBindings_.begin(), buttonBindings_.end(), [&](const RmlButtonBinding& binding) {
        return binding.label == collapsed;
    });
    if (it == buttonBindings_.end()) {
        return false;
    }

    if (it->close) {
        closeModal();
        return true;
    }
    if (!it->helpDismiss.empty()) {
        dismissHelp(it->helpDismiss);
        return true;
    }
    if (!it->modal.empty()) {
        openModal(it->modal);
        return true;
    }
    if (!it->action.empty()) {
        dispatchAction(it->action);
        return true;
    }
    if (it->helpToggle) {
        rr_rml_set_help_disabled(rr_rml_help_disabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    if (it->cameraShakeToggle) {
        rr_rml_set_camera_shake_disabled(rr_rml_camera_shake_disabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    if (it->debugToolsToggle) {
        rr_rml_set_debug_tools_enabled(rr_rml_debug_tools_enabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    return false;
}

void GameRmlUi::rebuildDocument()
{
    if (!initialized_ || !g_context) {
        return;
    }

    if (g_document) {
        g_context->UnloadDocument(g_document);
        g_document = nullptr;
        g_elementButtonBindings.clear();
    }

    const std::string documentRml = buildDocumentRml(panelHtml_, openModalId_);
    rr_rml_set_modal_open(openModalId_.empty() ? 0 : 1);
    g_document = g_context->LoadDocumentFromMemory(documentRml, "rocket://panel.rml");
    if (g_document) {
        bindLoadedButtons(documentRml);
        g_document->AddEventListener(Rml::EventId::Change, &g_settingsEventListener);
        g_document->Show();
    } else {
        g_elementButtonBindings.clear();
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load Rocket Rogue RmlUi document.");
    }
}

} // namespace rocket

#else

namespace rocket {

bool GameRmlUi::initialize(ActionHandler actionHandler)
{
    actionHandler_ = std::move(actionHandler);
    initialized_ = false;
    return false;
}

void GameRmlUi::setPanelHtml(const std::string& html)
{
    panelHtml_ = html;
}

void GameRmlUi::render() {}
bool GameRmlUi::mouseMove(int, int) { return false; }
bool GameRmlUi::mouseDown(int, int, int) { return false; }
bool GameRmlUi::mouseUp(int, int, int) { return false; }
bool GameRmlUi::mouseWheel(int, int, double) { return false; }
bool GameRmlUi::hitTest(int, int) const { return false; }
void GameRmlUi::openModal(const std::string&) {}
void GameRmlUi::closeModal() {}
void GameRmlUi::dispatchAction(const std::string&) {}
void GameRmlUi::rebuildDocument() {}

} // namespace rocket

#endif

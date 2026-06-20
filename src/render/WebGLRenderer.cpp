#include "render/WebGLRenderer.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>

namespace rocket {

namespace {

constexpr float kPi = 3.1415926535F;
constexpr float kSceneViewportPadding = 0.92F;

enum ArtAsset {
    EarthAsset = 0,
    MoonAsset = 1,
    MarsAsset = 2,
    RocketAsset = 3,
    ExplosionAsset = 4,
    ThrustAsset = 5
};

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct RouteCurve {
    Vec2 a;
    Vec2 b;
    Vec2 c;
};

RouteCurve routeCurve(const RenderSnapshot& snapshot)
{
    if (snapshot.destinationTier == 0 && !snapshot.frontierTransfer) {
        return {{-0.16F, -0.72F}, {-0.12F, -0.20F}, {0.30F, 0.10F}};
    }
    if (snapshot.destinationTier == 1) {
        return {{-0.18F, -0.70F}, {0.10F, 0.08F}, {0.72F, 0.54F}};
    }
    return {{-0.24F, -0.62F}, {0.16F, 0.18F}, {0.76F, 0.56F}};
}

Vec2 normalize(Vec2 vector)
{
    const float length = std::sqrt(vector.x * vector.x + vector.y * vector.y);
    if (length <= 0.0001F) {
        return {0.0F, 1.0F};
    }
    return {vector.x / length, vector.y / length};
}

Vec2 routeDerivative(const RouteCurve& curve, float progress)
{
    const float t = std::clamp(progress, 0.0F, 1.0F);
    return {
        2.0F * (1.0F - t) * (curve.b.x - curve.a.x) + 2.0F * t * (curve.c.x - curve.b.x),
        2.0F * (1.0F - t) * (curve.b.y - curve.a.y) + 2.0F * t * (curve.c.y - curve.b.y)
    };
}

Vec2 routePoint(const RenderSnapshot& snapshot, float progress)
{
    const float rawT = progress;
    const float t = std::clamp(rawT, 0.0F, 1.0F);
    const float inv = 1.0F - t;
    const RouteCurve curve = routeCurve(snapshot);
    Vec2 point {
        inv * inv * curve.a.x + 2.0F * inv * t * curve.b.x + t * t * curve.c.x,
        inv * inv * curve.a.y + 2.0F * inv * t * curve.b.y + t * t * curve.c.y
    };

    if (rawT > 1.0F) {
        const Vec2 tangent = normalize(routeDerivative(curve, 1.0F));
        const float overburn = rawT - 1.0F;
        point.x += tangent.x * overburn * 0.44F;
        point.y += tangent.y * overburn * 0.44F;
    }

    return point;
}

Vec2 routeTangent(const RenderSnapshot& snapshot, float progress)
{
    const RouteCurve curve = routeCurve(snapshot);
    return normalize(routeDerivative(curve, progress));
}

#ifdef __EMSCRIPTEN__
EM_JS(void, rr_request_image, (const char* keyPtr, const char* pathPtr), {
    const key = UTF8ToString(keyPtr);
    const path = UTF8ToString(pathPtr);
    Module.RocketArt = Module.RocketArt || {};
    if (Module.RocketArt[key]) {
        return;
    }

    const image = new Image();
    const record = { image, ready: false, failed: false, width: 0, height: 0 };
    image.onload = () => {
        record.ready = true;
        record.width = image.naturalWidth || image.width;
        record.height = image.naturalHeight || image.height;
    };
    image.onerror = () => {
        record.failed = true;
    };
    image.src = path;
    Module.RocketArt[key] = record;
});

EM_JS(int, rr_image_ready, (const char* keyPtr), {
    const key = UTF8ToString(keyPtr);
    const record = Module.RocketArt && Module.RocketArt[key];
    return record && record.ready ? 1 : 0;
});

EM_JS(int, rr_upload_image_texture, (const char* keyPtr, int textureId), {
    const key = UTF8ToString(keyPtr);
    const record = Module.RocketArt && Module.RocketArt[key];
    if (!record || !record.ready || !GLctx || !GL || !GL.textures[textureId]) {
        return 0;
    }

    const gl = GLctx;
    gl.bindTexture(gl.TEXTURE_2D, GL.textures[textureId]);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, record.image);
    return 1;
});

EM_JS(int, rr_image_width, (const char* keyPtr), {
    const key = UTF8ToString(keyPtr);
    const record = Module.RocketArt && Module.RocketArt[key];
    return record && record.ready ? record.width : 0;
});

EM_JS(int, rr_image_height, (const char* keyPtr), {
    const key = UTF8ToString(keyPtr);
    const record = Module.RocketArt && Module.RocketArt[key];
    return record && record.ready ? record.height : 0;
});

EM_JS(double, rr_device_pixel_ratio, (), {
    const ratio = globalThis.devicePixelRatio || 1;
    return Math.max(1, Math.min(2, ratio));
});

EM_JS(void, rr_sync_canvas_to_visual_viewport, (), {
    const canvas = document.getElementById("canvas");
    if (!canvas) {
        return;
    }

    const viewport = globalThis.visualViewport;
    const width = Math.max(1, Math.round((viewport && viewport.width) || globalThis.innerWidth || canvas.clientWidth || 1));
    const height = Math.max(1, Math.round((viewport && viewport.height) || globalThis.innerHeight || canvas.clientHeight || 1));
    canvas.style.width = width + "px";
    canvas.style.height = height + "px";
});

EM_JS(double, rr_scene_left_ndc, (), {
    const canvas = document.getElementById("canvas");
    const panel = document.getElementById("panel");
    const width = (canvas && canvas.clientWidth) || globalThis.innerWidth || 1;
    if (!panel || width <= 720) {
        return -1;
    }

    const rect = panel.getBoundingClientRect();
    const gutter = 24;
    const leftPx = Math.max(0, rect.right + gutter);
    if (width - leftPx < 520) {
        return -1;
    }

    return Math.max(-1, Math.min(0.45, leftPx / width * 2 - 1));
});

GLuint compileShader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

GLuint createProgram()
{
    constexpr const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
out vec4 v_color;
out vec2 v_uv;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
    v_uv = a_uv;
}
)";

    constexpr const char* fragmentSource = R"(#version 300 es
precision mediump float;
in vec4 v_color;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_useTexture;
out vec4 out_color;
void main()
{
    vec4 sprite = texture(u_texture, v_uv) * v_color;
    out_color = mix(v_color, sprite, u_useTexture);
}
)";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}
#endif

void pushVertex(std::vector<float>& vertices, float x, float y, Color color, float u = 0.0F, float v = 0.0F)
{
    vertices.push_back(x);
    vertices.push_back(y);
    vertices.push_back(color.r);
    vertices.push_back(color.g);
    vertices.push_back(color.b);
    vertices.push_back(color.a);
    vertices.push_back(u);
    vertices.push_back(v);
}

Color mix(Color a, Color b, float t)
{
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    return {
        a.r + (b.r - a.r) * clamped,
        a.g + (b.g - a.g) * clamped,
        a.b + (b.b - a.b) * clamped,
        a.a + (b.a - a.a) * clamped
    };
}

} // namespace

bool WebGLRenderer::initialize()
{
    assets_[EarthAsset] = {"earth", "assets/art/earth.png"};
    assets_[MoonAsset] = {"moon", "assets/art/moon.png"};
    assets_[MarsAsset] = {"mars", "assets/art/mars.png"};
    assets_[RocketAsset] = {"rocket", "assets/art/rocket.png"};
    assets_[ExplosionAsset] = {"explosion", "assets/art/explosion-sheet.png"};
    assets_[ThrustAsset] = {"thrust", "assets/art/thrust-sheet.png"};

#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.alpha = EM_FALSE;
    attributes.depth = EM_FALSE;
    attributes.stencil = EM_FALSE;
    attributes.antialias = EM_TRUE;

    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context("#canvas", &attributes);
    if (context <= 0) {
        return false;
    }
    emscripten_webgl_make_context_current(context);

    program_ = createProgram();
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 8, reinterpret_cast<void*>(sizeof(float) * 2));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8, reinterpret_cast<void*>(sizeof(float) * 6));
    glUseProgram(program_);
    useTextureUniform_ = glGetUniformLocation(program_, "u_useTexture");
    samplerUniform_ = glGetUniformLocation(program_, "u_texture");
    glUniform1i(samplerUniform_, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::array<GLuint, 6> textureIds {};
    glGenTextures(static_cast<GLsizei>(textureIds.size()), textureIds.data());
    for (std::size_t i = 0; i < assets_.size(); ++i) {
        assets_[i].texture = textureIds[i];
    }

    for (auto& asset : assets_) {
        rr_request_image(asset.key, asset.path);
        asset.requested = true;
    }

    initialized_ = true;
    return true;
#else
    initialized_ = true;
    return true;
#endif
}

void WebGLRenderer::render(const RenderSnapshot& snapshot)
{
    if (!initialized_) {
        return;
    }

    beginFrame(snapshot);
    warmTextures();
    drawBackdrop(snapshot);
    drawRocket(snapshot);
    drawTelemetry(snapshot);
}

void WebGLRenderer::beginFrame(const RenderSnapshot& snapshot)
{
#ifdef __EMSCRIPTEN__
    rr_sync_canvas_to_visual_viewport();

    double cssWidth = 1280.0;
    double cssHeight = 720.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    const double pixelRatio = rr_device_pixel_ratio();
    const int drawingWidth = static_cast<int>(std::ceil(cssWidth * pixelRatio));
    const int drawingHeight = static_cast<int>(std::ceil(cssHeight * pixelRatio));
    emscripten_set_canvas_element_size("#canvas", drawingWidth, drawingHeight);
    glViewport(0, 0, drawingWidth, drawingHeight);

    sceneCssWidth_ = std::max(1.0F, static_cast<float>(cssWidth));
    sceneCssHeight_ = std::max(1.0F, static_cast<float>(cssHeight));
    const float sceneLeftNdc = static_cast<float>(rr_scene_left_ndc());
    scenePixelLeft_ = (sceneLeftNdc + 1.0F) * 0.5F * sceneCssWidth_;
    scenePixelRight_ = sceneCssWidth_;
    const float sceneWidthPixels = std::max(1.0F, scenePixelRight_ - scenePixelLeft_);
    const float sceneHeightPixels = sceneCssHeight_;
    scenePixelCenterX_ = scenePixelLeft_ + sceneWidthPixels * 0.5F;
    scenePixelCenterY_ = sceneCssHeight_ * 0.5F;
    sceneWorldUnit_ = std::max(1.0F, std::min(sceneWidthPixels, sceneHeightPixels) * 0.5F * kSceneViewportPadding);
    sceneAspect_ = std::max(0.10F, sceneWidthPixels / sceneHeightPixels);

    const float heat = static_cast<float>(std::clamp(snapshot.heat, 0.0, 1.0));
    glClearColor(0.02F + heat * 0.05F, 0.03F, 0.05F + heat * 0.02F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
#else
    (void)snapshot;
#endif
}

void WebGLRenderer::drawRect(float cx, float cy, float w, float h, Color color, bool worldSpace)
{
    std::vector<float>& vertices = scratchVertices(48);
    appendRect(vertices, cx, cy, w, h, color);
    submit(vertices, 0x0004, false, 0, worldSpace);
}

void WebGLRenderer::drawLine(float ax, float ay, float bx, float by, Color color, float width, bool worldSpace)
{
    std::vector<float>& vertices = scratchVertices(16);
    appendLine(vertices, ax, ay, bx, by, color);
    submitLines(vertices, width, worldSpace);
}

void WebGLRenderer::drawTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color color, bool worldSpace)
{
    std::vector<float>& vertices = scratchVertices(24);
    pushVertex(vertices, ax, ay, color);
    pushVertex(vertices, bx, by, color);
    pushVertex(vertices, cx, cy, color);
    submit(vertices, 0x0004, false, 0, worldSpace);
}

void WebGLRenderer::drawCircle(float cx, float cy, float radius, Color color, int segments, bool worldSpace)
{
    std::vector<float>& vertices = scratchVertices(static_cast<std::size_t>(segments) * 24);
    for (int i = 0; i < segments; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * kPi * 2.0F;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * kPi * 2.0F;
        pushVertex(vertices, cx, cy, color);
        pushVertex(vertices, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, color);
        pushVertex(vertices, cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
    submit(vertices, 0x0004, false, 0, worldSpace);
}

std::vector<float>& WebGLRenderer::scratchVertices(std::size_t reserveCount)
{
    vertices_.clear();
    if (vertices_.capacity() < reserveCount) {
        vertices_.reserve(reserveCount);
    }
    return vertices_;
}

void WebGLRenderer::appendRect(std::vector<float>& vertices, float cx, float cy, float w, float h, Color color)
{
    const float left = cx - w * 0.5F;
    const float right = cx + w * 0.5F;
    const float top = cy + h * 0.5F;
    const float bottom = cy - h * 0.5F;
    pushVertex(vertices, left, bottom, color);
    pushVertex(vertices, right, bottom, color);
    pushVertex(vertices, right, top, color);
    pushVertex(vertices, left, bottom, color);
    pushVertex(vertices, right, top, color);
    pushVertex(vertices, left, top, color);
}

void WebGLRenderer::appendLine(std::vector<float>& vertices, float ax, float ay, float bx, float by, Color color)
{
    pushVertex(vertices, ax, ay, color);
    pushVertex(vertices, bx, by, color);
}

bool WebGLRenderer::textureReady(int assetIndex)
{
    if (assetIndex < 0 || assetIndex >= static_cast<int>(assets_.size())) {
        return false;
    }

    TextureAsset& asset = assets_[static_cast<std::size_t>(assetIndex)];
    if (asset.ready) {
        return true;
    }

#ifdef __EMSCRIPTEN__
    if (!asset.requested) {
        rr_request_image(asset.key, asset.path);
        asset.requested = true;
    }

    if (rr_image_ready(asset.key) != 0 && rr_upload_image_texture(asset.key, static_cast<int>(asset.texture)) != 0) {
        asset.width = rr_image_width(asset.key);
        asset.height = rr_image_height(asset.key);
        asset.ready = asset.width > 0 && asset.height > 0;
    }
#endif

    return asset.ready;
}

void WebGLRenderer::warmTextures()
{
    for (int i = 0; i < static_cast<int>(assets_.size()); ++i) {
        (void)textureReady(i);
    }
}

void WebGLRenderer::drawSprite(float cx, float cy, float w, float h, Color tint, int assetIndex, int frameIndex, int frameCount, bool worldSpace)
{
    if (!textureReady(assetIndex)) {
        return;
    }

    TextureAsset& asset = assets_[static_cast<std::size_t>(assetIndex)];
    const int frames = std::max(1, frameCount);
    const int frame = std::clamp(frameIndex, 0, frames - 1);
    const float u0 = static_cast<float>(frame) / static_cast<float>(frames);
    const float u1 = static_cast<float>(frame + 1) / static_cast<float>(frames);
    const float v0 = 0.0F;
    const float v1 = 1.0F;
    const float left = cx - w * 0.5F;
    const float right = cx + w * 0.5F;
    const float top = cy + h * 0.5F;
    const float bottom = cy - h * 0.5F;

    std::vector<float>& vertices = scratchVertices(48);
    pushVertex(vertices, left, bottom, tint, u0, v1);
    pushVertex(vertices, right, bottom, tint, u1, v1);
    pushVertex(vertices, right, top, tint, u1, v0);
    pushVertex(vertices, left, bottom, tint, u0, v1);
    pushVertex(vertices, right, top, tint, u1, v0);
    pushVertex(vertices, left, top, tint, u0, v0);
    submit(vertices, 0x0004, true, asset.texture, worldSpace);
}

void WebGLRenderer::drawTelemetry(const RenderSnapshot& snapshot)
{
    const float left = 0.22F;
    const float right = 0.92F;
    const float bottom = -0.86F;
    const float top = -0.60F;

    drawRect((left + right) * 0.5F, (bottom + top) * 0.5F, right - left, top - bottom, {0.02F, 0.05F, 0.07F, 0.72F}, false);
    drawLine(left, bottom, right, bottom, {0.36F, 0.55F, 0.68F, 0.55F}, 1.0F, false);
    drawLine(left, bottom, left, top, {0.36F, 0.55F, 0.68F, 0.55F}, 1.0F, false);

    if (snapshot.telemetryCount <= 1) {
        return;
    }

    Color warningSafe {0.35F, 0.84F, 1.0F, 1.0F};
    Color warningHot {1.0F, 0.38F, 0.28F, 1.0F};
    Color heatColor {1.0F, 0.78F, 0.25F, 0.90F};

    drawLine(left, bottom + (top - bottom) * 0.70F, right, bottom + (top - bottom) * 0.70F, {1.0F, 0.80F, 0.30F, 0.22F}, 1.0F, false);

    std::vector<float>& heatVertices = scratchVertices(static_cast<std::size_t>(snapshot.telemetryCount - 1) * 16);
    for (int i = 1; i < snapshot.telemetryCount; ++i) {
        const float t0 = static_cast<float>(i - 1) / static_cast<float>(snapshot.telemetryCount - 1);
        const float t1 = static_cast<float>(i) / static_cast<float>(snapshot.telemetryCount - 1);
        const float h0 = bottom + static_cast<float>(snapshot.heatTelemetry[static_cast<std::size_t>(i - 1)]) * (top - bottom);
        const float h1 = bottom + static_cast<float>(snapshot.heatTelemetry[static_cast<std::size_t>(i)]) * (top - bottom);
        const float x0 = left + t0 * (right - left);
        const float x1 = left + t1 * (right - left);
        appendLine(heatVertices, x0, h0, x1, h1, heatColor);
    }
    submitLines(heatVertices, 1.5F, false);

    std::vector<float>& warningVertices = scratchVertices(static_cast<std::size_t>(snapshot.telemetryCount - 1) * 16);
    const Color warningColor = mix(warningSafe, warningHot, static_cast<float>(snapshot.warning));
    for (int i = 1; i < snapshot.telemetryCount; ++i) {
        const float t0 = static_cast<float>(i - 1) / static_cast<float>(snapshot.telemetryCount - 1);
        const float t1 = static_cast<float>(i) / static_cast<float>(snapshot.telemetryCount - 1);
        const float y0 = bottom + static_cast<float>(snapshot.telemetry[static_cast<std::size_t>(i - 1)]) * (top - bottom);
        const float y1 = bottom + static_cast<float>(snapshot.telemetry[static_cast<std::size_t>(i)]) * (top - bottom);
        const float x0 = left + t0 * (right - left);
        const float x1 = left + t1 * (right - left);
        appendLine(warningVertices, x0, y0, x1, y1, warningColor);
    }
    submitLines(warningVertices, 2.2F, false);
}

void WebGLRenderer::drawStars()
{
    std::vector<float>& vertices = scratchVertices(52 * 48);
    for (int i = 0; i < 52; ++i) {
        const float x = -0.95F + static_cast<float>((i * 37) % 190) / 95.0F;
        const float y = -0.92F + static_cast<float>((i * 71) % 184) / 92.0F;
        const float alpha = 0.18F + static_cast<float>((i * 19) % 60) / 100.0F;
        appendRect(vertices, x, y, 0.004F, 0.004F, {0.75F, 0.88F, 1.0F, alpha});
    }
    submit(vertices, 0x0004);
}

void WebGLRenderer::drawRoute(const RenderSnapshot& snapshot)
{
    std::vector<float>& routeVertices = scratchVertices(28 * 16);
    Vec2 previous = routePoint(snapshot, 0.0F);
    for (int i = 1; i <= 28; ++i) {
        const float t = static_cast<float>(i) / 28.0F;
        const Vec2 next = routePoint(snapshot, t);
        const Color routeColor = t <= snapshot.travelProgress ? Color{0.42F, 0.88F, 1.0F, 0.42F} : Color{0.25F, 0.42F, 0.52F, 0.22F};
        appendLine(routeVertices, previous.x, previous.y, next.x, next.y, routeColor);
        previous = next;
    }
    submitLines(routeVertices, 1.0F);

    if (snapshot.travelProgress <= 1.0) {
        return;
    }

    std::vector<float>& overburnVertices = scratchVertices(8 * 16);
    Vec2 overburnPrevious = routePoint(snapshot, 1.0F);
    for (int i = 1; i <= 8; ++i) {
        const float t = 1.0F + (static_cast<float>(snapshot.travelProgress) - 1.0F) * (static_cast<float>(i) / 8.0F);
        const Vec2 overburnNext = routePoint(snapshot, t);
        appendLine(overburnVertices, overburnPrevious.x, overburnPrevious.y, overburnNext.x, overburnNext.y, {0.90F, 0.50F, 0.28F, 0.44F});
        overburnPrevious = overburnNext;
    }
    submitLines(overburnVertices, 1.0F);
}

void WebGLRenderer::drawRocket(const RenderSnapshot& snapshot)
{
    const Vec2 route = routePoint(snapshot, static_cast<float>(snapshot.travelProgress));
    Vec2 forward = routeTangent(snapshot, static_cast<float>(snapshot.travelProgress));
    if (snapshot.returningHome) {
        const float turn = static_cast<float>(std::clamp(snapshot.returnTurnProgress, 0.0, 1.0));
        const float outboundAngle = std::atan2(forward.y, forward.x);
        const float returnAngle = outboundAngle + kPi * turn;
        forward = {std::cos(returnAngle), std::sin(returnAngle)};
    }
    const Vec2 right {forward.y, -forward.x};
    const float hangarLift = snapshot.screen == Screen::Hangar ? 0.02F : 0.0F;
    const float cx = route.x;
    const float cy = route.y + hangarLift;
    const float scale = std::clamp(0.26F - static_cast<float>(snapshot.travelProgress) * 0.06F, 0.16F, 0.26F);
    const float damage = static_cast<float>(std::clamp(snapshot.shipDamage / 100.0, 0.0, 1.0));
    const Color body = mix({0.86F, 0.93F, 0.96F, 1.0F}, {0.45F, 0.46F, 0.48F, 1.0F}, damage);
    const Color accent = mix({0.30F, 0.80F, 1.0F, 1.0F}, {1.0F, 0.42F, 0.30F, 1.0F}, static_cast<float>(snapshot.warning));

    auto world = [&](float localX, float localY) {
        return Vec2 {
            cx + right.x * localX * scale + forward.x * localY * scale,
            cy + right.y * localX * scale + forward.y * localY * scale
        };
    };

    auto triangle = [&](float ax, float ay, float bx, float by, float tx, float ty, Color color) {
        const Vec2 a = world(ax, ay);
        const Vec2 b = world(bx, by);
        const Vec2 t = world(tx, ty);
        drawTriangle(a.x, a.y, b.x, b.y, t.x, t.y, color);
    };

    auto quad = [&](float centerX, float centerY, float width, float height, Color color) {
        const float halfW = width * 0.5F;
        const float halfH = height * 0.5F;
        const Vec2 bl = world(centerX - halfW, centerY - halfH);
        const Vec2 br = world(centerX + halfW, centerY - halfH);
        const Vec2 tr = world(centerX + halfW, centerY + halfH);
        const Vec2 tl = world(centerX - halfW, centerY + halfH);
        drawTriangle(bl.x, bl.y, br.x, br.y, tr.x, tr.y, color);
        drawTriangle(bl.x, bl.y, tr.x, tr.y, tl.x, tl.y, color);
    };

    auto texturedQuad = [&](int assetIndex, float width, float height, Color tint, int frameIndex = 0, int frameCount = 1, float offsetRight = 0.0F, float offsetForward = 0.0F) {
        if (!textureReady(assetIndex)) {
            return false;
        }

        TextureAsset& asset = assets_[static_cast<std::size_t>(assetIndex)];
        const int frames = std::max(1, frameCount);
        const int frame = std::clamp(frameIndex, 0, frames - 1);
        const float u0 = static_cast<float>(frame) / static_cast<float>(frames);
        const float u1 = static_cast<float>(frame + 1) / static_cast<float>(frames);
        const float halfW = width * 0.5F;
        const float halfH = height * 0.5F;
        const float centerX = cx + right.x * offsetRight + forward.x * offsetForward;
        const float centerY = cy + right.y * offsetRight + forward.y * offsetForward;
        const Vec2 bl {centerX - right.x * halfW - forward.x * halfH, centerY - right.y * halfW - forward.y * halfH};
        const Vec2 br {centerX + right.x * halfW - forward.x * halfH, centerY + right.y * halfW - forward.y * halfH};
        const Vec2 tr {centerX + right.x * halfW + forward.x * halfH, centerY + right.y * halfW + forward.y * halfH};
        const Vec2 tl {centerX - right.x * halfW + forward.x * halfH, centerY - right.y * halfW + forward.y * halfH};

        std::vector<float>& vertices = scratchVertices(48);
        pushVertex(vertices, bl.x, bl.y, tint, u0, 1.0F);
        pushVertex(vertices, br.x, br.y, tint, u1, 1.0F);
        pushVertex(vertices, tr.x, tr.y, tint, u1, 0.0F);
        pushVertex(vertices, bl.x, bl.y, tint, u0, 1.0F);
        pushVertex(vertices, tr.x, tr.y, tint, u1, 0.0F);
        pushVertex(vertices, tl.x, tl.y, tint, u0, 0.0F);
        submit(vertices, 0x0004, true, asset.texture);
        return true;
    };

    if (snapshot.lastResult == LaunchResultType::Destroyed && textureReady(ExplosionAsset)) {
        const int frame = std::clamp(static_cast<int>(snapshot.animationTime * 9.5), 0, 7);
        const float blastSize = std::max(0.22F, 1.55F * scale);
        drawSprite(cx, cy, blastSize, blastSize, {1.0F, 1.0F, 1.0F, 1.0F}, ExplosionAsset, frame, 8);
        return;
    }

    if (textureReady(RocketAsset)) {
        if (snapshot.poweredFlight && textureReady(ThrustAsset)) {
            const int thrustFrame = static_cast<int>(snapshot.animationTime * 18.0) % 6;
            texturedQuad(ThrustAsset, 0.28F * scale, 0.38F * scale, {1.0F, 1.0F, 1.0F, 1.0F}, thrustFrame, 6, 0.01F * scale, -0.22F * scale);
        }
        texturedQuad(RocketAsset, 0.86F * scale, 0.86F * scale, {1.0F, 1.0F, 1.0F, 1.0F});
        return;
    }

    triangle(0.0F, 0.34F, -0.075F, 0.19F, 0.075F, 0.19F, body);
    quad(0.0F, 0.0F, 0.15F, 0.38F, body);
    quad(0.0F, 0.08F, 0.16F, 0.045F, accent);
    triangle(-0.075F, -0.16F, -0.19F, -0.28F, -0.075F, -0.03F, {0.58F, 0.66F, 0.72F, 1.0F});
    triangle(0.075F, -0.16F, 0.19F, -0.28F, 0.075F, -0.03F, {0.58F, 0.66F, 0.72F, 1.0F});

    if (snapshot.screen == Screen::Launch) {
        const float plume = 0.22F + static_cast<float>(snapshot.currentMultiplier) * 0.035F;
        triangle(0.0F, -0.46F - plume * 0.20F, -0.08F, -0.20F, 0.08F, -0.20F, {1.0F, 0.50F, 0.12F, 0.88F});
        triangle(0.0F, -0.36F - plume * 0.15F, -0.045F, -0.20F, 0.045F, -0.20F, {1.0F, 0.88F, 0.42F, 0.92F});
    }

    if (snapshot.lastResult == LaunchResultType::Destroyed) {
        drawCircle(cx, cy + 0.04F * scale, 0.34F * scale, {1.0F, 0.32F, 0.18F, 0.22F}, 48);
        drawCircle(cx + 0.08F * scale, cy + 0.10F * scale, 0.18F * scale, {1.0F, 0.78F, 0.22F, 0.22F}, 32);
    }
}

void WebGLRenderer::drawBackdrop(const RenderSnapshot& snapshot)
{
    drawRect(0.0F, 0.0F, 2.0F, 2.0F, {0.015F, 0.022F, 0.032F, 1.0F}, false);
    drawStars();

    if (snapshot.destinationTier == 0 && !snapshot.frontierTransfer) {
        const float earthX = -0.16F;
        const float earthY = -1.10F;
        const float earthR = 0.58F;
        const Vec2 distantMoon {0.72F, 0.50F};
        if (textureReady(EarthAsset)) {
            drawSprite(earthX, earthY, earthR * 2.25F, earthR * 2.25F, {1.0F, 1.0F, 1.0F, 0.95F}, EarthAsset);
        } else {
            drawCircle(earthX, earthY, earthR * 1.10F, {0.24F, 0.62F, 0.96F, 0.08F}, 72);
            drawCircle(earthX, earthY, earthR, {0.18F, 0.48F, 0.78F, 0.82F}, 72);
            drawCircle(earthX - 0.16F, earthY + 0.18F, earthR * 0.16F, {0.28F, 0.58F, 0.36F, 0.72F}, 24);
            drawCircle(earthX + 0.14F, earthY + 0.28F, earthR * 0.12F, {0.28F, 0.58F, 0.36F, 0.64F}, 20);
        }
        drawEllipseLine(earthX, earthY, earthR * 1.08F, earthR * 0.56F, {0.45F, 0.88F, 1.0F, 0.22F}, 42, 0.13F * kPi, 0.92F * kPi);
        if (textureReady(MoonAsset)) {
            drawSprite(distantMoon.x, distantMoon.y, 0.13F, 0.13F, {1.0F, 1.0F, 1.0F, 0.72F}, MoonAsset);
        } else {
            drawCircle(distantMoon.x, distantMoon.y, 0.036F, {0.72F, 0.74F, 0.72F, 0.58F}, 32);
            drawCircle(distantMoon.x + 0.010F, distantMoon.y + 0.008F, 0.010F, {0.48F, 0.50F, 0.50F, 0.30F}, 12);
        }
    } else if (snapshot.destinationTier == 1) {
        const Vec2 moon = routePoint(snapshot, 1.0F);
        const float earthX = -0.26F;
        const float earthY = -0.88F;
        const float earthR = 0.30F;
        if (textureReady(EarthAsset)) {
            drawSprite(earthX, earthY, earthR * 2.32F, earthR * 2.32F, {1.0F, 1.0F, 1.0F, 0.86F}, EarthAsset);
        } else {
            drawCircle(earthX, earthY, earthR * 1.20F, {0.24F, 0.62F, 0.96F, 0.08F}, 64);
            drawCircle(earthX, earthY, earthR, {0.18F, 0.48F, 0.78F, 0.72F}, 64);
            drawCircle(earthX - 0.08F, earthY + 0.08F, earthR * 0.16F, {0.30F, 0.60F, 0.38F, 0.70F}, 20);
            drawCircle(earthX + 0.10F, earthY + 0.14F, earthR * 0.12F, {0.30F, 0.60F, 0.38F, 0.58F}, 20);
        }
        drawEllipseLine(earthX, earthY, 1.08F, 0.76F, {0.40F, 0.62F, 0.78F, 0.20F}, 96, -0.04F * kPi, 0.82F * kPi);
        if (textureReady(MoonAsset)) {
            drawSprite(moon.x, moon.y, 0.22F, 0.22F, {1.0F, 1.0F, 1.0F, 1.0F}, MoonAsset);
        } else {
            drawCircle(moon.x, moon.y, 0.060F, {0.72F, 0.74F, 0.72F, 0.78F}, 48);
            drawCircle(moon.x + 0.018F, moon.y + 0.015F, 0.018F, {0.48F, 0.50F, 0.50F, 0.36F}, 16);
        }
    } else {
        const float tier = static_cast<float>(snapshot.destinationTier);
        const float radius = 0.065F + tier * 0.010F;
        const Color destination = mix({0.42F, 0.66F, 0.88F, 0.60F}, {0.95F, 0.72F, 0.35F, 0.72F}, tier / 5.0F);
        const Vec2 endpoint = routePoint(snapshot, 1.0F);
        if (snapshot.destinationTier == 2) {
            const float earthX = -0.34F;
            const float earthY = -0.89F;
            const float earthR = 0.16F;
            if (textureReady(EarthAsset)) {
                drawSprite(earthX, earthY, earthR * 2.30F, earthR * 2.30F, {1.0F, 1.0F, 1.0F, 0.80F}, EarthAsset);
            } else {
                drawCircle(earthX, earthY, earthR * 1.18F, {0.24F, 0.62F, 0.96F, 0.07F}, 48);
                drawCircle(earthX, earthY, earthR, {0.18F, 0.48F, 0.78F, 0.62F}, 48);
                drawCircle(earthX - 0.04F, earthY + 0.04F, earthR * 0.16F, {0.30F, 0.60F, 0.38F, 0.58F}, 14);
                drawCircle(earthX + 0.05F, earthY + 0.07F, earthR * 0.12F, {0.30F, 0.60F, 0.38F, 0.48F}, 14);
            }
        }
        if (snapshot.destinationTier == 2 && textureReady(MarsAsset)) {
            drawSprite(endpoint.x, endpoint.y, radius * 2.55F, radius * 2.55F, {1.0F, 1.0F, 1.0F, 0.86F}, MarsAsset);
        } else {
            drawCircle(endpoint.x, endpoint.y, radius, destination, 56);
            drawCircle(endpoint.x, endpoint.y, radius * 1.65F, {destination.r, destination.g, destination.b, 0.09F}, 64);
        }
    }

    drawRoute(snapshot);

    if ((snapshot.destinationTier == 0 && !snapshot.frontierTransfer) || snapshot.destinationTier > 2) {
        const Vec2 targetMarker = routePoint(snapshot, 1.0F);
        drawLine(targetMarker.x, targetMarker.y - 0.055F, targetMarker.x, targetMarker.y + 0.055F, {0.98F, 0.82F, 0.36F, 0.70F}, 2.0F);
    }
}

void WebGLRenderer::drawEllipseLine(float cx, float cy, float rx, float ry, Color color, int segments, float start, float end)
{
    auto& vertices = scratchVertices(static_cast<std::size_t>(segments) * 16U);
    Vec2 previous {cx + std::cos(start) * rx, cy + std::sin(start) * ry};
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = start + (end - start) * t;
        const Vec2 next {cx + std::cos(angle) * rx, cy + std::sin(angle) * ry};
        appendLine(vertices, previous.x, previous.y, next.x, next.y, color);
        previous = next;
    }
    submitLines(vertices, 1.0F);
}

void WebGLRenderer::submit(const std::vector<float>& vertices, int primitive, bool textured, unsigned int texture, bool worldSpace)
{
#ifdef __EMSCRIPTEN__
    if (vertices.empty()) {
        return;
    }

    const std::vector<float>* uploadVertices = &vertices;
    if (worldSpace) {
        projectedVertices_ = vertices;
        const float invCssWidth = 2.0F / std::max(1.0F, sceneCssWidth_);
        const float invCssHeight = 2.0F / std::max(1.0F, sceneCssHeight_);
        for (std::size_t i = 0; i + 1 < projectedVertices_.size(); i += 8) {
            const float pixelX = scenePixelCenterX_ + projectedVertices_[i] * sceneWorldUnit_;
            const float pixelY = scenePixelCenterY_ + projectedVertices_[i + 1] * sceneWorldUnit_;
            projectedVertices_[i] = pixelX * invCssWidth - 1.0F;
            projectedVertices_[i + 1] = pixelY * invCssHeight - 1.0F;
        }
        uploadVertices = &projectedVertices_;
    }

    glUseProgram(program_);
    glUniform1f(useTextureUniform_, textured ? 1.0F : 0.0F);
    if (textured) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(samplerUniform_, 0);
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(uploadVertices->size() * sizeof(float)), uploadVertices->data(), GL_DYNAMIC_DRAW);
    glDrawArrays(static_cast<GLenum>(primitive), 0, static_cast<GLsizei>(uploadVertices->size() / 8));
#else
    (void)vertices;
    (void)primitive;
    (void)textured;
    (void)texture;
    (void)worldSpace;
#endif
}

void WebGLRenderer::submitLines(const std::vector<float>& vertices, float width, bool worldSpace)
{
#ifdef __EMSCRIPTEN__
    glLineWidth(width);
#else
    (void)width;
#endif
    submit(vertices, 0x0001, false, 0, worldSpace);
}

} // namespace rocket

#include "render/WebGLRenderer.h"

#include "core/Tuning.h"

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
constexpr float kMiningLightRadiusCells = 2.15F;
constexpr float kMiningScannerPulseSeconds = 0.9F;

enum ArtAsset {
    EarthAsset = 0,
    MoonAsset = 1,
    MarsAsset = 2,
    RocketAsset = 3,
    ExplosionAsset = 4,
    ThrustAsset = 5,
    MiningDroneAsset = 6,
    DrillBitAsset = 7,
    LocalSolarBgAsset = 8,
    MercuryAsset = 9,
    VenusAsset = 10,
    JupiterAsset = 11,
    SaturnAsset = 12,
    UranusAsset = 13,
    NeptuneAsset = 14
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
    if (snapshot.destinationTier == 2) {
        return {{-0.30F, -0.72F}, {0.12F, 0.16F}, {0.76F, 0.56F}};
    }
    if (snapshot.destinationTier == 3) {
        return {{-0.42F, -0.78F}, {-0.10F, 0.08F}, {0.78F, 0.58F}};
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

int destinationBodyAsset(int destinationTier)
{
    if (destinationTier == 1) {
        return MoonAsset;
    }
    if (destinationTier == 2) {
        return MarsAsset;
    }
    if (destinationTier == 3) {
        return JupiterAsset;
    }
    return -1;
}

float bodySpriteScale(int assetIndex)
{
    if (assetIndex == MoonAsset) {
        return 2.40F;
    }
    if (assetIndex == MarsAsset || assetIndex == JupiterAsset) {
        return 2.55F;
    }
    return 2.40F;
}

Color miningMaterialColor(int material, float integrity, bool revealed, bool hazard, int destinationTier, float light)
{
    Color color {0.025F, 0.035F, 0.040F, 1.0F};
    const bool moon = destinationTier == 1;
    const bool mars = destinationTier == 2;
    if (!revealed) {
        const bool empty = material == 0;
        color = empty
            ? Color {0.010F, 0.015F, 0.020F, 0.24F}
            : (mars ? Color {0.105F, 0.055F, 0.038F, 0.58F} : (moon ? Color {0.092F, 0.090F, 0.084F, 0.58F} : Color {0.070F, 0.060F, 0.052F, 0.58F}));
        const float shade = 0.16F + light * 0.70F;
        color.r *= shade;
        color.g *= shade;
        color.b *= shade;
        return color;
    }
    switch (material) {
    case 1:
        color = mars ? Color {0.54F, 0.25F, 0.16F, 1.0F} : (moon ? Color {0.46F, 0.43F, 0.39F, 1.0F} : Color {0.38F, 0.32F, 0.26F, 1.0F});
        break;
    case 2:
        color = mars ? Color {0.27F, 0.15F, 0.13F, 1.0F} : (moon ? Color {0.26F, 0.27F, 0.29F, 1.0F} : Color {0.22F, 0.23F, 0.26F, 1.0F});
        break;
    case 3:
        color = mars ? Color {0.66F, 0.36F, 0.18F, 1.0F} : (moon ? Color {0.54F, 0.52F, 0.45F, 1.0F} : Color {0.38F, 0.42F, 0.34F, 1.0F});
        break;
    case 4:
        color = mars ? Color {0.40F, 0.40F, 0.55F, 1.0F} : (moon ? Color {0.36F, 0.52F, 0.62F, 1.0F} : Color {0.22F, 0.48F, 0.58F, 1.0F});
        break;
    case 5:
        color = mars ? Color {0.58F, 0.26F, 0.72F, 1.0F} : (moon ? Color {0.45F, 0.36F, 0.70F, 1.0F} : Color {0.46F, 0.28F, 0.70F, 1.0F});
        break;
    case 6:
        color = {0.78F, 0.55F, 0.18F, 1.0F};
        break;
    case 7:
        color = {0.56F, 0.18F, 0.16F, 1.0F};
        break;
    case 8:
        color = {0.10F, 0.11F, 0.13F, 1.0F};
        break;
    default:
        color = mars ? Color {0.030F, 0.020F, 0.018F, 1.0F} : (moon ? Color {0.026F, 0.028F, 0.032F, 1.0F} : Color {0.020F, 0.030F, 0.035F, 1.0F});
        break;
    }
    const float breakGlow = 1.0F + (1.0F - integrity) * 0.20F;
    const float lit = 0.62F + light * 0.58F;
    color.r *= lit * breakGlow;
    color.g *= lit * breakGlow;
    color.b *= lit * breakGlow;
    color.a = material == 0 ? 0.20F : (hazard ? 0.92F : 0.86F);
    return color;
}

Color miningEnemyColor(int type)
{
    switch (type) {
    case static_cast<int>(MiningEnemyType::Ant):
        return {0.92F, 0.34F, 0.22F, 0.94F};
    case static_cast<int>(MiningEnemyType::Flying):
        return {0.86F, 0.52F, 1.0F, 0.90F};
    case static_cast<int>(MiningEnemyType::Beetle):
        return {0.42F, 0.72F, 0.34F, 0.96F};
    case static_cast<int>(MiningEnemyType::Elemental):
        return {0.24F, 0.86F, 1.0F, 0.92F};
    case static_cast<int>(MiningEnemyType::Mammal):
        return {0.86F, 0.70F, 0.42F, 0.96F};
    default:
        return {1.0F, 0.28F, 0.18F, 0.90F};
    }
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
    assets_[MiningDroneAsset] = {"mining_drone", "assets/art/mining-drone.png"};
    assets_[DrillBitAsset] = {"drill_bit", "assets/art/drill-bit-sheet.png"};
    assets_[LocalSolarBgAsset] = {"local_solar_bg", "assets/art/local-solar-bg-sheet.png"};
    assets_[MercuryAsset] = {"mercury", "assets/art/mercury.png"};
    assets_[VenusAsset] = {"venus", "assets/art/venus.png"};
    assets_[JupiterAsset] = {"jupiter", "assets/art/jupiter.png"};
    assets_[SaturnAsset] = {"saturn", "assets/art/saturn.png"};
    assets_[UranusAsset] = {"uranus", "assets/art/uranus.png"};
    assets_[NeptuneAsset] = {"neptune", "assets/art/neptune.png"};

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
    std::array<GLuint, 15> textureIds {};
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
    if (snapshot.screen == Screen::Mining) {
        drawMining(snapshot);
        return;
    }
    if (snapshot.screen == Screen::Flyby) {
        drawFlyby(snapshot);
        return;
    }
    if (snapshot.screen == Screen::Orbit) {
        drawOrbit(snapshot);
        return;
    }
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
    const float launchShake = static_cast<float>(std::clamp(snapshot.launchShake, 0.0, 1.0));
    if (launchShake > 0.0F) {
        const float shake = launchShake * launchShake;
        scenePixelCenterX_ += std::sin(static_cast<float>(snapshot.animationTime) * 72.0F) * shake * 7.0F;
        scenePixelCenterY_ += std::cos(static_cast<float>(snapshot.animationTime) * 61.0F) * shake * 5.0F;
    }
    if (snapshot.screen == Screen::ArrivalFanfare) {
        const float arrival = 1.0F - static_cast<float>(std::clamp(snapshot.animationTime / tuning::session::arrivalFanfareSeconds, 0.0, 1.0));
        const float shimmer = arrival * arrival;
        scenePixelCenterX_ += std::sin(static_cast<float>(snapshot.animationTime) * 34.0F) * shimmer * 3.5F;
        scenePixelCenterY_ += std::cos(static_cast<float>(snapshot.animationTime) * 29.0F) * shimmer * 2.5F;
    }

    const float heat = static_cast<float>(std::clamp(snapshot.heat, 0.0, 1.0));
    const float arrivalGlow = snapshot.screen == Screen::ArrivalFanfare
        ? 0.018F * (1.0F - static_cast<float>(std::clamp(snapshot.animationTime / tuning::session::arrivalFanfareSeconds, 0.0, 1.0)))
        : 0.0F;
    glClearColor(0.02F + heat * 0.05F + arrivalGlow, 0.03F + arrivalGlow * 0.70F, 0.05F + heat * 0.02F + arrivalGlow * 0.35F, 1.0F);
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

void WebGLRenderer::drawSpriteRotated(float cx, float cy, float w, float h, float forwardX, float forwardY, Color tint, int assetIndex, int frameIndex, int frameCount, bool worldSpace)
{
    if (!textureReady(assetIndex)) {
        return;
    }

    TextureAsset& asset = assets_[static_cast<std::size_t>(assetIndex)];
    const Vec2 forward = normalize({forwardX, forwardY});
    const Vec2 right {forward.y, -forward.x};
    const float halfW = w * 0.5F;
    const float halfH = h * 0.5F;
    const int frames = std::max(1, frameCount);
    const int frame = std::clamp(frameIndex, 0, frames - 1);
    const float u0 = static_cast<float>(frame) / static_cast<float>(frames);
    const float u1 = static_cast<float>(frame + 1) / static_cast<float>(frames);

    auto corner = [&](float sx, float sy) {
        return Vec2 {
            cx + right.x * sx * halfW + forward.x * sy * halfH,
            cy + right.y * sx * halfW + forward.y * sy * halfH
        };
    };

    const Vec2 bl = corner(-1.0F, -1.0F);
    const Vec2 br = corner(1.0F, -1.0F);
    const Vec2 tr = corner(1.0F, 1.0F);
    const Vec2 tl = corner(-1.0F, 1.0F);
    std::vector<float>& vertices = scratchVertices(48);
    pushVertex(vertices, bl.x, bl.y, tint, u0, 1.0F);
    pushVertex(vertices, br.x, br.y, tint, u1, 1.0F);
    pushVertex(vertices, tr.x, tr.y, tint, u1, 0.0F);
    pushVertex(vertices, bl.x, bl.y, tint, u0, 1.0F);
    pushVertex(vertices, tr.x, tr.y, tint, u1, 0.0F);
    pushVertex(vertices, tl.x, tl.y, tint, u0, 0.0F);
    submit(vertices, 0x0004, true, asset.texture, worldSpace);
}

void WebGLRenderer::drawFlyby(const RenderSnapshot& snapshot)
{
    drawRect(0.0F, 0.0F, 2.0F, 2.0F, {0.012F, 0.017F, 0.027F, 1.0F}, false);
    drawSolarBackground(snapshot, 0.72F);

    const float destX = static_cast<float>(snapshot.flybyDestinationX);
    const float destY = static_cast<float>(snapshot.flybyDestinationY);
    const float goodBand = static_cast<float>(snapshot.flybyGoodBand);
    const float perfectBand = static_cast<float>(snapshot.flybyPerfectBand);
    const float pulse = 0.5F + 0.5F * std::sin(static_cast<float>(snapshot.animationTime) * 5.6F);

    auto pathPoint = [](float t) {
        const float u = 1.0F - t;
        return Vec2 {
            u * u * u * static_cast<float>(tuning::flyby::startX)
                + 3.0F * u * u * t * static_cast<float>(tuning::flyby::control1X)
                + 3.0F * u * t * t * static_cast<float>(tuning::flyby::control2X)
                + t * t * t * static_cast<float>(tuning::flyby::endX),
            u * u * u * static_cast<float>(tuning::flyby::startY)
                + 3.0F * u * u * t * static_cast<float>(tuning::flyby::control1Y)
                + 3.0F * u * t * t * static_cast<float>(tuning::flyby::control2Y)
                + t * t * t * static_cast<float>(tuning::flyby::endY)
        };
    };
    auto pathDerivative = [](float t) {
        const float u = 1.0F - t;
        return Vec2 {
            3.0F * u * u * (static_cast<float>(tuning::flyby::control1X) - static_cast<float>(tuning::flyby::startX))
                + 6.0F * u * t * (static_cast<float>(tuning::flyby::control2X) - static_cast<float>(tuning::flyby::control1X))
                + 3.0F * t * t * (static_cast<float>(tuning::flyby::endX) - static_cast<float>(tuning::flyby::control2X)),
            3.0F * u * u * (static_cast<float>(tuning::flyby::control1Y) - static_cast<float>(tuning::flyby::startY))
                + 6.0F * u * t * (static_cast<float>(tuning::flyby::control2Y) - static_cast<float>(tuning::flyby::control1Y))
                + 3.0F * t * t * (static_cast<float>(tuning::flyby::endY) - static_cast<float>(tuning::flyby::control2Y))
        };
    };
    auto offsetPoint = [&](float t, float offset) {
        const Vec2 p = pathPoint(t);
        const Vec2 tangent = normalize(pathDerivative(t));
        const Vec2 normal {-tangent.y, tangent.x};
        return Vec2 {p.x + normal.x * offset, p.y + normal.y * offset};
    };
    auto drawCurveOffset = [&](float offset, Color color, float width) {
        std::vector<float>& vertices = scratchVertices(96 * 12);
        constexpr int segments = 96;
        Vec2 previous = offsetPoint(0.0F, offset);
        for (int i = 1; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const Vec2 current = offsetPoint(t, offset);
            appendLine(vertices, previous.x, previous.y, current.x, current.y, color);
            previous = current;
        }
        submitLines(vertices, width);
    };

    for (int i = -5; i <= 5; ++i) {
        drawCurveOffset(goodBand * static_cast<float>(i) / 5.0F, {0.10F, 0.46F, 0.62F, 0.020F}, 1.0F);
    }
    drawCurveOffset(-goodBand, {0.20F, 0.72F, 1.0F, 0.28F}, 2.0F);
    drawCurveOffset(goodBand, {0.20F, 0.72F, 1.0F, 0.28F}, 2.0F);
    drawCurveOffset(-perfectBand, {1.0F, 0.82F, 0.28F, 0.42F + pulse * 0.08F}, 3.0F);
    drawCurveOffset(perfectBand, {1.0F, 0.82F, 0.28F, 0.42F + pulse * 0.08F}, 3.0F);
    drawCurveOffset(0.0F, {0.86F, 0.96F, 1.0F, 0.30F}, 1.2F);

    const Vec2 startGate = pathPoint(0.0F);
    const Vec2 endGate = pathPoint(1.0F);
    const Vec2 endTangent = normalize(pathDerivative(1.0F));
    const Vec2 endNormal {-endTangent.y, endTangent.x};
    auto drawFinishLine = [&](float halfWidth, Color color, float width) {
        std::vector<float>& finishVertices = scratchVertices(12);
        appendLine(
            finishVertices,
            endGate.x - endNormal.x * halfWidth,
            endGate.y - endNormal.y * halfWidth,
            endGate.x + endNormal.x * halfWidth,
            endGate.y + endNormal.y * halfWidth,
            color);
        submitLines(finishVertices, width);
    };
    drawCircle(startGate.x, startGate.y, 0.032F, {0.34F, 0.90F, 1.0F, 0.22F}, 24);
    drawFinishLine(goodBand, {0.28F, 0.88F, 1.0F, 0.38F}, 3.0F);
    drawFinishLine(perfectBand, {1.0F, 0.82F, 0.28F, 0.66F + pulse * 0.12F}, 5.0F);
    drawCircle(endGate.x, endGate.y, 0.024F + pulse * 0.004F, {1.0F, 0.82F, 0.28F, 0.32F}, 24);

    const float planetRadius = 0.13F + std::min(4.0F, static_cast<float>(snapshot.destinationTier)) * 0.012F;
    const int destinationAsset = destinationBodyAsset(snapshot.destinationTier);
    if (destinationAsset >= 0 && textureReady(destinationAsset)) {
        const float scale = bodySpriteScale(destinationAsset);
        drawSprite(destX, destY, planetRadius * scale, planetRadius * scale, {1.0F, 1.0F, 1.0F, 1.0F}, destinationAsset);
    } else {
        const Color body = snapshot.destinationTier >= 3
            ? Color{0.72F, 0.56F, 0.34F, 0.90F}
            : Color{0.58F, 0.68F, 0.74F, 0.90F};
        drawCircle(destX, destY, planetRadius, body, 72);
        drawCircle(destX + planetRadius * 0.22F, destY + planetRadius * 0.16F, planetRadius * 0.62F, {0.92F, 0.78F, 0.46F, 0.36F}, 48);
        if (snapshot.destinationTier >= 3) {
            drawEllipseLine(destX, destY, planetRadius * 2.36F, planetRadius * 0.54F, {0.54F, 0.80F, 0.94F, 0.30F}, 88, -0.10F * kPi, 1.10F * kPi);
        }
    }
    drawCircle(destX, destY, planetRadius * (1.55F + pulse * 0.10F), {0.30F, 0.86F, 1.0F, 0.08F}, 72);

    const float shipX = static_cast<float>(snapshot.flybyShipX);
    const float shipY = static_cast<float>(snapshot.flybyShipY);
    Vec2 velocity = normalize({static_cast<float>(snapshot.flybyVelocityX), static_cast<float>(snapshot.flybyVelocityY)});
    if (std::abs(velocity.x) + std::abs(velocity.y) < 0.001F) {
        velocity = normalize({destX - shipX, destY - shipY});
    }

    if (snapshot.flybyTrailPoints.size() >= 2) {
        std::vector<float>& pathVertices = scratchVertices(snapshot.flybyTrailPoints.size() * 12);
        for (std::size_t i = 1; i < snapshot.flybyTrailPoints.size(); ++i) {
            const FlybyTrailPointSnapshot& previous = snapshot.flybyTrailPoints[i - 1];
            const FlybyTrailPointSnapshot& current = snapshot.flybyTrailPoints[i];
            const float alpha = 0.18F + 0.34F * (static_cast<float>(i) / static_cast<float>(snapshot.flybyTrailPoints.size() - 1));
            appendLine(
                pathVertices,
                static_cast<float>(previous.x),
                static_cast<float>(previous.y),
                static_cast<float>(current.x),
                static_cast<float>(current.y),
                {1.0F, 0.18F, 0.16F, alpha});
        }
        submitLines(pathVertices, 2.4F);
    }

    std::vector<float>& trailVertices = scratchVertices(8 * 16);
    for (int i = 1; i <= 8; ++i) {
        const float a = static_cast<float>(i - 1) / 8.0F;
        const float b = static_cast<float>(i) / 8.0F;
        const float tailA = 0.18F * a;
        const float tailB = 0.18F * b;
        appendLine(
            trailVertices,
            shipX - velocity.x * tailA,
            shipY - velocity.y * tailA,
            shipX - velocity.x * tailB,
            shipY - velocity.y * tailB,
            {0.42F, 0.88F, 1.0F, 0.28F * (1.0F - b)});
    }
    submitLines(trailVertices, 1.6F);

    const int zone = snapshot.flybyCompleted ? snapshot.flybyResult : snapshot.flybyZone;
    const bool perfectZone = snapshot.flybyCompleted ? zone >= 3 : zone >= 2;
    const bool goodZone = snapshot.flybyCompleted ? zone >= 2 : zone >= 1;
    const Color zoneGlow = perfectZone
        ? Color{1.0F, 0.78F, 0.22F, 0.18F}
        : (goodZone ? Color{0.34F, 0.90F, 1.0F, 0.15F} : Color{1.0F, 0.28F, 0.20F, 0.10F});
    drawCircle(shipX, shipY, 0.090F + pulse * 0.010F, zoneGlow, 42);

    const float throttleInput = static_cast<float>(snapshot.flybyInputY);
    if (!snapshot.flybyCompleted && throttleInput > 0.05F) {
        const Vec2 thrust {-velocity.x, -velocity.y};
        const Vec2 tail {
            shipX + thrust.x * 0.072F,
            shipY + thrust.y * 0.072F
        };
        drawCircle(tail.x, tail.y, 0.026F + pulse * 0.006F, {1.0F, 0.62F, 0.16F, 0.58F}, 18);
        drawCircle(tail.x + thrust.x * 0.030F, tail.y + thrust.y * 0.030F, 0.014F, {1.0F, 0.92F, 0.36F, 0.64F}, 14);
    }

    if (textureReady(RocketAsset)) {
        if (!snapshot.flybyCompleted && throttleInput > 0.05F && textureReady(ThrustAsset)) {
            const int thrustFrame = static_cast<int>(snapshot.animationTime * 18.0) % 6;
            drawSpriteRotated(
                shipX - velocity.x * 0.030F,
                shipY - velocity.y * 0.030F,
                0.040F,
                0.070F,
                velocity.x,
                velocity.y,
                {1.0F, 1.0F, 1.0F, 0.88F},
                ThrustAsset,
                thrustFrame,
                6);
        }
        drawSpriteRotated(shipX, shipY, 0.12F, 0.12F, velocity.x, velocity.y, {1.0F, 1.0F, 1.0F, 1.0F}, RocketAsset);
    } else {
        const Vec2 right {velocity.y, -velocity.x};
        drawTriangle(
            shipX + velocity.x * 0.050F,
            shipY + velocity.y * 0.050F,
            shipX - velocity.x * 0.048F + right.x * 0.030F,
            shipY - velocity.y * 0.048F + right.y * 0.030F,
            shipX - velocity.x * 0.048F - right.x * 0.030F,
            shipY - velocity.y * 0.048F - right.y * 0.030F,
            {0.86F, 0.94F, 0.98F, 1.0F});
    }

    if (snapshot.flybyCompleted) {
        const Color resultColor = snapshot.flybyResult >= 3
            ? Color{1.0F, 0.78F, 0.22F, 0.18F}
            : (snapshot.flybyResult >= 2 ? Color{0.34F, 0.90F, 1.0F, 0.14F} : Color{1.0F, 0.22F, 0.18F, 0.12F});
        drawCircle(shipX, shipY, 0.16F + pulse * 0.025F, resultColor, 64);
    }
}

void WebGLRenderer::drawOrbit(const RenderSnapshot& snapshot)
{
    drawRect(0.0F, 0.0F, 2.0F, 2.0F, {0.010F, 0.015F, 0.024F, 1.0F}, false);
    drawSolarBackground(snapshot, 0.68F);

    const float pulse = 0.5F + 0.5F * std::sin(static_cast<float>(snapshot.animationTime) * 4.7F);
    const float planetRadius = static_cast<float>(snapshot.orbitPlanetRadius);
    const float targetRadius = static_cast<float>(snapshot.orbitTargetRadius);
    const float goodBand = static_cast<float>(snapshot.orbitGoodBand);
    const float perfectBand = static_cast<float>(snapshot.orbitPerfectBand);

    for (int i = -4; i <= 4; ++i) {
        const float offset = goodBand * static_cast<float>(i) / 4.0F;
        drawEllipseLine(0.0F, 0.0F, targetRadius + offset, targetRadius + offset, {0.14F, 0.54F, 0.68F, 0.035F}, 128, 0.0F, 2.0F * kPi);
    }
    drawEllipseLine(0.0F, 0.0F, targetRadius - goodBand, targetRadius - goodBand, {0.24F, 0.82F, 1.0F, 0.28F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius + goodBand, targetRadius + goodBand, {0.24F, 0.82F, 1.0F, 0.28F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius - perfectBand, targetRadius - perfectBand, {1.0F, 0.80F, 0.24F, 0.44F + pulse * 0.10F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius + perfectBand, targetRadius + perfectBand, {1.0F, 0.80F, 0.24F, 0.44F + pulse * 0.10F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius, targetRadius, {0.86F, 0.96F, 1.0F, 0.24F}, 128, 0.0F, 2.0F * kPi);

    const float progress = static_cast<float>(std::clamp(snapshot.orbitProgress, 0.0, 1.0));
    if (progress > 0.0F) {
        const float startAngle = static_cast<float>(tuning::orbit::startAngleRadians);
        drawEllipseLine(0.0F, 0.0F, targetRadius, targetRadius, {0.98F, 0.96F, 0.82F, 0.74F}, 128, startAngle, startAngle + progress * 2.0F * kPi);
    }

    const int destinationAsset = destinationBodyAsset(snapshot.destinationTier);
    if (destinationAsset >= 0 && textureReady(destinationAsset)) {
        const float scale = bodySpriteScale(destinationAsset);
        drawSprite(0.0F, 0.0F, planetRadius * scale, planetRadius * scale, {1.0F, 1.0F, 1.0F, 1.0F}, destinationAsset);
    } else {
        const Color body = snapshot.destinationTier >= 3
            ? Color{0.70F, 0.56F, 0.36F, 0.94F}
            : Color{0.56F, 0.66F, 0.74F, 0.92F};
        drawCircle(0.0F, 0.0F, planetRadius, body, 88);
        drawCircle(planetRadius * 0.22F, planetRadius * 0.16F, planetRadius * 0.62F, {0.92F, 0.78F, 0.46F, 0.34F}, 56);
        if (snapshot.destinationTier >= 3) {
            drawEllipseLine(0.0F, 0.0F, planetRadius * 2.30F, planetRadius * 0.52F, {0.54F, 0.80F, 0.94F, 0.30F}, 96, -0.10F * kPi, 1.10F * kPi);
        }
    }
    drawCircle(0.0F, 0.0F, planetRadius * (1.55F + pulse * 0.08F), {0.30F, 0.86F, 1.0F, 0.065F}, 88);

    if (snapshot.orbitTrailPoints.size() >= 2) {
        std::vector<float>& pathVertices = scratchVertices(snapshot.orbitTrailPoints.size() * 12);
        for (std::size_t i = 1; i < snapshot.orbitTrailPoints.size(); ++i) {
            const FlybyTrailPointSnapshot& previous = snapshot.orbitTrailPoints[i - 1];
            const FlybyTrailPointSnapshot& current = snapshot.orbitTrailPoints[i];
            const float alpha = 0.12F + 0.40F * (static_cast<float>(i) / static_cast<float>(snapshot.orbitTrailPoints.size() - 1));
            appendLine(
                pathVertices,
                static_cast<float>(previous.x),
                static_cast<float>(previous.y),
                static_cast<float>(current.x),
                static_cast<float>(current.y),
                {0.58F, 0.92F, 1.0F, alpha});
        }
        submitLines(pathVertices, 2.2F);
    }

    const float shipX = static_cast<float>(snapshot.orbitShipX);
    const float shipY = static_cast<float>(snapshot.orbitShipY);
    Vec2 velocity = normalize({static_cast<float>(snapshot.orbitVelocityX), static_cast<float>(snapshot.orbitVelocityY)});
    if (std::abs(velocity.x) + std::abs(velocity.y) < 0.001F) {
        velocity = normalize({-shipY, shipX});
    }

    const int zone = snapshot.orbitCompleted ? snapshot.orbitResult : snapshot.orbitZone;
    const bool perfectZone = snapshot.orbitCompleted ? zone >= 3 : zone >= 2;
    const bool goodZone = snapshot.orbitCompleted ? zone >= 2 : zone >= 1;
    const Color zoneGlow = perfectZone
        ? Color{1.0F, 0.78F, 0.22F, 0.18F}
        : (goodZone ? Color{0.34F, 0.90F, 1.0F, 0.15F} : Color{1.0F, 0.28F, 0.20F, 0.10F});
    drawCircle(shipX, shipY, 0.082F + pulse * 0.010F, zoneGlow, 42);

    const float inputMagnitude = std::hypot(static_cast<float>(snapshot.orbitInputX), static_cast<float>(snapshot.orbitInputY));
    if (!snapshot.orbitCompleted && inputMagnitude > 0.05F) {
        const Vec2 radial = normalize({shipX, shipY});
        const Vec2 tangent {-radial.y, radial.x};
        const Vec2 input = normalize({
            radial.x * static_cast<float>(snapshot.orbitInputX) + tangent.x * static_cast<float>(snapshot.orbitInputY),
            radial.y * static_cast<float>(snapshot.orbitInputX) + tangent.y * static_cast<float>(snapshot.orbitInputY)
        });
        const Vec2 tail {shipX - input.x * 0.066F, shipY - input.y * 0.066F};
        drawCircle(tail.x, tail.y, 0.024F + pulse * 0.005F, {1.0F, 0.62F, 0.16F, 0.52F}, 18);
        drawCircle(tail.x - input.x * 0.026F, tail.y - input.y * 0.026F, 0.012F, {1.0F, 0.92F, 0.36F, 0.58F}, 14);
    }

    if (textureReady(RocketAsset)) {
        drawSpriteRotated(shipX, shipY, 0.11F, 0.11F, velocity.x, velocity.y, {1.0F, 1.0F, 1.0F, 1.0F}, RocketAsset);
    } else {
        const Vec2 right {velocity.y, -velocity.x};
        drawTriangle(
            shipX + velocity.x * 0.048F,
            shipY + velocity.y * 0.048F,
            shipX - velocity.x * 0.044F + right.x * 0.028F,
            shipY - velocity.y * 0.044F + right.y * 0.028F,
            shipX - velocity.x * 0.044F - right.x * 0.028F,
            shipY - velocity.y * 0.044F - right.y * 0.028F,
            {0.86F, 0.94F, 0.98F, 1.0F});
    }

    if (snapshot.orbitCompleted) {
        const Color resultColor = snapshot.orbitResult >= 3
            ? Color{1.0F, 0.78F, 0.22F, 0.18F}
            : (snapshot.orbitResult >= 2 ? Color{0.34F, 0.90F, 1.0F, 0.14F} : Color{1.0F, 0.22F, 0.18F, 0.12F});
        drawCircle(shipX, shipY, 0.15F + pulse * 0.024F, resultColor, 64);
    }
}

void WebGLRenderer::drawMining(const RenderSnapshot& snapshot)
{
    drawRect(0.0F, 0.0F, 2.0F, 2.0F, {0.012F, 0.014F, 0.018F, 1.0F}, false);
    drawSolarBackground(snapshot, 0.42F);
    if (snapshot.miningWidth <= 0 || snapshot.miningHeight <= 0) {
        return;
    }

    const float left = -0.92F;
    const float right = 0.92F;
    const float top = 0.78F;
    const float bottom = -0.86F;
    const float cellW = (right - left) / static_cast<float>(snapshot.miningWidth);
    const float cellH = (top - bottom) / static_cast<float>(snapshot.miningHeight);
    auto cellCenter = [&](double x, double y) {
        return Vec2 {
            left + static_cast<float>(x) * cellW + cellW * 0.5F,
            top - static_cast<float>(y) * cellH - cellH * 0.5F
        };
    };
    auto gridPoint = [&](double x, double y) {
        return Vec2 {
            left + static_cast<float>(x) * cellW,
            top - static_cast<float>(y) * cellH
        };
    };

    drawRect((left + right) * 0.5F, (top + bottom) * 0.5F, right - left + 0.035F, top - bottom + 0.035F, {0.02F, 0.03F, 0.04F, 0.92F});

    std::vector<float>& terrainVertices = scratchVertices(snapshot.miningCells.size() * 48U);
    for (const MiningCellSnapshot& cell : snapshot.miningCells) {
        const Vec2 center = cellCenter(static_cast<double>(cell.x), static_cast<double>(cell.y));
        const float dxCells = static_cast<float>(static_cast<double>(cell.x) + 0.5 - snapshot.miningDroneX);
        const float dyCells = static_cast<float>(static_cast<double>(cell.y) + 0.5 - snapshot.miningDroneY);
        const float distCells = std::sqrt(dxCells * dxCells + dyCells * dyCells);
        float localLight = std::clamp(1.0F - distCells / kMiningLightRadiusCells, 0.0F, 1.0F);
        if (snapshot.miningScannerPulse > 0.0) {
            const float pulse = static_cast<float>(std::clamp(snapshot.miningScannerPulse / kMiningScannerPulseSeconds, 0.0, 1.0));
            const float ringRadius = 2.2F + (1.0F - pulse) * 4.9F;
            const float ringWidth = 1.4F + pulse * 0.7F;
            const float ring = std::clamp(1.0F - std::abs(distCells - ringRadius) / ringWidth, 0.0F, 1.0F) * 0.42F * pulse;
            localLight = std::max(localLight, ring);
        }
        const Color color = miningMaterialColor(cell.material, static_cast<float>(cell.integrity), cell.revealed, cell.hazard && cell.revealed, snapshot.destinationTier, localLight);
        appendRect(terrainVertices, center.x, center.y, cellW * 0.96F, cellH * 0.96F, color);
    }
    submit(terrainVertices, 0x0004);

    for (const MiningEnemySnapshot& enemy : snapshot.miningEnemies) {
        if (!enemy.active) {
            continue;
        }
        const Vec2 enemyCenter = cellCenter(enemy.x, enemy.y);
        const float health = static_cast<float>(std::clamp(enemy.maxHealth <= 0.0 ? 1.0 : enemy.health / enemy.maxHealth, 0.0, 1.0));
        const Color base = miningEnemyColor(enemy.type);
        drawCircle(enemyCenter.x, enemyCenter.y, std::min(cellW, cellH) * 1.42F, {base.r, base.g, base.b, 0.18F}, 18);
        drawCircle(enemyCenter.x, enemyCenter.y, std::min(cellW, cellH) * (0.56F + health * 0.34F), base, 16);
        drawRect(enemyCenter.x, enemyCenter.y - cellH * 0.72F, cellW * 1.18F * health, cellH * 0.10F, {1.0F, 0.18F, 0.12F, 0.82F});
    }

    Vec2 drone = cellCenter(snapshot.miningDroneX, snapshot.miningDroneY);
    if (snapshot.miningBounce > 0.0) {
        const float bounce = static_cast<float>(std::clamp(snapshot.miningBounce, 0.0, tuning::mining::contactBounceMaxCells));
        drone.x += static_cast<float>(snapshot.miningRecoilX) * cellW * bounce;
        drone.y -= static_cast<float>(snapshot.miningRecoilY) * cellH * bounce;
    }
    const float cellSize = std::min(cellW, cellH);
    drawCircle(drone.x, drone.y, cellSize * 6.8F, {0.18F, 0.52F, 0.68F, 0.055F}, 40);
    drawCircle(drone.x, drone.y, cellSize * 3.2F, {0.28F, 0.82F, 0.98F, 0.080F}, 32);
    if (snapshot.miningFailurePulse > 0.0) {
        const float pulse = static_cast<float>(std::clamp(snapshot.miningFailurePulse, 0.0, 1.0));
        const float beat = 0.55F + 0.45F * std::sin(static_cast<float>(snapshot.animationTime) * 34.0F);
        drawCircle(drone.x, drone.y, cellSize * (4.0F + (1.0F - pulse) * 5.0F), {1.0F, 0.16F, 0.08F, 0.16F * pulse}, 42);
        drawCircle(drone.x, drone.y, cellSize * (2.1F + beat * 0.9F), {1.0F, 0.48F, 0.18F, 0.18F * pulse}, 30);
    }
    if (snapshot.miningScannerPulse > 0.0) {
        const float pulse = static_cast<float>(std::clamp(snapshot.miningScannerPulse / kMiningScannerPulseSeconds, 0.0, 1.0));
        const float radius = cellSize * (5.2F + (1.0F - pulse) * 12.0F);
        drawCircle(drone.x, drone.y, radius, {0.30F, 0.88F, 1.0F, 0.055F * pulse}, 48);
    }

    const Vec2 target = gridPoint(snapshot.miningTargetX, snapshot.miningTargetY);
    const float droneSize = std::min(cellW, cellH) * 4.35F;
    Vec2 drillDirection = normalize({
        static_cast<float>(snapshot.miningDrillDirX) * cellW,
        -static_cast<float>(snapshot.miningDrillDirY) * cellH
    });
    if (std::abs(drillDirection.x) + std::abs(drillDirection.y) < 0.001F) {
        drillDirection = normalize({target.x - drone.x, target.y - drone.y});
    }
    const Vec2 drillOrigin {
        drone.x + drillDirection.x * droneSize * 0.18F,
        drone.y + drillDirection.y * droneSize * 0.18F
    };
    Vec2 particleAnchor = target;

    if (textureReady(MiningDroneAsset)) {
        drawSpriteRotated(
            drone.x,
            drone.y,
            droneSize,
            droneSize,
            -drillDirection.x,
            -drillDirection.y,
            {1.0F, 1.0F, 1.0F, 1.0F},
            MiningDroneAsset);
    } else {
        drawCircle(drone.x, drone.y, cellW * 1.15F, {0.10F, 0.14F, 0.18F, 1.0F}, 24);
        drawCircle(drone.x, drone.y, cellW * 0.72F, {0.28F, 0.82F, 0.98F, 1.0F}, 20);
        drawRect(drone.x, drone.y - cellH * 0.95F, cellW * 1.0F, cellH * 0.42F, {0.82F, 0.88F, 0.92F, 1.0F});
    }

    if (textureReady(DrillBitAsset) && snapshot.miningTargetDrillable) {
        const float dx = target.x - drillOrigin.x;
        const float dy = target.y - drillOrigin.y;
        const float contactDistance = std::max(0.0F, dx * drillDirection.x + dy * drillDirection.y);
        const float drillH = std::clamp(contactDistance + cellSize * 0.55F, cellSize * 2.30F, cellSize * 3.75F);
        const float drillW = drillH * 0.88F;
        const Vec2 bitCenter {
            drillOrigin.x + drillDirection.x * drillH * 0.5F,
            drillOrigin.y + drillDirection.y * drillH * 0.5F
        };
        particleAnchor = {
            drillOrigin.x + drillDirection.x * drillH,
            drillOrigin.y + drillDirection.y * drillH
        };
        const int drillFrame = snapshot.miningDrilling ? static_cast<int>(snapshot.animationTime * 18.0) % 6 : 0;
        drawSpriteRotated(
            bitCenter.x,
            bitCenter.y,
            drillW,
            drillH,
            -drillDirection.x,
            -drillDirection.y,
            {1.0F, 1.0F, 1.0F, 1.0F},
            DrillBitAsset,
            drillFrame,
            6);
    }

    if (snapshot.miningDrilling || snapshot.miningFailurePulse > 0.0) {
        const int failureBurst = snapshot.miningFailurePulse > 0.0 ? 18 : 0;
        const int particleCount = 8 + failureBurst + static_cast<int>(std::round(snapshot.miningContactIntensity * 10.0));
        std::vector<float>& particleVertices = scratchVertices(static_cast<std::size_t>(particleCount) * 48U);
        for (int i = 0; i < particleCount; ++i) {
            const float t = static_cast<float>(std::fmod(snapshot.animationTime * 9.0 + static_cast<double>(i) * 0.37, 1.0));
            const float angle = static_cast<float>(i) * 1.73F + t * kPi * 2.0F;
            const float failureScale = static_cast<float>(snapshot.miningFailurePulse);
            const float radius = (0.2F + t * (0.9F + static_cast<float>(snapshot.miningContactIntensity) * 0.7F + failureScale * 1.3F)) * std::min(cellW, cellH);
            const float px = particleAnchor.x + std::cos(angle) * radius;
            const float py = particleAnchor.y + std::sin(angle) * radius;
            const Color spark = snapshot.miningFailurePulse > 0.0
                ? mix({1.0F, 0.18F, 0.08F, 0.95F}, {1.0F, 0.78F, 0.22F, 0.20F}, t)
                : mix({1.0F, 0.82F, 0.28F, 0.95F}, {0.72F, 0.48F, 0.34F, 0.15F}, t);
            appendRect(particleVertices, px, py, cellW * 0.22F, cellH * 0.22F, spark);
        }
        submit(particleVertices, 0x0004);
    }
}

void WebGLRenderer::drawTelemetry(const RenderSnapshot& snapshot)
{
    const float left = 0.18F;
    const float right = 0.94F;
    const float bottom = -0.86F;
    const float top = -0.58F;
    const float width = right - left;
    const float height = top - bottom;
    const float cautionY = bottom + height * 0.70F;

    drawRect((left + right) * 0.5F, (bottom + top) * 0.5F, width, height, {0.02F, 0.05F, 0.07F, 0.72F});
    drawRect((left + right) * 0.5F, top - 0.03F, width, 0.02F, {0.18F, 0.30F, 0.42F, 0.10F});
    drawRect((left + right) * 0.5F, cautionY + height * 0.15F, width, height * 0.30F, {0.24F, 0.09F, 0.08F, 0.08F});
    drawLine(left, bottom, right, bottom, {0.36F, 0.55F, 0.68F, 0.55F});
    drawLine(left, bottom, left, top, {0.36F, 0.55F, 0.68F, 0.55F});
    drawLine(left, top, right, top, {0.24F, 0.42F, 0.54F, 0.28F});
    drawLine(right, bottom, right, top, {0.24F, 0.42F, 0.54F, 0.22F});

    for (int i = 1; i <= 3; ++i) {
        const float y = bottom + height * (static_cast<float>(i) / 4.0F);
        drawLine(left, y, right, y, {0.24F, 0.38F, 0.48F, 0.14F});
    }

    for (int i = 1; i <= 4; ++i) {
        const float x = left + width * (static_cast<float>(i) / 5.0F);
        drawLine(x, bottom, x, top, {0.18F, 0.30F, 0.40F, 0.10F});
    }

    if (snapshot.telemetryCount <= 1) {
        return;
    }
    const float sampleDenominator = static_cast<float>(std::max(1, static_cast<int>(snapshot.telemetry.size()) - 1));

    Color warningSafe {0.35F, 0.84F, 1.0F, 1.0F};
    Color warningHot {1.0F, 0.38F, 0.28F, 1.0F};
    Color heatColor {1.0F, 0.78F, 0.25F, 0.90F};
    Color heatGlow {1.0F, 0.72F, 0.22F, 0.18F};
    Color cautionColor {1.0F, 0.80F, 0.30F, 0.22F};

    drawLine(left, cautionY, right, cautionY, cautionColor, 1.6F);

    std::vector<float>& heatVertices = scratchVertices(static_cast<std::size_t>(snapshot.telemetryCount - 1) * 16);
    std::vector<float>& heatGlowVertices = scratchVertices(static_cast<std::size_t>(snapshot.telemetryCount - 1) * 16);
    for (int i = 1; i < snapshot.telemetryCount; ++i) {
        const float t0 = static_cast<float>(i - 1) / sampleDenominator;
        const float t1 = static_cast<float>(i) / sampleDenominator;
        const float h0 = bottom + static_cast<float>(snapshot.heatTelemetry[static_cast<std::size_t>(i - 1)]) * height;
        const float h1 = bottom + static_cast<float>(snapshot.heatTelemetry[static_cast<std::size_t>(i)]) * height;
        const float x0 = left + t0 * width;
        const float x1 = left + t1 * width;
        appendLine(heatGlowVertices, x0, h0, x1, h1, heatGlow);
        appendLine(heatVertices, x0, h0, x1, h1, heatColor);
    }
    submitLines(heatGlowVertices, 5.0F);
    submitLines(heatVertices, 1.5F);

    std::vector<float>& warningVertices = scratchVertices(static_cast<std::size_t>(snapshot.telemetryCount - 1) * 16);
    std::vector<float>& warningGlowVertices = scratchVertices(static_cast<std::size_t>(snapshot.telemetryCount - 1) * 16);
    const Color warningColor = mix(warningSafe, warningHot, static_cast<float>(snapshot.warning));
    const Color warningGlow = {warningColor.r, warningColor.g, warningColor.b, 0.22F};
    for (int i = 1; i < snapshot.telemetryCount; ++i) {
        const float t0 = static_cast<float>(i - 1) / sampleDenominator;
        const float t1 = static_cast<float>(i) / sampleDenominator;
        const float y0 = bottom + static_cast<float>(snapshot.telemetry[static_cast<std::size_t>(i - 1)]) * height;
        const float y1 = bottom + static_cast<float>(snapshot.telemetry[static_cast<std::size_t>(i)]) * height;
        const float x0 = left + t0 * width;
        const float x1 = left + t1 * width;
        appendLine(warningGlowVertices, x0, y0, x1, y1, warningGlow);
        appendLine(warningVertices, x0, y0, x1, y1, warningColor);
    }
    submitLines(warningGlowVertices, 6.0F);
    submitLines(warningVertices, 2.2F);

    const float endpointT = static_cast<float>(snapshot.telemetryCount - 1) / sampleDenominator;
    const float warningX = left + endpointT * width;
    const float warningY = bottom + static_cast<float>(snapshot.telemetry[static_cast<std::size_t>(snapshot.telemetryCount - 1)]) * height;
    const float heatX = warningX;
    const float heatY = bottom + static_cast<float>(snapshot.heatTelemetry[static_cast<std::size_t>(snapshot.telemetryCount - 1)]) * height;
    drawCircle(warningX, warningY, 0.016F, {warningColor.r, warningColor.g, warningColor.b, 0.20F}, 20);
    drawCircle(warningX, warningY, 0.007F, warningColor, 18);
    drawCircle(heatX, heatY, 0.014F, {heatColor.r, heatColor.g, heatColor.b, 0.18F}, 18);
    drawCircle(heatX, heatY, 0.006F, heatColor, 16);
}

void WebGLRenderer::drawSolarBackground(const RenderSnapshot& snapshot, float alpha)
{
    if (!textureReady(LocalSolarBgAsset)) {
        return;
    }

    const double cycle = std::fmod(std::max(0.0, snapshot.animationTime) * 0.16, 4.0);
    const int frame = std::clamp(static_cast<int>(std::floor(cycle)), 0, 3);
    const int nextFrame = (frame + 1) % 4;
    const float blend = static_cast<float>(cycle - static_cast<double>(frame));
    const float smoothBlend = blend * blend * (3.0F - 2.0F * blend);
    const float clampedAlpha = std::clamp(alpha, 0.0F, 1.0F);
    drawSprite(0.0F, 0.0F, 2.06F, 2.06F, {1.0F, 1.0F, 1.0F, clampedAlpha * (1.0F - smoothBlend)}, LocalSolarBgAsset, frame, 4, false);
    drawSprite(0.0F, 0.0F, 2.06F, 2.06F, {1.0F, 1.0F, 1.0F, clampedAlpha * smoothBlend}, LocalSolarBgAsset, nextFrame, 4, false);
}

void WebGLRenderer::drawRoute(const RenderSnapshot& snapshot)
{
    const bool arrivalFanfare = snapshot.screen == Screen::ArrivalFanfare;
    const float flash = arrivalFanfare
        ? 0.55F + 0.45F * std::sin(static_cast<float>(snapshot.animationTime) * 18.0F)
        : 0.0F;
    std::vector<float>& routeVertices = scratchVertices(28 * 16);
    Vec2 previous = routePoint(snapshot, 0.0F);
    for (int i = 1; i <= 28; ++i) {
        const float t = static_cast<float>(i) / 28.0F;
        const Vec2 next = routePoint(snapshot, t);
        const bool completed = arrivalFanfare || t <= snapshot.travelProgress;
        const Color routeColor = completed
            ? mix({0.42F, 0.88F, 1.0F, 0.46F}, {1.0F, 0.82F, 0.28F, 0.72F}, flash)
            : Color{0.25F, 0.42F, 0.52F, 0.22F};
        appendLine(routeVertices, previous.x, previous.y, next.x, next.y, routeColor);
        previous = next;
    }
    submitLines(routeVertices, arrivalFanfare ? 2.0F : 1.0F);

    if (arrivalFanfare) {
        std::vector<float>& flashVertices = scratchVertices(28 * 16);
        previous = routePoint(snapshot, 0.0F);
        for (int i = 1; i <= 28; ++i) {
            const float t = static_cast<float>(i) / 28.0F;
            const Vec2 next = routePoint(snapshot, t);
            const float tail = std::clamp(1.0F - std::abs(t - 0.78F - flash * 0.18F) / 0.22F, 0.0F, 1.0F);
            appendLine(flashVertices, previous.x, previous.y, next.x, next.y, {0.95F, 0.96F, 1.0F, 0.22F * tail});
            previous = next;
        }
        submitLines(flashVertices, 3.0F);
    }

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
    drawSolarBackground(snapshot, 0.70F);

    auto drawBodySprite = [&](int assetIndex, Vec2 center, float size, float alpha) {
        if (textureReady(assetIndex)) {
            drawSprite(center.x, center.y, size, size, {1.0F, 1.0F, 1.0F, alpha}, assetIndex);
        }
    };

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
        } else if (snapshot.destinationTier == 3) {
            const float earthX = -0.42F;
            const float earthY = -0.91F;
            const float earthR = 0.105F;
            const Vec2 mercury {-0.48F, -0.56F};
            const Vec2 venus {-0.34F, -0.60F};
            const Vec2 moon {-0.22F, -0.68F};
            const Vec2 mars = routePoint(snapshot, 0.34F);
            if (textureReady(EarthAsset)) {
                drawSprite(earthX, earthY, earthR * 2.35F, earthR * 2.35F, {1.0F, 1.0F, 1.0F, 0.72F}, EarthAsset);
            } else {
                drawCircle(earthX, earthY, earthR * 1.18F, {0.24F, 0.62F, 0.96F, 0.06F}, 40);
                drawCircle(earthX, earthY, earthR, {0.18F, 0.48F, 0.78F, 0.52F}, 40);
                drawCircle(earthX - 0.03F, earthY + 0.03F, earthR * 0.16F, {0.30F, 0.60F, 0.38F, 0.50F}, 12);
            }
            drawEllipseLine(earthX, earthY, 0.44F, 0.25F, {0.40F, 0.62F, 0.78F, 0.16F}, 76, 0.08F * kPi, 0.84F * kPi);
            drawBodySprite(MercuryAsset, mercury, 0.047F, 0.46F);
            drawBodySprite(VenusAsset, venus, 0.064F, 0.48F);
            if (textureReady(MoonAsset)) {
                drawSprite(moon.x, moon.y, 0.060F, 0.060F, {1.0F, 1.0F, 1.0F, 0.54F}, MoonAsset);
            } else {
                drawCircle(moon.x, moon.y, 0.016F, {0.72F, 0.74F, 0.72F, 0.42F}, 22);
            }
            if (textureReady(MarsAsset)) {
                drawSprite(mars.x, mars.y, 0.095F, 0.095F, {1.0F, 1.0F, 1.0F, 0.58F}, MarsAsset);
            } else {
                drawCircle(mars.x, mars.y, 0.026F, {0.78F, 0.28F, 0.16F, 0.42F}, 32);
            }
        }
        if (snapshot.destinationTier == 2 && textureReady(MarsAsset)) {
            drawSprite(endpoint.x, endpoint.y, radius * 2.55F, radius * 2.55F, {1.0F, 1.0F, 1.0F, 0.86F}, MarsAsset);
        } else if (snapshot.destinationTier == 3) {
            const float arrivalBeat = snapshot.screen == Screen::ArrivalFanfare
                ? 0.5F + 0.5F * std::sin(static_cast<float>(snapshot.animationTime) * 8.0F)
                : 0.0F;
            const float bodyPulse = snapshot.screen == Screen::ArrivalFanfare ? 1.0F + arrivalBeat * 0.08F : 1.0F;
            const float bodyRadius = radius * 1.12F * bodyPulse;
            if (snapshot.screen == Screen::ArrivalFanfare) {
                drawCircle(endpoint.x, endpoint.y, radius * (1.72F + arrivalBeat * 0.28F), {1.0F, 0.78F, 0.24F, 0.12F}, 72);
            }
            if (textureReady(JupiterAsset)) {
                drawCircle(endpoint.x, endpoint.y, bodyRadius * 1.55F, {1.0F, 0.72F, 0.28F, 0.10F}, 72);
                drawSprite(endpoint.x, endpoint.y, bodyRadius * 2.58F, bodyRadius * 2.58F, {1.0F, 1.0F, 1.0F, 0.92F}, JupiterAsset);
            } else {
                drawCircle(endpoint.x, endpoint.y, bodyRadius, {0.78F, 0.58F, 0.30F, 0.64F}, 64);
                drawCircle(endpoint.x + radius * 0.25F, endpoint.y + radius * 0.15F, radius * 0.62F * bodyPulse, {0.92F, 0.75F, 0.42F, 0.54F}, 48);
            }
            drawBodySprite(SaturnAsset, {endpoint.x - radius * 2.35F, endpoint.y + radius * 1.18F}, radius * 2.45F, 0.62F);
            drawBodySprite(UranusAsset, {endpoint.x + radius * 1.94F, endpoint.y + radius * 0.90F}, radius * 1.10F, 0.58F);
            drawBodySprite(NeptuneAsset, {endpoint.x + radius * 2.40F, endpoint.y - radius * 0.70F}, radius * 1.00F, 0.52F);
        } else if (snapshot.destinationTier == 4) {
            const float arrivalBeat = snapshot.screen == Screen::ArrivalFanfare
                ? 0.5F + 0.5F * std::sin(static_cast<float>(snapshot.animationTime) * 8.0F)
                : 0.0F;
            const float pulse = snapshot.screen == Screen::ArrivalFanfare ? 1.0F + arrivalBeat * 0.07F : 1.0F;
            const float bodyRadius = radius * 1.18F * pulse;
            if (snapshot.screen == Screen::ArrivalFanfare) {
                drawCircle(endpoint.x, endpoint.y, radius * (2.05F + arrivalBeat * 0.36F), {0.42F, 0.90F, 1.0F, 0.12F}, 72);
            }
            drawCircle(endpoint.x, endpoint.y, bodyRadius * 1.62F, {0.28F, 0.90F, 1.0F, 0.10F}, 72);
            drawCircle(endpoint.x, endpoint.y, bodyRadius, {0.20F, 0.34F, 0.48F, 0.78F}, 72);
            drawCircle(endpoint.x + bodyRadius * 0.22F, endpoint.y + bodyRadius * 0.15F, bodyRadius * 0.70F, {0.48F, 0.72F, 0.76F, 0.34F}, 48);
            drawCircle(endpoint.x - bodyRadius * 0.32F, endpoint.y - bodyRadius * 0.22F, bodyRadius * 0.22F, {0.82F, 0.36F, 0.30F, 0.38F}, 24);
            drawEllipseLine(endpoint.x, endpoint.y, bodyRadius * 2.60F, bodyRadius * 0.58F, {0.38F, 0.84F, 1.0F, 0.30F}, 96, -0.12F * kPi, 1.12F * kPi);
            drawCircle(endpoint.x + bodyRadius * 1.90F, endpoint.y - bodyRadius * 0.22F, bodyRadius * 0.16F, {0.66F, 0.74F, 0.82F, 0.46F}, 18);
        } else if (snapshot.destinationTier >= 5) {
            const float arrivalBeat = snapshot.screen == Screen::ArrivalFanfare
                ? 0.5F + 0.5F * std::sin(static_cast<float>(snapshot.animationTime) * 8.0F)
                : 0.0F;
            const float ringAlpha = snapshot.screen == Screen::ArrivalFanfare ? 0.36F + arrivalBeat * 0.08F : 0.28F;
            drawCircle(endpoint.x, endpoint.y, radius * 0.82F, {0.54F, 0.42F, 0.72F, 0.60F}, 64);
            drawCircle(endpoint.x + radius * 0.20F, endpoint.y + radius * 0.14F, radius * 0.46F, {0.78F, 0.60F, 0.88F, 0.34F}, 36);
            drawEllipseLine(endpoint.x, endpoint.y, radius * 3.30F, radius * 0.74F, {0.95F, 0.74F, 0.38F, ringAlpha}, 110, -0.16F * kPi, 1.18F * kPi);
            drawEllipseLine(endpoint.x, endpoint.y, radius * 2.35F, radius * 0.48F, {0.48F, 0.86F, 1.0F, 0.18F}, 90, -0.08F * kPi, 1.08F * kPi);
            drawCircle(endpoint.x - radius * 2.40F, endpoint.y + radius * 0.48F, radius * 0.15F, {0.76F, 0.72F, 0.64F, 0.48F}, 16);
            drawCircle(endpoint.x + radius * 2.36F, endpoint.y - radius * 0.42F, radius * 0.13F, {0.58F, 0.68F, 0.80F, 0.42F}, 16);
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

    if (snapshot.screen == Screen::ArrivalFanfare) {
        const Vec2 endpoint = routePoint(snapshot, 1.0F);
        const float time = static_cast<float>(snapshot.animationTime);
        const float life = 1.0F - static_cast<float>(std::clamp(snapshot.animationTime / tuning::session::arrivalFanfareSeconds, 0.0, 1.0));
        const float beat = 0.5F + 0.5F * std::sin(time * 15.0F);
        const float baseRadius = 0.13F + static_cast<float>(snapshot.destinationTier) * 0.015F;
        drawCircle(endpoint.x, endpoint.y, baseRadius * (1.0F + beat * 0.22F), {0.30F, 0.88F, 1.0F, 0.12F + beat * 0.06F}, 64);
        drawCircle(endpoint.x, endpoint.y, baseRadius * (1.58F + (1.0F - life) * 0.36F), {1.0F, 0.78F, 0.22F, 0.16F * life}, 72);
        drawCircle(endpoint.x, endpoint.y, baseRadius * (2.10F + (1.0F - life) * 0.72F), {0.36F, 0.90F, 1.0F, 0.10F * life}, 72);
        for (int i = 0; i < 14; ++i) {
            const float seed = static_cast<float>(i) * 2.39996F;
            const float angle = seed + time * (0.15F + static_cast<float>(i % 3) * 0.025F);
            const float radius = baseRadius * (1.35F + 0.46F * std::sin(time * 3.0F + seed));
            const float length = baseRadius * (0.14F + 0.06F * static_cast<float>(i % 4));
            const float ax = endpoint.x + std::cos(angle) * radius;
            const float ay = endpoint.y + std::sin(angle) * radius;
            const float bx = endpoint.x + std::cos(angle) * (radius + length);
            const float by = endpoint.y + std::sin(angle) * (radius + length);
            const Color sparkle = (i % 2 == 0)
                ? Color{1.0F, 0.84F, 0.30F, 0.34F * life}
                : Color{0.40F, 0.92F, 1.0F, 0.28F * life};
            drawLine(ax, ay, bx, by, sparkle, 1.4F);
        }
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

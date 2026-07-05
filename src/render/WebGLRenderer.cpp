#include "render/WebGLRenderer.h"

#include "core/Tuning.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#ifdef __EMSCRIPTEN__
EM_JS(int, rr_camera_shake_enabled, (), {
    try {
        return window.localStorage.getItem("rocket_rogue_camera_shake_disabled") === "1" ? 0 : 1;
    } catch (error) {
        return 1;
    }
});
#endif

namespace rocket {

namespace {

constexpr float kPi = 3.1415926535F;
constexpr float kSceneViewportPadding = 0.92F;
constexpr float kMiningLightRadiusCells = 2.15F;
constexpr float kMiningScannerPulseSeconds = 0.9F;
constexpr int kMiningPickupCargoMaterial = -1;

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
    if (canvas.width !== width) {
        canvas.width = width;
    }
    if (canvas.height !== height) {
        canvas.height = height;
    }
});

EM_JS(double, rr_scene_left_ndc, (), {
    const canvas = document.getElementById("canvas");
    const panel = document.getElementById("panel");
    const width = (canvas && canvas.clientWidth) || globalThis.innerWidth || 1;
    if (width <= 720) {
        return -1;
    }

    const flybyVisible = panel && (
        panel.querySelector("[data-flyby-run]") ||
        panel.querySelector("[data-flyby-stamp]"));
    const gutter = 24;
    let leftPx = 0;
    if (document.body.classList.contains("rmlui-enabled") && flybyVisible) {
        leftPx = 16 + 482 + gutter;
    } else if (panel) {
        const rect = panel.getBoundingClientRect();
        leftPx = Math.max(0, rect.right + gutter);
    }
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

Color miningEnemyColor(int type, int affinity)
{
    if (type == static_cast<int>(MiningEnemyType::Elemental)) {
        switch (affinity) {
        case static_cast<int>(MiningElementalAffinity::Thermal):
            return {1.0F, 0.38F, 0.16F, 0.94F};
        case static_cast<int>(MiningElementalAffinity::Cryo):
            return {0.42F, 0.82F, 1.0F, 0.92F};
        case static_cast<int>(MiningElementalAffinity::Radiation):
            return {0.64F, 1.0F, 0.28F, 0.92F};
        case static_cast<int>(MiningElementalAffinity::Toxic):
            return {0.74F, 0.42F, 1.0F, 0.92F};
        default:
            break;
        }
    }
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

bool miningRewardMaterial(int material)
{
    switch (static_cast<MiningCellMaterial>(material)) {
    case MiningCellMaterial::CommonOre:
    case MiningCellMaterial::RareOre:
    case MiningCellMaterial::ExoticVein:
    case MiningCellMaterial::ArtifactCache:
        return true;
    case MiningCellMaterial::Empty:
    case MiningCellMaterial::Regolith:
    case MiningCellMaterial::HardRock:
    case MiningCellMaterial::HazardPocket:
    case MiningCellMaterial::Bedrock:
        return false;
    }
    return false;
}

bool miningScannerPingMaterial(int material)
{
    switch (static_cast<MiningCellMaterial>(material)) {
    case MiningCellMaterial::Regolith:
    case MiningCellMaterial::HardRock:
    case MiningCellMaterial::CommonOre:
    case MiningCellMaterial::RareOre:
    case MiningCellMaterial::ExoticVein:
    case MiningCellMaterial::ArtifactCache:
    case MiningCellMaterial::HazardPocket:
        return true;
    case MiningCellMaterial::Empty:
    case MiningCellMaterial::Bedrock:
        return false;
    }
    return false;
}

Color miningRewardGlowColor(int material)
{
    switch (static_cast<MiningCellMaterial>(material)) {
    case MiningCellMaterial::CommonOre:
        return {0.98F, 0.68F, 0.22F, 1.0F};
    case MiningCellMaterial::RareOre:
        return {0.34F, 0.92F, 1.0F, 1.0F};
    case MiningCellMaterial::ExoticVein:
        return {0.78F, 0.42F, 1.0F, 1.0F};
    case MiningCellMaterial::ArtifactCache:
        return {1.0F, 0.86F, 0.30F, 1.0F};
    case MiningCellMaterial::Empty:
    case MiningCellMaterial::Regolith:
    case MiningCellMaterial::HardRock:
    case MiningCellMaterial::HazardPocket:
    case MiningCellMaterial::Bedrock:
        break;
    }
    return {1.0F, 0.82F, 0.28F, 1.0F};
}

Color miningPickupGlowColor(int material)
{
    if (material == kMiningPickupCargoMaterial) {
        return {0.60F, 0.94F, 0.76F, 1.0F};
    }
    return miningRewardGlowColor(material);
}

Color miningScannerPingColor(int material)
{
    if (miningRewardMaterial(material)) {
        return miningRewardGlowColor(material);
    }
    switch (static_cast<MiningCellMaterial>(material)) {
    case MiningCellMaterial::HazardPocket:
        return {1.0F, 0.28F, 0.16F, 1.0F};
    case MiningCellMaterial::HardRock:
        return {0.48F, 0.72F, 0.86F, 1.0F};
    case MiningCellMaterial::Regolith:
        return {0.86F, 0.70F, 0.46F, 1.0F};
    case MiningCellMaterial::Empty:
    case MiningCellMaterial::CommonOre:
    case MiningCellMaterial::RareOre:
    case MiningCellMaterial::ExoticVein:
    case MiningCellMaterial::ArtifactCache:
    case MiningCellMaterial::Bedrock:
        break;
    }
    return {0.70F, 0.86F, 0.92F, 1.0F};
}

MiningCellMaterial surfaceScanPingMaterial(const RenderSnapshot& snapshot, int pingIndex)
{
    if (!snapshot.surfaceScanPreviewMarkers.empty()) {
        const int index = std::clamp(pingIndex, 0, static_cast<int>(snapshot.surfaceScanPreviewMarkers.size()) - 1);
        return snapshot.surfaceScanPreviewMarkers[static_cast<std::size_t>(index)];
    }
    int cursor = std::max(0, snapshot.surfaceScanMaterials.common);
    if (pingIndex < cursor) {
        return MiningCellMaterial::CommonOre;
    }
    cursor += std::max(0, snapshot.surfaceScanMaterials.rare) * 2;
    if (pingIndex < cursor) {
        return MiningCellMaterial::RareOre;
    }
    cursor += std::max(0, snapshot.surfaceScanMaterials.exotic) * 3;
    if (pingIndex < cursor) {
        return MiningCellMaterial::ExoticVein;
    }
    return MiningCellMaterial::ArtifactCache;
}

Color miningArtifactColor(int kind, int state)
{
    if (state == static_cast<int>(MiningArtifactState::Destroyed)) {
        return {0.48F, 0.18F, 0.14F, 0.86F};
    }
    if (state == static_cast<int>(MiningArtifactState::Delivered)) {
        return {0.42F, 1.0F, 0.72F, 0.92F};
    }
    if (kind == static_cast<int>(ArtifactKind::Story)) {
        return {1.0F, 0.82F, 0.28F, 0.96F};
    }
    return {0.58F, 0.88F, 1.0F, 0.94F};
}

int miningMaterialBucket(int material)
{
    switch (static_cast<MiningCellMaterial>(material)) {
    case MiningCellMaterial::Regolith:
    case MiningCellMaterial::CommonOre:
        return 0;
    case MiningCellMaterial::RareOre:
        return 1;
    case MiningCellMaterial::ExoticVein:
        return 2;
    case MiningCellMaterial::Empty:
    case MiningCellMaterial::HardRock:
    case MiningCellMaterial::HazardPocket:
    case MiningCellMaterial::ArtifactCache:
    case MiningCellMaterial::Bedrock:
        break;
    }
    return -1;
}

int miningDisplayMaterialForBucket(int bucket)
{
    if (bucket == 0) {
        return static_cast<int>(MiningCellMaterial::CommonOre);
    }
    if (bucket == 1) {
        return static_cast<int>(MiningCellMaterial::RareOre);
    }
    if (bucket == 2) {
        return static_cast<int>(MiningCellMaterial::ExoticVein);
    }
    return static_cast<int>(MiningCellMaterial::CommonOre);
}

float miningCellNoise(int x, int y, int salt)
{
    unsigned int n = static_cast<unsigned int>(x * 374761393 + y * 668265263 + salt * 2246822519U);
    n = (n ^ (n >> 13U)) * 1274126177U;
    return static_cast<float>((n ^ (n >> 16U)) & 1023U) / 1023.0F;
}

Color miningPressureColor(float oxygenPressure, float fuelPressure, float hazardPressure, float heatPressure)
{
    if (oxygenPressure > 0.0F) {
        return {1.0F, 0.14F, 0.08F, oxygenPressure};
    }
    if (fuelPressure > 0.0F) {
        return {1.0F, 0.62F, 0.16F, fuelPressure};
    }
    if (hazardPressure > 0.0F) {
        return {1.0F, 0.18F, 0.12F, hazardPressure};
    }
    if (heatPressure > 0.0F) {
        return {1.0F, 0.48F, 0.12F, heatPressure};
    }
    return {0.0F, 0.0F, 0.0F, 0.0F};
}

Color miningHeatSpriteTint(double miningHeat, double animationTime)
{
    const float heat = static_cast<float>(std::clamp((miningHeat - 0.35) / 0.65, 0.0, 1.0));
    if (heat <= 0.0F) {
        return {1.0F, 1.0F, 1.0F, 1.0F};
    }

    const float pulse = 0.86F + 0.14F * std::sin(static_cast<float>(animationTime) * (7.0F + heat * 5.0F));
    const float strain = heat * pulse;
    return {
        1.0F,
        1.0F - strain * 0.26F,
        1.0F - strain * 0.34F,
        1.0F
    };
}

int pickupDigitMask(char digit)
{
    switch (digit) {
    case '0':
        return 0b0111111;
    case '1':
        return 0b0000110;
    case '2':
        return 0b1011011;
    case '3':
        return 0b1001111;
    case '4':
        return 0b1100110;
    case '5':
        return 0b1101101;
    case '6':
        return 0b1111101;
    case '7':
        return 0b0000111;
    case '8':
        return 0b1111111;
    case '9':
        return 0b1101111;
    default:
        break;
    }
    return 0;
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

    if (snapshot.screen != Screen::Mining) {
        previousMiningActive_ = false;
        previousMiningWidth_ = 0;
        previousMiningHeight_ = 0;
        previousMiningMaterials_.clear();
        previousMiningInventory_ = {};
        previousMiningCargo_ = 0;
        miningPickupBursts_.clear();
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
    if (snapshot.screen == Screen::SurfaceScan) {
        drawSurfaceScan(snapshot);
        return;
    }
    if (snapshot.screen == Screen::SurfacePush) {
        drawSurfacePush(snapshot);
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
    const bool cameraShakeEnabled = rr_camera_shake_enabled() != 0;
    const float launchShake = cameraShakeEnabled ? static_cast<float>(std::clamp(snapshot.launchShake, 0.0, 1.0)) : 0.0F;
    if (launchShake > 0.0F) {
        const float shake = launchShake * launchShake;
        scenePixelCenterX_ += std::sin(static_cast<float>(snapshot.animationTime) * 72.0F) * shake * 7.0F;
        scenePixelCenterY_ += std::cos(static_cast<float>(snapshot.animationTime) * 61.0F) * shake * 5.0F;
    }
    if (cameraShakeEnabled && snapshot.screen == Screen::ArrivalFanfare) {
        const float arrival = 1.0F - static_cast<float>(std::clamp(snapshot.animationTime / tuning::session::arrivalFanfareSeconds, 0.0, 1.0));
        const float shimmer = arrival * arrival;
        scenePixelCenterX_ += std::sin(static_cast<float>(snapshot.animationTime) * 34.0F) * shimmer * 3.5F;
        scenePixelCenterY_ += std::cos(static_cast<float>(snapshot.animationTime) * 29.0F) * shimmer * 2.5F;
    }
    if (cameraShakeEnabled && snapshot.screen == Screen::Mining) {
        const float contactShake = static_cast<float>(std::clamp(snapshot.miningContactIntensity, 0.0, 1.0)) * (snapshot.miningDrilling ? 1.0F : 0.35F);
        const float failureShake = static_cast<float>(std::clamp(snapshot.miningFailurePulse, 0.0, 1.0));
        const float shake = contactShake * 1.8F + failureShake * 4.5F;
        scenePixelCenterX_ += std::sin(static_cast<float>(snapshot.animationTime) * 97.0F) * shake;
        scenePixelCenterY_ += std::cos(static_cast<float>(snapshot.animationTime) * 83.0F) * shake;
    }

    const float heat = static_cast<float>(std::clamp(snapshot.heat, 0.0, 1.0));
    const float arrivalGlow = snapshot.screen == Screen::ArrivalFanfare
        ? 0.018F * (1.0F - static_cast<float>(std::clamp(snapshot.animationTime / tuning::session::arrivalFanfareSeconds, 0.0, 1.0)))
        : 0.0F;
    glDisable(GL_SCISSOR_TEST);
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

void WebGLRenderer::drawRadialGlow(float cx, float cy, float radius, Color centerColor, int segments, bool worldSpace)
{
    Color edgeColor = centerColor;
    edgeColor.a = 0.0F;

    std::vector<float>& vertices = scratchVertices(static_cast<std::size_t>(segments) * 24);
    for (int i = 0; i < segments; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * kPi * 2.0F;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * kPi * 2.0F;
        pushVertex(vertices, cx, cy, centerColor);
        pushVertex(vertices, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, edgeColor);
        pushVertex(vertices, cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, edgeColor);
    }
    submit(vertices, 0x0004, false, 0, worldSpace);
}

void WebGLRenderer::drawMiningOreSparkle(float cx, float cy, float unitSize, int material, float animationTime, float phaseSeed, float alphaScale)
{
    drawMiningOreSparkleColor(cx, cy, unitSize, miningRewardGlowColor(material), animationTime, phaseSeed, alphaScale);
}

void WebGLRenderer::drawMiningOreSparkleColor(float cx, float cy, float unitSize, Color glow, float animationTime, float phaseSeed, float alphaScale)
{
    const float activeWindow = 0.42F;
    const float phase = std::fmod(animationTime * 1.35F + phaseSeed, 1.0F);
    if (phase > activeWindow) {
        return;
    }

    const float flare = 1.0F - phase / activeWindow;
    const float length = unitSize * (0.34F + flare * 0.44F);
    const float alpha = ((0.20F + flare * 0.44F) * alphaScale);
    drawLine(cx - length, cy, cx + length, cy, {glow.r, glow.g, glow.b, alpha}, 1.4F);
    drawLine(cx, cy - length, cx, cy + length, {glow.r, glow.g, glow.b, alpha}, 1.4F);
}

void WebGLRenderer::drawMiningPickupText(float cx, float cy, float unitSize, int material, int amount, float age)
{
    if (amount <= 0 || age < 0.0F || age > 1.05F) {
        return;
    }

    const float t = std::clamp(age / 1.05F, 0.0F, 1.0F);
    const float fade = (1.0F - t) * (1.0F - t);
    const float lift = unitSize * (0.55F + t * 1.75F);
    const float scale = unitSize * (0.72F + 0.18F * (1.0F - std::abs(t - 0.18F) / 0.18F));
    const std::string text = "+" + std::to_string(amount);
    const float glyphW = scale * 0.48F;
    const float glyphH = scale * 0.78F;
    const float gap = scale * 0.14F;
    const float totalW = static_cast<float>(text.size()) * glyphW + static_cast<float>(std::max(0, static_cast<int>(text.size()) - 1)) * gap;
    const float startX = cx - totalW * 0.5F;
    const float baseY = cy + lift;

    auto appendGlyph = [&](std::vector<float>& vertices, char ch, float x, float y, Color color) {
        auto add = [&](float ax, float ay, float bx, float by) {
            appendLine(vertices, x + ax * glyphW, y + ay * glyphH, x + bx * glyphW, y + by * glyphH, color);
        };
        if (ch == '+') {
            add(0.18F, 0.50F, 0.82F, 0.50F);
            add(0.50F, 0.22F, 0.50F, 0.78F);
            return;
        }
        const int mask = pickupDigitMask(ch);
        if ((mask & (1 << 0)) != 0) {
            add(0.20F, 0.92F, 0.80F, 0.92F);
        }
        if ((mask & (1 << 1)) != 0) {
            add(0.84F, 0.88F, 0.84F, 0.54F);
        }
        if ((mask & (1 << 2)) != 0) {
            add(0.84F, 0.46F, 0.84F, 0.12F);
        }
        if ((mask & (1 << 3)) != 0) {
            add(0.20F, 0.08F, 0.80F, 0.08F);
        }
        if ((mask & (1 << 4)) != 0) {
            add(0.16F, 0.46F, 0.16F, 0.12F);
        }
        if ((mask & (1 << 5)) != 0) {
            add(0.16F, 0.88F, 0.16F, 0.54F);
        }
        if ((mask & (1 << 6)) != 0) {
            add(0.20F, 0.50F, 0.80F, 0.50F);
        }
    };

    const Color glow = miningPickupGlowColor(material);
    std::vector<float>& shadowVertices = scratchVertices(text.size() * 14U);
    const Color shadow {0.005F, 0.010F, 0.012F, 0.62F * fade};
    for (std::size_t i = 0; i < text.size(); ++i) {
        appendGlyph(shadowVertices, text[i], startX + static_cast<float>(i) * (glyphW + gap) + unitSize * 0.045F, baseY - glyphH * 0.5F - unitSize * 0.045F, shadow);
    }
    submitLines(shadowVertices, 3.5F);

    std::vector<float>& textVertices = scratchVertices(text.size() * 14U);
    const Color color {glow.r, glow.g, glow.b, 0.88F * fade};
    for (std::size_t i = 0; i < text.size(); ++i) {
        appendGlyph(textVertices, text[i], startX + static_cast<float>(i) * (glyphW + gap), baseY - glyphH * 0.5F, color);
    }
    submitLines(textVertices, 2.0F);
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

    drawCurveOffset(0.0F, {0.74F, 0.86F, 0.92F, 0.18F}, 1.2F);
    drawCurveOffset(-goodBand, {0.18F, 0.78F, 1.0F, 0.30F}, 2.0F);
    drawCurveOffset(goodBand, {0.18F, 0.78F, 1.0F, 0.30F}, 2.0F);
    drawCurveOffset(-perfectBand, {1.0F, 0.82F, 0.28F, 0.38F + pulse * 0.06F}, 2.4F);
    drawCurveOffset(perfectBand, {1.0F, 0.82F, 0.28F, 0.38F + pulse * 0.06F}, 2.4F);

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
    drawRadialGlow(destX, destY, planetRadius * (1.46F + pulse * 0.05F), {0.01F, 0.22F, 0.36F, 0.14F}, 72);
    drawEllipseLine(destX, destY, planetRadius * (1.28F + pulse * 0.03F), planetRadius * (1.28F + pulse * 0.03F), {0.12F, 0.66F, 0.86F, 0.34F}, 72, 0.0F, 2.0F * kPi);

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

    const int zone = snapshot.flybyCompleted ? snapshot.flybyResult : snapshot.flybyZone;
    const bool perfectZone = snapshot.flybyCompleted ? zone >= 3 : zone >= 2;
    const bool goodZone = snapshot.flybyCompleted ? zone >= 2 : zone >= 1;
    const Color zoneGlow = perfectZone
        ? Color{0.92F, 0.42F, 0.04F, 0.18F}
        : (goodZone ? Color{0.02F, 0.28F, 0.46F, 0.16F} : Color{0.32F, 0.0F, 0.035F, 0.24F});
    const Color zoneRing = perfectZone
        ? Color{1.0F, 0.76F, 0.22F, 0.54F}
        : (goodZone ? Color{0.22F, 0.86F, 1.0F, 0.46F} : Color{0.78F, 0.04F, 0.04F, 0.52F});
    drawRadialGlow(shipX, shipY, 0.078F + pulse * 0.008F, zoneGlow, 42);
    drawEllipseLine(shipX, shipY, 0.052F + pulse * 0.005F, 0.052F + pulse * 0.005F, zoneRing, 42, 0.0F, 2.0F * kPi);

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
            ? Color{0.92F, 0.44F, 0.04F, 0.18F}
            : (snapshot.flybyResult >= 2 ? Color{0.02F, 0.28F, 0.46F, 0.16F} : Color{0.34F, 0.0F, 0.025F, 0.26F});
        const Color resultRing = snapshot.flybyResult >= 3
            ? Color{1.0F, 0.76F, 0.22F, 0.50F}
            : (snapshot.flybyResult >= 2 ? Color{0.22F, 0.82F, 1.0F, 0.42F} : Color{0.82F, 0.04F, 0.035F, 0.54F});
        drawRadialGlow(shipX, shipY, 0.128F + pulse * 0.012F, resultColor, 64);
        drawEllipseLine(shipX, shipY, 0.092F + pulse * 0.008F, 0.092F + pulse * 0.008F, resultRing, 64, 0.0F, 2.0F * kPi);
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

    drawEllipseLine(0.0F, 0.0F, targetRadius, targetRadius, {0.74F, 0.86F, 0.92F, 0.18F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius - goodBand, targetRadius - goodBand, {0.18F, 0.78F, 1.0F, 0.30F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius + goodBand, targetRadius + goodBand, {0.18F, 0.78F, 1.0F, 0.30F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius - perfectBand, targetRadius - perfectBand, {1.0F, 0.80F, 0.24F, 0.38F + pulse * 0.08F}, 128, 0.0F, 2.0F * kPi);
    drawEllipseLine(0.0F, 0.0F, targetRadius + perfectBand, targetRadius + perfectBand, {1.0F, 0.80F, 0.24F, 0.38F + pulse * 0.08F}, 128, 0.0F, 2.0F * kPi);

    const float progress = static_cast<float>(std::clamp(snapshot.orbitProgress, 0.0, 1.0));
    if (progress > 0.0F) {
        const float startAngle = static_cast<float>(tuning::orbit::startAngleRadians);
        drawEllipseLine(0.0F, 0.0F, targetRadius, targetRadius, {0.98F, 0.96F, 0.82F, 0.74F}, 128, startAngle, startAngle + progress * 2.0F * kPi);
    }

    drawRadialGlow(0.0F, 0.0F, planetRadius * (1.46F + pulse * 0.05F), {0.01F, 0.20F, 0.34F, 0.12F}, 88);
    drawEllipseLine(0.0F, 0.0F, planetRadius * (1.26F + pulse * 0.03F), planetRadius * (1.26F + pulse * 0.03F), {0.12F, 0.64F, 0.84F, 0.30F}, 88, 0.0F, 2.0F * kPi);

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
        ? Color{0.92F, 0.42F, 0.04F, 0.17F}
        : (goodZone ? Color{0.02F, 0.26F, 0.44F, 0.14F} : Color{0.32F, 0.0F, 0.035F, 0.22F});
    const Color zoneRing = perfectZone
        ? Color{1.0F, 0.76F, 0.22F, 0.50F}
        : (goodZone ? Color{0.22F, 0.84F, 1.0F, 0.42F} : Color{0.78F, 0.04F, 0.04F, 0.50F});
    drawRadialGlow(shipX, shipY, 0.074F + pulse * 0.008F, zoneGlow, 42);
    drawEllipseLine(shipX, shipY, 0.050F + pulse * 0.004F, 0.050F + pulse * 0.004F, zoneRing, 42, 0.0F, 2.0F * kPi);

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
            ? Color{0.92F, 0.44F, 0.04F, 0.17F}
            : (snapshot.orbitResult >= 2 ? Color{0.02F, 0.28F, 0.46F, 0.15F} : Color{0.34F, 0.0F, 0.025F, 0.24F});
        const Color resultRing = snapshot.orbitResult >= 3
            ? Color{1.0F, 0.76F, 0.22F, 0.48F}
            : (snapshot.orbitResult >= 2 ? Color{0.22F, 0.82F, 1.0F, 0.40F} : Color{0.82F, 0.04F, 0.035F, 0.50F});
        drawRadialGlow(shipX, shipY, 0.122F + pulse * 0.012F, resultColor, 64);
        drawEllipseLine(shipX, shipY, 0.086F + pulse * 0.007F, 0.086F + pulse * 0.007F, resultRing, 64, 0.0F, 2.0F * kPi);
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
    const float cellSize = std::min(cellW, cellH);
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

    const float oxygenPressure = std::clamp((12.0F - static_cast<float>(snapshot.miningOxygenSeconds)) / 12.0F, 0.0F, 1.0F);
    const float fuelUnitSeconds = static_cast<float>(tuning::mining::fuelSecondsPerUnit);
    const float fuelPressure = snapshot.miningSharedFuel <= 0
        ? 1.0F
        : std::clamp(static_cast<float>(snapshot.miningFuelBurnSeconds) / std::max(1.0F, fuelUnitSeconds), 0.0F, 1.0F);
    const float integrityPressure = std::clamp((1.0F - static_cast<float>(snapshot.miningDrillIntegrity)) * 1.1F, 0.0F, 1.0F);
    const float hazardPressure = std::max(std::clamp(static_cast<float>(snapshot.miningHazardDelta) * 2.2F, 0.0F, 1.0F), integrityPressure);
    const float heatPressure = std::clamp(static_cast<float>(snapshot.miningHeat), 0.0F, 1.0F);
    const Color pressureColor = miningPressureColor(
        oxygenPressure * 0.050F,
        fuelPressure > 0.84F ? (fuelPressure - 0.84F) * 0.080F : 0.0F,
        hazardPressure * 0.045F,
        heatPressure > 0.86F ? (heatPressure - 0.86F) * 0.060F : 0.0F);
    const float warningBeat = 0.78F + 0.22F * std::sin(static_cast<float>(snapshot.animationTime) * 6.0F);
    const float scannerPulse = static_cast<float>(std::clamp(snapshot.miningScannerPulse / kMiningScannerPulseSeconds, 0.0, 1.0));
    const float scannerRevealRadiusCells = std::max(kMiningLightRadiusCells, static_cast<float>(std::max(0.0, snapshot.miningScannerRadius)));
    const float scannerSweepStartCells = std::min(scannerRevealRadiusCells, kMiningLightRadiusCells + 0.35F);
    const float scannerSweepRadiusCells = scannerSweepStartCells + (scannerRevealRadiusCells - scannerSweepStartCells) * (1.0F - scannerPulse);
    auto scannerSweepBoost = [&](float distCells, float widthBase) {
        if (scannerPulse <= 0.0F || scannerRevealRadiusCells <= 0.0F) {
            return 0.0F;
        }
        const float ringWidth = widthBase + scannerPulse * 0.45F;
        return std::clamp(1.0F - std::abs(distCells - scannerSweepRadiusCells) / std::max(0.1F, ringWidth), 0.0F, 1.0F) * scannerPulse;
    };

    const int cellCount = snapshot.miningWidth * snapshot.miningHeight;
    std::vector<int> currentMiningMaterials(static_cast<std::size_t>(std::max(0, cellCount)), static_cast<int>(MiningCellMaterial::Empty));
    for (const MiningCellSnapshot& cell : snapshot.miningCells) {
        const int index = cell.y * snapshot.miningWidth + cell.x;
        if (index >= 0 && index < cellCount) {
            currentMiningMaterials[static_cast<std::size_t>(index)] = cell.material;
        }
    }
    if (previousMiningActive_ && previousMiningWidth_ == snapshot.miningWidth && previousMiningHeight_ == snapshot.miningHeight) {
        struct PickupCandidate {
            Vec2 center;
            int bucket = -1;
        };
        std::vector<PickupCandidate> candidates;
        candidates.reserve(6);
        for (const MiningCellSnapshot& cell : snapshot.miningCells) {
            const int index = cell.y * snapshot.miningWidth + cell.x;
            if (index < 0 || index >= cellCount || static_cast<std::size_t>(index) >= previousMiningMaterials_.size()) {
                continue;
            }
            const int previousMaterial = previousMiningMaterials_[static_cast<std::size_t>(index)];
            const int bucket = miningMaterialBucket(previousMaterial);
            if (bucket >= 0 && cell.material == static_cast<int>(MiningCellMaterial::Empty)) {
                const Vec2 burstCenter = cellCenter(static_cast<double>(cell.x), static_cast<double>(cell.y));
                candidates.push_back({burstCenter, bucket});
            }
        }

        int remaining[3] = {
            std::max(0, snapshot.miningMaterials.common - previousMiningInventory_.common),
            std::max(0, snapshot.miningMaterials.rare - previousMiningInventory_.rare),
            std::max(0, snapshot.miningMaterials.exotic - previousMiningInventory_.exotic)
        };
        int remainingCandidates[3] = {};
        for (const PickupCandidate& candidate : candidates) {
            if (candidate.bucket >= 0 && candidate.bucket < 3) {
                ++remainingCandidates[candidate.bucket];
            }
        }
        int remainingCargo = std::max(0, snapshot.miningCargo - previousMiningCargo_);
        int remainingCargoCandidates = static_cast<int>(candidates.size());
        auto trimPickupBursts = [&]() {
            if (miningPickupBursts_.size() > 44U) {
                miningPickupBursts_.erase(miningPickupBursts_.begin(), miningPickupBursts_.begin() + static_cast<std::ptrdiff_t>(miningPickupBursts_.size() - 44U));
            }
        };
        for (const PickupCandidate& candidate : candidates) {
            if (remainingCargo <= 0 || remainingCargoCandidates <= 0) {
                continue;
            }
            const int amount = std::max(1, (remainingCargo + remainingCargoCandidates - 1) / remainingCargoCandidates);
            const int clampedAmount = std::min(amount, remainingCargo);
            remainingCargo -= clampedAmount;
            --remainingCargoCandidates;
            miningPickupBursts_.push_back({
                candidate.center.x,
                candidate.center.y,
                kMiningPickupCargoMaterial,
                clampedAmount,
                snapshot.animationTime,
                -cellW * 0.34F
            });
            trimPickupBursts();
        }
        for (const PickupCandidate& candidate : candidates) {
            if (candidate.bucket < 0 || candidate.bucket >= 3 || remaining[candidate.bucket] <= 0 || remainingCandidates[candidate.bucket] <= 0) {
                continue;
            }
            const int amount = std::max(1, (remaining[candidate.bucket] + remainingCandidates[candidate.bucket] - 1) / remainingCandidates[candidate.bucket]);
            const int clampedAmount = std::min(amount, remaining[candidate.bucket]);
            remaining[candidate.bucket] -= clampedAmount;
            --remainingCandidates[candidate.bucket];
            miningPickupBursts_.push_back({
                candidate.center.x,
                candidate.center.y,
                miningDisplayMaterialForBucket(candidate.bucket),
                clampedAmount,
                snapshot.animationTime,
                cellW * 0.34F
            });
            trimPickupBursts();
        }
    }
    previousMiningActive_ = true;
    previousMiningWidth_ = snapshot.miningWidth;
    previousMiningHeight_ = snapshot.miningHeight;
    previousMiningMaterials_ = std::move(currentMiningMaterials);
    previousMiningInventory_ = snapshot.miningMaterials;
    previousMiningCargo_ = snapshot.miningCargo;

    drawRect((left + right) * 0.5F, (top + bottom) * 0.5F, right - left + 0.035F, top - bottom + 0.035F, {0.010F, 0.014F, 0.016F, 1.0F});

    std::vector<float>& strataVertices = scratchVertices(512);
    for (int i = 0; i < 13; ++i) {
        const float y = top - (static_cast<float>(i) + 0.35F) * ((top - bottom) / 13.0F);
        const float drift = std::sin(static_cast<float>(snapshot.animationTime) * 0.16F + static_cast<float>(i) * 1.7F) * cellH * 0.18F;
        const float alpha = 0.035F + 0.020F * miningCellNoise(i, snapshot.destinationTier, 5);
        appendRect(strataVertices, (left + right) * 0.5F + drift, y, right - left, cellH * 0.32F, {0.16F, 0.20F, 0.18F, alpha});
    }
    for (int i = 0; i < 54; ++i) {
        const float nx = miningCellNoise(i, snapshot.destinationTier, 11);
        const float ny = miningCellNoise(i, snapshot.destinationTier, 23);
        const float wobble = std::sin(static_cast<float>(snapshot.animationTime) * 0.55F + static_cast<float>(i)) * cellW * 0.18F;
        const float x = left + nx * (right - left) + wobble;
        const float y = bottom + ny * (top - bottom);
        const float size = cellSize * (0.030F + miningCellNoise(i, snapshot.destinationTier, 31) * 0.035F);
        appendRect(strataVertices, x, y, size, size, {0.78F, 0.86F, 0.80F, 0.050F + miningCellNoise(i, snapshot.destinationTier, 41) * 0.035F});
    }
    submit(strataVertices, 0x0004);

    const Vec2 shipBay = cellCenter(static_cast<double>(snapshot.miningWidth) * 0.5 - 0.5, tuning::mining::artifactShipBayY - 0.5);
    drawRect(shipBay.x, shipBay.y, cellW * 6.2F, cellH * 1.55F, {0.10F, 0.18F, 0.22F, 0.78F});
    drawLine(shipBay.x - cellW * 3.0F, shipBay.y - cellH * 0.82F, shipBay.x + cellW * 3.0F, shipBay.y - cellH * 0.82F, {0.46F, 0.90F, 1.0F, 0.46F}, 2.0F);
    drawLine(shipBay.x - cellW * 3.0F, shipBay.y + cellH * 0.82F, shipBay.x + cellW * 3.0F, shipBay.y + cellH * 0.82F, {0.46F, 0.90F, 1.0F, 0.30F}, 1.5F);

    std::vector<float>& terrainVertices = scratchVertices(snapshot.miningCells.size() * 48U);
    for (const MiningCellSnapshot& cell : snapshot.miningCells) {
        const Vec2 center = cellCenter(static_cast<double>(cell.x), static_cast<double>(cell.y));
        const float dxCells = static_cast<float>(static_cast<double>(cell.x) + 0.5 - snapshot.miningDroneX);
        const float dyCells = static_cast<float>(static_cast<double>(cell.y) + 0.5 - snapshot.miningDroneY);
        const float distCells = std::sqrt(dxCells * dxCells + dyCells * dyCells);
        float localLight = std::clamp(1.0F - distCells / kMiningLightRadiusCells, 0.0F, 1.0F) * 0.20F;
        localLight = std::max(localLight, scannerSweepBoost(distCells, 0.85F) * 0.032F);
        const Color color = miningMaterialColor(cell.material, static_cast<float>(cell.integrity), cell.revealed, cell.hazard && cell.revealed, snapshot.destinationTier, localLight);
        appendRect(terrainVertices, center.x, center.y, cellW * 0.96F, cellH * 0.96F, color);
    }
    submit(terrainVertices, 0x0004);

    std::vector<float>& edgeGlowVertices = scratchVertices(snapshot.miningCells.size() * 32U);
    for (const MiningCellSnapshot& cell : snapshot.miningCells) {
        if (!cell.revealed || cell.material != static_cast<int>(MiningCellMaterial::Empty)) {
            continue;
        }
        const int x = cell.x;
        const int y = cell.y;
        const Vec2 center = cellCenter(static_cast<double>(x), static_cast<double>(y));
        const int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& offset : offsets) {
            const int nx = x + offset[0];
            const int ny = y + offset[1];
            const int index = ny * snapshot.miningWidth + nx;
            if (nx < 0 || nx >= snapshot.miningWidth || ny < 0 || ny >= snapshot.miningHeight || index < 0 || static_cast<std::size_t>(index) >= previousMiningMaterials_.size()) {
                continue;
            }
            const int neighborMaterial = previousMiningMaterials_[static_cast<std::size_t>(index)];
            if (!miningScannerPingMaterial(neighborMaterial)) {
                continue;
            }
            const Color glow = miningRewardMaterial(neighborMaterial)
                ? miningRewardGlowColor(neighborMaterial)
                : Color{0.30F, 0.50F, 0.58F, 1.0F};
            const float edgeX = center.x + static_cast<float>(offset[0]) * cellW * 0.48F;
            const float edgeY = center.y - static_cast<float>(offset[1]) * cellH * 0.48F;
            const float w = offset[0] == 0 ? cellW * 0.84F : cellW * 0.055F;
            const float h = offset[1] == 0 ? cellH * 0.84F : cellH * 0.055F;
            appendRect(edgeGlowVertices, edgeX, edgeY, w, h, {glow.r, glow.g, glow.b, miningRewardMaterial(neighborMaterial) ? 0.22F : 0.10F});
        }
    }
    submit(edgeGlowVertices, 0x0004);

    std::vector<float>& oreGlowVertices = scratchVertices(snapshot.miningCells.size() * 48U);
    for (const MiningCellSnapshot& cell : snapshot.miningCells) {
        if (!cell.revealed) {
            continue;
        }
        const Vec2 center = cellCenter(static_cast<double>(cell.x), static_cast<double>(cell.y));
        const float dxCells = static_cast<float>(static_cast<double>(cell.x) + 0.5 - snapshot.miningDroneX);
        const float dyCells = static_cast<float>(static_cast<double>(cell.y) + 0.5 - snapshot.miningDroneY);
        const float distCells = std::sqrt(dxCells * dxCells + dyCells * dyCells);
        const float scannerBoost = scannerSweepBoost(distCells, 0.78F);
        const bool rewardCell = miningRewardMaterial(cell.material);
        const bool scannerPing = scannerBoost > 0.04F && miningScannerPingMaterial(cell.material);
        if (!rewardCell && !scannerPing) {
            continue;
        }
        const Color glow = rewardCell ? miningRewardGlowColor(cell.material) : miningScannerPingColor(cell.material);
        const float integrity = static_cast<float>(std::clamp(cell.integrity, 0.0, 1.0));
        const float cracked = 1.0F - integrity;
        const float shimmer = 0.5F + 0.5F * std::sin(static_cast<float>(snapshot.animationTime) * 5.8F + static_cast<float>(cell.x * 13 + cell.y * 7));
        const float baseSize = rewardCell ? 0.34F : 0.24F;
        const float size = cellSize * (baseSize + cracked * 0.22F + scannerBoost * 0.42F);
        const float baseAlpha = rewardCell ? 0.18F + shimmer * 0.18F : 0.04F;
        const float alpha = baseAlpha + (rewardCell ? cracked * 0.16F : 0.0F) + scannerBoost * (rewardCell ? 0.34F : 0.42F);
        appendRect(oreGlowVertices, center.x, center.y, size, size, {glow.r, glow.g, glow.b, std::min(alpha, 0.78F)});
    }
    submit(oreGlowVertices, 0x0004);

    std::vector<float>& oreSparkVertices = scratchVertices(snapshot.miningCells.size() * 32U);
    for (const MiningCellSnapshot& cell : snapshot.miningCells) {
        if (!cell.revealed) {
            continue;
        }
        const Vec2 center = cellCenter(static_cast<double>(cell.x), static_cast<double>(cell.y));
        const float dxCells = static_cast<float>(static_cast<double>(cell.x) + 0.5 - snapshot.miningDroneX);
        const float dyCells = static_cast<float>(static_cast<double>(cell.y) + 0.5 - snapshot.miningDroneY);
        const float distCells = std::sqrt(dxCells * dxCells + dyCells * dyCells);
        const float scannerBoost = scannerSweepBoost(distCells, 0.78F);
        const bool rewardCell = miningRewardMaterial(cell.material);
        const bool scannerPing = scannerBoost > 0.18F && miningScannerPingMaterial(cell.material);
        if (!rewardCell && !scannerPing) {
            continue;
        }
        const float phase = std::fmod(
            static_cast<float>(snapshot.animationTime) * 1.35F + static_cast<float>(cell.x) * 0.17F + static_cast<float>(cell.y) * 0.11F,
            1.0F);
        const float activeWindow = scannerPing ? 0.72F : 0.42F;
        if (phase > activeWindow) {
            continue;
        }
        const Color glow = rewardCell ? miningRewardGlowColor(cell.material) : miningScannerPingColor(cell.material);
        const float flare = std::max(1.0F - phase / activeWindow, scannerPing ? scannerBoost : 0.0F);
        const float length = cellSize * ((rewardCell ? 0.34F : 0.24F) + flare * 0.44F);
        const float alpha = (rewardCell ? 0.20F : 0.08F) + flare * (rewardCell ? 0.44F : 0.34F);
        appendLine(oreSparkVertices, center.x - length, center.y, center.x + length, center.y, {glow.r, glow.g, glow.b, alpha});
        appendLine(oreSparkVertices, center.x, center.y - length, center.x, center.y + length, {glow.r, glow.g, glow.b, alpha});
    }
    submitLines(oreSparkVertices, 1.4F);

    std::vector<float>& pickupVertices = scratchVertices(miningPickupBursts_.size() * 360U);
    std::vector<MiningPickupBurst> activeBursts;
    activeBursts.reserve(miningPickupBursts_.size());
    for (const MiningPickupBurst& burst : miningPickupBursts_) {
        const float age = static_cast<float>(snapshot.animationTime - burst.startedAt);
        if (age < 0.0F || age > 1.05F) {
            continue;
        }
        activeBursts.push_back(burst);
        const float t = std::clamp(age / 0.82F, 0.0F, 1.0F);
        const float fade = (1.0F - t) * (1.0F - t);
        const Color glow = miningPickupGlowColor(burst.material);
        for (int i = 0; i < 9; ++i) {
            const float seed = miningCellNoise(static_cast<int>(burst.x * 1000.0F), static_cast<int>(burst.y * 1000.0F), i + burst.material * 7);
            const float angle = seed * kPi * 2.0F + static_cast<float>(i) * 0.63F;
            const float spray = cellSize * (0.28F + t * (1.15F + seed * 0.82F));
            const float arcX = std::cos(angle) * spray;
            const float arcY = std::sin(angle) * spray + cellSize * std::sin(t * kPi) * (0.22F + seed * 0.26F);
            const float px = burst.x + arcX;
            const float py = burst.y + arcY;
            if (i < 4) {
                appendLine(pickupVertices, burst.x, burst.y, px, py, {glow.r, glow.g, glow.b, 0.16F * fade});
            }
            const float size = cellSize * (0.10F + seed * 0.075F) * (0.80F + fade);
            appendRect(pickupVertices, px, py, size, size, {glow.r, glow.g, glow.b, 0.42F * fade});
        }
        appendRect(pickupVertices, burst.x, burst.y, cellSize * (0.72F + t * 1.18F), cellSize * (0.72F + t * 1.18F), {glow.r, glow.g, glow.b, 0.075F * fade});
    }
    submit(pickupVertices, 0x0004);
    for (const MiningPickupBurst& burst : activeBursts) {
        const float age = static_cast<float>(snapshot.animationTime - burst.startedAt);
        drawMiningPickupText(burst.x + burst.textOffsetX, burst.y, cellSize, burst.material, burst.amount, age);
    }
    miningPickupBursts_ = std::move(activeBursts);

    for (const MiningEnemySnapshot& enemy : snapshot.miningEnemies) {
        if (!enemy.active) {
            continue;
        }
        const Vec2 enemyCenter = cellCenter(enemy.x, enemy.y);
        const float health = static_cast<float>(std::clamp(enemy.maxHealth <= 0.0 ? 1.0 : enemy.health / enemy.maxHealth, 0.0, 1.0));
        const Color base = miningEnemyColor(enemy.type, enemy.affinity);
        if (enemy.effectRadius > 0.0) {
            drawCircle(enemyCenter.x, enemyCenter.y, static_cast<float>(enemy.effectRadius) * std::min(cellW, cellH), {base.r, base.g, base.b, 0.075F}, 28);
        }
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
    if (snapshot.miningArtifact.present && (snapshot.miningArtifact.revealed || snapshot.miningArtifact.state != static_cast<int>(MiningArtifactState::Embedded))) {
        const Vec2 artifact = cellCenter(snapshot.miningArtifact.x - 0.5, snapshot.miningArtifact.y - 0.5);
        const Color artifactColor = miningArtifactColor(snapshot.miningArtifact.kind, snapshot.miningArtifact.state);
        if (snapshot.miningArtifact.tethered) {
            drawLine(drone.x, drone.y, artifact.x, artifact.y, {0.62F, 0.92F, 1.0F, 0.58F}, 2.0F);
            drawRadialGlow(artifact.x, artifact.y, cellSize * 1.8F, {0.52F, 0.92F, 1.0F, 0.032F}, 24);
        }
        const float statePulse = snapshot.miningArtifact.state == static_cast<int>(MiningArtifactState::Delivered)
            ? 0.35F + 0.18F * std::sin(static_cast<float>(snapshot.animationTime) * 12.0F)
            : 0.0F;
        drawCircle(artifact.x, artifact.y, cellSize * (0.72F + statePulse), {artifactColor.r, artifactColor.g, artifactColor.b, artifactColor.a}, 18);
        drawCircle(artifact.x, artifact.y, cellSize * 0.34F, {1.0F, 1.0F, 0.86F, snapshot.miningArtifact.state == static_cast<int>(MiningArtifactState::Destroyed) ? 0.20F : 0.82F}, 14);
        const float health = static_cast<float>(std::clamp(snapshot.miningArtifact.maxHealth <= 0.0 ? 0.0 : snapshot.miningArtifact.health / snapshot.miningArtifact.maxHealth, 0.0, 1.0));
        drawRect(artifact.x, artifact.y - cellH * 0.92F, cellW * 1.65F, cellH * 0.12F, {0.12F, 0.04F, 0.04F, 0.76F});
        drawRect(artifact.x - cellW * 0.825F * (1.0F - health), artifact.y - cellH * 0.92F, cellW * 1.65F * health, cellH * 0.12F, {0.34F + (1.0F - health) * 0.66F, 0.95F * health, 0.24F, 0.90F});
    }
    if (snapshot.miningScannerPulse > 0.0) {
        const float revealRadiusX = scannerRevealRadiusCells * cellW;
        const float revealRadiusY = scannerRevealRadiusCells * cellH;
        const float sweepRadiusX = scannerSweepRadiusCells * cellW;
        const float sweepRadiusY = scannerSweepRadiusCells * cellH;
        drawEllipseLine(drone.x, drone.y, revealRadiusX, revealRadiusY, {1.0F, 0.82F, 0.38F, 0.12F * scannerPulse}, 72, 0.0F, 2.0F * kPi);
        drawEllipseLine(drone.x, drone.y, sweepRadiusX, sweepRadiusY, {0.36F, 0.92F, 1.0F, 0.12F * scannerPulse}, 64, 0.0F, 2.0F * kPi);
        drawEllipseLine(drone.x, drone.y, revealRadiusX * 0.58F, revealRadiusY * 0.58F, {1.0F, 0.64F, 0.22F, 0.035F * scannerPulse}, 64, 0.0F, 2.0F * kPi);
        std::vector<float>& scannerGridVertices = scratchVertices(384);
        const int sweepCells = static_cast<int>(std::ceil(scannerRevealRadiusCells));
        for (int i = -sweepCells; i <= sweepCells; ++i) {
            if (i % 2 != 0) {
                continue;
            }
            const float gx = drone.x + static_cast<float>(i) * cellW;
            const float gy = drone.y + static_cast<float>(i) * cellH;
            const float lineExtentCells = std::sqrt(std::max(0.0F, scannerRevealRadiusCells * scannerRevealRadiusCells - static_cast<float>(i * i)));
            if (gx >= left && gx <= right && lineExtentCells > 0.0F) {
                const float yExtent = lineExtentCells * cellH;
                appendLine(scannerGridVertices, gx, std::max(bottom, drone.y - yExtent), gx, std::min(top, drone.y + yExtent), {0.40F, 0.92F, 1.0F, 0.028F * scannerPulse});
            }
            if (gy >= bottom && gy <= top && lineExtentCells > 0.0F) {
                const float xExtent = lineExtentCells * cellW;
                appendLine(scannerGridVertices, std::max(left, drone.x - xExtent), gy, std::min(right, drone.x + xExtent), gy, {0.40F, 0.92F, 1.0F, 0.028F * scannerPulse});
            }
        }
        submitLines(scannerGridVertices, 1.0F);
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
    const Color heatTint = miningHeatSpriteTint(snapshot.miningHeat, snapshot.animationTime);

    if (textureReady(MiningDroneAsset)) {
        drawSpriteRotated(
            drone.x,
            drone.y,
            droneSize,
            droneSize,
            -drillDirection.x,
            -drillDirection.y,
            heatTint,
            MiningDroneAsset);
    } else {
        drawCircle(drone.x, drone.y, cellW * 1.15F, {0.10F * heatTint.r, 0.14F * heatTint.g, 0.18F * heatTint.b, 1.0F}, 24);
        drawCircle(drone.x, drone.y, cellW * 0.72F, {0.28F * heatTint.r, 0.82F * heatTint.g, 0.98F * heatTint.b, 1.0F}, 20);
        drawRect(drone.x, drone.y - cellH * 0.95F, cellW * 1.0F, cellH * 0.42F, {0.82F * heatTint.r, 0.88F * heatTint.g, 0.92F * heatTint.b, 1.0F});
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
            heatTint,
            DrillBitAsset,
            drillFrame,
            6);
    }

    if (snapshot.miningTargetDrillable && (snapshot.miningDrilling || snapshot.miningContactIntensity > 0.12)) {
        const float contact = static_cast<float>(std::clamp(snapshot.miningContactIntensity, 0.0, 1.0));
        const float crackAlpha = 0.20F + contact * 0.46F;
        std::vector<float>& crackVertices = scratchVertices(96);
        for (int i = 0; i < 8; ++i) {
            const float seed = miningCellNoise(static_cast<int>(snapshot.miningTargetX), static_cast<int>(snapshot.miningTargetY), i + 61);
            const float angle = static_cast<float>(i) * 0.78F + seed * 0.38F + static_cast<float>(snapshot.animationTime) * 0.10F;
            const float inner = cellSize * (0.13F + seed * 0.08F);
            const float outer = cellSize * (0.45F + contact * 0.44F + seed * 0.34F);
            appendLine(
                crackVertices,
                particleAnchor.x + std::cos(angle) * inner,
                particleAnchor.y + std::sin(angle) * inner,
                particleAnchor.x + std::cos(angle) * outer,
                particleAnchor.y + std::sin(angle) * outer,
                {1.0F, 0.74F, 0.32F, crackAlpha * (0.55F + seed * 0.45F)});
        }
        submitLines(crackVertices, 1.5F + contact * 1.2F);
    }

    if (snapshot.miningDrilling || snapshot.miningFailurePulse > 0.0) {
        const int failureBurst = snapshot.miningFailurePulse > 0.0 ? 18 : 0;
        const int particleCount = 12 + failureBurst + static_cast<int>(std::round(snapshot.miningContactIntensity * 18.0));
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
            const float size = cellSize * (0.16F + miningCellNoise(i, static_cast<int>(snapshot.animationTime * 10.0), 73) * 0.15F);
            appendRect(particleVertices, px, py, size, size, spark);
        }
        submit(particleVertices, 0x0004);
    }

    if (pressureColor.a > 0.001F) {
        const float alpha = pressureColor.a * (0.65F + warningBeat * 0.35F);
        drawLine(left, top, right, top, {pressureColor.r, pressureColor.g, pressureColor.b, 0.024F * alpha}, 1.1F);
        drawLine(left, bottom, right, bottom, {pressureColor.r, pressureColor.g, pressureColor.b, 0.020F * alpha}, 1.1F);
        drawLine(left, bottom, left, top, {pressureColor.r, pressureColor.g, pressureColor.b, 0.016F * alpha}, 1.1F);
        drawLine(right, bottom, right, top, {pressureColor.r, pressureColor.g, pressureColor.b, 0.016F * alpha}, 1.1F);
    }
}

void WebGLRenderer::drawSurfaceScan(const RenderSnapshot& snapshot)
{
    drawBackdrop(snapshot);
    const Vec2 destination = routePoint(snapshot, 1.0F);
    const float time = static_cast<float>(snapshot.animationTime);
    const float signal = static_cast<float>(std::clamp(snapshot.surfaceScanSignal, 0.0, 1.0));
    const float interference = static_cast<float>(std::clamp(snapshot.surfaceScanInterference, 0.0, 1.0));
    const float risk = static_cast<float>(std::clamp(snapshot.surfaceScanBustRisk, 0.0, 1.0));
    const float baseRadius = 0.16F + static_cast<float>(snapshot.destinationTier) * 0.012F;
    const float surfaceRadius = snapshot.destinationTier == 1
        ? 0.104F
        : std::min(baseRadius * 0.72F, 0.098F + static_cast<float>(snapshot.destinationTier) * 0.007F);
    const float sweep = std::fmod(time * 0.38F + signal * 0.18F, 1.0F) * 2.0F * kPi;
    const int maxScanLayers = std::max(1, snapshot.surfaceScanMaxPulses);
    auto scanLayerRadiusScale = [&](int layer) {
        const float depthT = maxScanLayers <= 1
            ? 0.0F
            : static_cast<float>(std::clamp(layer, 0, maxScanLayers - 1)) / static_cast<float>(maxScanLayers - 1);
        return 0.24F + 0.68F * (1.0F - depthT);
    };

    if (snapshot.surfaceScanPulses > 0) {
        const int scannedLayerCount = std::clamp(snapshot.surfaceScanPulses, 1, maxScanLayers);
        const int activeScanLayer = scannedLayerCount - 1;
        for (int layer = 0; layer < scannedLayerCount; ++layer) {
            const bool active = layer == activeScanLayer;
            const float radiusX = surfaceRadius * scanLayerRadiusScale(layer);
            const float radiusY = radiusX * 0.72F;
            const float alpha = active
                ? 0.34F + signal * 0.16F
                : std::max(0.10F, 0.20F - static_cast<float>(activeScanLayer - layer) * 0.035F);
            const Color ring = active
                ? Color{1.0F, 0.74F, 0.20F, alpha}
                : Color{0.36F, 0.94F, 1.0F, alpha};
            drawEllipseLine(destination.x, destination.y, radiusX, radiusY, ring, 56, 0.0F, 2.0F * kPi);
        }

        const float start = sweep + static_cast<float>(activeScanLayer) * 0.42F;
        const float end = start + (0.22F + signal * 0.16F);
        const float radiusX = surfaceRadius * scanLayerRadiusScale(activeScanLayer);
        const float radiusY = radiusX * 0.72F;
        drawEllipseLine(destination.x, destination.y, radiusX, radiusY, {0.36F, 0.94F, 1.0F, 0.18F + signal * 0.08F}, 32, start, end);
    }

    const int totalFinds = snapshot.surfaceScanPreviewMarkers.empty()
        ? snapshot.surfaceScanMaterials.common + snapshot.surfaceScanMaterials.rare * 2 + snapshot.surfaceScanMaterials.exotic * 3 + snapshot.surfaceScanArtifacts * 4
        : static_cast<int>(snapshot.surfaceScanPreviewMarkers.size());
    const int pingCount = std::clamp(totalFinds, 0, 14);
    for (int i = 0; i < pingCount; ++i) {
        const float seed = static_cast<float>(i) * 2.39996F;
        const int depthOffset = i < static_cast<int>(snapshot.surfaceScanPreviewDepthOffsets.size())
            ? std::max(0, snapshot.surfaceScanPreviewDepthOffsets[static_cast<std::size_t>(i)])
            : i % maxScanLayers;
        const float radius = surfaceRadius * (scanLayerRadiusScale(depthOffset) + 0.018F * std::sin(time * 0.62F + seed));
        const float angle = seed + time * 0.11F;
        const float x = destination.x + std::cos(angle) * radius;
        const float y = destination.y + std::sin(angle) * radius * 0.72F;
        const MiningCellMaterial material = surfaceScanPingMaterial(snapshot, i);
        const Color ping = material == MiningCellMaterial::CommonOre
            ? Color{0.48F, 0.92F, 0.68F, 0.72F}
            : (material == MiningCellMaterial::RareOre
                ? Color{1.0F, 0.74F, 0.24F, 0.78F}
                : (material == MiningCellMaterial::ExoticVein
                    ? Color{0.95F, 0.28F, 0.78F, 0.78F}
                    : Color{0.72F, 0.46F, 1.0F, 0.82F}));
        drawCircle(x, y, 0.005F + 0.002F * std::sin(time + seed), ping, 12);
        drawMiningOreSparkle(
            x,
            y,
            surfaceRadius * 0.085F,
            static_cast<int>(material),
            time,
            static_cast<float>(i) * 0.17F + signal * 0.23F,
            0.74F + signal * 0.28F);
    }

    if (snapshot.surfaceScanBusted) {
        drawEllipseLine(destination.x, destination.y, surfaceRadius * 0.98F, surfaceRadius * 0.74F, {1.0F, 0.24F, 0.12F, 0.45F}, 64, 0.0F, 2.0F * kPi);
        drawRadialGlow(destination.x, destination.y, surfaceRadius * 1.05F, {0.95F, 0.12F, 0.08F, 0.040F}, 48);
    }
}

void WebGLRenderer::drawSurfacePush(const RenderSnapshot& snapshot)
{
    drawBackdrop(snapshot);
    const Vec2 destination = routePoint(snapshot, 1.0F);
    const float pressure = static_cast<float>(std::clamp(snapshot.surfacePushPressure, 0.0, 1.0));
    const float risk = static_cast<float>(std::clamp(snapshot.surfacePushCollapseRisk, 0.0, 1.0));
    const float baseRadius = 0.16F + static_cast<float>(snapshot.destinationTier) * 0.012F;
    const float shaftTop = destination.y - baseRadius * 1.25F;
    const float shaftBottom = -0.82F;
    const float shaftX = std::clamp(destination.x - 0.10F, -0.45F, 0.45F);

    drawRadialGlow(destination.x, destination.y, baseRadius * 2.35F, {0.05F, 0.26F, 0.42F, 0.045F}, 64);
    drawEllipseLine(destination.x, destination.y, baseRadius * 1.6F, baseRadius * 1.6F, {0.18F, 0.78F, 0.94F, 0.30F}, 72, 0.0F, 2.0F * kPi);
    drawRect(shaftX, (shaftTop + shaftBottom) * 0.5F, 0.26F, std::abs(shaftTop - shaftBottom), {0.015F, 0.022F, 0.026F, 0.72F});

    const int safeSteps = std::max(1, snapshot.surfacePushMaxSteps);
    for (int i = 0; i <= safeSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(safeSteps);
        const float y = shaftTop + (shaftBottom - shaftTop) * t;
        const Color line = i <= snapshot.surfacePushSteps
            ? Color{1.0F, 0.72F, 0.22F, 0.58F}
            : Color{0.28F, 0.76F, 0.95F, 0.22F};
        drawLine(shaftX - 0.16F, y, shaftX + 0.16F, y, line, 1.5F);
    }

    const float progress = static_cast<float>(std::clamp(
        static_cast<double>(snapshot.surfacePushSteps) / static_cast<double>(safeSteps),
        0.0,
        1.0));
    const float probeY = shaftTop + (shaftBottom - shaftTop) * progress;
    drawLine(shaftX, shaftTop, shaftX, probeY, {0.90F, 0.62F, 0.22F, 0.70F}, 2.0F);
    drawCircle(shaftX, probeY, 0.042F, {0.08F, 0.30F, 0.42F, 0.90F}, 22);
    drawCircle(shaftX, probeY, 0.022F, {1.0F, 0.72F, 0.24F, 0.85F}, 18);

    std::vector<MiningCellMaterial> pockets = snapshot.surfacePushRewardMarkers;
    if (pockets.empty()) {
        for (int i = 0; i < std::max(0, snapshot.surfacePushMaterials.common); ++i) {
            pockets.push_back(MiningCellMaterial::CommonOre);
        }
        for (int i = 0; i < std::max(0, snapshot.surfacePushMaterials.rare); ++i) {
            pockets.push_back(MiningCellMaterial::RareOre);
        }
        for (int i = 0; i < std::max(0, snapshot.surfacePushMaterials.exotic); ++i) {
            pockets.push_back(MiningCellMaterial::ExoticVein);
        }
        for (int i = 0; i < std::max(0, snapshot.surfacePushArtifacts); ++i) {
            pockets.push_back(MiningCellMaterial::ArtifactCache);
        }
    }

    auto pocketColor = [](MiningCellMaterial material) {
        switch (material) {
        case MiningCellMaterial::CommonOre:
            return Color{0.48F, 0.92F, 0.68F, 0.55F};
        case MiningCellMaterial::RareOre:
            return Color{1.0F, 0.74F, 0.24F, 0.65F};
        case MiningCellMaterial::ExoticVein:
            return Color{0.95F, 0.28F, 0.78F, 0.66F};
        case MiningCellMaterial::ArtifactCache:
            return Color{0.72F, 0.46F, 1.0F, 0.74F};
        default:
            return Color{0.48F, 0.92F, 0.68F, 0.48F};
        }
    };

    auto markerPosition = [&](int index, int depthOffset, float lateralScale) {
        const int clampedOffset = std::clamp(depthOffset, 0, safeSteps);
        const float layerT = static_cast<float>(clampedOffset) / static_cast<float>(safeSteps);
        const float seed = miningCellNoise(index, snapshot.destinationTier + clampedOffset * 7, 151);
        const float y = shaftTop + (shaftBottom - shaftTop) * std::clamp(layerT + std::sin(static_cast<float>(snapshot.animationTime) * 0.18F + seed * kPi) * 0.006F, 0.0F, 1.0F);
        const float side = index % 2 == 0 ? -1.0F : 1.0F;
        const float x = shaftX + side * lateralScale * (0.72F + 0.20F * seed) +
            std::sin(static_cast<float>(snapshot.animationTime) * (0.52F + seed * 0.34F) + static_cast<float>(index)) * 0.007F;
        return Vec2{x, y};
    };

    const int forecastCount = std::min(static_cast<int>(snapshot.surfacePushForecastMarkers.size()), 14);
    for (int i = 0; i < forecastCount; ++i) {
        const int depthOffset = i < static_cast<int>(snapshot.surfacePushForecastDepthOffsets.size())
            ? snapshot.surfacePushForecastDepthOffsets[static_cast<std::size_t>(i)]
            : i % safeSteps;
        const MiningCellMaterial material = snapshot.surfacePushForecastMarkers[static_cast<std::size_t>(i)];
        const Color forecast = pocketColor(material);
        const Vec2 position = markerPosition(i, depthOffset, 0.145F);
        drawEllipseLine(position.x, position.y, 0.014F, 0.014F, {forecast.r, forecast.g, forecast.b, 0.24F + pressure * 0.08F}, 18, 0.0F, 2.0F * kPi);
    }

    const int pocketCount = std::min(static_cast<int>(pockets.size()), 10);
    for (int i = 0; i < pocketCount; ++i) {
        const float seed = miningCellNoise(i, snapshot.surfacePushSteps + snapshot.destinationTier * 5, 131);
        const int depthOffset = i < static_cast<int>(snapshot.surfacePushRewardDepthOffsets.size())
            ? snapshot.surfacePushRewardDepthOffsets[static_cast<std::size_t>(i)]
            : std::max(1, snapshot.surfacePushSteps);
        const Vec2 position = markerPosition(i, depthOffset, 0.10F);
        const float x = position.x;
        const float y = position.y;
        const MiningCellMaterial material = pockets[static_cast<std::size_t>(i)];
        const Color pocket = pocketColor(material);
        const float pulse = 0.80F + 0.20F * std::sin(static_cast<float>(snapshot.animationTime) * (1.4F + seed * 0.8F) + static_cast<float>(i) * 0.73F);
        drawCircle(x, y, 0.012F + pulse * 0.004F, {pocket.r, pocket.g, pocket.b, pocket.a * (0.82F + pulse * 0.18F)}, 14);
        drawMiningOreSparkleColor(
            x,
            y,
            0.038F + seed * 0.014F,
            {pocket.r, pocket.g, pocket.b, 1.0F},
            static_cast<float>(snapshot.animationTime) * (1.18F + pressure * 0.22F),
            seed + static_cast<float>(i) * 0.117F + pressure * 0.19F,
            1.15F + pressure * 0.42F);
        const float twinkle = std::fmod(static_cast<float>(snapshot.animationTime) * (1.55F + seed * 0.75F) + seed + static_cast<float>(i) * 0.071F, 1.0F);
        if (twinkle < 0.62F) {
            const Color sparkle{pocket.r, pocket.g, pocket.b, 1.0F};
            const float flare = 1.0F - twinkle / 0.62F;
            const float length = 0.018F + seed * 0.018F + flare * 0.014F;
            const float alpha = (0.32F + flare * 0.58F) * (0.78F + pressure * 0.30F);
            drawLine(x - length, y, x + length, y, {sparkle.r, sparkle.g, sparkle.b, alpha}, 1.25F);
            drawLine(x, y - length, x, y + length, {sparkle.r, sparkle.g, sparkle.b, alpha}, 1.25F);
        }
    }

    if (pressure > 0.0F) {
        drawRect(shaftX, shaftBottom + 0.06F, 0.30F, 0.018F + pressure * 0.045F, {1.0F, 0.36F, 0.12F, 0.14F + risk * 0.18F});
    }
    if (snapshot.surfacePushBusted) {
        drawRadialGlow(shaftX, probeY, 0.28F, {1.0F, 0.22F, 0.10F, 0.070F}, 48);
        drawEllipseLine(shaftX, probeY, 0.18F, 0.11F, {1.0F, 0.28F, 0.10F, 0.52F}, 48, 0.0F, 2.0F * kPi);
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
    drawRect((left + right) * 0.5F, cautionY + height * 0.15F, width, height * 0.30F, {0.24F, 0.09F, 0.08F, 0.08F});
    drawLine(left, bottom, right, bottom, {0.36F, 0.55F, 0.68F, 0.55F});
    drawLine(left, bottom, left, top, {0.36F, 0.55F, 0.68F, 0.55F});
    drawLine(left, top, right, top, {0.24F, 0.42F, 0.54F, 0.28F});
    drawLine(right, bottom, right, top, {0.24F, 0.42F, 0.54F, 0.22F});

    for (int i = 1; i <= 3; ++i) {
        const float y = bottom + height * (static_cast<float>(i) / 4.0F);
        if (y >= cautionY) {
            continue;
        }
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
        ? 0.42F + 0.28F * std::sin(static_cast<float>(snapshot.animationTime) * 18.0F)
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
            appendLine(flashVertices, previous.x, previous.y, next.x, next.y, {0.95F, 0.96F, 1.0F, 0.14F * tail});
            previous = next;
        }
        submitLines(flashVertices, 2.0F);
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
        const float ringLife = std::clamp(0.28F + life * 0.72F, 0.0F, 1.0F);
        const float sweep = time * 1.65F;
        const float lockAngle = std::atan2(endpoint.y - routePoint(snapshot, 0.72F).y, endpoint.x - routePoint(snapshot, 0.72F).x);

        drawRadialGlow(endpoint.x, endpoint.y, baseRadius * (0.62F + beat * 0.08F), {0.50F, 0.92F, 1.0F, 0.020F + beat * 0.010F}, 48);

        auto brokenArc = [&](float radius, float squash, float rotation, Color color, float width, int pieces) {
            for (int i = 0; i < pieces; ++i) {
                const float start = rotation + static_cast<float>(i) * (2.0F * kPi / static_cast<float>(pieces)) + 0.10F * std::sin(time * 2.0F + static_cast<float>(i));
                const float span = (0.34F + 0.08F * static_cast<float>((i + pieces) % 3)) * kPi;
                drawEllipseLine(endpoint.x, endpoint.y, radius, radius * squash, color, 28, start, start + span);
                if (width > 1.01F) {
                    drawEllipseLine(endpoint.x, endpoint.y, radius * 1.012F, radius * squash * 1.012F, {color.r, color.g, color.b, color.a * 0.58F}, 28, start, start + span);
                }
            }
        };

        brokenArc(
            baseRadius * (1.00F + beat * 0.04F),
            0.84F,
            sweep,
            {0.38F, 0.92F, 1.0F, 0.34F * ringLife},
            1.7F,
            3);
        brokenArc(
            baseRadius * (1.42F + (1.0F - life) * 0.14F),
            0.78F,
            -sweep * 0.72F + 0.45F,
            {1.0F, 0.76F, 0.30F, 0.30F * ringLife},
            1.5F,
            4);
        brokenArc(
            baseRadius * (1.86F + (1.0F - life) * 0.24F),
            0.74F,
            sweep * 0.42F + 0.18F,
            {0.48F, 0.78F, 1.0F, 0.18F * ringLife},
            1.2F,
            5);

        for (int i = 0; i < 24; ++i) {
            const float seed = static_cast<float>(i) * 2.39996F;
            const float cadence = std::fmod(time * (0.68F + static_cast<float>(i % 4) * 0.08F) + seed, 1.0F);
            const float flare = 1.0F - std::abs(cadence - 0.38F) / 0.38F;
            const float alpha = std::clamp(flare, 0.0F, 1.0F) * (0.12F + 0.24F * life);
            if (alpha <= 0.01F) {
                continue;
            }
            const float angle = lockAngle + seed + std::sin(time * 0.4F + seed) * 0.05F;
            const float inner = baseRadius * (1.10F + 0.34F * static_cast<float>(i % 3));
            const float outer = inner + baseRadius * (0.10F + 0.09F * static_cast<float>(i % 5));
            const float ax = endpoint.x + std::cos(angle) * inner;
            const float ay = endpoint.y + std::sin(angle) * inner * 0.80F;
            const float bx = endpoint.x + std::cos(angle) * outer;
            const float by = endpoint.y + std::sin(angle) * outer * 0.80F;
            const Color tick = (i % 3 == 0)
                ? Color{1.0F, 0.84F, 0.36F, alpha}
                : Color{0.48F, 0.92F, 1.0F, alpha * 0.92F};
            drawLine(ax, ay, bx, by, tick, 1.25F);
        }

        for (int i = 0; i < 6; ++i) {
            const float side = i % 2 == 0 ? -1.0F : 1.0F;
            const float lane = static_cast<float>(i) - 2.5F;
            const float angle = lockAngle + side * (0.55F + 0.06F * lane);
            const float pulse = std::fmod(time * 0.95F + static_cast<float>(i) * 0.17F, 1.0F);
            const float fade = (1.0F - pulse) * (1.0F - pulse) * life;
            const float start = baseRadius * (0.82F + pulse * 0.40F);
            const float end = start + baseRadius * (0.42F + 0.18F * static_cast<float>(i % 3));
            const float ax = endpoint.x + std::cos(angle) * start;
            const float ay = endpoint.y + std::sin(angle) * start;
            const float bx = endpoint.x + std::cos(angle) * end;
            const float by = endpoint.y + std::sin(angle) * end;
            drawLine(ax, ay, bx, by, {0.92F, 0.96F, 1.0F, 0.14F * fade}, 2.0F);
            drawLine(ax, ay, bx, by, {0.34F, 0.90F, 1.0F, 0.20F * fade}, 1.0F);
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

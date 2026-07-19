#pragma once

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace rocket {

struct Color {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

enum class TextureId : std::uint8_t {
    None = 0,
    Earth,
    Moon,
    Mars,
    RocketClosed,
    Explosion,
    Thrust,
    MiningDrone,
    DrillBit,
    LocalSolarBackground,
    Mercury,
    Venus,
    Jupiter,
    Saturn,
    Uranus,
    Neptune,
    ArkOperational,
    ArkDamaged,
    OuterPlanet01,
    OuterPlanet02,
    OuterPlanet03,
    OuterPlanet04,
    OuterPlanet05,
    OuterPlanet06,
    OuterPlanet07,
    OuterPlanet08,
    OuterPlanet09,
    RocketOpen,
    MiniDroneMining,
    MiniDroneResource,
    MiniDroneSurvey,
    MiniDroneHazard,
    MiniDroneAttack,
    MiniDroneDefense,
    HeroicCapybara,
    Count
};

enum class BlendMode : std::uint8_t {
    Alpha
};

enum class PipelineClass : std::uint8_t {
    Solid,
    Textured,
    RoundedFrame
};

enum class CoordinateSpace : std::uint8_t {
    World,
    Clip
};

// Vertex streams retain submission order through SceneDraw, but have distinct
// upload lifetimes. Frame vertices are rewritten every presented frame. Mining
// terrain vertices remain valid until their presentation revision changes.
enum class SceneVertexStream : std::uint8_t {
    Frame,
    MiningTerrain
};

// Ordered scene draws either consume authored/tessellated triangles or a
// compact instance stream expanded from a static six-vertex unit quad in the
// backend vertex shader. The distinction is presentation-only and never
// escapes the frame-lifetime ScenePacket.
enum class SceneDrawType : std::uint8_t {
    Triangles,
    InstancedQuad
};

enum class SceneInstanceStream : std::uint8_t {
    Frame,
    MiningTerrain
};

enum class SceneInstanceShape : std::uint8_t {
    Rectangle,
    Polygon,
    RadialGlow
};

inline constexpr std::uint8_t kSceneInstanceShapeMask = 0x7fU;
inline constexpr std::uint8_t kSceneInstanceTexturedBit = 0x80U;
inline constexpr std::uint8_t kNoSceneAtlasPage = std::numeric_limits<std::uint8_t>::max();

struct SceneVertex {
    float x = 0.0F;
    float y = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
    float u = 0.0F;
    float v = 0.0F;
};

static_assert(sizeof(SceneVertex) == sizeof(float) * 8U);

// Compact GPU-facing representation. SceneComposer may retain full-precision
// working vertices, but every backend consumes this frame packet layout.
struct PackedSceneVertex {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
    std::uint16_t u = 0;
    std::uint16_t v = 0;
};

static_assert(std::is_standard_layout_v<PackedSceneVertex>);
static_assert(std::is_trivially_copyable_v<PackedSceneVertex>);
static_assert(sizeof(PackedSceneVertex) == 12U);
static_assert(offsetof(PackedSceneVertex, x) == 0U);
static_assert(offsetof(PackedSceneVertex, y) == 2U);
static_assert(offsetof(PackedSceneVertex, r) == 4U);
static_assert(offsetof(PackedSceneVertex, u) == 8U);
static_assert(offsetof(PackedSceneVertex, v) == 10U);
static_assert(sizeof(PackedSceneVertex) * 2U < sizeof(SceneVertex));

inline std::uint16_t packSceneHalf(float value) noexcept
{
    constexpr float maximumHalf = 65504.0F;
    if (std::isnan(value)) {
        return 0;
    }
    value = std::clamp(value, -maximumHalf, maximumHalf);

    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
    const std::uint32_t exponent = (bits >> 23U) & 0xffU;
    std::uint32_t mantissa = bits & 0x007fffffU;
    int halfExponent = static_cast<int>(exponent) - 112;

    if (halfExponent <= 0) {
        if (halfExponent < -10 || exponent == 0) {
            return sign;
        }
        mantissa |= 0x00800000U;
        const int shift = 14 - halfExponent;
        std::uint32_t halfMantissa = mantissa >> shift;
        const std::uint32_t remainderMask = (std::uint32_t {1} << shift) - 1U;
        const std::uint32_t remainder = mantissa & remainderMask;
        const std::uint32_t halfway = std::uint32_t {1} << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (halfMantissa & 1U) != 0U)) {
            ++halfMantissa;
        }
        return static_cast<std::uint16_t>(sign | halfMantissa);
    }

    std::uint32_t halfMantissa = mantissa >> 13U;
    const std::uint32_t remainder = mantissa & 0x1fffU;
    if (remainder > 0x1000U || (remainder == 0x1000U && (halfMantissa & 1U) != 0U)) {
        ++halfMantissa;
        if (halfMantissa == 0x400U) {
            halfMantissa = 0;
            ++halfExponent;
        }
    }
    if (halfExponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7bffU);
    }
    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint32_t>(halfExponent) << 10U) | halfMantissa);
}

inline float unpackSceneHalf(std::uint16_t value) noexcept
{
    const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
    std::uint32_t exponent = (value >> 10U) & 0x1fU;
    std::uint32_t mantissa = value & 0x03ffU;
    std::uint32_t bits = sign;
    if (exponent == 0) {
        if (mantissa != 0) {
            int unbiasedExponent = -14;
            while ((mantissa & 0x0400U) == 0U) {
                mantissa <<= 1U;
                --unbiasedExponent;
            }
            mantissa &= 0x03ffU;
            bits |= static_cast<std::uint32_t>(unbiasedExponent + 127) << 23U;
            bits |= mantissa << 13U;
        }
    } else if (exponent == 0x1fU) {
        bits |= 0x7f800000U | (mantissa << 13U);
    } else {
        bits |= (exponent + 112U) << 23U;
        bits |= mantissa << 13U;
    }
    return std::bit_cast<float>(bits);
}

inline std::uint8_t packSceneUnorm8(float value) noexcept
{
    if (std::isnan(value)) {
        value = 0.0F;
    }
    value = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(value * 255.0F + 0.5F);
}

inline float unpackSceneUnorm8(std::uint8_t value) noexcept
{
    return static_cast<float>(value) / 255.0F;
}

inline std::uint16_t packSceneUnorm16(float value) noexcept
{
    if (std::isnan(value)) {
        value = 0.0F;
    }
    value = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint16_t>(value * 65535.0F + 0.5F);
}

inline float unpackSceneUnorm16(std::uint16_t value) noexcept
{
    return static_cast<float>(value) / 65535.0F;
}

inline PackedSceneVertex packSceneVertex(const SceneVertex& vertex) noexcept
{
    return {
        packSceneHalf(vertex.x),
        packSceneHalf(vertex.y),
        packSceneUnorm8(vertex.r),
        packSceneUnorm8(vertex.g),
        packSceneUnorm8(vertex.b),
        packSceneUnorm8(vertex.a),
        packSceneHalf(vertex.u),
        packSceneHalf(vertex.v)
    };
}

inline SceneVertex unpackSceneVertex(const PackedSceneVertex& vertex) noexcept
{
    return {
        unpackSceneHalf(vertex.x),
        unpackSceneHalf(vertex.y),
        unpackSceneUnorm8(vertex.r),
        unpackSceneUnorm8(vertex.g),
        unpackSceneUnorm8(vertex.b),
        unpackSceneUnorm8(vertex.a),
        unpackSceneHalf(vertex.u),
        unpackSceneHalf(vertex.v)
    };
}

// Backend-neutral instance description. axisX and axisY are the half-extent
// basis vectors applied to a static unit quad in [-1, 1]. This represents both
// axis-aligned and rotated sprites without regenerating six vertices. Polygon
// instances retain the original circle segment count so their silhouette and
// radial interpolation match the former triangle fan.
struct SceneInstance {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float axisXx = 0.5F;
    float axisXy = 0.0F;
    float axisYx = 0.0F;
    float axisYy = 0.5F;
    Color color;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 1.0F;
    float v1 = 1.0F;
    SceneInstanceShape shape = SceneInstanceShape::Rectangle;
    std::uint8_t segments = 4;
    bool textured = false;
};

// 28 bytes replaces the 72-byte packed six-vertex quad previously uploaded
// for every rectangle/sprite, and replaces substantially larger polygon fans.
struct PackedSceneInstance {
    std::uint16_t centerX = 0;
    std::uint16_t centerY = 0;
    std::uint16_t axisXx = 0;
    std::uint16_t axisXy = 0;
    std::uint16_t axisYx = 0;
    std::uint16_t axisYy = 0;
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
    std::uint16_t u0 = 0;
    std::uint16_t v0 = 0;
    std::uint16_t u1 = 0;
    std::uint16_t v1 = 0;
    std::uint8_t shape = 0;
    std::uint8_t segments = 4;
    std::uint16_t reserved = 0;
};

static_assert(std::is_standard_layout_v<PackedSceneInstance>);
static_assert(std::is_trivially_copyable_v<PackedSceneInstance>);
static_assert(sizeof(PackedSceneInstance) == 28U);
static_assert(offsetof(PackedSceneInstance, centerX) == 0U);
static_assert(offsetof(PackedSceneInstance, axisXx) == 4U);
static_assert(offsetof(PackedSceneInstance, axisYx) == 8U);
static_assert(offsetof(PackedSceneInstance, r) == 12U);
static_assert(offsetof(PackedSceneInstance, u0) == 16U);
static_assert(offsetof(PackedSceneInstance, u1) == 20U);
static_assert(offsetof(PackedSceneInstance, shape) == 24U);

inline PackedSceneInstance packSceneInstance(const SceneInstance& instance) noexcept
{
    return {
        packSceneHalf(instance.centerX),
        packSceneHalf(instance.centerY),
        packSceneHalf(instance.axisXx),
        packSceneHalf(instance.axisXy),
        packSceneHalf(instance.axisYx),
        packSceneHalf(instance.axisYy),
        packSceneUnorm8(instance.color.r),
        packSceneUnorm8(instance.color.g),
        packSceneUnorm8(instance.color.b),
        packSceneUnorm8(instance.color.a),
        packSceneUnorm16(instance.u0),
        packSceneUnorm16(instance.v0),
        packSceneUnorm16(instance.u1),
        packSceneUnorm16(instance.v1),
        static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>(instance.shape) & kSceneInstanceShapeMask)
            | (instance.textured ? kSceneInstanceTexturedBit : 0U)),
        static_cast<std::uint8_t>(std::clamp<int>(instance.segments, 3, 255)),
        0
    };
}

inline SceneInstance unpackSceneInstance(const PackedSceneInstance& instance) noexcept
{
    return {
        unpackSceneHalf(instance.centerX),
        unpackSceneHalf(instance.centerY),
        unpackSceneHalf(instance.axisXx),
        unpackSceneHalf(instance.axisXy),
        unpackSceneHalf(instance.axisYx),
        unpackSceneHalf(instance.axisYy),
        {
            unpackSceneUnorm8(instance.r),
            unpackSceneUnorm8(instance.g),
            unpackSceneUnorm8(instance.b),
            unpackSceneUnorm8(instance.a)
        },
        unpackSceneUnorm16(instance.u0),
        unpackSceneUnorm16(instance.v0),
        unpackSceneUnorm16(instance.u1),
        unpackSceneUnorm16(instance.v1),
        static_cast<SceneInstanceShape>(instance.shape & kSceneInstanceShapeMask),
        instance.segments,
        (instance.shape & kSceneInstanceTexturedBit) != 0U
    };
}

constexpr bool compatibleSceneAtlasPages(std::uint8_t left, std::uint8_t right) noexcept
{
    return left == kNoSceneAtlasPage || right == kNoSceneAtlasPage || left == right;
}

constexpr std::uint8_t mergedSceneAtlasPage(std::uint8_t left, std::uint8_t right) noexcept
{
    return left == kNoSceneAtlasPage ? right : left;
}

struct SceneDraw {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    SceneVertexStream vertexStream = SceneVertexStream::Frame;
    TextureId texture = TextureId::None;
    BlendMode blend = BlendMode::Alpha;
    PipelineClass pipeline = PipelineClass::Solid;
    CoordinateSpace coordinateSpace = CoordinateSpace::World;
    Color effectColor;
    std::array<float, 4> effectParams {};
    std::array<float, 2> effectSize {};
    SceneDrawType drawType = SceneDrawType::Triangles;
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
    SceneInstanceStream instanceStream = SceneInstanceStream::Frame;
    // The resolved atlas page is explicit render state. TextureId remains the
    // logical asset identity used for readiness, while draws from different
    // logical textures can batch when their page and other ordered state match.
    std::uint8_t atlasPage = kNoSceneAtlasPage;
};

struct SceneTransform {
    float cssWidth = 1280.0F;
    float cssHeight = 720.0F;
    float pixelCenterX = 640.0F;
    float pixelCenterY = 360.0F;
    float worldUnitX = 360.0F;
    float worldUnitY = 360.0F;
};

// Frame-lifetime view into SceneComposer-owned storage. A backend must consume
// this packet synchronously before the next compose() call.
struct ScenePacket {
    std::span<const PackedSceneVertex> vertices;
    std::span<const PackedSceneVertex> miningTerrainVertices;
    std::span<const PackedSceneInstance> instances;
    std::span<const PackedSceneInstance> miningTerrainInstances;
    std::span<const SceneDraw> draws;
    SceneTransform transform;
    Color clearColor {0.02F, 0.03F, 0.05F, 1.0F};
    std::uint64_t miningTerrainRevision = 0;
};

constexpr std::size_t textureIndex(TextureId id) noexcept
{
    return static_cast<std::size_t>(id);
}

} // namespace rocket

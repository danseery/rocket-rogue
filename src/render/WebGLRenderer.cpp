#include "render/WebGLRenderer.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>

namespace rocket {

namespace {

constexpr float kPi = 3.1415926535F;

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
out vec4 v_color;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
}
)";

    constexpr const char* fragmentSource = R"(#version 300 es
precision mediump float;
in vec4 v_color;
out vec4 out_color;
void main()
{
    out_color = v_color;
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

void pushVertex(std::vector<float>& vertices, float x, float y, Color color)
{
    vertices.push_back(x);
    vertices.push_back(y);
    vertices.push_back(color.r);
    vertices.push_back(color.g);
    vertices.push_back(color.b);
    vertices.push_back(color.a);
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 6, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 6, reinterpret_cast<void*>(sizeof(float) * 2));
    glUseProgram(program_);

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
    drawBackdrop(snapshot);
    drawRocket(snapshot);
    drawTelemetry(snapshot);
}

void WebGLRenderer::beginFrame(const RenderSnapshot& snapshot)
{
#ifdef __EMSCRIPTEN__
    double cssWidth = 1280.0;
    double cssHeight = 720.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    emscripten_set_canvas_element_size("#canvas", static_cast<int>(cssWidth), static_cast<int>(cssHeight));
    glViewport(0, 0, static_cast<int>(cssWidth), static_cast<int>(cssHeight));

    const float heat = static_cast<float>(std::clamp(snapshot.heat, 0.0, 1.0));
    glClearColor(0.02F + heat * 0.05F, 0.03F, 0.05F + heat * 0.02F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
#else
    (void)snapshot;
#endif
}

void WebGLRenderer::drawRect(float cx, float cy, float w, float h, Color color)
{
    const float left = cx - w * 0.5F;
    const float right = cx + w * 0.5F;
    const float top = cy + h * 0.5F;
    const float bottom = cy - h * 0.5F;

    std::vector<float> vertices;
    vertices.reserve(36);
    pushVertex(vertices, left, bottom, color);
    pushVertex(vertices, right, bottom, color);
    pushVertex(vertices, right, top, color);
    pushVertex(vertices, left, bottom, color);
    pushVertex(vertices, right, top, color);
    pushVertex(vertices, left, top, color);
    submit(vertices, 0x0004);
}

void WebGLRenderer::drawLine(float ax, float ay, float bx, float by, Color color, float width)
{
#ifdef __EMSCRIPTEN__
    glLineWidth(width);
#else
    (void)width;
#endif
    std::vector<float> vertices;
    vertices.reserve(12);
    pushVertex(vertices, ax, ay, color);
    pushVertex(vertices, bx, by, color);
    submit(vertices, 0x0001);
}

void WebGLRenderer::drawTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color color)
{
    std::vector<float> vertices;
    vertices.reserve(18);
    pushVertex(vertices, ax, ay, color);
    pushVertex(vertices, bx, by, color);
    pushVertex(vertices, cx, cy, color);
    submit(vertices, 0x0004);
}

void WebGLRenderer::drawCircle(float cx, float cy, float radius, Color color, int segments)
{
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 18);
    for (int i = 0; i < segments; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * kPi * 2.0F;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * kPi * 2.0F;
        pushVertex(vertices, cx, cy, color);
        pushVertex(vertices, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, color);
        pushVertex(vertices, cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
    submit(vertices, 0x0004);
}

void WebGLRenderer::drawTelemetry(const RenderSnapshot& snapshot)
{
    const float left = 0.22F;
    const float right = 0.92F;
    const float bottom = -0.86F;
    const float top = -0.60F;

    drawRect((left + right) * 0.5F, (bottom + top) * 0.5F, right - left, top - bottom, {0.02F, 0.05F, 0.07F, 0.72F});
    drawLine(left, bottom, right, bottom, {0.36F, 0.55F, 0.68F, 0.55F}, 1.0F);
    drawLine(left, bottom, left, top, {0.36F, 0.55F, 0.68F, 0.55F}, 1.0F);

    if (snapshot.telemetryCount <= 1) {
        return;
    }

    Color warningSafe {0.35F, 0.84F, 1.0F, 1.0F};
    Color warningHot {1.0F, 0.38F, 0.28F, 1.0F};
    Color heatColor {1.0F, 0.78F, 0.25F, 0.90F};

    drawLine(left, bottom + (top - bottom) * 0.70F, right, bottom + (top - bottom) * 0.70F, {1.0F, 0.80F, 0.30F, 0.22F}, 1.0F);

    for (int i = 1; i < snapshot.telemetryCount; ++i) {
        const float t0 = static_cast<float>(i - 1) / static_cast<float>(snapshot.telemetryCount - 1);
        const float t1 = static_cast<float>(i) / static_cast<float>(snapshot.telemetryCount - 1);
        const float y0 = bottom + static_cast<float>(snapshot.telemetry[static_cast<std::size_t>(i - 1)]) * (top - bottom);
        const float y1 = bottom + static_cast<float>(snapshot.telemetry[static_cast<std::size_t>(i)]) * (top - bottom);
        const float h0 = bottom + static_cast<float>(snapshot.heatTelemetry[static_cast<std::size_t>(i - 1)]) * (top - bottom);
        const float h1 = bottom + static_cast<float>(snapshot.heatTelemetry[static_cast<std::size_t>(i)]) * (top - bottom);
        const float x0 = left + t0 * (right - left);
        const float x1 = left + t1 * (right - left);
        drawLine(x0, h0, x1, h1, heatColor, 1.5F);
        drawLine(x0, y0, x1, y1, mix(warningSafe, warningHot, static_cast<float>(snapshot.warning)), 2.2F);
    }
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
    drawRect(0.0F, 0.0F, 2.0F, 2.0F, {0.015F, 0.022F, 0.032F, 1.0F});

    for (int i = 0; i < 52; ++i) {
        const float x = -0.95F + static_cast<float>((i * 37) % 190) / 95.0F;
        const float y = -0.92F + static_cast<float>((i * 71) % 184) / 92.0F;
        const float alpha = 0.18F + static_cast<float>((i * 19) % 60) / 100.0F;
        drawRect(x, y, 0.004F, 0.004F, {0.75F, 0.88F, 1.0F, alpha});
    }

    auto drawEllipseLine = [&](float cx, float cy, float rx, float ry, Color color, int segments, float start, float end) {
        Vec2 previous {cx + std::cos(start) * rx, cy + std::sin(start) * ry};
        for (int i = 1; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float angle = start + (end - start) * t;
            const Vec2 next {cx + std::cos(angle) * rx, cy + std::sin(angle) * ry};
            drawLine(previous.x, previous.y, next.x, next.y, color, 1.0F);
            previous = next;
        }
    };

    if (snapshot.destinationTier == 0 && !snapshot.frontierTransfer) {
        const float earthX = -0.16F;
        const float earthY = -1.10F;
        const float earthR = 0.58F;
        drawCircle(earthX, earthY, earthR * 1.10F, {0.24F, 0.62F, 0.96F, 0.08F}, 72);
        drawCircle(earthX, earthY, earthR, {0.18F, 0.48F, 0.78F, 0.82F}, 72);
        drawCircle(earthX - 0.16F, earthY + 0.18F, earthR * 0.16F, {0.28F, 0.58F, 0.36F, 0.72F}, 24);
        drawCircle(earthX + 0.14F, earthY + 0.28F, earthR * 0.12F, {0.28F, 0.58F, 0.36F, 0.64F}, 20);
        drawEllipseLine(earthX, earthY, earthR * 1.08F, earthR * 0.56F, {0.45F, 0.88F, 1.0F, 0.22F}, 42, 0.13F * kPi, 0.92F * kPi);
    } else if (snapshot.destinationTier == 1) {
        const Vec2 moon = routePoint(snapshot, 1.0F);
        const float earthX = -0.26F;
        const float earthY = -0.88F;
        const float earthR = 0.30F;
        drawCircle(earthX, earthY, earthR * 1.20F, {0.24F, 0.62F, 0.96F, 0.08F}, 64);
        drawCircle(earthX, earthY, earthR, {0.18F, 0.48F, 0.78F, 0.72F}, 64);
        drawCircle(earthX - 0.08F, earthY + 0.08F, earthR * 0.16F, {0.30F, 0.60F, 0.38F, 0.70F}, 20);
        drawCircle(earthX + 0.10F, earthY + 0.14F, earthR * 0.12F, {0.30F, 0.60F, 0.38F, 0.58F}, 20);
        drawEllipseLine(earthX, earthY, 1.08F, 0.76F, {0.40F, 0.62F, 0.78F, 0.20F}, 96, -0.04F * kPi, 0.82F * kPi);
        drawCircle(moon.x, moon.y, 0.060F, {0.72F, 0.74F, 0.72F, 0.78F}, 48);
        drawCircle(moon.x + 0.018F, moon.y + 0.015F, 0.018F, {0.48F, 0.50F, 0.50F, 0.36F}, 16);
    } else {
        const float tier = static_cast<float>(snapshot.destinationTier);
        const float radius = 0.065F + tier * 0.010F;
        const Color destination = mix({0.42F, 0.66F, 0.88F, 0.60F}, {0.95F, 0.72F, 0.35F, 0.72F}, tier / 5.0F);
        const Vec2 endpoint = routePoint(snapshot, 1.0F);
        drawCircle(endpoint.x, endpoint.y, radius, destination, 56);
        drawCircle(endpoint.x, endpoint.y, radius * 1.65F, {destination.r, destination.g, destination.b, 0.09F}, 64);
    }

    Vec2 previous = routePoint(snapshot, 0.0F);
    for (int i = 1; i <= 28; ++i) {
        const float t = static_cast<float>(i) / 28.0F;
        const Vec2 next = routePoint(snapshot, t);
        const Color routeColor = t <= snapshot.travelProgress ? Color{0.42F, 0.88F, 1.0F, 0.42F} : Color{0.25F, 0.42F, 0.52F, 0.22F};
        drawLine(previous.x, previous.y, next.x, next.y, routeColor, 1.0F);
        previous = next;
    }

    if (snapshot.travelProgress > 1.0) {
        Vec2 overburnPrevious = routePoint(snapshot, 1.0F);
        for (int i = 1; i <= 8; ++i) {
            const float t = 1.0F + (static_cast<float>(snapshot.travelProgress) - 1.0F) * (static_cast<float>(i) / 8.0F);
            const Vec2 overburnNext = routePoint(snapshot, t);
            drawLine(overburnPrevious.x, overburnPrevious.y, overburnNext.x, overburnNext.y, {0.90F, 0.50F, 0.28F, 0.44F}, 1.0F);
            overburnPrevious = overburnNext;
        }
    }

    const Vec2 targetMarker = routePoint(snapshot, 1.0F);
    drawLine(targetMarker.x, targetMarker.y - 0.055F, targetMarker.x, targetMarker.y + 0.055F, {0.98F, 0.82F, 0.36F, 0.85F}, 2.0F);
}

void WebGLRenderer::submit(const std::vector<float>& vertices, int primitive)
{
#ifdef __EMSCRIPTEN__
    if (vertices.empty()) {
        return;
    }

    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(static_cast<GLenum>(primitive), 0, static_cast<GLsizei>(vertices.size() / 6));
#else
    (void)vertices;
    (void)primitive;
#endif
}

} // namespace rocket

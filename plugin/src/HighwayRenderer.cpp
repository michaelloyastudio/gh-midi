#include "HighwayRenderer.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <array>

using namespace juce::gl;

// ============================== scene constants ==============================
namespace {
constexpr float kLaneW = 0.44f;
constexpr float kBoardHalf = 1.22f;
constexpr float kZStrike = -1.35f;
constexpr float kZFar = -10.5f;
constexpr float kTravelSecs = 2.8f;

const float laneColours[5][3] = {
    { 0.10f, 0.78f, 0.15f },  // green
    { 0.92f, 0.15f, 0.12f },  // red
    { 0.98f, 0.80f, 0.05f },  // yellow
    { 0.10f, 0.42f, 0.90f },  // blue
    { 0.98f, 0.52f, 0.08f },  // orange
};
const float openColour[3] = { 0.63f, 0.37f, 0.94f };

float laneXw(int lane) { return (lane - 2) * kLaneW; }

float zForAge(double age)
{
    const float t = juce::jlimit(0.0f, 1.0f, (float) (age / kTravelSecs));
    const float e = 1.0f - std::pow(1.0f - t, 1.35f);
    return kZStrike + (kZFar - kZStrike) * e;
}

// ---------- column-major mat4 helpers ----------
using Mat4 = std::array<float, 16>;

Mat4 matIdentity()
{
    Mat4 m {};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

Mat4 matMul(const Mat4& a, const Mat4& b)
{
    Mat4 r {};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            for (int k = 0; k < 4; ++k)
                r[c * 4 + rr] += a[k * 4 + rr] * b[c * 4 + k];
    return r;
}

Mat4 matPerspective(float fovyDeg, float aspect, float zNear, float zFarP)
{
    const float f = 1.0f / std::tan(fovyDeg * juce::MathConstants<float>::pi / 360.0f);
    Mat4 m {};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFarP + zNear) / (zNear - zFarP);
    m[11] = -1.0f;
    m[14] = (2.0f * zFarP * zNear) / (zNear - zFarP);
    return m;
}

Mat4 matLookAt(float ex, float ey, float ez, float cxp, float cyp, float czp)
{
    const float fx0 = cxp - ex, fy0 = cyp - ey, fz0 = czp - ez;
    const float fl = std::sqrt(fx0 * fx0 + fy0 * fy0 + fz0 * fz0);
    const float fx = fx0 / fl, fy = fy0 / fl, fz = fz0 / fl;
    // up = (0,1,0)
    float sx = fy * 0.0f - fz * 1.0f, sy = fz * 0.0f - fx * 0.0f, sz = fx * 1.0f - fy * 0.0f;
    const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    Mat4 m = matIdentity();
    m[0] = sx; m[4] = sy; m[8] = sz;
    m[1] = ux; m[5] = uy; m[9] = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[12] = -(sx * ex + sy * ey + sz * ez);
    m[13] = -(ux * ex + uy * ey + uz * ez);
    m[14] = (fx * ex + fy * ey + fz * ez);
    return m;
}

Mat4 matTRS(float tx, float ty, float tz, float s, float sy = -1.0f)
{
    Mat4 m {};
    m[0] = s;
    m[5] = sy < 0.0f ? s : sy;
    m[10] = s;
    m[15] = 1.0f;
    m[12] = tx; m[13] = ty; m[14] = tz;
    return m;
}

// ============================== shaders ==============================
const char* kBgVert = R"(#version 150
in vec2 aPos;
out vec2 vUV;
void main() { vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); })";

const char* kBgFrag = R"(#version 150
in vec2 vUV;
out vec4 frag;
uniform float uTime;
float hash(float n) { return fract(sin(n) * 43758.5453); }
void main()
{
    vec3 top = vec3(0.075, 0.075, 0.145);
    vec3 bot = vec3(0.012, 0.012, 0.030);
    vec3 c = mix(bot, top, vUV.y);
    // stage bloom around the horizon
    float horizon = 0.62;
    float d = distance(vUV, vec2(0.5, horizon));
    c += vec3(0.23, 0.16, 0.40) * (1.0 - smoothstep(0.0, 0.55, d)) * 0.85;
    // sweeping beams
    for (int b = 0; b < 2; ++b)
    {
        float fb = float(b);
        float bx = 0.5 + (fb * 2.0 - 1.0) * 0.20 + sin(uTime * 0.25 + fb * 2.6) * 0.13;
        float w = 0.018 + (1.0 - vUV.y) * 0.0;
        float span = (vUV.y - horizon);
        if (span > 0.0)
        {
            float lx = bx + (vUV.x - bx) * 1.0;
            float dd = abs(vUV.x - (bx + (vUV.y - horizon) * (fb * 2.0 - 1.0) * 0.22));
            c += vec3(0.9) * (1.0 - smoothstep(0.0, w + span * 0.10, dd)) * 0.05;
        }
    }
    // drifting embers
    for (int i = 0; i < 12; ++i)
    {
        float fi = float(i);
        float ex = fract(hash(fi * 7.1) + sin(uTime * (0.03 + 0.01 * hash(fi)) + fi) * 0.12);
        float ey = fract(hash(fi * 3.7) + uTime * (0.015 + 0.012 * hash(fi * 1.3)));
        float dd = distance(vUV, vec2(ex, ey));
        c += vec3(0.9, 0.55, 0.25) * (1.0 - smoothstep(0.0, 0.0035, dd)) * 0.5;
    }
    // vignette
    float vig = 1.0 - smoothstep(0.35, 1.05, distance(vUV, vec2(0.5, 0.45)));
    c *= mix(0.55, 1.0, vig);
    frag = vec4(c, 1.0);
})";

const char* kBoardVert = R"(#version 150
in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vWorld;
void main() { vWorld = aPos; gl_Position = uViewProj * vec4(aPos, 1.0); })";

const char* kBoardFrag = R"(#version 150
in vec3 vWorld;
out vec4 frag;
uniform float uScroll;
uniform float uLaneW;
uniform float uHalfW;
float lineSm(float d, float w) { return 1.0 - smoothstep(w * 0.35, w, d); }
void main()
{
    float x = vWorld.x;
    float z = vWorld.z;
    float nearF = clamp((z - (-12.0)) / ((-1.35) - (-12.0)), 0.0, 1.0);

    // base board
    vec3 c = mix(vec3(0.030, 0.030, 0.052), vec3(0.055, 0.055, 0.082), nearF);
    // centre sheen
    c += vec3(0.05) * (1.0 - smoothstep(0.0, uHalfW, abs(x))) * 0.35 * nearF;

    // lane dividers
    float lane = abs(fract(x / uLaneW + 0.5) - 0.5) * uLaneW;
    float inLanes = step(abs(x), uLaneW * 2.5);
    c += vec3(0.20) * lineSm(lane, 0.012) * inLanes * (0.30 + 0.55 * nearF);

    // beat lines (scroll away from the player) + half beats
    float beat = fract(z * 0.42 + uScroll);
    float bd = min(beat, 1.0 - beat);
    c += vec3(0.85) * lineSm(bd, 0.016) * 0.30 * nearF * nearF;
    float half1 = fract(z * 0.42 + uScroll + 0.5);
    float hd = min(half1, 1.0 - half1);
    c += vec3(0.65) * lineSm(hd, 0.010) * 0.035 * nearF * nearF;

    // edge rails: hot white core + colored bloom
    float eDist = uHalfW - abs(x);
    c += vec3(1.0, 1.0, 1.1) * lineSm(abs(eDist - 0.035), 0.016) * (0.35 + 0.65 * nearF);
    c += vec3(0.55, 0.60, 1.0) * (1.0 - smoothstep(0.0, 0.14, abs(eDist - 0.035))) * 0.26;

    // distance fog into the stage glow
    float fog = 1.0 - smoothstep(-8.8, -3.8, z);
    c = mix(c, vec3(0.185, 0.150, 0.33), fog);
    frag = vec4(c, 1.0);
})";

const char* kMeshVert = R"(#version 150
in vec3 aPos;
in vec3 aNormal;
in float aMat;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorld;
out float vMat;
void main()
{
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = normalize(transpose(inverse(mat3(uModel))) * aNormal);
    vMat = aMat;
    gl_Position = uViewProj * w;
})";

const char* kMeshFrag = R"(#version 150
in vec3 vNormal;
in vec3 vWorld;
in float vMat;
out vec4 frag;
uniform vec3 uLaneColor;
uniform vec3 uCapColor;
uniform vec3 uEye;
uniform float uEmissive;
void main()
{
    vec3 base = vMat < 0.5 ? uCapColor : uLaneColor;
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(-0.25, 1.0, 0.55));
    vec3 V = normalize(uEye - vWorld);
    vec3 Hv = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float shine = vMat > 0.5 ? 78.0 : 30.0;
    float spec = pow(max(dot(N, Hv), 0.0), shine);
    vec3 c = base * clamp(0.55 + 0.70 * diff, 0.0, 1.30)
           + vec3(1.0) * spec * (vMat > 0.5 ? 0.85 : 0.30);
    c += uLaneColor * uEmissive;
    // fog
    float fog = (1.0 - smoothstep(-8.8, -3.8, vWorld.z)) * 0.72;
    c = mix(c, vec3(0.185, 0.150, 0.33), fog);
    frag = vec4(c, 1.0);
})";

const char* kSpriteVert = R"(#version 150
in vec2 aPos;
uniform mat4 uViewProj;
uniform mat4 uView;
uniform vec3 uCenter;
uniform vec2 uSize;
uniform int uMode;      // 0 = camera billboard, 1 = flat on board, 2 = ribbon
uniform vec2 uP0;       // ribbon: x,z of tail
uniform vec2 uP1;       // ribbon: x,z of head
out vec2 vUV;
void main()
{
    vUV = aPos;
    if (uMode == 0)
    {
        vec4 vc = uView * vec4(uCenter, 1.0);
        vc.xy += aPos * uSize;
        // inverse of view rotation not needed: project from view space
        gl_Position = uViewProj * vec4(uCenter, 1.0);
        vec4 clipC = gl_Position;
        // offset in clip space proportional to distance
        gl_Position = clipC + vec4(aPos * uSize * vec2(1.0, 1.6) * (1.0 / max(-vc.z, 0.1)) * 2.2, 0.0, 0.0) * clipC.w;
    }
    else if (uMode == 1)
    {
        vec3 w = uCenter + vec3(aPos.x * uSize.x, 0.0, aPos.y * uSize.y);
        gl_Position = uViewProj * vec4(w, 1.0);
    }
    else
    {
        vec2 mid = mix(uP0, uP1, aPos.y * 0.5 + 0.5);
        vec2 dir = uP1 - uP0;
        vec2 perp = vec2(-dir.y, dir.x) / max(length(dir), 1.0e-5);
        vec3 w = vec3(mid.x + perp.x * aPos.x * uSize.x, uCenter.y,
                      mid.y + perp.y * aPos.x * uSize.x);
        gl_Position = uViewProj * vec4(w, 1.0);
    }
})";

const char* kSpriteFrag = R"(#version 150
in vec2 vUV;
out vec4 frag;
uniform vec4 uColor;
uniform int uShape;   // 0 = radial, 1 = edge-soft bar
uniform float uWiggle;   // 0..1: sustain wobble while the whammy is worked
uniform float uTimeS;
void main()
{
    float a;
    if (uShape == 0)
        a = 1.0 - smoothstep(0.0, 1.0, length(vUV));
    else if (uShape == 2)
    {
        float r = length(vUV);
        float ang = atan(vUV.y, vUV.x);
        float streak = pow(abs(cos(ang * 2.0)), 10.0);
        a = (1.0 - smoothstep(0.0, 0.40, r))
          + streak * (1.0 - smoothstep(0.0, 1.0, r)) * 0.9;
        a = clamp(a, 0.0, 1.0);
    }
    else
    {
        float ctr = uWiggle * 0.5 * sin(vUV.y * 15.0 - uTimeS * 22.0);
        a = (1.0 - smoothstep(0.5, 1.0, abs(vUV.x - ctr)))
          * (1.0 - smoothstep(0.9, 1.05, abs(vUV.y)));
    }
    frag = vec4(uColor.rgb, uColor.a * a);
})";

std::unique_ptr<juce::OpenGLShaderProgram> makeProgram(
    juce::OpenGLContext& ctx, const char* vert, const char* frag,
    std::initializer_list<const char*> attribs)
{
    auto p = std::make_unique<juce::OpenGLShaderProgram>(ctx);
    if (! p->addVertexShader(vert) || ! p->addFragmentShader(frag))
    {
        DBG("shader compile failed: " << p->getLastError());
        return nullptr;
    }
    int loc = 0;
    for (auto* a : attribs)
        glBindAttribLocation(p->getProgramID(), (GLuint) loc++, a);
    if (! p->link())
    {
        DBG("shader link failed: " << p->getLastError());
        return nullptr;
    }
    return p;
}
} // namespace

// ============================== mesh building ==============================
HighwayRenderer::Mesh HighwayRenderer::buildLathe(
    const juce::Array<juce::Point<float>>& profile, const juce::Array<float>& mats, int segments)
{
    juce::Array<float> verts;
    juce::Array<unsigned short> idx;
    const int rings = profile.size();

    for (int i = 0; i < rings; ++i)
    {
        // profile normal from neighbours (in r-y plane)
        const auto p = profile[i];
        const auto pPrev = profile[juce::jmax(0, i - 1)];
        const auto pNext = profile[juce::jmin(rings - 1, i + 1)];
        float dr = pNext.x - pPrev.x, dy = pNext.y - pPrev.y;
        float len = std::sqrt(dr * dr + dy * dy);
        if (len < 1.0e-6f) { dr = 0; dy = 1; len = 1; }
        const float nr = -dy / len, ny = dr / len;  // rotate tangent +90° (outward)

        for (int s = 0; s <= segments; ++s)
        {
            const float a = (float) s / (float) segments * juce::MathConstants<float>::twoPi;
            const float ca = std::cos(a), sa = std::sin(a);
            verts.add(p.x * ca);        // pos
            verts.add(p.y);
            verts.add(p.x * sa);
            verts.add(nr * ca);         // normal
            verts.add(ny);
            verts.add(nr * sa);
            verts.add(mats[i]);         // material id
        }
    }
    for (int i = 0; i < rings - 1; ++i)
        for (int s = 0; s < segments; ++s)
        {
            const int a0 = i * (segments + 1) + s;
            const int a1 = a0 + 1;
            const int b0 = a0 + segments + 1;
            const int b1 = b0 + 1;
            idx.add((unsigned short) a0); idx.add((unsigned short) b0); idx.add((unsigned short) a1);
            idx.add((unsigned short) a1); idx.add((unsigned short) b0); idx.add((unsigned short) b1);
        }

    Mesh m;
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * (int) sizeof(float), verts.getRawDataPointer(), GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * (int) sizeof(unsigned short), idx.getRawDataPointer(), GL_STATIC_DRAW);
    m.indexCount = idx.size();
    return m;
}

HighwayRenderer::Mesh HighwayRenderer::buildQuad()
{
    const float v[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    const unsigned short ix[] = { 0, 1, 2, 2, 1, 3 };
    Mesh m;
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(ix), ix, GL_STATIC_DRAW);
    m.indexCount = 6;
    return m;
}

// ============================== GL lifecycle ==============================
void HighwayRenderer::newOpenGLContextCreated()
{
    if (auto* env = std::getenv("GHMIDI_SNAPSHOT"))
        snapshotDir = env;

    // one VAO for everything (core profile requires one bound)
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    bgProg = makeProgram(*context, kBgVert, kBgFrag, { "aPos" });
    boardProg = makeProgram(*context, kBoardVert, kBoardFrag, { "aPos" });
    meshProg = makeProgram(*context, kMeshVert, kMeshFrag, { "aPos", "aNormal", "aMat" });
    spriteProg = makeProgram(*context, kSpriteVert, kSpriteFrag, { "aPos" });

    quad = buildQuad();

    // board: simple quad in world space (drawn with its own shader)
    {
        const float apron = 1.9f;
        const float v[] = {
            -kBoardHalf, 0, kZStrike + apron,
             kBoardHalf, 0, kZStrike + apron,
            -kBoardHalf, 0, -14.0f,
             kBoardHalf, 0, -14.0f,
        };
        const unsigned short ix[] = { 0, 1, 2, 2, 1, 3 };
        glGenBuffers(1, &board.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, board.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
        glGenBuffers(1, &board.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, board.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(ix), ix, GL_STATIC_DRAW);
        board.indexCount = 6;
    }

    // gem: squat glossy puck with a raised colored ring (GH3/CH style)
    {
        juce::Array<juce::Point<float>> prof;
        juce::Array<float> mats;
        auto add = [&](float r, float y, float mat) { prof.add({ r, y }); mats.add(mat); };
        add(0.000f, 0.150f, 1.0f);   // colored dome centre
        add(0.055f, 0.142f, 1.0f);
        add(0.092f, 0.118f, 1.0f);   // dome shoulder
        add(0.110f, 0.096f, 0.0f);   // dark bevel inner
        add(0.148f, 0.088f, 0.0f);   // bevel top
        add(0.166f, 0.050f, 0.0f);   // rim
        add(0.170f, 0.000f, 0.0f);   // base
        gemMesh = buildLathe(prof, mats, 28);
    }

    // strike button: donut with a hole
    {
        juce::Array<juce::Point<float>> prof;
        juce::Array<float> mats;
        auto add = [&](float r, float y, float mat) { prof.add({ r, y }); mats.add(mat); };
        add(0.075f, 0.000f, 0.0f);   // hole bottom inner
        add(0.075f, 0.050f, 0.0f);   // hole wall
        add(0.100f, 0.072f, 0.0f);   // inner slope (dark)
        add(0.140f, 0.086f, 0.0f);   // top face (dark)
        add(0.158f, 0.084f, 1.0f);   // thin colored rim
        add(0.186f, 0.060f, 1.0f);
        add(0.200f, 0.032f, 0.0f);   // outer wall
        add(0.205f, 0.000f, 0.0f);   // outer base
        buttonMesh = buildLathe(prof, mats, 28);
    }

    // pressed cap: small dome that rises in the button hole
    {
        juce::Array<juce::Point<float>> prof;
        juce::Array<float> mats;
        auto add = [&](float r, float y, float mat) { prof.add({ r, y }); mats.add(mat); };
        add(0.000f, 0.085f, 1.0f);
        add(0.045f, 0.080f, 1.0f);
        add(0.068f, 0.050f, 1.0f);
        add(0.070f, 0.000f, 0.0f);
        capMesh = buildLathe(prof, mats, 20);
    }

    startTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    ready = bgProg && boardProg && meshProg && spriteProg;
}

void HighwayRenderer::openGLContextClosing()
{
    auto kill = [](Mesh& m)
    {
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        m = {};
    };
    kill(quad); kill(board); kill(gemMesh); kill(buttonMesh); kill(capMesh);
    bgProg.reset(); boardProg.reset(); meshProg.reset(); spriteProg.reset();
    ready = false;
}

void HighwayRenderer::drawMesh(const Mesh& m)
{
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    const auto stride = (GLsizei) (7 * sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*) 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*) (3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*) (6 * sizeof(float)));
    glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_SHORT, nullptr);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
}

// ============================== frame ==============================
void HighwayRenderer::renderOpenGL()
{
    if (! ready)
    {
        juce::OpenGLHelpers::clear(juce::Colour(0xff05050a));
        if (snapshotDir.isNotEmpty() && juce::JUCEApplicationBase::isStandaloneApp()
            && ++notReadyFrames >= 90)
        {
            std::cerr << "GHMIDI_SNAPSHOT: renderer not ready (shader build failed)\n";
            notReadyFrames = -100000;
            juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
        }
        return;
    }

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float t = (float) std::fmod(now - startTime, 4096.0);
    frameT = t;
    const float scale = (float) context->getRenderingScale();
    const uint64_t vs = viewSize.load();
    const int wPx = juce::jmax(1, juce::roundToInt(scale * (float) (int) (vs >> 32)));
    const int hPx = juce::jmax(1, juce::roundToInt(scale * (float) (int) (vs & 0xffffffffu)));
    glViewport(0, 0, wPx, hPx);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // camera
    const float eye[3] = { 0.0f, 1.62f, 0.70f };
    const Mat4 view = matLookAt(eye[0], eye[1], eye[2], 0.0f, -0.30f, -3.55f);
    const Mat4 projM = matPerspective(55.0f, (float) wPx / (float) hPx, 0.1f, 60.0f);
    const Mat4 vp = matMul(projM, view);

    // ---- background ----
    bgProg->use();
    bgProg->setUniform("uTime", t);
    glBindBuffer(GL_ARRAY_BUFFER, quad.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad.ibo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*) 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);

    // ---- board ----
    boardProg->use();
    boardProg->setUniformMat4("uViewProj", vp.data(), 1, GL_FALSE);
    boardProg->setUniform("uScroll", (float) std::fmod(t * 1.05, 1.0));
    boardProg->setUniform("uLaneW", kLaneW);
    boardProg->setUniform("uHalfW", kBoardHalf);
    glBindBuffer(GL_ARRAY_BUFFER, board.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, board.ibo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*) 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);

    // gather state
    const auto gems = proc.guitar().getRecentGems();
    const int frets = proc.guitar().uiFretBits.load();
    const float dt = (float) (lastFrameTime < 0.0 ? 1.0 / 60.0
                              : juce::jlimit(0.001, 0.1, now - lastFrameTime));
    lastFrameTime = now;
    const float pressAlpha = 1.0f - std::exp(-dt * 26.0f);
    for (int i = 0; i < 5; ++i)
    {
        const float target = (frets & (1 << i)) ? 1.0f : 0.0f;
        pressAmount[i] += (target - pressAmount[i]) * pressAlpha;
    }

    // ---- contact shadows (blended, flat on board) ----
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (int lane = 0; lane < 5; ++lane)
        drawSprite(vp.data(), laneXw(lane), 0.004f, kZStrike, 0.30f, 0.30f,
                   juce::Colours::black, 0.55f, 1);
    for (const auto& gem : gems)
    {
        const double age = now - gem.t0;
        if (age > kTravelSecs || gem.mask == 0)
            continue;
        const float z = zForAge(age);
        const float fade = juce::jlimit(0.0f, 1.0f, (1.0f - (float) (age / kTravelSecs)) * 5.0f);
        for (int lane = 0; lane < 5; ++lane)
            if (gem.mask & (1 << lane))
                drawSprite(vp.data(), laneXw(lane), 0.004f, z, 0.26f, 0.26f,
                           juce::Colours::black, 0.45f * fade, 1);
    }

    // ---- sustain ribbons (additive) ----
    // eased whammy-wiggle: morphs in fast, melts out slower
    {
        const float target = proc.guitar().uiWhammy.load();
        const float rate = target > wigSm ? 11.0f : 4.5f;
        wigSm += (target - wigSm) * (1.0f - std::exp(-dt * rate));
        if (wigSm < 0.003f)
            wigSm = 0.0f;
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (const auto& gem : gems)
    {
        const double headAge = now - gem.t0;
        if (headAge > kTravelSecs)
            continue;
        const double tailAge = gem.t1 < 0 ? 0.0 : now - gem.t1;
        const float zHead = zForAge(headAge);
        const float zTail = zForAge(tailAge);
        if (zTail - zHead < 0.05f && zHead - zTail < 0.05f)
            continue;
        const float fade = juce::jlimit(0.0f, 1.0f, (1.0f - (float) (headAge / kTravelSecs)) * 5.0f);
        if (gem.mask == 0)
            continue;
        const float heldBoost = gem.t1 < 0 ? 1.0f : 0.62f;
        // GH-style: working the whammy makes a held sustain wobble
        const float wig = gem.t1 < 0 ? wigSm : 0.0f;
        for (int lane = 0; lane < 5; ++lane)
        {
            if (! (gem.mask & (1 << lane)))
                continue;
            const auto& lc = laneColours[lane];
            const juce::Colour c(juce::uint8(lc[0] * 255), juce::uint8(lc[1] * 255), juce::uint8(lc[2] * 255));
            drawSprite(vp.data(), 0.0f, 0.020f, 0.0f, 0.155f, 0.0f, c.withMultipliedBrightness(0.8f),
                       0.30f * fade * heldBoost, 2, laneXw(lane), zTail, laneXw(lane), zHead, 1, wig);
            drawSprite(vp.data(), 0.0f, 0.024f, 0.0f, 0.075f, 0.0f, c.brighter(0.6f),
                       0.75f * fade * heldBoost, 2, laneXw(lane), zTail, laneXw(lane), zHead, 1, wig);
        }
    }

    // ---- solid meshes ----
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    meshProg->use();
    meshProg->setUniformMat4("uViewProj", vp.data(), 1, GL_FALSE);
    meshProg->setUniform("uEye", eye[0], eye[1], eye[2]);

    // strike buttons
    for (int lane = 0; lane < 5; ++lane)
    {
        const auto& lc = laneColours[lane];
        const Mat4 model = matTRS(laneXw(lane), 0.0f, kZStrike, 1.12f);
        meshProg->setUniformMat4("uModel", model.data(), 1, GL_FALSE);
        const float lum = 0.62f + 0.38f * pressAmount[lane];
        meshProg->setUniform("uLaneColor", lc[0] * lum, lc[1] * lum, lc[2] * lum);
        meshProg->setUniform("uCapColor", 0.055f, 0.055f, 0.075f);
        meshProg->setUniform("uEmissive", pressAmount[lane] * 0.65f);
        drawMesh(buttonMesh);
        if (pressAmount[lane] > 0.03f)
        {
            const Mat4 capM = matTRS(laneXw(lane), 0.01f + 0.02f * pressAmount[lane], kZStrike,
                                     1.0f, pressAmount[lane]);
            meshProg->setUniformMat4("uModel", capM.data(), 1, GL_FALSE);
            meshProg->setUniform("uEmissive", pressAmount[lane] * 0.9f);
            drawMesh(capMesh);
        }
    }

    // note gems
    for (const auto& gem : gems)
    {
        const double age = now - gem.t0;
        if (age > kTravelSecs || gem.mask == 0)
            continue;
        const float z = zForAge(age);
        for (int lane = 0; lane < 5; ++lane)
        {
            if (! (gem.mask & (1 << lane)))
                continue;
            const auto& lc = laneColours[lane];
            const float s = gem.legato ? 1.02f : 1.25f;
            const Mat4 model = matTRS(laneXw(lane), 0.0f, z, s);
            meshProg->setUniformMat4("uModel", model.data(), 1, GL_FALSE);
            meshProg->setUniform("uLaneColor", lc[0], lc[1], lc[2]);
            if (gem.legato)
                meshProg->setUniform("uCapColor", 0.92f, 0.92f, 0.95f);  // HOPO: white ring
            else
                meshProg->setUniform("uCapColor", 0.055f, 0.055f, 0.075f);
            meshProg->setUniform("uEmissive", 0.06f);
            drawMesh(gemMesh);
        }
    }

    // open-strum bars (solid purple slab via ribbon, then glow)
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (const auto& gem : gems)
    {
        const double age = now - gem.t0;
        if (age > kTravelSecs || gem.mask != 0)
            continue;
        const float z = zForAge(age);
        const float fade = juce::jlimit(0.0f, 1.0f, (1.0f - (float) (age / kTravelSecs)) * 5.0f);
        const juce::Colour pc(juce::uint8(openColour[0] * 255), juce::uint8(openColour[1] * 255),
                              juce::uint8(openColour[2] * 255));
        drawSprite(vp.data(), 0.0f, 0.030f, 0.0f, 0.05f, 0.0f, pc, 0.9f * fade, 2,
                   -kBoardHalf * 0.9f, z, kBoardHalf * 0.9f, z);
        drawSprite(vp.data(), 0.0f, 0.05f, z, kBoardHalf * 0.95f, 0.22f, pc, 0.30f * fade, 1);
    }

    // glow billboards over gems + hit flashes
    for (const auto& gem : gems)
    {
        const double age = now - gem.t0;
        if (age > kTravelSecs || gem.mask == 0)
            continue;
        const float z = zForAge(age);
        const float fade = juce::jlimit(0.0f, 1.0f, (1.0f - (float) (age / kTravelSecs)) * 5.0f);
        for (int lane = 0; lane < 5; ++lane)
        {
            if (! (gem.mask & (1 << lane)))
                continue;
            const auto& lc = laneColours[lane];
            const juce::Colour c(juce::uint8(lc[0] * 255), juce::uint8(lc[1] * 255), juce::uint8(lc[2] * 255));
            drawSprite(vp.data(), laneXw(lane), 0.12f, z, 0.34f, 0.34f, c, 0.22f * fade, 0);
            if (age < 0.16)
            {
                const float fa = 1.0f - (float) (age / 0.16);
                drawSprite(vp.data(), laneXw(lane), 0.10f, kZStrike,
                           0.50f * (2.2f - fa), 0.50f * (2.2f - fa),
                           c.brighter(0.4f), 0.55f * fa, 0, 0, 0, 0, 0, 2);
                drawSprite(vp.data(), laneXw(lane), 0.10f, kZStrike,
                           0.30f * (2.0f - fa), 0.30f * (2.0f - fa),
                           juce::Colours::white, 0.85f * fa, 0);
            }
        }
    }
    // pressed-button glows
    for (int lane = 0; lane < 5; ++lane)
        if (pressAmount[lane] > 0.05f)
        {
            const auto& lc = laneColours[lane];
            const juce::Colour c(juce::uint8(lc[0] * 255), juce::uint8(lc[1] * 255), juce::uint8(lc[2] * 255));
            drawSprite(vp.data(), laneXw(lane), 0.10f, kZStrike, 0.42f, 0.42f, c,
                       0.35f * pressAmount[lane], 0);
        }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    ++frameCount;
    saveSnapshotIfRequested(wPx, hPx);
}

void HighwayRenderer::drawSprite(const float* viewProj, float wx, float wy, float wz,
                                 float sx, float sy, juce::Colour c, float alpha,
                                 int mode, float p0x, float p0z, float p1x, float p1z,
                                 int shape, float wiggle)
{
    spriteProg->use();
    spriteProg->setUniformMat4("uViewProj", viewProj, 1, GL_FALSE);
    // view matrix only needed for billboards; recompute (cheap)
    const Mat4 view = matLookAt(0.0f, 1.62f, 0.70f, 0.0f, -0.30f, -3.55f);
    spriteProg->setUniformMat4("uView", view.data(), 1, GL_FALSE);
    spriteProg->setUniform("uCenter", wx, wy, wz);
    spriteProg->setUniform("uSize", sx, sy);
    spriteProg->setUniform("uMode", mode);
    spriteProg->setUniform("uP0", p0x, p0z);
    spriteProg->setUniform("uP1", p1x, p1z);
    spriteProg->setUniform("uColor", c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), alpha);
    spriteProg->setUniform("uShape", shape >= 0 ? shape : (mode == 2 ? 1 : 0));
    spriteProg->setUniform("uWiggle", wiggle);
    spriteProg->setUniform("uTimeS", frameT);
    glBindBuffer(GL_ARRAY_BUFFER, quad.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad.ibo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*) 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
}

void HighwayRenderer::saveSnapshotIfRequested(int wPx, int hPx)
{
    // snapshot/auto-quit is a standalone-only development mode; never allow
    // it to run (and quit the process!) inside a host like FL Studio
    if (snapshotDir.isEmpty() || ! juce::JUCEApplicationBase::isStandaloneApp())
        return;
    static const int frames[] = { 200, 300, 420 };
    if (snapshotsSaved >= 3 || frameCount != frames[snapshotsSaved])
        return;

    juce::Image img(juce::Image::ARGB, wPx, hPx, false);
    juce::HeapBlock<uint8_t> pixels(wPx * hPx * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, wPx, hPx, GL_RGBA, GL_UNSIGNED_BYTE, pixels.getData());
    {
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < hPx; ++y)
        {
            const uint8_t* src = pixels.getData() + (size_t) (hPx - 1 - y) * (size_t) wPx * 4;
            for (int x = 0; x < wPx; ++x)
                bd.setPixelColour(x, y, juce::Colour(src[x * 4], src[x * 4 + 1], src[x * 4 + 2]));
        }
    }
    const auto file = juce::File(snapshotDir)
                          .getChildFile("snap_" + juce::String(snapshotsSaved) + ".png");
    file.deleteFile();
    juce::FileOutputStream os(file);
    if (os.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(img, os);
    }
    ++snapshotsSaved;
    if (snapshotsSaved >= 3)
        juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
}

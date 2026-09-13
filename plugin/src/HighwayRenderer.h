#pragma once
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"

// Real 3D Guitar Hero-style highway: perspective camera, lathe-built gem and
// button meshes with Phong lighting, shader highway with scrolling beat lines
// and distance fog, additive billboard glows.
//
// If GHMIDI_SNAPSHOT=<dir> is set, saves PNGs of the GL frame at a few fixed
// frame numbers and then quits the app (used for automated visual iteration).
class HighwayRenderer : public juce::OpenGLRenderer
{
public:
    HighwayRenderer(GHMidiProcessor& p, juce::Component& hostComp)
        : proc(p), host(hostComp) {}

    void setContext(juce::OpenGLContext* c) { context = c; }

    // called from the message thread; read atomically on the GL thread
    void setViewSize(int w, int h) noexcept
    {
        viewSize.store(((uint64_t) (uint32_t) juce::jmax(1, w) << 32)
                       | (uint64_t) (uint32_t) juce::jmax(1, h));
    }

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

private:
    struct Mesh
    {
        unsigned int vbo = 0, ibo = 0;
        int indexCount = 0;
    };

    Mesh buildLathe(const juce::Array<juce::Point<float>>& profile,
                    const juce::Array<float>& mats, int segments);
    Mesh buildQuad();
    void drawMesh(const Mesh& m);
    void drawSprite(const float* viewProj, float wx, float wy, float wz,
                    float sx, float sy, juce::Colour c, float alpha,
                    int mode, float p0x = 0, float p0z = 0, float p1x = 0, float p1z = 0,
                    int shape = -1, float wiggle = 0.0f);
    void saveSnapshotIfRequested(int wPx, int hPx);

    GHMidiProcessor& proc;
    juce::Component& host;
    juce::OpenGLContext* context = nullptr;

    std::unique_ptr<juce::OpenGLShaderProgram> bgProg, boardProg, meshProg, spriteProg;
    Mesh quad, board, gemMesh, buttonMesh, capMesh;
    bool ready = false;

    float pressAmount[5] { 0, 0, 0, 0, 0 };
    std::atomic<uint64_t> viewSize { ((uint64_t) 720 << 32) | 620 };
    double startTime = 0.0;
    double lastFrameTime = -1.0;
    float frameT = 0.0f;
    float wigSm = 0.0f;
    int notReadyFrames = 0;
    int frameCount = 0;
    juce::String snapshotDir;
    int snapshotsSaved = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HighwayRenderer)
};

#pragma once

#include <vector>

// Rectangle in normalized coordinates of the decoded video frame (0..1, origin top-left).
struct NormRect
{
    float x = 0, y = 0, w = 1, h = 1;
    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
};

struct EffectSettings
{
    bool mirror = false;
    bool rotate180 = false;
    float redness = 0;   // 0..1: how much facial redness to remove
    float smoothing = 0; // 0..1: skin smoothing strength
};

struct FrameParams
{
    NormRect crop;               // part of the input shown in the output (same aspect as the input)
    EffectSettings effects;
    std::vector<NormRect> faces; // where to apply retouching
    bool hasFaceInfo = false;    // false: the phone doesn't send faces -> retouch all skin-coloured pixels
};

// Virtual camera operator: follows the detected face(s) with a smooth crop ("auto-framing").
class FaceTracker
{
public:
    void Reset();

    // faces: latest detections (may be empty); facesFresh: detections are recent enough to trust.
    // tightness 0..1: 0 = wide (head and shoulders with lots of room), 1 = tight (head fills the frame).
    NormRect Update(const std::vector<NormRect>& faces, bool facesFresh, bool enabled, float tightness, double dt);

    static constexpr float MaxZoom = 3.0f;

private:
    // Current and target crop: centre and size (fraction of the full frame on both axes).
    float m_cx = 0.5f, m_cy = 0.5f, m_size = 1.0f;
    float m_targetCx = 0.5f, m_targetCy = 0.5f, m_targetSize = 1.0f;
    double m_timeWithoutFace = 0;
};

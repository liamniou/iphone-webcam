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

enum class FramingMode
{
    Off,    // full picture
    Follow, // auto-framing: continuously follows the face(s)
    Hold,   // on-demand framing: frames the face(s) once and keeps that crop
};

// Virtual camera operator: crops and zooms smoothly to frame the detected face(s).
class FaceTracker
{
public:
    void Reset();

    // faces: latest detections (may be empty); facesFresh: detections are recent enough to trust.
    // tightness 0..1: 0 = wide (head and shoulders with lots of room), 1 = tight (head fills the frame).
    NormRect Update(const std::vector<NormRect>& faces, bool facesFresh, FramingMode mode, float tightness, double dt);

    // Sets the crop that Hold mode moves to and keeps; returns false if there's no face to frame.
    bool FrameOnce(const std::vector<NormRect>& faces, float tightness);

    static constexpr float MaxZoom = 3.0f;

private:
    // Crop for the given faces: centre and size (fraction of the full frame on both axes).
    static void ComputeCrop(const std::vector<NormRect>& faces, float tightness, float& cx, float& cy, float& size);

    // Current and target crop.
    float m_cx = 0.5f, m_cy = 0.5f, m_size = 1.0f;
    float m_targetCx = 0.5f, m_targetCy = 0.5f, m_targetSize = 1.0f;
    float m_holdCx = 0.5f, m_holdCy = 0.5f, m_holdSize = 1.0f;
    double m_timeWithoutFace = 0;
};

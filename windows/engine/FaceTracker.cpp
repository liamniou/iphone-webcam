#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double ZoomOutAfterSeconds = 2.0;  // no face for this long -> back to the full frame
    constexpr double PositionTimeConstant = 0.45; // seconds; smaller = follows faster
    constexpr double SizeTimeConstant = 0.7;
    constexpr double ZoomOutTimeConstant = 1.2;
    constexpr float DeadZone = 0.05f;            // ignore movements smaller than this (fraction of the crop)
    constexpr float SizeDeadZone = 0.12f;

    float Ease(float current, float target, double dt, double timeConstant)
    {
        float alpha = static_cast<float>(1.0 - std::exp(-dt / timeConstant));
        return current + (target - current) * alpha;
    }
}

void FaceTracker::Reset()
{
    m_cx = m_cy = m_targetCx = m_targetCy = 0.5f;
    m_size = m_targetSize = 1.0f;
    m_timeWithoutFace = 0;
}

NormRect FaceTracker::Update(const std::vector<NormRect>& faces, bool facesFresh, bool enabled, float tightness, double dt)
{
    dt = std::clamp(dt, 0.0, 0.25);
    tightness = std::clamp(tightness, 0.0f, 1.0f);
    double positionTau = PositionTimeConstant;

    if (!enabled)
    {
        m_targetCx = m_targetCy = 0.5f;
        m_targetSize = 1.0f;
        m_timeWithoutFace = 0;
    }
    else if (facesFresh && !faces.empty())
    {
        m_timeWithoutFace = 0;

        // Frame all faces together.
        float left = 1, top = 1, right = 0, bottom = 0;
        for (const auto& f : faces)
        {
            left = std::min(left, f.x);
            top = std::min(top, f.y);
            right = std::max(right, f.x + f.w);
            bottom = std::max(bottom, f.y + f.h);
        }
        float faceW = std::max(0.0f, right - left), faceH = std::max(0.0f, bottom - top);

        // Crop height as a multiple of the face height: 4.5x (wide: head and shoulders) .. 2.2x (tight).
        float factor = 4.5f - 2.3f * tightness;
        float size = std::max(faceH * factor, faceW * 1.8f);
        size = std::clamp(size, 1.0f / MaxZoom, 1.0f);

        // Put the eyes a bit above the centre, like a camera operator would.
        float cx = left + faceW / 2;
        float cy = top + faceH / 2 + 0.08f * size;

        bool moved = std::abs(cx - m_targetCx) > DeadZone * m_targetSize ||
                     std::abs(cy - m_targetCy) > DeadZone * m_targetSize ||
                     std::abs(size - m_targetSize) > SizeDeadZone * m_targetSize;
        if (moved)
        {
            m_targetCx = cx;
            m_targetCy = cy;
            m_targetSize = size;
        }
    }
    else
    {
        m_timeWithoutFace += dt;
        if (m_timeWithoutFace > ZoomOutAfterSeconds)
        {
            m_targetCx = m_targetCy = 0.5f;
            m_targetSize = 1.0f;
            positionTau = ZoomOutTimeConstant;
        }
    }

    double sizeTau = m_targetSize > m_size ? std::max(SizeTimeConstant, positionTau) : SizeTimeConstant;
    m_size = Ease(m_size, m_targetSize, dt, sizeTau);
    m_cx = Ease(m_cx, m_targetCx, dt, positionTau);
    m_cy = Ease(m_cy, m_targetCy, dt, positionTau);

    // Keep the crop inside the frame.
    float half = m_size / 2;
    m_cx = std::clamp(m_cx, half, 1.0f - half);
    m_cy = std::clamp(m_cy, half, 1.0f - half);

    return NormRect{ m_cx - half, m_cy - half, m_size, m_size };
}

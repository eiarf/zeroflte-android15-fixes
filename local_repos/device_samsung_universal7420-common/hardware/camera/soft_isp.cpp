#define LOG_TAG "s6cam_isp"
#include "soft_isp.h"

#include <aidl/android/hardware/camera/device/CameraBlob.h>
#include <aidl/android/hardware/camera/device/CameraBlobId.h>
#include <hardware/gralloc.h>
#include <jpeglib.h>
#include <log/log.h>
#include <system/camera_metadata.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace s6cam {

namespace {

constexpr int kBlackLevelFront = 64;
constexpr int kBlackLevelRear = 4;
constexpr float kLinearTarget = 215.0f;

struct BayerOffsets {
    int rx, ry;
    int g0x, g0y;
    int g1x, g1y;
    int bx, by;
};

constexpr BayerOffsets getBayerOffsets(BayerPattern p) {
    switch (p) {
        case BayerPattern::GRBG:
            return {1, 0, 0, 0, 1, 1, 0, 1};
        case BayerPattern::GBRG:
            return {0, 1, 0, 0, 1, 1, 1, 0};
        case BayerPattern::RGGB:
            return {0, 0, 1, 0, 0, 1, 1, 1};
        case BayerPattern::BGGR:
        default:
            return {1, 1, 1, 0, 0, 1, 0, 0};
    }
}

inline uint8_t clampU8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

inline int clamp10(int v) {
    return v < 0 ? 0 : (v > 1023 ? 1023 : v);
}

inline int subBlack(uint16_t px, int blackLevel) {
    int v = static_cast<int>(px & 0x3FF) - blackLevel;
    return v > 0 ? v : 0;
}

// 4-neighbor same-plane impulse clamp for Defect Pixel Correction (DPC)
inline int clampDpc10(int c, int n0, int n1, int n2, int n3) {
    const int lo = std::min(std::min(n0, n1), std::min(n2, n3));
    const int hi = std::max(std::max(n0, n1), std::max(n2, n3));
    const int avg = (n0 + n1 + n2 + n3) >> 2;
    const int margin = 48 + (avg >> 3);
    if (c > hi + margin) return hi + (margin >> 1);
    if (c < lo - margin) return lo - (margin >> 1);
    return c;
}

inline int readDpcRaw(const uint16_t* raw, int32_t rawW, int32_t rawH, int32_t strideU16,
                      int x, int y, int blackLevel) {
    const int c = subBlack(raw[static_cast<size_t>(y) * strideU16 + x], blackLevel);
    if (x < 2 || x >= rawW - 2 || y < 2 || y >= rawH - 2) return c;
    const int n0 = subBlack(raw[static_cast<size_t>(y) * strideU16 + (x - 2)], blackLevel);
    const int n1 = subBlack(raw[static_cast<size_t>(y) * strideU16 + (x + 2)], blackLevel);
    const int n2 = subBlack(raw[static_cast<size_t>(y - 2) * strideU16 + x], blackLevel);
    const int n3 = subBlack(raw[static_cast<size_t>(y + 2) * strideU16 + x], blackLevel);
    return clampDpc10(c, n0, n1, n2, n3);
}

struct CellTap1D {
    int32_t i0;
    int32_t i1;
    int32_t w0;
    int32_t w1;
};

std::vector<CellTap1D> buildCellTapLut(float cropStart, float cropLen,
                                       int32_t cellDim, int32_t dstDim) {
    std::vector<CellTap1D> taps(static_cast<size_t>(dstDim));
    const float step = cropLen / std::max(1, dstDim);
    const int32_t maxIdx = std::max(0, cellDim - 1);
    for (int32_t d = 0; d < dstDim; ++d) {
        const float s = cropStart + (d + 0.5f) * step;
        const float fc = std::clamp(s * 0.5f - 0.5f, 0.0f, static_cast<float>(maxIdx));
        const int32_t i0 = std::clamp(static_cast<int32_t>(std::floor(fc)), 0, maxIdx);
        const int32_t i1 = std::min(i0 + 1, maxIdx);
        const int32_t w1 = (i0 == i1) ? 0 : static_cast<int32_t>((fc - i0) * 256.0f + 0.5f);
        taps[d] = {i0, i1, 256 - w1, w1};
    }
    return taps;
}

inline int clampDpcH10(int c, int left, int right) {
    const int lo = left < right ? left : right;
    const int hi = left > right ? left : right;
    const int margin = 64 + ((left + right) >> 3);
    if (c > hi + margin) return hi + (margin >> 1);
    if (c < lo - margin) return lo - (margin >> 1);
    return c;
}

inline int softKnee10Fast(int v) {
    if (v <= 820) return v < 0 ? 0 : v;
    if (v <= 1228) return 820 + ((v - 820) >> 2);
    const int out = 922 + ((v - 1228) >> 4);
    return out > 1023 ? 1023 : out;
}

inline void applyWbCcmFast(const WbCcmQ10& wc, int lr, int lg, int lb,
                           int& outR, int& outG, int& outB) {
    const int wr = std::min(1535, (lr * wc.rGainQ8) >> 8);
    const int wg = std::min(1535, (lg * wc.gGainQ8) >> 8);
    const int wb = std::min(1535, (lb * wc.bGainQ8) >> 8);
    const int* m = wc.ccm;
    outR = softKnee10Fast((m[0] * wr + m[1] * wg + m[2] * wb) >> 10);
    outG = softKnee10Fast((m[3] * wr + m[4] * wg + m[5] * wb) >> 10);
    outB = softKnee10Fast((m[6] * wr + m[7] * wg + m[8] * wb) >> 10);
}

void computeAspectSafeCrop(int32_t rawW, int32_t rawH, int32_t activeW, int32_t activeH,
                           int32_t dstW, int32_t dstH, const IspRequestParams& params,
                           float& outX, float& outY, float& outW, float& outH) {
    float normX = 0.0f, normY = 0.0f, normW = 1.0f, normH = 1.0f;
    if (params.hasCropRegion && params.cropW > 0 && params.cropH > 0 && activeW > 0 && activeH > 0) {
        normX = static_cast<float>(params.cropX) / activeW;
        normY = static_cast<float>(params.cropY) / activeH;
        normW = static_cast<float>(params.cropW) / activeW;
        normH = static_cast<float>(params.cropH) / activeH;
    }
    if (params.zoomRatio > 1.01f) {
        float invZ = 1.0f / std::clamp(params.zoomRatio, 1.0f, 8.0f);
        float cx = normX + normW * 0.5f;
        float cy = normY + normH * 0.5f;
        normW *= invZ;
        normH *= invZ;
        normX = cx - normW * 0.5f;
        normY = cy - normH * 0.5f;
    }

    float baseX = normX * rawW;
    float baseY = normY * rawH;
    float baseW = normW * rawW;
    float baseH = normH * rawH;

    float dstAspect = static_cast<float>(dstW) / std::max(1, dstH);
    float srcAspect = baseW / std::max(1.0f, baseH);
    if (srcAspect > dstAspect * 1.01f) {
        float newW = baseH * dstAspect;
        baseX += (baseW - newW) * 0.5f;
        baseW = newW;
    } else if (dstAspect > srcAspect * 1.01f) {
        float newH = baseW / dstAspect;
        baseY += (baseH - newH) * 0.5f;
        baseH = newH;
    }

    outX = std::clamp(baseX, 0.0f, static_cast<float>(rawW - 4));
    outY = std::clamp(baseY, 0.0f, static_cast<float>(rawH - 4));
    outW = std::clamp(baseW, 4.0f, static_cast<float>(rawW) - outX);
    outH = std::clamp(baseH, 4.0f, static_cast<float>(rawH) - outY);
}

WbCcmQ10 buildWbCcm(const AeAwbResult& gains) {
    static constexpr float kCcmFront[3][3] = {
        { 1.24f, -0.16f, -0.08f},
        {-0.06f,  1.12f, -0.06f},
        {-0.04f, -0.18f,  1.22f},
    };
    static constexpr float kCcmRear[3][3] = {
        { 1.24f, -0.16f, -0.08f},
        {-0.03f,  1.03f,  0.00f},
        {-0.04f, -0.16f,  1.20f},
    };
    const float (&kCcm)[3][3] = gains.isFront ? kCcmFront : kCcmRear;
    const int blackLevel = gains.isFront ? kBlackLevelFront : kBlackLevelRear;
    const float whiteScale = 256.0f * 1023.0f / (1023.0f - blackLevel);
    const float scale = std::clamp(gains.swDgain, 1.0f, 8.0f) * whiteScale;
    WbCcmQ10 wc{};
    wc.rGainQ8 = static_cast<int32_t>(std::lround(gains.rGain * scale));
    wc.gGainQ8 = static_cast<int32_t>(std::lround(gains.gGain * scale));
    wc.bGainQ8 = static_cast<int32_t>(std::lround(gains.bGain * scale));
    const float isoDesat = std::clamp((gains.swDgain - 1.0f) * 0.25f, 0.0f, 0.50f);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const float ident = (r == c) ? 1.0f : 0.0f;
            const float val = kCcm[r][c] * (1.0f - isoDesat) + ident * isoDesat;
            wc.ccm[r * 3 + c] = static_cast<int32_t>(std::lround(val * 1024.0f));
        }
    }
    return wc;
}

// Smooth highlight roll-off above linear 820 so bright highlights compress smoothly
inline int softKnee10(int v) {
    if (v <= 820) return v < 0 ? 0 : v;
    const int over = v - 820;
    return std::min(1023, 820 + (over * 203) / (over + 203));
}

inline void applyWbCcm(const WbCcmQ10& wc, int lr, int lg, int lb,
                       int& outR, int& outG, int& outB) {
    const int wr = std::min(1535, (lr * wc.rGainQ8) >> 8);
    const int wg = std::min(1535, (lg * wc.gGainQ8) >> 8);
    const int wb = std::min(1535, (lb * wc.bGainQ8) >> 8);
    const int* m = wc.ccm;
    outR = softKnee10((m[0] * wr + m[1] * wg + m[2] * wb) >> 10);
    outG = softKnee10((m[3] * wr + m[4] * wg + m[5] * wb) >> 10);
    outB = softKnee10((m[6] * wr + m[7] * wg + m[8] * wb) >> 10);
}

// 5-tap edge-aware Chroma NR using pre-allocated scratch buffers (zero heap alloc per row)
inline void chromaNrPtrRow(int32_t n, uint8_t* cb, uint8_t* cr, size_t step,
                           uint8_t* cbCopy, uint8_t* crCopy) {
    if (n < 5) return;
    for (int32_t i = 0; i < n; ++i) {
        cbCopy[i] = cb[static_cast<size_t>(i) * step];
        crCopy[i] = cr[static_cast<size_t>(i) * step];
    }
    constexpr int32_t kEdgeThresh = 24;
    for (int32_t i = 2; i < n - 2; ++i) {
        const int cCb = cbCopy[i];
        const int cCr = crCopy[i];
        int sumCb = cCb * 2, sumCr = cCr * 2, wSum = 2;
        for (int k = -2; k <= 2; ++k) {
            if (k == 0) continue;
            const int nCb = cbCopy[i + k];
            const int nCr = crCopy[i + k];
            if (std::abs(nCb - cCb) + std::abs(nCr - cCr) <= kEdgeThresh) {
                const int w = (std::abs(k) == 1) ? 2 : 1;
                sumCb += nCb * w;
                sumCr += nCr * w;
                wSum += w;
            }
        }
        cb[static_cast<size_t>(i) * step] = static_cast<uint8_t>(sumCb / wSum);
        cr[static_cast<size_t>(i) * step] = static_cast<uint8_t>(sumCr / wSum);
    }
}

int orientationToExif(int deg) {
    switch (((deg % 360) + 360) % 360) {
        case 90:  return 6;
        case 180: return 3;
        case 270: return 8;
        default:  return 1;
    }
}

std::vector<uint8_t> buildExifApp1(int32_t orientationDeg, int64_t expoNs, int32_t iso, float focalMm) {
    std::vector<uint8_t> b;
    auto push16 = [&](uint16_t v) {
        b.push_back(static_cast<uint8_t>(v >> 8));
        b.push_back(static_cast<uint8_t>(v & 0xFF));
    };
    auto push32 = [&](uint32_t v) {
        b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        b.push_back(static_cast<uint8_t>(v & 0xFF));
    };
    auto patch32 = [&](size_t pos, uint32_t v) {
        b[pos + 0] = static_cast<uint8_t>((v >> 24) & 0xFF);
        b[pos + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        b[pos + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        b[pos + 3] = static_cast<uint8_t>(v & 0xFF);
    };

    const char hdr[] = {'E', 'x', 'i', 'f', 0, 0};
    b.insert(b.end(), hdr, hdr + 6);
    const size_t tiffStart = b.size();
    b.push_back('M'); b.push_back('M');
    push16(42);
    push32(8);

    push16(4);
    const size_t makeOffPos = b.size() + 8;
    push16(0x010F); push16(2); push32(8); push32(0);
    const size_t modelOffPos = b.size() + 8;
    push16(0x0110); push16(2); push32(9); push32(0);
    push16(0x0112); push16(3); push32(1); push16(orientationToExif(orientationDeg)); push16(0);
    const size_t exifIfdOffPos = b.size() + 8;
    push16(0x8769); push16(4); push32(1); push32(0);
    push32(0);

    patch32(makeOffPos, static_cast<uint32_t>(b.size() - tiffStart));
    const char mk[] = "samsung";
    b.insert(b.end(), mk, mk + sizeof(mk));

    patch32(modelOffPos, static_cast<uint32_t>(b.size() - tiffStart));
    const char md[] = "SM-G920I";
    b.insert(b.end(), md, md + sizeof(md));
    if ((b.size() & 1) != 0) b.push_back(0);

    patch32(exifIfdOffPos, static_cast<uint32_t>(b.size() - tiffStart));
    push16(3);
    const size_t expOffPos = b.size() + 8;
    push16(0x829A); push16(5); push32(1); push32(0);
    push16(0x8827); push16(3); push32(1); push16(static_cast<uint16_t>(std::clamp(iso, 50, 6400))); push16(0);
    const size_t focOffPos = b.size() + 8;
    push16(0x920A); push16(5); push32(1); push32(0);
    push32(0);

    patch32(expOffPos, static_cast<uint32_t>(b.size() - tiffStart));
    uint32_t expUs = static_cast<uint32_t>(std::max<int64_t>(100, expoNs / 1000LL));
    push32(expUs); push32(1000000U);

    patch32(focOffPos, static_cast<uint32_t>(b.size() - tiffStart));
    push32(static_cast<uint32_t>(std::lround(focalMm * 100.0f))); push32(100U);

    return b;
}

}  // namespace

AeAwbController::AeAwbController(BayerPattern pattern, bool isFront)
    : pattern_(pattern), isFront_(isFront) {}

void AeAwbController::reset(const SensorCtrlLimits& limits) {
    limits_ = limits;
    state_.exposureLines = std::clamp(limits_.expoDef, limits_.expoMin, limits_.expoMax);
    state_.analogGain = std::clamp(limits_.againDef, limits_.againMin, limits_.againMax);
    state_.digitalGain = std::clamp(limits_.dgainDef, limits_.dgainMin, limits_.dgainMax);
    state_.swDgain = 1.0f;
    state_.appliedExposureNs = static_cast<int64_t>(state_.exposureLines * limits_.lineDurationNs);
    state_.appliedSensitivity = (state_.analogGain * 100) / std::max(1, limits_.againMin);
    state_.rGain = isFront_ ? 1.65f : 1.38f;
    state_.gGain = 1.00f;
    state_.bGain = isFront_ ? 1.80f : 2.35f;
    state_.isFront = isFront_;
    state_.aeState = ANDROID_CONTROL_AE_STATE_SEARCHING;
    state_.awbState = ANDROID_CONTROL_AWB_STATE_SEARCHING;
    exposureValue_ = state_.exposureLines * (state_.analogGain / 32.0) * (state_.digitalGain / 256.0);
    aeHoldFrames_ = 0;
    wbFrames_ = 0;
    initialized_ = true;
}

AeAwbResult AeAwbController::updateFromRaw(const uint16_t* raw, int32_t width, int32_t height,
                                           int32_t strideBytes, const IspRequestParams& params,
                                           bool fastConvergence, bool freezeAe) {
    state_.isFront = isFront_;
    if (!initialized_) {
        reset(limits_);
    }

    if (params.aeMode == ANDROID_CONTROL_AE_MODE_OFF || params.controlMode == ANDROID_CONTROL_MODE_OFF) {
        int32_t reqLines = static_cast<int32_t>(
            std::llround(params.manualExposureNs / std::max(1.0, limits_.lineDurationNs)));
        state_.exposureLines = std::clamp(reqLines, limits_.expoMin, limits_.expoMax);
        int32_t reqAgain = std::clamp((params.manualSensitivity * limits_.againMin) / 100,
                                      limits_.againMin, limits_.againMax);
        state_.analogGain = reqAgain;
        state_.digitalGain = limits_.dgainDef;
        state_.swDgain = 1.0f;
        state_.appliedExposureNs = static_cast<int64_t>(state_.exposureLines * limits_.lineDurationNs);
        state_.appliedSensitivity = (state_.analogGain * 100) / std::max(1, limits_.againMin);
        state_.aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
    } else if (params.aeLock) {
        state_.aeState = ANDROID_CONTROL_AE_STATE_LOCKED;
    }

    if (params.awbMode == ANDROID_CONTROL_AWB_MODE_INCANDESCENT) {
        state_.rGain = 1.05f; state_.gGain = 1.0f; state_.bGain = 2.05f;
        state_.awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    } else if (params.awbMode == ANDROID_CONTROL_AWB_MODE_FLUORESCENT) {
        state_.rGain = 1.25f; state_.gGain = 1.0f; state_.bGain = 1.65f;
        state_.awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    } else if (params.awbMode == ANDROID_CONTROL_AWB_MODE_DAYLIGHT) {
        state_.rGain = 1.48f; state_.gGain = 1.0f; state_.bGain = 1.38f;
        state_.awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    } else if (params.awbMode == ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT) {
        state_.rGain = 1.60f; state_.gGain = 1.0f; state_.bGain = 1.28f;
        state_.awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
    } else if (params.awbLock) {
        state_.awbState = ANDROID_CONTROL_AWB_STATE_LOCKED;
    }

    if (!raw || width < 16 || height < 16) return state_;

    const int32_t strideU16 = strideBytes / 2;
    const BayerOffsets bo = getBayerOffsets(pattern_);
    const int stepX = std::max(2, (width / 64) & ~1);
    const int stepY = std::max(2, (height / 48) & ~1);

    double sumLuma = 0.0;
    double countLuma = 0.0;
    double highlightCount = 0.0;
    double totalSamples = 0.0;
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    double countWb = 0.0;
    double allR = 0.0, allG = 0.0, allB = 0.0;
    double countAll = 0.0;

    const int marginX = width / 10;
    const int marginY = height / 10;
    const int centerX0 = width / 4;
    const int centerX1 = (width * 3) / 4;
    const int centerY0 = height / 4;
    const int centerY1 = (height * 3) / 4;
    const double curRGain = static_cast<double>(state_.rGain);
    const double curBGain = static_cast<double>(state_.bGain);

    const int blackLevel = isFront_ ? kBlackLevelFront : kBlackLevelRear;
    for (int y = marginY; y < height - marginY - 1; y += stepY) {
        const int cy = y & ~1;
        const bool inCenterY = (cy >= centerY0 && cy <= centerY1);
        for (int x = marginX; x < width - marginX - 1; x += stepX) {
            const int cx = x & ~1;
            int r = static_cast<int>(raw[(cy + bo.ry) * strideU16 + cx + bo.rx] & 0x3FF) - blackLevel;
            int g0 = static_cast<int>(raw[(cy + bo.g0y) * strideU16 + cx + bo.g0x] & 0x3FF) - blackLevel;
            int g1 = static_cast<int>(raw[(cy + bo.g1y) * strideU16 + cx + bo.g1x] & 0x3FF) - blackLevel;
            int b = static_cast<int>(raw[(cy + bo.by) * strideU16 + cx + bo.bx] & 0x3FF) - blackLevel;
            r = std::max(0, r);
            b = std::max(0, b);
            int g = std::max(0, (g0 + g1) >> 1);

            const double weight = (inCenterY && cx >= centerX0 && cx <= centerX1) ? 2.0 : 1.0;
            double yLin = 0.299 * r + 0.587 * g + 0.114 * b;
            sumLuma += yLin * weight;
            countLuma += weight;

            int maxC = std::max({r, g, b});
            int minC = std::min({r, g, b});
            if (maxC >= 920) highlightCount += 1.0;
            totalSamples += 1.0;

            const double effGain = std::clamp(static_cast<double>(state_.swDgain), 1.0, 8.0);
            if (maxC < 900 && (static_cast<double>(minC) * effGain) > 10.0) {
                allR += r;
                allG += g;
                allB += b;
                countAll += 1.0;

                // Evaluate neutrality in current WB-compensated space (not raw space, where G naturally exceeds R/B > 2.25x)
                const double wr = r * curRGain;
                const double wg = static_cast<double>(g);
                const double wb = b * curBGain;
                const double maxW = std::max({wr, wg, wb});
                const double minW = std::min({wr, wg, wb});
                const double neutTol = (!isFront_ && wbFrames_ > 6) ? 1.38 : 1.45;
                if (maxW <= minW * neutTol) {
                    const double wbW = (maxC >= 180) ? 2.0 : 1.0;
                    sumR += r * wbW;
                    sumG += g * wbW;
                    sumB += b * wbW;
                    countWb += wbW;
                }
            }
        }
    }

    if (freezeAe && !fastConvergence) {
        // Keep exposure and sensor gain frozen during active AF sweeps
    } else if (aeHoldFrames_ > 0 && !fastConvergence) {
        --aeHoldFrames_;
    } else if (countLuma > 0.0 &&
        params.aeMode != ANDROID_CONTROL_AE_MODE_OFF &&
        params.controlMode != ANDROID_CONTROL_MODE_OFF &&
        !params.aeLock) {
        aeHoldFrames_ = 0;
        double meanY = std::max(4.0, (sumLuma / countLuma) * static_cast<double>(state_.swDgain));
        double compMult = std::pow(2.0, params.aeExposureComp / 3.0);
        double hiFrac = (totalSamples > 0.0) ? (highlightCount / totalSamples) : 0.0;
        double hiGuard = (hiFrac > 0.06) ? std::max(0.78, 1.0 - (hiFrac - 0.06) * 2.0) : 1.0;
        double target = kLinearTarget * compMult * hiGuard;
        double ratio = target / meanY;

        if (std::abs(ratio - 1.0) < 0.12) {
            state_.aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
        } else {
            state_.aeState = ANDROID_CONTROL_AE_STATE_SEARCHING;
            ratio = fastConvergence ? std::clamp(ratio, 0.03, 4.0) : std::clamp(ratio, 0.5, 2.0);
            const double damp = fastConvergence ? 1.00 : 0.35;
            double dampedRatio = 1.0 + damp * (ratio - 1.0);
            const int32_t totalDgainMax = isFront_ ? 768 : 1344;
            const int32_t hwDgainMax = std::clamp(768, limits_.dgainMin, limits_.dgainMax);
            double maxEv = static_cast<double>(limits_.expoMax) *
                           (limits_.againMax / 32.0) * (totalDgainMax / 256.0);
            double minEv = static_cast<double>(limits_.expoMin);
            exposureValue_ = std::clamp(exposureValue_ * dampedRatio, minEv, maxEv);

            double prefMaxLines = std::min<double>(limits_.expoMax,
                                                   33333333.0 / std::max(1.0, limits_.lineDurationNs));
            if (prefMaxLines < limits_.expoMin) prefMaxLines = limits_.expoMax;

            double lines = std::clamp(exposureValue_, static_cast<double>(limits_.expoMin), prefMaxLines);
            double rem = exposureValue_ / std::max(1.0, lines);
            double againF = std::clamp(rem * 32.0, static_cast<double>(limits_.againMin),
                                       static_cast<double>(limits_.againMax));
            double totalDgainF = std::clamp((exposureValue_ / std::max(1.0, lines * (againF / 32.0))) * 256.0,
                                            static_cast<double>(limits_.dgainMin),
                                            static_cast<double>(totalDgainMax));
            double hwDgainF = std::clamp(totalDgainF, static_cast<double>(limits_.dgainMin),
                                         static_cast<double>(hwDgainMax));
            float newSwDgain = static_cast<float>(
                std::clamp(totalDgainF / std::max(1.0, hwDgainF), 1.0, 8.0));

            int32_t newExpo = static_cast<int32_t>(std::lround(lines));
            int32_t newAgain = static_cast<int32_t>(std::lround(againF));
            int32_t newDgain = static_cast<int32_t>(std::lround(hwDgainF));
            const bool hwUnchanged =
                (newExpo == state_.exposureLines && newAgain == state_.analogGain &&
                 newDgain == state_.digitalGain && std::abs(newSwDgain - state_.swDgain) < 0.02f);
            if (newExpo != state_.exposureLines || newAgain != state_.analogGain ||
                newDgain != state_.digitalGain) {
                aeHoldFrames_ = 2;
            }
            state_.exposureLines = newExpo;
            state_.analogGain = newAgain;
            state_.digitalGain = newDgain;
            state_.swDgain = newSwDgain;
            if (hwUnchanged) {
                state_.aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
            }
            state_.appliedExposureNs = static_cast<int64_t>(state_.exposureLines * limits_.lineDurationNs);
            double totalIsoMult = (state_.analogGain / std::max(1.0, static_cast<double>(limits_.againMin))) *
                                  (state_.digitalGain / 256.0) * state_.swDgain;
            state_.appliedSensitivity = static_cast<int32_t>(std::llround(totalIsoMult * 100.0));
        }
    }

    const bool useNeutral = (countWb >= 24.0);
    if ((useNeutral || countAll >= 24.0) &&
        params.awbMode == ANDROID_CONTROL_AWB_MODE_AUTO && !params.awbLock) {
        const double denom = useNeutral ? countWb : countAll;
        double avgR = std::max(6.0, (useNeutral ? sumR : allR) / denom);
        double avgG = std::max(6.0, (useNeutral ? sumG : allG) / denom);
        double avgB = std::max(6.0, (useNeutral ? sumB : allB) / denom);
        const double minG = isFront_ ? 0.75 : 0.70;
        const double maxG = isFront_ ? 4.20 : 6.50;
        const double rearRedMul = isFront_ ? 1.0 : 1.04;
        const double rearBlueMul = isFront_ ? 1.0 : 1.02;
        float targetR = static_cast<float>(std::clamp((avgG / avgR) * rearRedMul, minG, maxG));
        float targetB = static_cast<float>(std::clamp((avgG / avgB) * rearBlueMul, minG, maxG));

        float errR = std::abs(targetR - state_.rGain);
        float errB = std::abs(targetB - state_.bGain);
        const float alpha = (fastConvergence || wbFrames_ < 6) ? 0.50f : 0.25f;
        state_.rGain = state_.rGain * (1.0f - alpha) + targetR * alpha;
        state_.gGain = 1.00f;
        state_.bGain = state_.bGain * (1.0f - alpha) + targetB * alpha;
        ++wbFrames_;
        state_.awbState = (errR < 0.04f && errB < 0.04f)
                              ? ANDROID_CONTROL_AWB_STATE_CONVERGED
                              : ANDROID_CONTROL_AWB_STATE_SEARCHING;
    }

    return state_;
}

SoftIsp::SoftIsp() {
    initGammaLut();
    constexpr size_t kNumWorkers = 5;
    workers_.reserve(kNumWorkers);
    for (size_t id = 0; id < kNumWorkers; ++id) {
        workers_.emplace_back([this, id]() {
            uint64_t seenEpoch = 0;
            while (true) {
                std::function<void(int32_t, int32_t)> fn;
                int32_t h = 0;
                {
                    std::unique_lock<std::mutex> lk(poolMtx_);
                    poolCv_.wait(lk, [&]() { return poolStop_ || jobEpoch_ > seenEpoch; });
                    if (poolStop_) return;
                    seenEpoch = jobEpoch_;
                    fn = jobFn_;
                    h = jobHeight_;
                }
                const int32_t total = static_cast<int32_t>(workers_.size()) + 1;
                const int32_t chunk = (h + total - 1) / total;
                const int32_t y0 = static_cast<int32_t>(id) * chunk;
                const int32_t y1 = std::min(h, y0 + chunk);
                if (y0 < y1 && fn) {
                    fn(y0, y1);
                }
                {
                    std::lock_guard<std::mutex> lk(poolMtx_);
                    if (--activeJobs_ == 0) {
                        doneCv_.notify_all();
                    }
                }
            }
        });
    }
}

SoftIsp::~SoftIsp() {
    {
        std::lock_guard<std::mutex> lk(poolMtx_);
        poolStop_ = true;
    }
    poolCv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void SoftIsp::runParallel(int32_t height, const std::function<void(int32_t, int32_t)>& fn) {
    if (height < 256 || workers_.empty()) {
        fn(0, height);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(poolMtx_);
        jobFn_ = fn;
        jobHeight_ = height;
        activeJobs_ = workers_.size();
        ++jobEpoch_;
    }
    poolCv_.notify_all();

    const int32_t total = static_cast<int32_t>(workers_.size()) + 1;
    const int32_t chunk = (height + total - 1) / total;
    const int32_t yLast0 = static_cast<int32_t>(workers_.size()) * chunk;
    if (yLast0 < height) {
        fn(yLast0, height);
    }

    std::unique_lock<std::mutex> lk(poolMtx_);
    doneCv_.wait(lk, [&]() { return activeJobs_ == 0; });
    jobFn_ = nullptr;
}

const uint16_t* SoftIsp::stageRaw(const uint16_t* raw, int32_t rawW, int32_t rawH,
                                  int32_t rawStrideBytes) {
    if (!raw || rawW <= 0 || rawH <= 0 || rawStrideBytes < rawW * 2) return nullptr;
    const size_t need = static_cast<size_t>(rawW) * rawH;
    if (cachedRaw_.size() < need) {
        cachedRaw_.resize(need);
    }
    stagedW_ = rawW;
    demosaicValid_ = false;
    demosaicSrc_ = nullptr;
    const uint8_t* srcBase = reinterpret_cast<const uint8_t*>(raw);
    uint16_t* dstBase = cachedRaw_.data();

    runParallel(rawH, [&](int32_t y0, int32_t y1) {
        for (int32_t y = y0; y < y1; ++y) {
            const uint16_t* sRow = reinterpret_cast<const uint16_t*>(
                srcBase + static_cast<size_t>(y) * rawStrideBytes);
            uint16_t* dRow = dstBase + static_cast<size_t>(y) * rawW;
            int32_t x = 0;
#if defined(__aarch64__)
            for (; x + 32 <= rawW; x += 32) {
                uint16x8_t v0 = vld1q_u16(sRow + x);
                uint16x8_t v1 = vld1q_u16(sRow + x + 8);
                uint16x8_t v2 = vld1q_u16(sRow + x + 16);
                uint16x8_t v3 = vld1q_u16(sRow + x + 24);
                vst1q_u16(dRow + x, v0);
                vst1q_u16(dRow + x + 8, v1);
                vst1q_u16(dRow + x + 16, v2);
                vst1q_u16(dRow + x + 24, v3);
            }
#endif
            if (x < rawW) {
                std::memcpy(dRow + x, sRow + x, static_cast<size_t>(rawW - x) * sizeof(uint16_t));
            }
        }
    });
    return cachedRaw_.data();
}

void SoftIsp::initGammaLut() {
    for (int i = 0; i < 1024; ++i) {
        double x = static_cast<double>(i) / 1023.0;
        double srgb = (x <= 0.0031308) ? (x * 12.92) : (1.055 * std::pow(x, 1.0 / 2.2) - 0.055);
        double scurve = srgb * srgb * (3.0 - 2.0 * srgb);
        double tone = srgb * 0.72 + scurve * 0.28;
        int v = static_cast<int>(std::lround(tone * 255.0));
        gammaLut_[i] = clampU8(v);
    }
}

bool SoftIsp::processOutput(const uint16_t* raw, int32_t rawW, int32_t rawH, int32_t rawStrideBytes,
                            int32_t activeW, int32_t activeH, BayerPattern pattern,
                            const AeAwbResult& gains, const IspRequestParams& params,
                            const LockedOutput& dst, int32_t* outBlobSizeBytes) {
    if (!raw || rawW < 8 || rawH < 8 || rawStrideBytes < rawW * 2) return false;

    const int32_t rawStrideU16 = rawStrideBytes / 2;
    float cropX = 0.0f, cropY = 0.0f, cropW = 0.0f, cropH = 0.0f;
    computeAspectSafeCrop(rawW, rawH, activeW, activeH, dst.width, dst.height, params,
                          cropX, cropY, cropW, cropH);

    const WbCcmQ10 wc = buildWbCcm(gains);
    const bool lowLightSmooth = (gains.swDgain > 1.25f || gains.appliedSensitivity >= 400);

    const int blackLevel = gains.isFront ? kBlackLevelFront : kBlackLevelRear;
    if (!gains.isFront) {
        static int32_t lastDiagW = 0;
        static int32_t lastDiagFmt = 0;
        const bool modeChanged = (rawW != lastDiagW || dst.format != lastDiagFmt);
        if (modeChanged || dst.format == HAL_PIXEL_FORMAT_BLOB) {
            lastDiagW = rawW;
            lastDiagFmt = dst.format;
            const BayerOffsets bo = getBayerOffsets(pattern);
            int minR = 1023, minG = 1023, minB = 1023;
            int maxR = 0, maxG = 0, maxB = 0;
            uint64_t sumR = 0, sumG = 0, sumB = 0, cnt = 0;
            const int x0 = (rawW / 4) & ~1, x1 = ((rawW * 3) / 4) & ~1;
            const int y0 = (rawH / 4) & ~1, y1 = ((rawH * 3) / 4) & ~1;
            const int st = std::max(2, ((rawW / 128) & ~1));
            for (int y = y0; y < y1 - 1; y += st) {
                for (int x = x0; x < x1 - 1; x += st) {
                    const int vr = raw[static_cast<size_t>(y + bo.ry) * rawStrideU16 + x + bo.rx] & 0x3FF;
                    const int vg0 = raw[static_cast<size_t>(y + bo.g0y) * rawStrideU16 + x + bo.g0x] & 0x3FF;
                    const int vg1 = raw[static_cast<size_t>(y + bo.g1y) * rawStrideU16 + x + bo.g1x] & 0x3FF;
                    const int vb = raw[static_cast<size_t>(y + bo.by) * rawStrideU16 + x + bo.bx] & 0x3FF;
                    const int vg = (vg0 + vg1) >> 1;
                    minR = std::min(minR, vr); minG = std::min(minG, vg); minB = std::min(minB, vb);
                    maxR = std::max(maxR, vr); maxG = std::max(maxG, vg); maxB = std::max(maxB, vb);
                    sumR += vr; sumG += vg; sumB += vb;
                    ++cnt;
                }
            }
            if (cnt > 0) {
                ALOGV("cam0 rawdiag %dx%d -> dst %dx%d fmt=0x%x yuv=%d | minRGB=(%d,%d,%d) avgRGB=(%.1f,%.1f,%.1f) maxRGB=(%d,%d,%d) | rG=%.2f bG=%.2f swD=%.2f iso=%d exp=%.1fms",
                      rawW, rawH, dst.width, dst.height, dst.format, dst.isYcbcr ? 1 : 0,
                      minR, minG, minB,
                      static_cast<double>(sumR) / cnt,
                      static_cast<double>(sumG) / cnt,
                      static_cast<double>(sumB) / cnt,
                      maxR, maxG, maxB,
                      gains.rGain, gains.bGain, gains.swDgain, gains.appliedSensitivity,
                      gains.appliedExposureNs / 1e6);
            }
        }
    }

    if (dst.format == HAL_PIXEL_FORMAT_BLOB) {
        return writeJpeg(raw, rawW, rawH, rawStrideU16, pattern, cropX, cropY, cropW, cropH,
                         wc, params, gains, blackLevel, dst, outBlobSizeBytes);
    }
    if (dst.isYcbcr) {
        return writeYuv(raw, rawW, rawH, rawStrideU16, pattern, cropX, cropY, cropW, cropH,
                        wc, lowLightSmooth, blackLevel, dst);
    }
    return writeRgba(raw, rawW, rawH, rawStrideU16, pattern, cropX, cropY, cropW, cropH,
                     wc, lowLightSmooth, blackLevel, dst);
}

void SoftIsp::ensureHalfResRgb(const uint16_t* raw, int32_t rawW, int32_t rawH, int32_t rawStrideU16,
                               BayerPattern pattern, const WbCcmQ10& wc, bool lowLightSmooth,
                               int blackLevel) {
    const int32_t cellW = std::max(1, rawW / 2);
    const int32_t cellH = std::max(1, rawH / 2);
    if (demosaicValid_ && demosaicSrc_ == raw && cachedCellW_ == cellW && cachedCellH_ == cellH) {
        return;
    }

    previewRgb_.resize(static_cast<size_t>(cellW) * cellH);

    runParallel(cellH, [&](int32_t cy0, int32_t cy1) {
        for (int32_t cy = cy0; cy < cy1; ++cy) {
            const uint16_t* r0 = raw + static_cast<size_t>(cy * 2) * rawStrideU16;
            const uint16_t* r1 = r0 + rawStrideU16;
            uint32_t* outRow = previewRgb_.data() + static_cast<size_t>(cy) * cellW;

            auto readCell = [&](int32_t cx, int& outR, int& outG, int& outB) {
                const int32_t rx = cx * 2;
                const int p00 = subBlack(r0[rx], blackLevel);
                const int p01 = subBlack(r0[rx + 1], blackLevel);
                const int p10 = subBlack(r1[rx], blackLevel);
                const int p11 = subBlack(r1[rx + 1], blackLevel);
                switch (pattern) {
                    case BayerPattern::GRBG:
                        outR = p01; outG = (p00 + p11) >> 1; outB = p10;
                        break;
                    case BayerPattern::BGGR:
                        outR = p11; outG = (p01 + p10) >> 1; outB = p00;
                        break;
                    case BayerPattern::GBRG:
                        outR = p10; outG = (p00 + p11) >> 1; outB = p01;
                        break;
                    case BayerPattern::RGGB:
                    default:
                        outR = p00; outG = (p01 + p10) >> 1; outB = p11;
                        break;
                }
            };

            auto encodeCell = [&](int lr, int lg, int lb) -> uint32_t {
                int cr, cg, cb;
                applyWbCcmFast(wc, lr, lg, lb, cr, cg, cb);
                const uint32_t r = gammaLut_[cr];
                const uint32_t g = gammaLut_[cg];
                const uint32_t b = gammaLut_[cb];
                return r | (g << 8) | (b << 16);
            };

            if (cellW == 1) {
                int lr, lg, lb;
                readCell(0, lr, lg, lb);
                outRow[0] = encodeCell(lr, lg, lb);
                continue;
            }

            int rPrev, gPrev, bPrev;
            int rCur, gCur, bCur;
            readCell(0, rPrev, gPrev, bPrev);
            outRow[0] = encodeCell(rPrev, gPrev, bPrev);
            readCell(1, rCur, gCur, bCur);

            for (int32_t cx = 1; cx < cellW - 1; ++cx) {
                int rNext, gNext, bNext;
                readCell(cx + 1, rNext, gNext, bNext);
                const int r = clampDpcH10(rCur, rPrev, rNext);
                const int g = clampDpcH10(gCur, gPrev, gNext);
                const int b = clampDpcH10(bCur, bPrev, bNext);
                outRow[cx] = encodeCell(r, g, b);
                rPrev = rCur; gPrev = gCur; bPrev = bCur;
                rCur = rNext; gCur = gNext; bCur = bNext;
            }
            outRow[cellW - 1] = encodeCell(rCur, gCur, bCur);

            if (lowLightSmooth && cellW >= 3) {
                uint32_t pPrev = outRow[0];
                uint32_t pCur = outRow[1];
                for (int32_t cx = 1; cx < cellW - 1; ++cx) {
                    const uint32_t pNext = outRow[cx + 1];
                    const int gP = static_cast<int>((pPrev >> 8) & 0xFF);
                    const int gC = static_cast<int>((pCur >> 8) & 0xFF);
                    const int gN = static_cast<int>((pNext >> 8) & 0xFF);
                    if (std::abs(gP - gC) + std::abs(gN - gC) <= 36) {
                        const int drP = static_cast<int>(pPrev & 0xFF) - gP;
                        const int drC = static_cast<int>(pCur & 0xFF) - gC;
                        const int drN = static_cast<int>(pNext & 0xFF) - gN;
                        const int dbP = static_cast<int>((pPrev >> 16) & 0xFF) - gP;
                        const int dbC = static_cast<int>((pCur >> 16) & 0xFF) - gC;
                        const int dbN = static_cast<int>((pNext >> 16) & 0xFF) - gN;
                        const uint32_t rOut = clampU8(gC + ((drP + 2 * drC + drN + 2) >> 2));
                        const uint32_t bOut = clampU8(gC + ((dbP + 2 * dbC + dbN + 2) >> 2));
                        outRow[cx] = rOut | (static_cast<uint32_t>(gC) << 8) | (bOut << 16);
                    }
                    pPrev = pCur;
                    pCur = pNext;
                }
            }
        }
    });

    if (lowLightSmooth && cellW >= 3 && cellH >= 3) {
        // Lightweight row-pair chroma smoothing across every 2nd row without 4MB copy
        runParallel(cellH / 2 - 1, [&](int32_t h0, int32_t h1) {
            for (int32_t idx = h0; idx < h1; ++idx) {
                const int32_t cy = idx * 2 + 1;
                uint32_t* rU = previewRgb_.data() + static_cast<size_t>(cy - 1) * cellW;
                uint32_t* rM = previewRgb_.data() + static_cast<size_t>(cy) * cellW;
                const uint32_t* rD = previewRgb_.data() + static_cast<size_t>(cy + 1) * cellW;
                for (int32_t cx = 0; cx < cellW; ++cx) {
                    const uint32_t u = rU[cx], m = rM[cx], d = rD[cx];
                    const int gU = static_cast<int>((u >> 8) & 0xFF);
                    const int gM = static_cast<int>((m >> 8) & 0xFF);
                    const int gD = static_cast<int>((d >> 8) & 0xFF);
                    if (std::abs(gU - gM) + std::abs(gD - gM) <= 36) {
                        const int dr = ((static_cast<int>(u & 0xFF) - gU) +
                                        2 * (static_cast<int>(m & 0xFF) - gM) +
                                        (static_cast<int>(d & 0xFF) - gD)) >> 2;
                        const int db = ((static_cast<int>((u >> 16) & 0xFF) - gU) +
                                        2 * (static_cast<int>((m >> 16) & 0xFF) - gM) +
                                        (static_cast<int>((d >> 16) & 0xFF) - gD)) >> 2;
                        rM[cx] = static_cast<uint32_t>(clampU8(gM + dr)) |
                                 (static_cast<uint32_t>(gM) << 8) |
                                 (static_cast<uint32_t>(clampU8(gM + db)) << 16);
                    }
                }
            }
        });
    }

    demosaicSrc_ = raw;
    cachedCellW_ = cellW;
    cachedCellH_ = cellH;
    demosaicValid_ = true;
}

bool SoftIsp::writeRgba(const uint16_t* raw, int32_t rawW, int32_t rawH, int32_t rawStrideU16,
                        BayerPattern pattern, float cropX, float cropY, float cropW, float cropH,
                        const WbCcmQ10& wc, bool lowLightSmooth, int blackLevel,
                        const LockedOutput& dst) {
    if (!dst.vaddr || dst.width <= 0 || dst.height <= 0) return false;
    uint8_t* base = static_cast<uint8_t*>(dst.vaddr);
    const int32_t stridePx = std::max(dst.stride, dst.width);

    ensureHalfResRgb(raw, rawW, rawH, rawStrideU16, pattern, wc, lowLightSmooth, blackLevel);

    const auto xTaps = buildCellTapLut(cropX, cropW, cachedCellW_, dst.width);
    const auto yTaps = buildCellTapLut(cropY, cropH, cachedCellH_, dst.height);
    const bool swapRB = (dst.format == HAL_PIXEL_FORMAT_BGRA_8888);
    const size_t rowBytes = static_cast<size_t>(dst.width) * 4;

    runParallel(dst.height, [&](int32_t y0, int32_t y1) {
        std::vector<uint8_t> rowBuf0(rowBytes);
        std::vector<uint8_t> rowBuf1(rowBytes);
        std::vector<uint8_t> outBuf(rowBytes);
        uint8_t* ptr0 = rowBuf0.data();
        uint8_t* ptr1 = rowBuf1.data();
        int32_t idx0 = -1;
        int32_t idx1 = -1;

        auto resampleHoriz = [&](int32_t cy, uint8_t* dstRow) {
            const uint32_t* s = previewRgb_.data() + static_cast<size_t>(cy) * cachedCellW_;
            uint32_t* out32 = reinterpret_cast<uint32_t*>(dstRow);
            for (int32_t dx = 0; dx < dst.width; ++dx) {
                const CellTap1D& xt = xTaps[dx];
                const uint32_t p0 = s[xt.i0];
                const uint32_t p1 = s[xt.i1];
                const uint32_t ur = (((p0 & 0xFFu) * xt.w0 + (p1 & 0xFFu) * xt.w1) >> 8);
                const uint32_t ug = ((((p0 >> 8) & 0xFFu) * xt.w0 + ((p1 >> 8) & 0xFFu) * xt.w1) >> 8);
                const uint32_t ub = ((((p0 >> 16) & 0xFFu) * xt.w0 + ((p1 >> 16) & 0xFFu) * xt.w1) >> 8);
                if (swapRB) {
                    out32[dx] = 0xFF000000u | (ur << 16) | (ug << 8) | ub;
                } else {
                    out32[dx] = 0xFF000000u | (ub << 16) | (ug << 8) | ur;
                }
            }
        };

        for (int32_t dy = y0; dy < y1; ++dy) {
            const CellTap1D& yt = yTaps[dy];
            if (yt.i0 != idx0) {
                if (yt.i0 == idx1) {
                    std::swap(ptr0, ptr1);
                    std::swap(idx0, idx1);
                } else {
                    resampleHoriz(yt.i0, ptr0);
                    idx0 = yt.i0;
                }
            }
            if (yt.i1 != yt.i0 && yt.w1 != 0 && yt.i1 != idx1) {
                resampleHoriz(yt.i1, ptr1);
                idx1 = yt.i1;
            }

            uint8_t* dstRow = base + static_cast<size_t>(dy) * stridePx * 4;
            if (yt.w1 == 0 || yt.i0 == yt.i1) {
                std::memcpy(dstRow, ptr0, rowBytes);
                continue;
            }
            if (yt.w0 == 0) {
                if (yt.i1 != idx1) {
                    resampleHoriz(yt.i1, ptr1);
                    idx1 = yt.i1;
                }
                std::memcpy(dstRow, ptr1, rowBytes);
                continue;
            }

            uint8_t* outPtr = outBuf.data();
            size_t bx = 0;
#if defined(__aarch64__)
            const uint8x8_t vw0 = vdup_n_u8(static_cast<uint8_t>(yt.w0));
            const uint8x8_t vw1 = vdup_n_u8(static_cast<uint8_t>(yt.w1));
            for (; bx + 16 <= rowBytes; bx += 16) {
                uint8x16_t a = vld1q_u8(ptr0 + bx);
                uint8x16_t b = vld1q_u8(ptr1 + bx);
                uint16x8_t lo = vmlal_u8(vmull_u8(vget_low_u8(a), vw0), vget_low_u8(b), vw1);
                uint16x8_t hi = vmlal_u8(vmull_u8(vget_high_u8(a), vw0), vget_high_u8(b), vw1);
                vst1q_u8(outPtr + bx, vcombine_u8(vrshrn_n_u16(lo, 8), vrshrn_n_u16(hi, 8)));
            }
#endif
            for (; bx < rowBytes; ++bx) {
                outPtr[bx] = static_cast<uint8_t>((ptr0[bx] * yt.w0 + ptr1[bx] * yt.w1 + 128) >> 8);
            }
            std::memcpy(dstRow, outPtr, rowBytes);
        }
    });
    return true;
}

bool SoftIsp::writeYuv(const uint16_t* raw, int32_t rawW, int32_t rawH, int32_t rawStrideU16,
                       BayerPattern pattern, float cropX, float cropY, float cropW, float cropH,
                       const WbCcmQ10& wc, bool lowLightSmooth, int blackLevel,
                       const LockedOutput& dst) {
    if (!dst.ycbcr.y || !dst.ycbcr.cb || !dst.ycbcr.cr || dst.width <= 0 || dst.height <= 0)
        return false;

    uint8_t* yPlane = static_cast<uint8_t*>(dst.ycbcr.y);
    uint8_t* cbPlane = static_cast<uint8_t*>(dst.ycbcr.cb);
    uint8_t* crPlane = static_cast<uint8_t*>(dst.ycbcr.cr);
    const size_t yStride = dst.ycbcr.ystride;
    const size_t cStride = dst.ycbcr.cstride;
    const size_t cStep = dst.ycbcr.chroma_step;

    ensureHalfResRgb(raw, rawW, rawH, rawStrideU16, pattern, wc, lowLightSmooth, blackLevel);

    const auto xTaps = buildCellTapLut(cropX, cropW, cachedCellW_, dst.width);
    const auto yTaps = buildCellTapLut(cropY, cropH, cachedCellH_, dst.height);

    const int32_t pairRows = (dst.height + 1) / 2;
    const int32_t halfW = (dst.width + 1) / 2;
    runParallel(pairRows, [&](int32_t py0, int32_t py1) {
        std::vector<uint8_t> yLine(static_cast<size_t>(dst.width));
        std::vector<uint8_t> rBuf(static_cast<size_t>(dst.width));
        std::vector<uint8_t> gBuf(static_cast<size_t>(dst.width));
        std::vector<uint8_t> bBuf(static_cast<size_t>(dst.width));

        for (int32_t py = py0; py < py1; ++py) {
            const int32_t yTop = py * 2;
            const int32_t yBot = yTop + 1;

            auto processRow = [&](int32_t dy, bool accumChroma) {
                const CellTap1D& yt = yTaps[dy];
                const uint32_t* s0 = previewRgb_.data() + static_cast<size_t>(yt.i0) * cachedCellW_;
                const uint32_t* s1 = previewRgb_.data() + static_cast<size_t>(yt.i1) * cachedCellW_;

                for (int32_t dx = 0; dx < dst.width; ++dx) {
                    const CellTap1D& xt = xTaps[dx];
                    const uint32_t p00 = s0[xt.i0];
                    const uint32_t p10 = s0[xt.i1];
                    const uint32_t p01 = s1[xt.i0];
                    const uint32_t p11 = s1[xt.i1];
                    const int w00 = (xt.w0 * yt.w0) >> 8;
                    const int w10 = (xt.w1 * yt.w0) >> 8;
                    const int w01 = (xt.w0 * yt.w1) >> 8;
                    const int w11 = 256 - w00 - w10 - w01;

                    const int r = ((p00 & 0xFFu) * w00 + (p10 & 0xFFu) * w10 +
                                   (p01 & 0xFFu) * w01 + (p11 & 0xFFu) * w11) >> 8;
                    const int g = (((p00 >> 8) & 0xFFu) * w00 + ((p10 >> 8) & 0xFFu) * w10 +
                                   ((p01 >> 8) & 0xFFu) * w01 + ((p11 >> 8) & 0xFFu) * w11) >> 8;
                    const int b = (((p00 >> 16) & 0xFFu) * w00 + ((p10 >> 16) & 0xFFu) * w10 +
                                   ((p01 >> 16) & 0xFFu) * w01 + ((p11 >> 16) & 0xFFu) * w11) >> 8;

                    yLine[dx] = clampU8((77 * r + 150 * g + 29 * b + 128) >> 8);
                    if (accumChroma) {
                        rBuf[dx] = static_cast<uint8_t>(r);
                        gBuf[dx] = static_cast<uint8_t>(g);
                        bBuf[dx] = static_cast<uint8_t>(b);
                    }
                }
                std::memcpy(yPlane + static_cast<size_t>(dy) * yStride, yLine.data(),
                            static_cast<size_t>(dst.width));
            };

            processRow(yTop, true);
            if (yBot < dst.height) {
                processRow(yBot, false);
            }

            uint8_t* cbRow = cbPlane + static_cast<size_t>(py) * cStride;
            uint8_t* crRow = crPlane + static_cast<size_t>(py) * cStride;
            for (int32_t px = 0; px < halfW; ++px) {
                const int32_t x = px * 2;
                int r = rBuf[x];
                int g = gBuf[x];
                int b = bBuf[x];
                if (x + 1 < dst.width) {
                    r = (r + rBuf[x + 1]) >> 1;
                    g = (g + gBuf[x + 1]) >> 1;
                    b = (b + bBuf[x + 1]) >> 1;
                }
                cbRow[px * cStep] = clampU8(((-43 * r - 85 * g + 128 * b + 128) >> 8) + 128);
                crRow[px * cStep] = clampU8(((128 * r - 107 * g - 21 * b + 128) >> 8) + 128);
            }
        }
    });
    return true;
}

bool SoftIsp::writeJpeg(const uint16_t* raw, int32_t rawW, int32_t rawH, int32_t rawStrideU16,
                        BayerPattern pattern, float cropX, float cropY, float cropW, float cropH,
                        const WbCcmQ10& wc, const IspRequestParams& params,
                        const AeAwbResult& gains, int blackLevel, const LockedOutput& dst,
                        int32_t* outBlobSizeBytes) {
    if (!dst.vaddr || dst.bufferSize <= static_cast<int32_t>(sizeof(::aidl::android::hardware::camera::device::CameraBlob)))
        return false;

    const int32_t w = dst.width;
    const int32_t h = dst.height;
    const BayerOffsets bo = getBayerOffsets(pattern);

    std::vector<uint16_t> linRaw(static_cast<size_t>(rawW) * rawH);
    runParallel(rawH, [&](int32_t y0, int32_t y1) {
        for (int32_t y = y0; y < y1; ++y) {
            uint16_t* dRow = linRaw.data() + static_cast<size_t>(y) * rawW;
            for (int32_t x = 0; x < rawW; ++x) {
                dRow[x] = static_cast<uint16_t>(readDpcRaw(raw, rawW, rawH, rawStrideU16, x, y, blackLevel));
            }
        }
    });

    auto pxAt = [&](int32_t x, int32_t y) -> int {
        x = std::clamp(x, 0, rawW - 1);
        y = std::clamp(y, 0, rawH - 1);
        return linRaw[static_cast<size_t>(y) * rawW + x];
    };

    const int32_t srcX0 = std::clamp(static_cast<int32_t>(std::floor(cropX)) - 1, 0, rawW - 2);
    const int32_t srcY0 = std::clamp(static_cast<int32_t>(std::floor(cropY)) - 1, 0, rawH - 2);
    const int32_t srcX1 = std::clamp(static_cast<int32_t>(std::ceil(cropX + cropW)) + 2, srcX0 + 2, rawW);
    const int32_t srcY1 = std::clamp(static_cast<int32_t>(std::ceil(cropY + cropH)) + 2, srcY0 + 2, rawH);
    const int32_t boxW = srcX1 - srcX0;
    const int32_t boxH = srcY1 - srcY0;

    std::vector<uint16_t> gPlane(static_cast<size_t>(boxW) * boxH);
    runParallel(boxH, [&](int32_t by0, int32_t by1) {
        for (int32_t by = by0; by < by1; ++by) {
            const int32_t sy = srcY0 + by;
            const int py = sy & 1;
            const bool isG0Row = (py == bo.g0y);
            const bool isG1Row = (py == bo.g1y);
            uint16_t* gOut = gPlane.data() + static_cast<size_t>(by) * boxW;
            for (int32_t bx = 0; bx < boxW; ++bx) {
                const int32_t sx = srcX0 + bx;
                const int px = sx & 1;
                const bool isGreen = (isG0Row && px == bo.g0x) || (isG1Row && px == bo.g1x);
                if (isGreen) {
                    gOut[bx] = static_cast<uint16_t>(pxAt(sx, sy));
                } else {
                    const int gN = pxAt(sx, sy - 1);
                    const int gS = pxAt(sx, sy + 1);
                    const int gW = pxAt(sx - 1, sy);
                    const int gE = pxAt(sx + 1, sy);
                    const int c = pxAt(sx, sy);
                    const int dh = std::abs(gW - gE) + std::abs(2 * c - pxAt(sx - 2, sy) - pxAt(sx + 2, sy));
                    const int dv = std::abs(gN - gS) + std::abs(2 * c - pxAt(sx, sy - 2) - pxAt(sx, sy + 2));
                    int gVal;
                    if (dh * 4 < dv * 3) {
                        gVal = (gW + gE) >> 1;
                    } else if (dv * 4 < dh * 3) {
                        gVal = (gN + gS) >> 1;
                    } else {
                        gVal = (gN + gS + gW + gE) >> 2;
                    }
                    gOut[bx] = static_cast<uint16_t>(clamp10(gVal));
                }
            }
        }
    });

    auto gBoxAt = [&](int32_t sx, int32_t sy) -> int {
        int32_t bx = std::clamp(sx - srcX0, 0, boxW - 1);
        int32_t by = std::clamp(sy - srcY0, 0, boxH - 1);
        return gPlane[static_cast<size_t>(by) * boxW + bx];
    };

    std::vector<uint8_t> boxRgb(static_cast<size_t>(boxW) * boxH * 3);
    runParallel(boxH, [&](int32_t by0, int32_t by1) {
        for (int32_t by = by0; by < by1; ++by) {
            const int32_t sy = srcY0 + by;
            const int py = sy & 1;
            uint8_t* outRow = boxRgb.data() + static_cast<size_t>(by) * boxW * 3;
            for (int32_t bx = 0; bx < boxW; ++bx) {
                const int32_t sx = srcX0 + bx;
                const int px = sx & 1;
                const int gC = gPlane[static_cast<size_t>(by) * boxW + bx];
                int rC = gC, bC = gC;

                if (px == bo.rx && py == bo.ry) {
                    rC = pxAt(sx, sy);
                    int db = ((pxAt(sx - 1, sy - 1) - gBoxAt(sx - 1, sy - 1)) +
                              (pxAt(sx + 1, sy - 1) - gBoxAt(sx + 1, sy - 1)) +
                              (pxAt(sx - 1, sy + 1) - gBoxAt(sx - 1, sy + 1)) +
                              (pxAt(sx + 1, sy + 1) - gBoxAt(sx + 1, sy + 1))) >> 2;
                    bC = clamp10(gC + db);
                } else if (px == bo.bx && py == bo.by) {
                    bC = pxAt(sx, sy);
                    int dr = ((pxAt(sx - 1, sy - 1) - gBoxAt(sx - 1, sy - 1)) +
                              (pxAt(sx + 1, sy - 1) - gBoxAt(sx + 1, sy - 1)) +
                              (pxAt(sx - 1, sy + 1) - gBoxAt(sx - 1, sy + 1)) +
                              (pxAt(sx + 1, sy + 1) - gBoxAt(sx + 1, sy + 1))) >> 2;
                    rC = clamp10(gC + dr);
                } else if (py == bo.ry) {
                    int dr = ((pxAt(sx - 1, sy) - gBoxAt(sx - 1, sy)) +
                              (pxAt(sx + 1, sy) - gBoxAt(sx + 1, sy))) >> 1;
                    int db = ((pxAt(sx, sy - 1) - gBoxAt(sx, sy - 1)) +
                              (pxAt(sx, sy + 1) - gBoxAt(sx, sy + 1))) >> 1;
                    rC = clamp10(gC + dr);
                    bC = clamp10(gC + db);
                } else {
                    int db = ((pxAt(sx - 1, sy) - gBoxAt(sx - 1, sy)) +
                              (pxAt(sx + 1, sy) - gBoxAt(sx + 1, sy))) >> 1;
                    int dr = ((pxAt(sx, sy - 1) - gBoxAt(sx, sy - 1)) +
                              (pxAt(sx, sy + 1) - gBoxAt(sx, sy + 1))) >> 1;
                    rC = clamp10(gC + dr);
                    bC = clamp10(gC + db);
                }

                int cr, cg, cb;
                applyWbCcm(wc, rC, gC, bC, cr, cg, cb);
                outRow[bx * 3 + 0] = gammaLut_[cr];
                outRow[bx * 3 + 1] = gammaLut_[cg];
                outRow[bx * 3 + 2] = gammaLut_[cb];
            }
        }
    });
    gPlane.clear();
    gPlane.shrink_to_fit();
    linRaw.clear();
    linRaw.shrink_to_fit();

    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    const float stepX = cropW / std::max(1, w);
    const float stepY = cropH / std::max(1, h);
    runParallel(h, [&](int32_t dy0, int32_t dy1) {
        for (int32_t dy = dy0; dy < dy1; ++dy) {
            const float fy = (cropY + (dy + 0.5f) * stepY) - static_cast<float>(srcY0) - 0.5f;
            const int32_t iy0 = std::clamp(static_cast<int32_t>(std::floor(fy)), 0, boxH - 2);
            const int32_t iy1 = iy0 + 1;
            const int32_t wy1 = std::clamp(static_cast<int32_t>((fy - iy0) * 256.0f + 0.5f), 0, 256);
            const int32_t wy0 = 256 - wy1;
            const uint8_t* r0 = boxRgb.data() + static_cast<size_t>(iy0) * boxW * 3;
            const uint8_t* r1 = boxRgb.data() + static_cast<size_t>(iy1) * boxW * 3;
            uint8_t* dRow = rgb.data() + static_cast<size_t>(dy) * w * 3;

            for (int32_t dx = 0; dx < w; ++dx) {
                const float fx = (cropX + (dx + 0.5f) * stepX) - static_cast<float>(srcX0) - 0.5f;
                const int32_t ix0 = std::clamp(static_cast<int32_t>(std::floor(fx)), 0, boxW - 2);
                const int32_t ix1 = ix0 + 1;
                const int32_t wx1 = std::clamp(static_cast<int32_t>((fx - ix0) * 256.0f + 0.5f), 0, 256);
                const int32_t wx0 = 256 - wx1;
                const int32_t w00 = (wx0 * wy0) >> 8;
                const int32_t w10 = (wx1 * wy0) >> 8;
                const int32_t w01 = (wx0 * wy1) >> 8;
                const int32_t w11 = 256 - w00 - w10 - w01;
                for (int c = 0; c < 3; ++c) {
                    int val = (r0[ix0 * 3 + c] * w00 + r0[ix1 * 3 + c] * w10 +
                               r1[ix0 * 3 + c] * w01 + r1[ix1 * 3 + c] * w11 + 128) >> 8;
                    dRow[dx * 3 + c] = clampU8(val);
                }
            }
        }
    });
    boxRgb.clear();
    boxRgb.shrink_to_fit();

    const size_t totalPx = static_cast<size_t>(w) * h;
    std::vector<uint8_t> yPlane(totalPx);
    std::vector<uint8_t> cbPlane(totalPx);
    std::vector<uint8_t> crPlane(totalPx);

    runParallel(h, [&](int32_t dy0, int32_t dy1) {
        std::vector<uint8_t> cbTmp(static_cast<size_t>(w));
        std::vector<uint8_t> crTmp(static_cast<size_t>(w));
        for (int32_t dy = dy0; dy < dy1; ++dy) {
            const uint8_t* sRow = rgb.data() + static_cast<size_t>(dy) * w * 3;
            uint8_t* yRow = yPlane.data() + static_cast<size_t>(dy) * w;
            uint8_t* cbRow = cbPlane.data() + static_cast<size_t>(dy) * w;
            uint8_t* crRow = crPlane.data() + static_cast<size_t>(dy) * w;
            for (int32_t dx = 0; dx < w; ++dx) {
                const int r = sRow[dx * 3 + 0];
                const int g = sRow[dx * 3 + 1];
                const int b = sRow[dx * 3 + 2];
                yRow[dx] = clampU8((77 * r + 150 * g + 29 * b + 128) >> 8);
                cbRow[dx] = clampU8(((-43 * r - 85 * g + 128 * b + 128) >> 8) + 128);
                crRow[dx] = clampU8(((128 * r - 107 * g - 21 * b + 128) >> 8) + 128);
            }
            chromaNrPtrRow(w, cbRow, crRow, 1, cbTmp.data(), crTmp.data());
        }
    });

    runParallel(w, [&](int32_t dx0, int32_t dx1) {
        std::vector<uint8_t> cbTmp(static_cast<size_t>(h));
        std::vector<uint8_t> crTmp(static_cast<size_t>(h));
        for (int32_t dx = dx0; dx < dx1; ++dx) {
            chromaNrPtrRow(h, cbPlane.data() + dx, crPlane.data() + dx, static_cast<size_t>(w),
                           cbTmp.data(), crTmp.data());
        }
    });

    const bool highIso = (gains.swDgain >= 1.6f || gains.appliedSensitivity >= 1600);
    if (highIso) {
        runParallel(h, [&](int32_t dy0, int32_t dy1) {
            std::vector<uint8_t> cbTmp(static_cast<size_t>(w));
            std::vector<uint8_t> crTmp(static_cast<size_t>(w));
            for (int32_t dy = dy0; dy < dy1; ++dy) {
                chromaNrPtrRow(w, cbPlane.data() + static_cast<size_t>(dy) * w,
                               crPlane.data() + static_cast<size_t>(dy) * w, 1, cbTmp.data(), crTmp.data());
            }
        });
        runParallel(w, [&](int32_t dx0, int32_t dx1) {
            std::vector<uint8_t> cbTmp(static_cast<size_t>(h));
            std::vector<uint8_t> crTmp(static_cast<size_t>(h));
            for (int32_t dx = dx0; dx < dx1; ++dx) {
                chromaNrPtrRow(h, cbPlane.data() + dx, crPlane.data() + dx, static_cast<size_t>(w),
                               cbTmp.data(), crTmp.data());
            }
        });
    }

    const int usmThresh = highIso ? 10 : 6;
    runParallel(h, [&](int32_t dy0, int32_t dy1) {
        for (int32_t dy = dy0; dy < dy1; ++dy) {
            uint8_t* dRow = rgb.data() + static_cast<size_t>(dy) * w * 3;
            const uint8_t* yRow = yPlane.data() + static_cast<size_t>(dy) * w;
            const uint8_t* cbRow = cbPlane.data() + static_cast<size_t>(dy) * w;
            const uint8_t* crRow = crPlane.data() + static_cast<size_t>(dy) * w;
            const uint8_t* yUp = (dy > 0) ? (yRow - w) : yRow;
            const uint8_t* yDn = (dy + 1 < h) ? (yRow + w) : yRow;

            for (int32_t dx = 0; dx < w; ++dx) {
                int y = yRow[dx];
                if (dx > 0 && dx + 1 < w && dy > 0 && dy + 1 < h) {
                    const int blur = (yRow[dx - 1] + yRow[dx + 1] + yUp[dx] + yDn[dx] + 4 * y) >> 3;
                    const int diff = y - blur;
                    if (std::abs(diff) > usmThresh) {
                        y = clampU8(y + (diff >> 2));
                    }
                }
                const int c = static_cast<int>(cbRow[dx]) - 128;
                const int d = static_cast<int>(crRow[dx]) - 128;
                dRow[dx * 3 + 0] = clampU8(y + ((359 * d) >> 8));
                dRow[dx * 3 + 1] = clampU8(y - ((88 * c + 183 * d) >> 8));
                dRow[dx * 3 + 2] = clampU8(y + ((454 * c) >> 8));
            }
        }
    });

    using ::aidl::android::hardware::camera::device::CameraBlob;
    using ::aidl::android::hardware::camera::device::CameraBlobId;
    const size_t blobArea = (dst.blobSize > 0) ? static_cast<size_t>(dst.blobSize)
                                               : static_cast<size_t>(dst.bufferSize);
    if (blobArea <= sizeof(CameraBlob)) return false;
    const size_t maxPayload = blobArea - sizeof(CameraBlob);
    const auto exif = buildExifApp1(params.jpegOrientation, gains.appliedExposureNs,
                                    gains.appliedSensitivity, params.focalLengthMm);

    auto encodeAtQuality = [&](int q, uint8_t** memOut, unsigned long* sizeOut) {
        *memOut = nullptr;
        *sizeOut = 0;
        struct jpeg_compress_struct cinfo{};
        struct jpeg_error_mgr jerr{};
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        jpeg_mem_dest(&cinfo, memOut, sizeOut);
        cinfo.image_width = static_cast<JDIMENSION>(w);
        cinfo.image_height = static_cast<JDIMENSION>(h);
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_RGB;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, std::clamp(q, 10, 100), TRUE);
        jpeg_start_compress(&cinfo, TRUE);
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1, exif.data(), static_cast<unsigned int>(exif.size()));
        while (cinfo.next_scanline < cinfo.image_height) {
            JSAMPROW row = rgb.data() + static_cast<size_t>(cinfo.next_scanline) * w * 3;
            jpeg_write_scanlines(&cinfo, &row, 1);
        }
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
    };

    uint8_t* outMem = nullptr;
    unsigned long outSize = 0;
    int q = std::clamp<int>(params.jpegQuality, 10, 100);
    encodeAtQuality(q, &outMem, &outSize);
    for (int retryQ : {85, 76, 68}) {
        if (outMem && outSize > maxPayload && retryQ < q) {
            free(outMem);
            encodeAtQuality(retryQ, &outMem, &outSize);
            q = retryQ;
        }
    }
    if (!outMem || outSize == 0 || outSize > maxPayload) {
        ALOGE("Encoded JPEG (%lu bytes) exceeds BLOB buffer payload (%zu bytes)", outSize, maxPayload);
        free(outMem);
        return false;
    }

    std::memcpy(dst.vaddr, outMem, outSize);
    free(outMem);

    CameraBlob blobHdr{};
    blobHdr.blobId = CameraBlobId::JPEG;
    blobHdr.blobSizeBytes = static_cast<int32_t>(outSize);
    std::memcpy(static_cast<uint8_t*>(dst.vaddr) + maxPayload, &blobHdr, sizeof(CameraBlob));

    if (outBlobSizeBytes) *outBlobSizeBytes = static_cast<int32_t>(outSize);
    return true;
}

double computeFocusSharpness(const uint16_t* raw, int32_t rawW, int32_t rawH, int32_t strideBytes,
                             int32_t activeW, int32_t activeH, BayerPattern pattern,
                             const IspRequestParams& params) {
    if (!raw || rawW < 48 || rawH < 48) return 0.0;

    const int32_t strideU16 = strideBytes / 2;
    const BayerOffsets bo = getBayerOffsets(pattern);

    int rx0 = (rawW * 30) / 100;
    int rx1 = (rawW * 70) / 100;
    int ry0 = (rawH * 30) / 100;
    int ry1 = (rawH * 70) / 100;

    if (params.hasAfRegion && params.afW > 0 && params.afH > 0 && activeW > 0 && activeH > 0) {
        int mapX0 = static_cast<int>(static_cast<int64_t>(params.afX) * rawW / activeW);
        int mapY0 = static_cast<int>(static_cast<int64_t>(params.afY) * rawH / activeH);
        int mapX1 = static_cast<int>(static_cast<int64_t>(params.afX + params.afW) * rawW / activeW);
        int mapY1 = static_cast<int>(static_cast<int64_t>(params.afY + params.afH) * rawH / activeH);
        if (mapX1 - mapX0 >= 16 && mapY1 - mapY0 >= 16) {
            const int minWinX = std::max(256, rawW / 7);
            const int minWinY = std::max(224, rawH / 5);
            if (mapX1 - mapX0 < minWinX) {
                const int cx = (mapX0 + mapX1) / 2;
                mapX0 = cx - minWinX / 2;
                mapX1 = cx + minWinX / 2;
            }
            if (mapY1 - mapY0 < minWinY) {
                const int cy = (mapY0 + mapY1) / 2;
                mapY0 = cy - minWinY / 2;
                mapY1 = cy + minWinY / 2;
            }
            rx0 = mapX0;
            ry0 = mapY0;
            rx1 = mapX1;
            ry1 = mapY1;
        }
    }

    rx0 = std::clamp(rx0, 4, rawW - 32) & ~1;
    ry0 = std::clamp(ry0, 4, rawH - 32) & ~1;
    rx1 = std::clamp(rx1, rx0 + 24, rawW - 12);
    ry1 = std::clamp(ry1, ry0 + 24, rawH - 12);

    const int blkW = (rx1 - rx0) / 4;
    const int blkH = (ry1 - ry0) / 4;
    if (blkW < 4 || blkH < 4) return 0.0;

    const int afBlack = (pattern == BayerPattern::GRBG) ? kBlackLevelRear : kBlackLevelFront;
    std::vector<int32_t> blk(static_cast<size_t>(blkW) * blkH);
    double lumaSum = 0.0;
    for (int by = 0; by < blkH; ++by) {
        const int sy = ry0 + by * 4;
        const uint16_t* r0 = raw + static_cast<size_t>(sy + bo.g0y) * strideU16 + bo.g0x;
        const uint16_t* r1 = raw + static_cast<size_t>(sy + bo.g1y) * strideU16 + bo.g1x;
        const uint16_t* r2 = raw + static_cast<size_t>(sy + 2 + bo.g0y) * strideU16 + bo.g0x;
        const uint16_t* r3 = raw + static_cast<size_t>(sy + 2 + bo.g1y) * strideU16 + bo.g1x;
        int32_t* bRow = blk.data() + static_cast<size_t>(by) * blkW;
        for (int bx = 0; bx < blkW; ++bx) {
            const int sx = rx0 + bx * 4;
            const int sum =
                std::max(0, static_cast<int>(r0[sx] & 0x3FF) - afBlack) +
                std::max(0, static_cast<int>(r1[sx] & 0x3FF) - afBlack) +
                std::max(0, static_cast<int>(r0[sx + 2] & 0x3FF) - afBlack) +
                std::max(0, static_cast<int>(r1[sx + 2] & 0x3FF) - afBlack) +
                std::max(0, static_cast<int>(r2[sx] & 0x3FF) - afBlack) +
                std::max(0, static_cast<int>(r3[sx] & 0x3FF) - afBlack) +
                std::max(0, static_cast<int>(r2[sx + 2] & 0x3FF) - afBlack) +
                std::max(0, static_cast<int>(r3[sx + 2] & 0x3FF) - afBlack);
            bRow[bx] = sum;
            lumaSum += static_cast<double>(sum);
        }
    }

    const int64_t count = static_cast<int64_t>(blkW) * blkH;
    const double meanBlk = lumaSum / static_cast<double>(count);
    const double noiseGate = std::max(6.0, 0.35 * std::sqrt(std::max(1.0, meanBlk)));

    double gradSum = 0.0;
    int64_t samples = 0;
    for (int by = 0; by < blkH - 2; ++by) {
        const int32_t* row = blk.data() + static_cast<size_t>(by) * blkW;
        const int32_t* row1 = row + blkW;
        const int32_t* row2 = row + blkW * 2;
        for (int bx = 0; bx < blkW - 2; ++bx) {
            const int c = row[bx];
            const double dx1 = std::max(0.0, static_cast<double>(std::abs(row[bx + 1] - c)) - noiseGate);
            const double dy1 = std::max(0.0, static_cast<double>(std::abs(row1[bx] - c)) - noiseGate);
            const double dx2 = std::max(0.0, static_cast<double>(std::abs(row[bx + 2] - c)) - noiseGate);
            const double dy2 = std::max(0.0, static_cast<double>(std::abs(row2[bx] - c)) - noiseGate);
            gradSum += (dx1 * dx1 + dy1 * dy1) * 1.5 + (dx2 * dx2 + dy2 * dy2) * 0.5;
            ++samples;
        }
    }

    if (samples <= 0) return 0.0;
    const double norm = (meanBlk + 96.0);
    return (gradSum * 10.0) / (norm * norm * static_cast<double>(samples));
}

}  // namespace s6cam

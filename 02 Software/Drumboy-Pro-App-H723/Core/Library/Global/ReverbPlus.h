// Custom: extra reverb engines (VINTAGE, PLATE, ROOM, NONLIN) for the Drumboy Pro.
//
// Header only, no hardware dependencies, so the same code is tested on a PC.
// Memory: the engine and its tank live in the CPU's fast DTCM RAM (unused by the firmware);
// the ROOM early-reflection line lives in spare SDRAM. NONLIN doesn't use the tank, so its
// line shares the tank's memory (everything is cleared when the type changes).
// Only one engine runs at a time; the STANDARD reverb keeps using the original Freeverb code.
//
//   VINTAGE  inspired by the Lexicon 224: big modulated hall, tank runs at half rate
//            (22 kHz, so the tail is naturally dark like the 224's ~10 kHz bandwidth) and the
//            tank input is cut to 12 bits for a touch of grit.
//   PLATE    inspired by the EMT 140: Dattorro plate at full rate. Instant, dense, bright.
//   ROOM     inspired by the AMS RMX16 Ambience: strong early reflections + a short tail.
//   NONLIN   inspired by the AMS RMX16 NonLin 2: a dense burst that stops dead (gated).
//            SIZE = gate length, DECAY = shape (0 reverse, 50 flat, 100 falling).

#ifndef REVERB_PLUS_H
#define REVERB_PLUS_H

#include <stdint.h>
#include <string.h>
#include <math.h>

namespace rvp {

enum : uint8_t { T_STANDARD = 0, T_VINTAGE = 1, T_PLATE = 2, T_ROOM = 3, T_NONLIN = 4 };
static const uint8_t kTypeCount = 5;

// Delay line with a fixed (maximum) buffer; the delay actually used is just a read offset,
// so changing SIZE never moves memory around.
struct Line {
    float* buf = nullptr;
    uint32_t size = 1;
    uint32_t w = 0;
    void init(float* b_, uint32_t n_) { buf = b_; size = n_; w = 0; }
    inline void write(float x_) { buf[w] = x_; if (++w >= size) w = 0; }
    // sample written d_ writes ago (d_ = 1 .. size)
    inline float tap(uint32_t d_) const { int32_t i = (int32_t)w - (int32_t)d_; if (i < 0) i += size; return buf[i]; }
    inline float tapf(float d_) const {
        uint32_t di = (uint32_t)d_;
        float fr = d_ - (float)di;
        float a = tap(di), b = tap(di + 1);
        return a + (b - a) * fr;
    }
    void clear() { if (buf) memset(buf, 0, size * sizeof(float)); w = 0; }
};

// Schroeder allpass on a Line: v = x + g*d, y = d - g*v
static inline float allpass(Line& l_, uint32_t d_, float g_, float x_) {
    float d = l_.tap(d_);
    float v = x_ + g_ * d;
    l_.write(v);
    return d - g_ * v;
}
static inline float allpassMod(Line& l_, float d_, float g_, float x_) {
    float d = l_.tapf(d_);
    float v = x_ + g_ * d;
    l_.write(v);
    return d - g_ * v;
}

// Dattorro's figure-of-eight tank lengths at 29761 Hz.
static const uint16_t kInAp[4] = {142, 107, 379, 277};
static constexpr uint16_t kApL1 = 672, kDL1 = 4453, kApL2 = 1800, kDL2 = 3720;
static constexpr uint16_t kApR1 = 908, kDR1 = 4217, kApR2 = 2656, kDR2 = 3163;
// Output taps (left output reads the right side and vice versa).
static const uint16_t kTapL[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};
static const uint16_t kTapR[7] = {353, 3627, 1228, 2673, 2111, 335, 121};

static constexpr float kMaxScale = 1.40f;   // buffer scale (PLATE at 44.1 kHz uses up to 1.393)
static constexpr uint16_t kModMargin = 48;

// Early reflections (ms at size 100) for ROOM.
static const float kErL[12] = {4.3f, 7.1f, 11.9f, 15.2f, 21.5f, 26.3f, 31.7f, 39.4f, 47.2f, 55.9f, 63.1f, 73.7f};
static const float kErR[12] = {5.1f, 8.8f, 10.7f, 17.3f, 19.9f, 28.4f, 33.9f, 41.6f, 44.8f, 58.2f, 66.7f, 77.0f};
static const int8_t kErSign[12] = {1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1, -1};
static constexpr uint32_t kErBuf = 4096;   // 93 ms

// NonLin taps: positions 0..1 across the gate, spread unevenly so they don't ring.
static const uint8_t kNlTaps = 18;
static const float kNlPosL[kNlTaps] = {0.013f, 0.061f, 0.112f, 0.168f, 0.219f, 0.274f, 0.331f, 0.383f, 0.441f,
                                       0.497f, 0.552f, 0.607f, 0.663f, 0.718f, 0.776f, 0.829f, 0.887f, 0.998f};
static const float kNlPosR[kNlTaps] = {0.029f, 0.083f, 0.139f, 0.191f, 0.247f, 0.302f, 0.356f, 0.412f, 0.469f,
                                       0.524f, 0.581f, 0.634f, 0.691f, 0.747f, 0.802f, 0.858f, 0.914f, 1.000f};
static constexpr uint32_t kNlBuf = 26624;  // 600 ms
static const uint16_t kNlApL[2] = {211, 157}, kNlApR[2] = {229, 143};

// Floats needed in fast RAM (tank; NONLIN overlaps it) and in SDRAM (ROOM reflections).
static constexpr uint32_t lenMax(uint16_t base_) { return (uint32_t)(base_ * kMaxScale) + kModMargin; }
static constexpr uint32_t fastFloats() {
    uint32_t n = 0;
    n += lenMax(142) + lenMax(107) + lenMax(379) + lenMax(277);
    n += lenMax(kApL1) + lenMax(kDL1) + lenMax(kApL2) + lenMax(kDL2);
    n += lenMax(kApR1) + lenMax(kDR1) + lenMax(kApR2) + lenMax(kDR2);
    return (n > (kNlBuf + 4 * 256)) ? n : (kNlBuf + 4 * 256);
}
static constexpr uint32_t slowFloats() { return kErBuf; }

struct Engine {
    uint8_t type = T_PLATE;

    // lines
    Line inAp[4];
    Line apL1, dL1, apL2, dL2, apR1, dR1, apR2, dR2;
    Line er, nl, nlApL[2], nlApR[2];

    // current lengths (samples at the engine rate)
    uint32_t nIn[4], nApL1, nDL1, nApL2, nDL2, nApR1, nDR1, nApR2, nDR2;
    uint32_t tL[7], tR[7];
    float exc = 16.0f;          // modulation depth (samples)
    float lfoInc = 0.0f, lfoInc2 = 0.0f;

    // coefficients
    float decay = 0.5f, damp = 0.0005f, bandwidth = 0.9995f;
    float inDiff1 = 0.75f, inDiff2 = 0.625f, decDiff1 = 0.70f, decDiff2 = 0.50f;
    float outGain = 0.6f, inGain = 1.0f;
    bool halfRate = false, grit = false;

    // ROOM
    uint32_t erL[12], erR[12];
    float erG[12];
    float erMix = 0.0f, tailMix = 1.0f, erLp = 0.0f, erLpCoef = 1.0f;

    // NONLIN
    uint32_t nlL[kNlTaps], nlR[kNlTaps];
    float nlGL[kNlTaps], nlGR[kNlTaps];
    float nlLpCoef = 1.0f, nlLpL = 0.0f, nlLpR = 0.0f;

    // state
    float bwState = 0.0f, lpL = 0.0f, lpR = 0.0f;
    float lfo = 0.0f, lfo2 = 0.0f;
    float lastL = 0.0f, lastR = 0.0f, prevL = 0.0f, prevR = 0.0f, inHold = 0.0f;
    float outLpL = 0.0f, outLpR = 0.0f;
    uint8_t phase = 0;

    void setup(float* fast_, float* slow_) {
        float* p = fast_;
        auto take = [&p](Line& l_, uint32_t n_) { l_.init(p, n_); p += n_; };
        for (uint8_t i = 0; i < 4; i++) take(inAp[i], lenMax(kInAp[i]));
        take(apL1, lenMax(kApL1)); take(dL1, lenMax(kDL1)); take(apL2, lenMax(kApL2)); take(dL2, lenMax(kDL2));
        take(apR1, lenMax(kApR1)); take(dR1, lenMax(kDR1)); take(apR2, lenMax(kApR2)); take(dR2, lenMax(kDR2));
        p = fast_;   // NONLIN shares the tank memory
        take(nl, kNlBuf);
        for (uint8_t i = 0; i < 2; i++) { take(nlApL[i], 256); take(nlApR[i], 256); }
        p = slow_;
        take(er, kErBuf);
    }

    void clear() {
        for (uint8_t i = 0; i < 4; i++) inAp[i].clear();
        apL1.clear(); dL1.clear(); apL2.clear(); dL2.clear();
        apR1.clear(); dR1.clear(); apR2.clear(); dR2.clear();
        er.clear(); nl.clear();
        for (uint8_t i = 0; i < 2; i++) { nlApL[i].clear(); nlApR[i].clear(); }
        bwState = lpL = lpR = 0.0f;
        lastL = lastR = prevL = prevR = inHold = 0.0f;
        outLpL = outLpR = erLp = nlLpL = nlLpR = 0.0f;
        phase = 0;
    }

    static inline uint32_t clampLen(float v_, const Line& l_) {
        uint32_t n = (uint32_t)(v_ + 0.5f);
        if (n < 1) n = 1;
        if (n > l_.size - 2) n = l_.size - 2;
        return n;
    }

    // size_, decay_, damping_: 0..100 (the knob values). Safe to call while audio runs.
    void configure(uint8_t type_, uint8_t size_, uint8_t decay_, uint8_t damping_) {
        type = type_;
        const float s = size_ / 100.0f, d = decay_ / 100.0f, dm = damping_ / 100.0f;
        float scale = kMaxScale, sizeMul = 1.0f;
        halfRate = false;
        grit = false;
        erMix = 0.0f;
        tailMix = 1.0f;
        inGain = 1.0f;
        const float rate = 44100.0f;

        switch (type_) {
            case T_VINTAGE:
                halfRate = true;
                grit = true;
                scale = 22050.0f / 29761.0f;
                sizeMul = 0.70f + 0.65f * s;                // big hall
                decay = 0.40f + 0.54f * d;
                damp = 0.10f + 0.55f * dm;
                bandwidth = 0.62f;                          // dark input
                exc = 22.0f;                                // deep, slow chorus in the tail
                lfoInc = 6.2831853f * 0.61f / (rate * 0.5f);
                lfoInc2 = 6.2831853f * 0.87f / (rate * 0.5f);
                decDiff1 = 0.66f; decDiff2 = 0.48f;
                inDiff1 = 0.72f; inDiff2 = 0.60f;
                outGain = 0.36f;
                break;
            case T_ROOM:
                sizeMul = 0.16f + 0.30f * s;                // short tail
                decay = 0.08f + 0.70f * d;
                damp = 0.08f + 0.60f * dm;
                bandwidth = 0.85f;
                exc = 6.0f;
                lfoInc = 6.2831853f * 1.10f / rate;
                lfoInc2 = 6.2831853f * 1.37f / rate;
                decDiff1 = 0.70f; decDiff2 = 0.50f;
                inDiff1 = 0.75f; inDiff2 = 0.625f;
                outGain = 0.6f;
                erMix = 0.28f;
                tailMix = 0.22f + 0.22f * d;
                erLpCoef = 0.95f - 0.55f * dm;
                {
                    const float erScale = (0.30f + 0.70f * s) * rate / 1000.0f;
                    for (uint8_t i = 0; i < 12; i++) {
                        erL[i] = clampLen(kErL[i] * erScale, er);
                        erR[i] = clampLen(kErR[i] * erScale, er);
                        erG[i] = kErSign[i] * (1.0f - i / 14.0f) * 0.42f;
                    }
                }
                break;
            case T_NONLIN: {
                const float gate = (0.060f + 0.490f * s) * rate;    // 60 .. 550 ms
                const float shape = (d - 0.5f) * 2.0f;               // -1 reverse .. +1 falling
                // computed in locals first: audio may be running while a knob turns
                float gL[kNlTaps], gR[kNlTaps];
                float sumL = 0.0f, sumR = 0.0f;
                for (uint8_t i = 0; i < kNlTaps; i++) {
                    nlL[i] = clampLen(kNlPosL[i] * gate, nl);
                    nlR[i] = clampLen(kNlPosR[i] * gate, nl);
                    float eL = 1.0f - shape * (kNlPosL[i] - 0.5f) * 1.7f;
                    float eR = 1.0f - shape * (kNlPosR[i] - 0.5f) * 1.7f;
                    if (eL < 0.06f) eL = 0.06f;
                    if (eR < 0.06f) eR = 0.06f;
                    const float sg = (i & 1) ? -1.0f : 1.0f;
                    gL[i] = sg * eL;
                    gR[i] = -sg * eR;
                    sumL += eL * eL;
                    sumR += eR * eR;
                }
                const float nL = 0.47f / sqrtf(sumL), nR = 0.47f / sqrtf(sumR);
                for (uint8_t i = 0; i < kNlTaps; i++) { nlGL[i] = gL[i] * nL; nlGR[i] = gR[i] * nR; }
                nlLpCoef = 0.92f - 0.75f * dm;
                return;
            }
            case T_PLATE:
            default:
                sizeMul = 0.45f + 0.49f * s;
                decay = 0.30f + 0.63f * d;
                damp = 0.0005f + 0.62f * dm;
                bandwidth = 0.9995f;
                exc = 16.0f;
                lfoInc = 6.2831853f * 1.0f / rate;
                lfoInc2 = 6.2831853f * 1.13f / rate;
                decDiff1 = 0.70f; decDiff2 = 0.50f;
                inDiff1 = 0.75f; inDiff2 = 0.625f;
                outGain = 0.165f;
                break;
        }

        const float k = scale * sizeMul;
        for (uint8_t i = 0; i < 4; i++) nIn[i] = clampLen(kInAp[i] * (type_ == T_VINTAGE ? scale * 1.0f : k), inAp[i]);
        nApL1 = clampLen(kApL1 * k, apL1) - (uint32_t)exc - 2;
        nApR1 = clampLen(kApR1 * k, apR1) - (uint32_t)exc - 2;
        nDL1 = clampLen(kDL1 * k, dL1);
        nApL2 = clampLen(kApL2 * k, apL2);
        nDL2 = clampLen(kDL2 * k, dL2);
        nDR1 = clampLen(kDR1 * k, dR1);
        nApR2 = clampLen(kApR2 * k, apR2);
        nDR2 = clampLen(kDR2 * k, dR2);
        for (uint8_t i = 0; i < 7; i++) { tL[i] = (uint32_t)(kTapL[i] * k) + 1; tR[i] = (uint32_t)(kTapR[i] * k) + 1; }
    }

    // One tank step: x_ = mono input (already band limited). Writes stereo output.
    inline void tank(float x_, float& oL_, float& oR_) {
        // input diffusion
        float x = allpass(inAp[0], nIn[0], inDiff1, x_);
        x = allpass(inAp[1], nIn[1], inDiff1, x);
        x = allpass(inAp[2], nIn[2], inDiff2, x);
        x = allpass(inAp[3], nIn[3], inDiff2, x);

        lfo += lfoInc; if (lfo > 6.2831853f) lfo -= 6.2831853f;
        lfo2 += lfoInc2; if (lfo2 > 6.2831853f) lfo2 -= 6.2831853f;
        const float m1 = exc * (1.0f + sinf(lfo)) * 0.5f;
        const float m2 = exc * (1.0f + sinf(lfo2)) * 0.5f;

        const float endL = dL2.tap(nDL2);
        const float endR = dR2.tap(nDR2);

        // left half
        float a = x + decay * endR;
        a = allpassMod(apL1, (float)nApL1 + m1, -decDiff1, a);
        float b = dL1.tap(nDL1);
        dL1.write(a);
        lpL += (1.0f - damp) * (b - lpL);
        float c = allpass(apL2, nApL2, decDiff2, lpL * decay);
        dL2.write(c);

        // right half
        a = x + decay * endL;
        a = allpassMod(apR1, (float)nApR1 + m2, -decDiff1, a);
        b = dR1.tap(nDR1);
        dR1.write(a);
        lpR += (1.0f - damp) * (b - lpR);
        c = allpass(apR2, nApR2, decDiff2, lpR * decay);
        dR2.write(c);

        // output taps
        float l = dR1.tap(tL[0]) + dR1.tap(tL[1]) - apR2.tap(tL[2]) + dR2.tap(tL[3]) - dL1.tap(tL[4]) - apL2.tap(tL[5]) - dL2.tap(tL[6]);
        float r = dL1.tap(tR[0]) + dL1.tap(tR[1]) - apL2.tap(tR[2]) + dL2.tap(tR[3]) - dR1.tap(tR[4]) - apR2.tap(tR[5]) - dR2.tap(tR[6]);
        oL_ = l * outGain;
        oR_ = r * outGain;
    }

    // One sample in, stereo wet out.
    inline void process(float in_, float& oL_, float& oR_) {
        switch (type) {
            case T_NONLIN: {
                nl.write(in_);
                float l = 0.0f, r = 0.0f;
                for (uint8_t i = 0; i < kNlTaps; i++) { l += nl.tap(nlL[i]) * nlGL[i]; r += nl.tap(nlR[i]) * nlGR[i]; }
                l = allpass(nlApL[0], kNlApL[0], 0.6f, l);
                l = allpass(nlApL[1], kNlApL[1], 0.6f, l);
                r = allpass(nlApR[0], kNlApR[0], 0.6f, r);
                r = allpass(nlApR[1], kNlApR[1], 0.6f, r);
                nlLpL += nlLpCoef * (l - nlLpL);
                nlLpR += nlLpCoef * (r - nlLpR);
                oL_ = nlLpL;
                oR_ = nlLpR;
                return;
            }
            case T_ROOM: {
                er.write(in_);
                float l = 0.0f, r = 0.0f;
                for (uint8_t i = 0; i < 12; i++) { l += er.tap(erL[i]) * erG[i]; r += er.tap(erR[i]) * erG[i]; }
                erLp += erLpCoef * (((l + r) * 0.5f) - erLp);
                bwState += bandwidth * (erLp - bwState);
                float tl, tr;
                tank(bwState * inGain, tl, tr);
                oL_ = l * erMix + tl * tailMix;
                oR_ = r * erMix + tr * tailMix;
                return;
            }
            case T_VINTAGE: {
                // tank at half rate: average two inputs, run once, interpolate the output
                inHold += in_;
                if (phase == 0) {
                    phase = 1;
                    oL_ = prevL + (lastL - prevL) * 0.5f;
                    oR_ = prevR + (lastR - prevR) * 0.5f;
                } else {
                    phase = 0;
                    float x = inHold * 0.5f;
                    inHold = 0.0f;
                    bwState += bandwidth * (x - bwState);
                    float q = bwState;
                    if (grit) q = (float)(int32_t)(q * 2048.0f) * (1.0f / 2048.0f);   // 12-bit tank input
                    prevL = lastL;
                    prevR = lastR;
                    tank(q, lastL, lastR);
                    oL_ = prevL;
                    oR_ = prevR;
                }
                // gentle smoothing of the 22 kHz steps
                outLpL += 0.55f * (oL_ - outLpL);
                outLpR += 0.55f * (oR_ - outLpR);
                oL_ = outLpL;
                oR_ = outLpR;
                return;
            }
            case T_PLATE:
            default: {
                bwState += bandwidth * (in_ - bwState);
                tank(bwState, oL_, oR_);
                return;
            }
        }
    }

    // Rough 60 dB decay time in seconds (for the page drawing), and the high-frequency one.
    float tailSeconds(bool highs_) const {
        if (type == T_NONLIN) return 0.0f;
        const float rate = halfRate ? 22050.0f : 44100.0f;
        // one trip round the figure-of-eight = both halves; two decay multipliers per trip
        const float loop = (float)(nApL1 + nDL1 + nApL2 + nDL2 + nApR1 + nDR1 + nApR2 + nDR2) / rate;
        float g = decay * decay;
        if (highs_) { const float hf = (1.0f - damp) / (1.0f + damp); g *= hf * hf; }
        if (g < 0.0001f) g = 0.0001f;
        if (g > 0.9999f) g = 0.9999f;
        return 0.5f * (-3.0f / log10f(g)) * loop;   // 0.5: measured against the real impulse response
    }
};

}  // namespace rvp

#endif

// Custom: layout + rasterising for the smooth Liberation Sans text (no hardware access here, so
// it is tested on a PC). Each text keeps the exact box the stock pixel font used
// (characters x cell width by the font height), so screens keep their layout and a new value
// always paints over the old one completely (no flicker, no leftovers).
#ifndef __SANS_TEXT_H
#define __SANS_TEXT_H

#include <stdint.h>
#include <string.h>
#include "FontSans.h"

namespace sans {

static const uint16_t kMaxChars = 160;

struct Layout {
    const SansFont* f = nullptr;
    uint16_t n = 0;                 // glyphs placed
    int16_t penX[kMaxChars];        // glyph pen x inside the box (px)
    uint8_t gi[kMaxChars];          // glyph index (char - 32)
    uint8_t extraRows = 0;          // rows needed below the box for descenders (g j p q y , _ ...)
};

// Inner alignment from the stock habit of padding with spaces: "   OFF" = right aligned in its
// box, "OFF   " = left, "  OFF  " = centred, no padding = the LCD alignment setting.
// align_: 0 = LEFT, 1 = RIGHT, 2 = CENTER (same values as LcdAlign).
static inline void layout(const SansFont& f_, const char* text_, uint16_t len_, uint16_t boxW_, uint8_t align_, Layout& L_) {
    L_.f = &f_;
    L_.n = 0;
    L_.extraRows = 0;
    if (len_ > kMaxChars) len_ = kMaxChars;
    uint16_t a = 0, b = len_;
    while ((a < b) && (text_[a] == ' ')) a++;
    while ((b > a) && (text_[b - 1] == ' ')) b--;
    if (a == b) return;
    const bool lead = (a > 0), trail = (b < len_);
    uint8_t mode = align_;
    if (lead && !trail) mode = 1;
    else if (trail && !lead) mode = 0;
    else if (lead && trail) mode = 2;

    // Custom: spaces that pad a number ("SLOPE  6" vs "SLOPE 12") are as wide as a digit, so
    // numbers keep their place and the rest of the line doesn't shift when a value changes.
    auto advOf = [&](uint16_t i_) -> uint16_t {
        uint8_t c = (uint8_t)text_[i_];
        if ((c < 32) || (c > 126)) c = '?';
        if (c == ' ') {
            uint16_t j = i_;
            while ((j < b) && (text_[j] == ' ')) j++;
            if ((j < b) && (((text_[j] >= '0') && (text_[j] <= '9')) || (text_[j] == '+') || (text_[j] == '-')))
                return f_.glyphs['0' - 32].adv;
        }
        return f_.glyphs[c - 32].adv;
    };
    // total width in 1/4 px
    uint32_t q = 0;
    for (uint16_t i = a; i < b; i++) q += advOf(i);
    int32_t wPx = (int32_t)((q + 2) / 4);
    // too wide for the box: squeeze the letter spacing a little (at most 1 px per letter)
    int32_t squeezeQ = 0;
    const uint16_t count = b - a;
    if ((wPx > boxW_) && (count > 1)) {
        squeezeQ = ((wPx - boxW_) * 4 + (count - 2)) / (count - 1);
        if (squeezeQ > 4) squeezeQ = 4;
        wPx -= (squeezeQ * (count - 1)) / 4;
    }
    int32_t x0 = 0;
    if (mode == 1) x0 = (int32_t)boxW_ - wPx;
    else if (mode == 2) x0 = ((int32_t)boxW_ - wPx) / 2;
    if (x0 < 0) x0 = 0;

    int32_t pen = x0 * 4;
    for (uint16_t i = a; i < b; i++) {
        uint8_t c = (uint8_t)text_[i];
        if ((c < 32) || (c > 126)) c = '?';
        const SansGlyph& g = f_.glyphs[c - 32];
        L_.penX[L_.n] = (int16_t)((pen + 2) / 4);
        L_.gi[L_.n] = c - 32;
        L_.n++;
        if (g.h) {
            const int16_t below = (int16_t)g.yoff + (int16_t)g.h;   // rows below the baseline
            if ((below > 0) && (below > L_.extraRows)) L_.extraRows = (uint8_t)below;
        }
        pen += advOf(i) - squeezeQ;
    }
}

// Coverage 0..15 for one row of the box (row 0 = top; the baseline is under row capH - 1).
static inline void row(const Layout& L_, int16_t row_, uint16_t boxW_, uint8_t* alpha_) {
    memset(alpha_, 0, boxW_);
    if (!L_.f) return;
    const SansFont& f = *L_.f;
    for (uint16_t k = 0; k < L_.n; k++) {
        const SansGlyph& g = f.glyphs[L_.gi[k]];
        if (!g.h) continue;
        const int16_t r = row_ - ((int16_t)f.capH + g.yoff);
        if ((r < 0) || (r >= g.h)) continue;
        const uint8_t* src = f.data + g.offset + r * ((g.w + 1) / 2);
        const int16_t gx = L_.penX[k] + g.xoff;
        for (uint8_t c = 0; c < g.w; c++) {
            const int16_t x = gx + c;
            if ((x < 0) || (x >= (int16_t)boxW_)) continue;
            const uint8_t v = (c & 1) ? (src[c >> 1] & 0x0F) : (src[c >> 1] >> 4);
            if (v > alpha_[x]) alpha_[x] = v;
        }
    }
}

// Custom: the firmware's strings are all capitals (the pixel font has no lowercase). For the
// smooth font, turn them into normal capitals: "FILTER 1  HIGHPASS  SLOPE 12 DB" ->
// "Filter 1  Highpass  Slope 12 dB". Kept as capitals: short words with no vowel (BD, SD, HH,
// FX, BPM), a few acronyms (EQ, CPU, LFO, MIDI ...) and single letters (note names, Q, V3).
// Units get their usual spelling: DB -> dB, HZ -> Hz, KHZ -> kHz, MS -> ms, and a lone K or S
// right after a number -> k / s ("1.2K" -> "1.2k", "2.5 S" -> "2.5 s").
static inline bool isUp(char c_) { return (c_ >= 'A') && (c_ <= 'Z'); }
static inline bool isLow(char c_) { return (c_ >= 'a') && (c_ <= 'z'); }
static inline bool isDig(char c_) { return (c_ >= '0') && (c_ <= '9'); }

static inline void mixedCase(const char* in_, uint16_t n_, char* out_) {
    static const char* const keep[] = {"EQ", "CPU", "LFO", "MIDI", "ADSR", "MPC", "USB", "OK", "RAM", "PPQ",
                                       "WAV", "DJ", "II", "III", "IV", "VI", "VII", "XL", "AI", "ID", "SD"};
    memcpy(out_, in_, n_);
    uint16_t i = 0;
    while (i < n_) {
        if (!isUp(in_[i]) && !isLow(in_[i])) { i++; continue; }
        uint16_t j = i;
        bool hasLow = false, vowel = false;
        while ((j < n_) && (isUp(in_[j]) || isLow(in_[j]))) {
            if (isLow(in_[j])) hasLow = true;
            const char u = (char)(in_[j] & ~0x20);
            if ((u == 'A') || (u == 'E') || (u == 'I') || (u == 'O') || (u == 'U') || (u == 'Y')) vowel = true;
            j++;
        }
        const uint16_t len = j - i;
        if (hasLow) { i = j; continue; }   // already written in mixed case: leave it
        // previous non-space character (for units after a number)
        int16_t k = (int16_t)i - 1;
        if ((k >= 0) && (in_[k] == ' ')) k--;
        const bool afterNum = (k >= 0) && isDig(in_[k]);
        auto is = [&](const char* w_) { return (strlen(w_) == len) && (memcmp(in_ + i, w_, len) == 0); };
        if (is("DB")) { out_[i] = 'd'; }
        else if (is("HZ")) { out_[i + 1] = 'z'; }
        else if (is("KHZ")) { out_[i] = 'k'; out_[i + 2] = 'z'; }
        else if (is("MS")) { out_[i] = 'm'; out_[i + 1] = 's'; }
        else if (len == 1) { if (afterNum && ((in_[i] == 'K') || (in_[i] == 'S'))) out_[i] = (char)(in_[i] | 0x20); }
        else {
            bool kept = !vowel && (len <= 3);
            for (uint8_t q = 0; !kept && (q < sizeof(keep) / sizeof(keep[0])); q++) kept = is(keep[q]);
            if (!kept)
                for (uint16_t m = i + 1; m < j; m++) out_[m] = (char)(in_[m] | 0x20);
        }
        i = j;
    }
}

// RGB565 blend table: 16 steps from back colour to fore colour.
static inline void blendTable(uint16_t fg_, uint16_t bg_, uint16_t* lut_) {
    const int fr = (fg_ >> 11) & 0x1F, fgc = (fg_ >> 5) & 0x3F, fb = fg_ & 0x1F;
    const int br = (bg_ >> 11) & 0x1F, bgc = (bg_ >> 5) & 0x3F, bb = bg_ & 0x1F;
    for (int i = 0; i < 16; i++) {
        const int r = (fr * i + br * (15 - i) + 7) / 15;
        const int g = (fgc * i + bgc * (15 - i) + 7) / 15;
        const int b = (fb * i + bb * (15 - i) + 7) / 15;
        lut_[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

}  // namespace sans

#endif

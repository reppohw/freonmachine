// gameboy.h — el chip de sonido de la Game Boy como sinte: pulso con duty y sweep,
// onda de 4 bits (32 pasos) y ruido LFSR de 15 o 7 bits; volumen en escalones de 1/16,
// envolvente por pasos de 1/64 s, vibrato, arpegio estilo LSDJ y paneo duro.
#pragma once
#include <array>
#include "dsp.h"

namespace gb {

struct GbParams {
    int chan = 0;            // 0 pulso, 1 onda, 2 ruido
    int duty = 2;            // 12,5 / 25 / 50 / 75 %
    int wtab = 0;            // tabla de onda 0..7
    int vol = 12;            // volumen inicial 1..15
    int envStep = 3;         // 0 = fijo; n = un paso cada n/64 s
    int sweepT = 0, sweepN = 2;
    int pan = 1;             // 0 izq., 1 centro, 2 der., 3 alterna
    float vibRate = 0.4f, vibDepth = 0.0f;
    int arp = 0, arpSpd = 2; // patrón de arpegio y velocidad en tics de 1/60 s
    float fine = 0.0f;       // cents
    float glide = 0.0f;      // s
    float dmg = 9000.0f;     // filtro de salida (Hz)
    float lvl = 0.7f;
    bool envUp = false, n7 = false, swUp = false, carp = true, octD = false, octU = false, vibD = false;
};

inline const int kGbArps[8][3] = {{0, 0, 0}, {0, 3, 7}, {0, 4, 7}, {0, 7, 7}, {0, 12, 12}, {0, 5, 7}, {0, 3, 6}, {0, 7, 12}};
inline const int kGbArpLen[8] = {1, 3, 3, 2, 2, 3, 3, 3};

class GameBoy {
public:
    static constexpr int kVoices = 4;
    GbParams p;

    void prepare(float sr) {
        sr_ = sr;
        const unsigned char D[4][8] = {{0, 0, 0, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 0}};
        for (int d = 0; d < 4; ++d) for (int k = 0; k < 8; ++k) duty_[d][k] = D[d][k];
        unsigned seed = 7;
        for (int t = 0; t < 8; ++t) for (int k = 0; k < 32; ++k) {
            const float x = k / 32.0f; float y;
            switch (t) {
                case 0: y = std::sin(2 * kPi * x); break;
                case 1: y = 1 - 4 * std::fabs(x - 0.5f); break;
                case 2: y = 2 * x - 1; break;
                case 3: y = x < 0.5f ? 1.0f : -1.0f; break;
                case 4: y = 0.8f * std::sin(2 * kPi * x) + 0.4f * std::sin(4 * kPi * x); break;
                case 5: y = (std::sin(2 * kPi * x) + 0.5f * std::sin(4 * kPi * x) + 0.3f * std::sin(8 * kPi * x)) / 1.4f; break;
                case 6: y = (std::sin(2 * kPi * x) + 0.7f * std::sin(6 * kPi * x) + 0.4f * std::sin(10 * kPi * x)) / 1.6f; break;
                default: seed = seed * 1103515245u + 12345u; y = static_cast<float>((seed >> 16) & 15) / 7.5f - 1.0f; break;
            }
            wave_[t][k] = static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround(std::clamp(y, -1.0f, 1.0f) * 7.5f + 7.5f)), 0, 15));
        }
        for (auto& v : v_) v = Voice{};
        lpL_.set(p.dmg, sr_); lpR_.set(p.dmg, sr_); hpL_.set(35.0f, sr_); hpR_.set(35.0f, sr_);
    }

    void noteOn(int note, int vel) {
        held_[nHeld_ < 16 ? nHeld_++ : 15] = note;
        vel_ = vel / 127.0f;
        if (p.carp) { retarget(true); return; }
        Voice* v = nullptr;
        for (auto& x : v_) if (x.on && x.note == note) v = &x;
        if (!v) for (auto& x : v_) if (!x.on && x.fade <= 0.0f) { v = &x; break; }
        if (!v) { unsigned b = ~0u; for (auto& x : v_) if (x.age < b) { b = x.age; v = &x; } }
        start(*v, note, nullptr, 0);
    }
    void noteOff(int note) {
        int j = 0; for (int i = 0; i < nHeld_; ++i) if (held_[i] != note) held_[j++] = held_[i]; nHeld_ = j;
        if (p.carp) { if (nHeld_ > 0) retarget(false); else v_[0].on = false; return; }
        for (auto& x : v_) if (x.on && x.note == note) x.on = false;
    }
    void allNotesOff() { nHeld_ = 0; for (auto& x : v_) x.on = false; }

    void render(float* L, float* R, int n) {
        lpL_.set(p.dmg, sr_); lpR_.set(p.dmg, sr_);
        const float envPer = p.envStep / 64.0f, swPer = p.sweepT / 128.0f, arpPer = std::max(1, p.arpSpd) / 60.0f;
        const float octm = std::exp2(static_cast<float>((p.octD ? -1 : 0) + (p.octU ? 1 : 0)) + p.fine / 1200.0f);
        const float gl = p.glide > 0.001f ? 1.0f - std::exp(-1.0f / (p.glide * 0.33f * sr_)) : 1.0f;
        for (int i = 0; i < n; ++i) {
            float sl = 0.0f, sr = 0.0f;
            for (auto& v : v_) {
                if (!v.on && v.fade <= 0.0f) continue;
                const float dt = 1.0f / sr_;
                // envolvente de volumen por pasos
                if (envPer > 0.0f && v.on) { v.envT += dt; while (v.envT >= envPer) { v.envT -= envPer; v.vol = std::clamp(v.vol + (p.envUp ? 1 : -1), 0, 15); } }
                // sweep (solo pulso)
                if (p.chan == 0 && swPer > 0.0f) { v.swT += dt; while (v.swT >= swPer) { v.swT -= swPer; v.sw *= 1.0f + (p.swUp ? 1.0f : -1.0f) * std::exp2(-static_cast<float>(std::max(p.sweepN, 0))); v.sw = std::clamp(v.sw, 0.01f, 64.0f); } }
                // arpegio
                const int alen = v.nOffs > 1 ? v.nOffs : kGbArpLen[std::clamp(p.arp, 0, 7)];
                if (alen > 1) { v.arpT += dt; while (v.arpT >= arpPer) { v.arpT -= arpPer; v.arpI = (v.arpI + 1) % alen; } }
                const int off = v.nOffs > 1 ? v.offs[v.arpI % v.nOffs] : kGbArps[std::clamp(p.arp, 0, 7)][v.arpI % 3];
                // vibrato (con retardo opcional)
                v.vibPh += (1.0f + p.vibRate * 9.0f) * dt; if (v.vibPh >= 1.0f) v.vibPh -= 1.0f;
                v.vibAge += dt;
                const float vd = p.vibDepth * (p.vibD ? std::min(1.0f, v.vibAge / 0.35f) : 1.0f);
                const float vib = (4.0f * std::fabs(v.vibPh - 0.5f) - 1.0f) * vd * 0.7f;
                v.semi += (static_cast<float>(v.note) - v.semi) * gl;
                const float f = mtof(v.semi + static_cast<float>(off) + vib) * octm * v.sw;
                float s;
                if (p.chan == 2) {
                    v.nAcc += f * 8.0f / sr_;
                    while (v.nAcc >= 1.0f) { v.nAcc -= 1.0f; const unsigned b = (v.lfsr ^ (v.lfsr >> 1)) & 1u; v.lfsr = (v.lfsr >> 1) | (b << 14); if (p.n7) v.lfsr = (v.lfsr & ~0x40u) | (b << 6); }
                    s = (v.lfsr & 1u) ? -0.5f : 0.5f;
                } else {
                    v.ph += f / sr_; if (v.ph >= 1.0f) v.ph -= std::floor(v.ph);
                    s = p.chan == 0 ? static_cast<float>(duty_[std::clamp(p.duty, 0, 3)][static_cast<int>(v.ph * 8.0f) & 7]) - 0.5f
                                    : static_cast<float>(wave_[std::clamp(p.wtab, 0, 7)][static_cast<int>(v.ph * 32.0f) & 31]) / 15.0f - 0.5f;
                }
                // fin de nota: 5 ms de caída para no hacer clic
                if (!v.on) v.fade -= 1.0f / (0.005f * sr_);
                const float a = s * (static_cast<float>(v.vol) / 15.0f) * (v.on ? 1.0f : std::max(0.0f, v.fade)) * velGain(vel_, 0.7f);
                sl += a * v.gl; sr += a * v.gr;
            }
            const float g = 0.55f * p.lvl;
            L[i] = hpL_.process(lpL_.process(sl * g)); R[i] = hpR_.process(lpR_.process(sr * g));
        }
    }

private:
    struct Voice {
        bool on = false; int note = 60; unsigned age = 0; float semi = 60.0f, ph = 0, nAcc = 0, envT = 0, swT = 0, sw = 1.0f, arpT = 0, vibPh = 0, vibAge = 0, fade = 0, gl = 1, gr = 1;
        int vol = 12, arpI = 0, nOffs = 0, offs[8] = {}; unsigned lfsr = 0x7fff;
    };
    void start(Voice& v, int note, const int* offs, int nOffs) {
        const bool glideFrom = p.glide > 0.001f && (v.on || v.fade > 0.0f);
        v.on = true; v.note = note; v.age = ++clock_; v.fade = 1.0f;
        if (!glideFrom) v.semi = static_cast<float>(note);
        v.vol = std::clamp(p.vol, 0, 15); v.envT = 0; v.swT = 0; v.sw = 1.0f; v.arpT = 0; v.arpI = 0; v.vibAge = 0; v.lfsr = 0x7fff;
        v.nOffs = nOffs; for (int k = 0; k < nOffs && k < 8; ++k) v.offs[k] = offs[k];
        int pv = p.pan; if (pv == 3) { alt_ = !alt_; pv = alt_ ? 0 : 2; }
        v.gl = pv == 2 ? 0.0f : 1.0f; v.gr = pv == 0 ? 0.0f : 1.0f;
    }
    // Acorde → arpegio en una sola voz con las notas pulsadas, desde la más grave
    void retarget(bool fresh) {
        int s[16] = {}; const int n = nHeld_; if (n <= 0) return; for (int i = 0; i < n; ++i) s[i] = held_[i];
        std::sort(s, s + n);
        int offs[8], k = 0; for (int i = 0; i < n && k < 8; ++i) offs[k++] = s[i] - s[0];
        Voice& v = v_[0];
        if (fresh || !v.on) start(v, s[0], offs, k);
        else { v.note = s[0]; v.nOffs = k; for (int i = 0; i < k; ++i) v.offs[i] = offs[i]; }
    }
    std::array<Voice, kVoices> v_;
    unsigned char duty_[4][8] = {}, wave_[8][32] = {};
    int held_[16] = {}, nHeld_ = 0; float vel_ = 0.8f, sr_ = 48000.0f; unsigned clock_ = 0; bool alt_ = false;
    OnePoleLP lpL_, lpR_; OnePoleHP hpL_, hpR_;
};

}  // namespace gb

// synths2.h — motores nuevos: wavetable, supersaw / string machine, modal (físico),
// west coast (oscilador complejo + wavefolder + lowpass gate) y órgano de drawbars.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "dsp.h"

namespace gb {

// ------------------------------------------------------------------ piezas comunes
// ADSR con coeficientes por bloque (más barato que calcularlos por muestra)
struct Adsr {
    int st = 0; float y = 0, ca = 0.01f, cd = 0.001f, cr = 0.001f, s = 1;
    void set(float a, float d, float sus, float r, float sr) {
        ca = 1.0f - std::exp(-1.0f / (std::max(a, 0.0005f) * 0.6f * sr)); cd = 1.0f - std::exp(-1.0f / (std::max(d, 0.0005f) * 0.3f * sr));
        cr = 1.0f - std::exp(-1.0f / (std::max(r, 0.0005f) * 0.3f * sr)); s = sus;
    }
    void on() { st = 1; } void off() { if (st) st = 3; } bool active() const { return st != 0; }
    float next() {
        switch (st) {
            case 1: y += (1.3f - y) * ca; if (y >= 1.0f) { y = 1.0f; st = 2; } break;
            case 2: y += (s - y) * cd; break;
            case 3: y += (0.0f - y) * cr; if (y < 1e-5f) { y = 0.0f; st = 0; } break;
            default: break;
        }
        return y;
    }
};
// Filtro de estado variable TPT (12 dB)
struct Svf2 {
    float ic1 = 0, ic2 = 0, g = 0.1f, k = 1.4f;
    void set(float fc, float res01, float sr) { g = std::tan(kPi * std::clamp(fc, 12.0f, 0.45f * sr) / sr); k = 2.0f - 1.96f * std::clamp(res01, 0.0f, 1.0f); }
    void tick(float x, float& lp, float& bp, float& hp) {
        const float v3 = x - ic2, v1 = (ic1 + g * v3) / (1.0f + g * (g + k)), v2 = ic2 + g * v1;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float lp(float x) { float l, b, h; tick(x, l, b, h); return l; }
    float hp(float x) { float l, b, h; tick(x, l, b, h); return h; }
    void reset() { ic1 = ic2 = 0; }
};
struct VBase { int note = -1; bool held = false, on = false; unsigned age = 0; float vel = 1; };
template <class V, int N> V& allocVoice(V (&v)[N], int note, unsigned& clk) {
    for (auto& x : v) if (x.on && x.note == note) { x.age = ++clk; return x; }
    for (auto& x : v) if (!x.on) { x.age = ++clk; return x; }
    V* b = nullptr; for (auto& x : v) if (!x.held && (!b || x.age < b->age)) b = &x;
    if (!b) { b = &v[0]; for (auto& x : v) if (x.age < b->age) b = &x; }
    b->age = ++clk; return *b;
}
inline float vsens(float vel, float amt) { return velGain(vel, amt); }

// FFT compleja iterativa (solo para generar tablas al arrancar)
inline void fft(std::vector<std::complex<double>>& a, bool inv) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * 3.14159265358979 / static_cast<double>(len) * (inv ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t j = 0; j < len / 2; ++j) { const auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

// ================================================================== WAVETABLE
struct WtParams {
    int table = 0; float pos = 0.2f, envPos = 0.3f, lfoPos = 0, lfoRate = 0.5f; int unison = 1; float detune = 0.15f, sub = 0;
    float cutoff = 6000, res = 0.15f, envFilt = 0.3f, kbd = 0.5f;
    float mA = 0.01f, mD = 0.6f, mS = 0.3f, mR = 0.5f, A = 0.005f, D = 0.5f, S = 0.8f, R = 0.4f, vel = 0.4f, volume = 0.8f;
};
// 6 tablas × 16 cuadros × 8 niveles (cada nivel con la mitad de armónicos, para no tener aliasing)
struct WtBank {
    static constexpr int T = 6, F = 16, M = 8, L = 1024, H = 512;
    std::vector<float> d;   // [t][f][m][L]
    float* at(int t, int f, int m) { return &d[((static_cast<size_t>(t) * F + f) * M + m) * L]; }
    static WtBank& get() { static WtBank b; return b; }
    WtBank() {
        d.assign(static_cast<size_t>(T) * F * M * L, 0.0f);
        std::vector<std::complex<double>> spec(H + 1);
        for (int t = 0; t < T; ++t) for (int f = 0; f < F; ++f) {
            const double x = f / (F - 1.0);
            harmonics(t, x, spec);
            float norm = 0;
            for (int m = 0; m < M; ++m) {
                const int hmax = H >> m;
                std::vector<std::complex<double>> a(L, 0.0);
                for (int k = 1; k <= hmax && k < L / 2; ++k) { a[static_cast<size_t>(k)] = spec[static_cast<size_t>(k)]; a[static_cast<size_t>(L - k)] = std::conj(spec[static_cast<size_t>(k)]); }
                fft(a, true);
                float* o = at(t, f, m);
                for (int i = 0; i < L; ++i) o[i] = static_cast<float>(a[static_cast<size_t>(i)].real());
                if (m == 0) { for (int i = 0; i < L; ++i) norm = std::max(norm, std::fabs(o[i])); norm = norm > 1e-9f ? 0.9f / norm : 1.0f; }
                for (int i = 0; i < L; ++i) o[i] *= norm;
            }
        }
    }
    // espectro (coeficientes complejos por armónico) de cada tabla en la posición x
    static void harmonics(int t, double x, std::vector<std::complex<double>>& s) {
        const double PI = 3.14159265358979;
        std::fill(s.begin(), s.end(), std::complex<double>(0));
        auto sine = [&](int k, double amp) { s[static_cast<size_t>(k)] += std::complex<double>(0, -0.5 * amp); };   // amp·sin(kθ)
        switch (t) {
        case 0: {   // básica: seno → triángulo → sierra → cuadrada
            const double p = x * 3; const int seg = std::min(2, static_cast<int>(p)); const double fr = p - seg;
            auto shape = [&](int sh, int k) -> double {
                if (sh == 0) return k == 1 ? 1.0 : 0.0;
                if (sh == 1) return k % 2 ? 8 / (PI * PI) * ((k / 2) % 2 ? -1.0 : 1.0) / (k * k) : 0.0;
                if (sh == 2) return 2 / PI * (k % 2 ? 1.0 : -1.0) / k;
                return k % 2 ? 4 / (PI * k) : 0.0;
            };
            for (int k = 1; k <= H; ++k) sine(k, shape(seg, k) * (1 - fr) + shape(seg + 1, k) * fr);
            break; }
        case 1: {   // PWM: pulso del 50 % al 4 %
            const double w = 0.5 - x * 0.46;
            for (int k = 1; k <= H; ++k) { const double a = 2 * std::sin(PI * k * w) / (PI * k); s[static_cast<size_t>(k)] = a * 0.5 * std::polar(1.0, -PI * k * w); }   // polar() no admite amplitudes negativas: en el Mac da NaN
            break; }
        case 2: {   // vocales: A E I O U
            static const double FM[5][2] = {{7, 11}, {4, 18}, {3, 23}, {5, 8}, {3, 7}};
            const double p = x * 4; const int seg = std::min(3, static_cast<int>(p)); const double fr = p - seg;
            const double f1 = FM[seg][0] * (1 - fr) + FM[seg + 1][0] * fr, f2 = FM[seg][1] * (1 - fr) + FM[seg + 1][1] * fr;
            for (int k = 1; k <= H; ++k) {
                auto pk = [&](double c) { const double bw = 1.2 + c * 0.12; return std::exp(-((k - c) * (k - c)) / (bw * bw)); };
                sine(k, (pk(f1) + 0.7 * pk(f2) + 0.08 / k) / std::sqrt(static_cast<double>(k)) * (k % 2 ? 1 : -1));
            }
            break; }
        case 3: {   // digital: patrones de armónicos que cambian
            const double p = x * 7; const int a = static_cast<int>(p); const double fr = p - a;
            auto pat = [](int seed, int k) { uint32_t h = static_cast<uint32_t>(seed * 7919 + k * 104729); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 3u) == 0 ? 1.0 : 0.0; };
            for (int k = 1; k <= H; ++k) sine(k, (k == 1 ? 1.0 : (pat(a, k) * (1 - fr) + pat(a + 1, k) * fr)) / std::pow(k, 0.75));
            break; }
        case 4: {   // sync: sierra sincronizada, la esclava sube de 1× a 8×
            const int N = 8192; const double r = 1 + x * 7;
            std::vector<std::complex<double>> a(N);
            for (int i = 0; i < N; ++i) { const double t0 = static_cast<double>(i) / N, ph = t0 * r - std::floor(t0 * r); a[static_cast<size_t>(i)] = (2 * ph - 1) * (1 - t0 * 0.3); }
            fft(a, false);
            for (int k = 1; k <= H; ++k) s[static_cast<size_t>(k)] = a[static_cast<size_t>(k)] / static_cast<double>(N);
            break; }
        default: {   // barrido de armónicos (aditivo): una banda que sube por la serie
            const double c = 1 + x * 48, w = 2 + x * 6;
            for (int k = 1; k <= H; ++k) sine(k, (k == 1 ? 0.5 : 0.0) + std::exp(-((k - c) * (k - c)) / (w * w)) / std::sqrt(static_cast<double>(k)));
            break; }
        }
    }
};
class WavetableSynth {
public:
    static constexpr int kVoices = 6;
    WtParams p;
    void prepare(float sr) { sr_ = sr; WtBank::get(); }
    void noteOn(int note, int vel) {
        Voice& v = allocVoice(v_, note, clk_);
        const bool fresh = !v.on; v.on = true; v.held = true; v.note = note; v.vel = vel / 127.0f; v.amp.on(); v.mod.on();
        if (fresh) { v.f.reset(); for (int u = 0; u < 3; ++u) v.ph[u] = rnd() ; v.sph = 0; }
    }
    void noteOff(int note) { for (auto& v : v_) if (v.on && v.held && v.note == note) { v.held = false; v.amp.off(); v.mod.off(); } }
    void allNotesOff() { for (auto& v : v_) if (v.on) { v.held = false; v.amp.off(); v.mod.off(); } }
    void render(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
        WtBank& B = WtBank::get(); const int tb = std::clamp(p.table, 0, WtBank::T - 1), U = std::clamp(p.unison, 1, 3);
        lfo_ += p.lfoRate * n / sr_; lfo_ -= std::floor(lfo_);
        const float lfoV = fsin(lfo_);
        for (auto& v : v_) {
            if (!v.on) continue;
            v.amp.set(p.A, p.D, p.S, p.R, sr_); v.mod.set(p.mA, p.mD, p.mS, p.mR, sr_);
            const float f0 = mtof(static_cast<float>(v.note)), vs = vsens(v.vel, p.vel);
            float dt[3]; static const float off[3] = {0, 1, -1};
            for (int u = 0; u < U; ++u) dt[u] = f0 * std::exp2(off[u] * p.detune * 0.5f / 12.0f) / sr_;
            const int harmOk = static_cast<int>(0.45f * sr_ / (f0 * 1.06f)); int mip = 0; while (mip < WtBank::M - 1 && (WtBank::H >> mip) > harmOk) ++mip;
            for (int i = 0; i < n; ++i) {
                const float ae = v.amp.next(), me = v.mod.next();
                if ((i & 15) == 0) {
                    const float pos = std::clamp(p.pos + p.envPos * me + p.lfoPos * lfoV * 0.5f, 0.0f, 1.0f) * (WtBank::F - 1);
                    v.fa = std::min(static_cast<int>(pos), WtBank::F - 2); v.ff = pos - v.fa;
                    const float fc = p.cutoff * std::pow(f0 / 261.6f, p.kbd) * std::exp2(p.envFilt * 6.0f * me * vs);
                    v.f.set(fc, p.res, sr_); v.f2.g = v.f.g; v.f2.k = v.f.k;
                }
                const float* t0 = B.at(tb, v.fa, mip); const float* t1 = B.at(tb, v.fa + 1, mip);
                float sl = 0, sr2 = 0;
                for (int u = 0; u < U; ++u) {
                    v.ph[u] += dt[u]; if (v.ph[u] >= 1.0f) v.ph[u] -= 1.0f;
                    const float x = v.ph[u] * WtBank::L; const int i0 = static_cast<int>(x) & (WtBank::L - 1), i1 = (i0 + 1) & (WtBank::L - 1); const float fr = x - std::floor(x);
                    const float a = t0[i0] + (t0[i1] - t0[i0]) * fr, b = t1[i0] + (t1[i1] - t1[i0]) * fr, s = a + (b - a) * v.ff;
                    const float pan = U == 1 ? 0.5f : (u == 0 ? 0.5f : u == 1 ? 0.15f : 0.85f);
                    sl += s * (1 - pan) * 1.4f; sr2 += s * pan * 1.4f;
                }
                if (p.sub > 0.001f) { v.sph += dt[0] * 0.5f; if (v.sph >= 1) v.sph -= 1; const float sb = fsin(v.sph) * p.sub; sl += sb; sr2 += sb; }
                const float g = ae * vs * p.volume * p.volume * 0.35f / std::sqrt(static_cast<float>(U));
                L[i] += v.f.lp(sl) * g; R[i] += v.f2.lp(sr2) * g;
            }
            if (!v.amp.active()) v.on = false;
        }
    }
private:
    struct Voice : VBase { float ph[3] = {}, sph = 0, ff = 0; int fa = 0; Adsr amp, mod; Svf2 f, f2; };
    float rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (seed_ >> 8) * (1.0f / 16777216.0f); }
    Voice v_[kVoices]; float sr_ = 48000, lfo_ = 0; unsigned clk_ = 0; uint32_t seed_ = 99;
};

// ================================================================== SUPERSAW / STRING MACHINE
struct SawParams {
    int mode = 0; float detune = 0.35f, mix = 0.6f, octave = 0, cutoff = 9000, res = 0.1f, hpf = 60, envFilt = 0.2f, kbd = 0.3f;
    float ens = 0.5f, ensRate = 0.4f, fA = 0.01f, fD = 0.8f, fS = 0.5f, fR = 0.6f, A = 0.01f, D = 0.5f, S = 0.85f, R = 0.5f, vel = 0.3f, volume = 0.8f;
};
// Ensemble estéreo: tres retardos modulados a la vez por un LFO lento y otro rápido
struct Ensemble {
    std::vector<float> b; int w = 0; float sr = 48000, ph1 = 0, ph2 = 0;
    void prepare(float s) { sr = s; b.assign(static_cast<size_t>(s * 0.05f), 0.0f); }
    void process(float* L, float* R, int n, float amt, float rate) {
        if (amt < 0.005f) return;
        const int N = static_cast<int>(b.size());
        for (int i = 0; i < n; ++i) {
            const float x = 0.5f * (L[i] + R[i]); b[static_cast<size_t>(w)] = x;
            ph1 += (0.5f + rate * 0.8f) / sr; if (ph1 >= 1) ph1 -= 1; ph2 += (5.0f + rate * 3.0f) / sr; if (ph2 >= 1) ph2 -= 1;
            float o[3];
            for (int k = 0; k < 3; ++k) {
                const float m = 0.8f * fsin(ph1 + k / 3.0f) + 0.2f * fsin(ph2 + k / 3.0f);
                const float d = (0.009f + 0.0035f * amt * m) * sr, rp = static_cast<float>(w) - d;
                int i0 = static_cast<int>(std::floor(rp)); const float fr = rp - i0; i0 = ((i0 % N) + N) % N; const int i1 = (i0 + 1) % N;
                o[k] = b[static_cast<size_t>(i0)] + (b[static_cast<size_t>(i1)] - b[static_cast<size_t>(i0)]) * fr;
            }
            if (++w >= N) w = 0;
            L[i] = L[i] * (1 - amt * 0.5f) + (o[0] + 0.5f * o[1]) * amt * 0.7f;
            R[i] = R[i] * (1 - amt * 0.5f) + (o[2] + 0.5f * o[1]) * amt * 0.7f;
        }
    }
};
class SuperSaw {
public:
    static constexpr int kVoices = 6;
    SawParams p;
    void prepare(float sr) { sr_ = sr; ens_.prepare(sr); }
    void noteOn(int note, int vel) {
        Voice& v = allocVoice(v_, note, clk_);
        const bool fresh = !v.on; v.on = true; v.held = true; v.note = note; v.vel = vel / 127.0f; v.amp.on(); v.fe.on();
        if (fresh) { v.f.reset(); v.f2.reset(); for (auto& ph : v.ph) ph = rnd(); }
    }
    void noteOff(int note) { for (auto& v : v_) if (v.on && v.held && v.note == note) { v.held = false; v.amp.off(); v.fe.off(); } }
    void allNotesOff() { for (auto& v : v_) if (v.on) { v.held = false; v.amp.off(); v.fe.off(); } }
    void render(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
        static const float OFF[7] = {-0.11f, -0.063f, -0.019f, 0, 0.019f, 0.063f, 0.11f};
        static const float PAN[7] = {0.1f, 0.8f, 0.3f, 0.5f, 0.7f, 0.2f, 0.9f};
        const bool strings = p.mode == 1;
        for (auto& v : v_) {
            if (!v.on) continue;
            v.amp.set(p.A, p.D, p.S, p.R, sr_); v.fe.set(p.fA, p.fD, p.fS, p.fR, sr_);
            const float f0 = mtof(static_cast<float>(v.note)), vs = vsens(v.vel, p.vel);
            float dt[8];
            if (!strings) for (int k = 0; k < 7; ++k) dt[k] = f0 * (1.0f + OFF[k] * p.detune * 0.6f) / sr_;
            else { dt[0] = f0 / sr_; dt[1] = f0 * (1.0f + 0.004f * p.detune) / sr_; dt[2] = 2 * f0 / sr_; dt[3] = 2 * f0 * (1.0f - 0.004f * p.detune) / sr_; }
            const int nk = strings ? 4 : 7;
            const float cg = 1.0f - 0.55f * p.mix, sg = 0.6f * p.mix;
            for (int i = 0; i < n; ++i) {
                const float ae = v.amp.next(), fe = v.fe.next();
                if ((i & 15) == 0) { const float fc = p.cutoff * std::pow(f0 / 261.6f, p.kbd) * std::exp2(p.envFilt * 5.0f * fe * vs); v.f.set(fc, p.res, sr_); v.f2.g = v.f.g; v.f2.k = v.f.k; }
                float sl = 0, sr2 = 0;
                for (int k = 0; k < nk; ++k) {
                    v.ph[k] += dt[k]; if (v.ph[k] >= 1) v.ph[k] -= 1;
                    float s = blepSaw(v.ph[k], dt[k]);
                    float pan;
                    if (strings) { s *= k < 2 ? 1.0f - 0.6f * p.octave : 0.4f + 0.6f * p.octave; pan = k % 2 ? 0.3f : 0.7f; }
                    else { s *= k == 3 ? cg : sg; pan = PAN[k]; }
                    sl += s * (1 - pan); sr2 += s * pan;
                }
                const float g = ae * vs * p.volume * p.volume * (strings ? 0.4f : 0.3f);
                L[i] += v.f.lp(sl) * g; R[i] += v.f2.lp(sr2) * g;
            }
            if (!v.amp.active()) v.on = false;
        }
        hp_[0].set(p.hpf, sr_); hp_[1].set(p.hpf, sr_);
        for (int i = 0; i < n; ++i) { L[i] = hp_[0].process(L[i]); R[i] = hp_[1].process(R[i]); }
        ens_.process(L, R, n, strings ? std::max(p.ens, 0.3f) : p.ens, p.ensRate);
    }
private:
    struct Voice : VBase { float ph[8] = {}; Adsr amp, fe; Svf2 f, f2; };
    float rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (seed_ >> 8) * (1.0f / 16777216.0f); }
    Voice v_[kVoices]; float sr_ = 48000; unsigned clk_ = 0; uint32_t seed_ = 7; Ensemble ens_; OnePoleHP hp_[2];
};

// ================================================================== MODAL / FÍSICO
struct ModalParams {
    int model = 0, exciter = 0; float bright = 0.6f, damping = 0.5f, position = 0.3f, structure = 0.3f, exLevel = 0.8f, noise = 0.1f;
    float release = 0.3f, spread = 0.5f, vel = 0.7f, volume = 0.8f;
};
class ModalSynth {
public:
    static constexpr int kVoices = 6, kModes = 8;
    ModalParams p;
    void prepare(float sr) { sr_ = sr; for (auto& v : v_) v.line.assign(static_cast<size_t>(sr / 20.0f) + 4, 0.0f); }
    void noteOn(int note, int vel) {
        Voice& v = allocVoice(v_, note, clk_);
        v.on = true; v.held = true; v.note = note; v.vel = vel / 127.0f; v.t = 0; v.silent = 0;
        // excitación: ráfaga de ruido filtrada (punteo), golpe de mazo, arco (ruido continuo) o ruido largo
        v.exLp.set(400.0f * std::pow(40.0f, p.bright) * (0.5f + 0.5f * v.vel), sr_);
        setupModes(v);
    }
    void noteOff(int note) { for (auto& v : v_) if (v.on && v.held && v.note == note) v.held = false; }
    void allNotesOff() { for (auto& v : v_) v.held = false; }
    void render(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
        for (auto& v : v_) {
            if (!v.on) continue;
            const float vs = vsens(v.vel, p.vel), lvl = p.volume * p.volume * vs;
            const bool str = p.model == 0;
            // decaimiento: mientras se mantiene, el de «Damping»; al soltar, el de «Release»
            const float t60 = v.held || p.exciter == 2 ? 0.15f + std::pow(p.damping, 2.0f) * 9.0f : 0.05f + p.release * 3.0f;
            float peak = 0;
            // coeficientes de pérdida calculados una vez por bloque (no en cada muestra)
            const float gStr = str ? std::pow(10.0f, -3.0f * (v.delay / sr_) / t60) : 0.0f;
            float rK[kModes];
            if (!str) for (int k = 0; k < kModes; ++k) rK[k] = std::pow(10.0f, -3.0f / (t60 * v.m[k].decayMul * sr_));
            for (int i = 0; i < n; ++i) {
                const float tt = v.t / sr_; v.t += 1;
                float ex = 0;
                switch (p.exciter) {
                    case 0: ex = tt < 0.006f ? nz_.next() * (1.0f - tt / 0.006f) : 0.0f; break;                 // punteo
                    case 1: ex = tt < 0.002f ? fsin(tt / 0.004f) * 1.5f : 0.0f; break;                            // mazo
                    case 2: ex = v.held ? nz_.next() * 0.03f * (0.6f + 0.4f * fsin(tt * 5.3f)) : 0.0f; break;     // arco
                    default: ex = tt < 0.08f ? nz_.next() * (1.0f - tt / 0.08f) * 0.5f : 0.0f; break;            // ruido
                }
                ex = v.exLp.process(ex) * p.exLevel * (0.4f + 0.6f * v.vel);
                float y;
                if (str) {
                    // cuerda (Karplus-Strong): retardo con filtro de pérdidas y posición de punteo
                    const size_t N = v.line.size(); const float d = v.delay;
                    float rp = static_cast<float>(v.w) - d; while (rp < 0) rp += static_cast<float>(N);
                    const size_t i0 = static_cast<size_t>(rp), i1 = (i0 + 1) % N; const float fr = rp - std::floor(rp);
                    const float del = v.line[i0] + (v.line[i1] - v.line[i0]) * fr;
                    const float g = gStr;
                    v.lp = v.lp + (del - v.lp) * (0.2f + 0.8f * p.bright);
                    const float pick = ex - v.exDelay(static_cast<int>(p.position * d * 0.5f), ex);
                    v.line[static_cast<size_t>(v.w)] = v.lp * g + pick;
                    if (++v.w >= static_cast<int>(N)) v.w = 0;
                    y = v.lp * 2.5f;
                } else {
                    // banco de resonadores
                    y = 0;
                    for (int k = 0; k < kModes; ++k) {
                        Mode& m = v.m[k]; if (m.amp <= 0) continue;
                        const float r = rK[k];
                        const float o = 2 * r * m.c * m.y1 - r * r * m.y2 + ex * m.amp * m.s * 0.8f;
                        m.y2 = m.y1; m.y1 = o; y += o;
                    }
                }
                y += ex * p.noise * 0.5f;
                const float o = softClip(y * 1.5f) * lvl * 0.7f;
                peak = std::max(peak, std::fabs(o));
                L[i] += o * (1.0f - v.pan); R[i] += o * v.pan;
            }
            v.silent = peak < 1e-4f ? v.silent + n : 0;
            if (!v.held && v.silent > sr_ * 0.05f) v.on = false;
            if (v.held && v.silent > sr_ * 2 && p.exciter != 2) v.on = false;
        }
    }
private:
    struct Mode { float c = 1, s = 0, y1 = 0, y2 = 0, amp = 0, decayMul = 1; };
    struct Voice : VBase {
        std::vector<float> line; int w = 0; float delay = 100, lp = 0, pan = 0.5f; long t = 0; long silent = 0;
        Mode m[kModes]; OnePoleLP exLp;
        float exd[2048] = {}; int exw = 0;
        float exDelay(int d, float x) { exd[exw] = x; const float o = exd[(exw - std::clamp(d, 1, 2000) + 2048) & 2047]; exw = (exw + 1) & 2047; return o; }
    };
    void setupModes(Voice& v) {
        static const float RAT[4][kModes] = {
            {1, 2, 3, 4, 5, 6, 7, 8},                                                    // (cuerda: no se usa)
            {1, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.81f, 31.87f},                  // barra
            {0.5f, 1, 1.183f, 1.506f, 2, 2.514f, 2.662f, 3.011f},                         // campana
            {1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f}};                 // placa
        const float f0 = mtof(static_cast<float>(v.note));
        v.pan = 0.5f + (rnd() - 0.5f) * p.spread;
        if (p.model == 0) { v.delay = std::clamp(sr_ / f0, 4.0f, static_cast<float>(v.line.size() - 2)); std::fill(v.line.begin(), v.line.end(), 0.0f); v.lp = 0; return; }
        const int md = std::clamp(p.model, 1, 3);
        for (int k = 0; k < kModes; ++k) {
            Mode& m = v.m[k]; m.y1 = m.y2 = 0;
            const float ratio = std::pow(RAT[md][k], 1.0f + (p.structure - 0.3f) * 0.4f), f = f0 * ratio;
            if (f > 0.45f * sr_) { m.amp = 0; continue; }
            m.c = std::cos(2 * kPi * f / sr_); m.s = std::sin(2 * kPi * f / sr_);
            m.amp = std::fabs(std::sin(kPi * (k + 1) * (0.05f + p.position * 0.9f))) * std::pow(0.75f, static_cast<float>(k)) * (0.3f + 0.7f * p.bright + (1 - p.bright) * (k == 0 ? 0.5f : 0.0f));
            m.decayMul = 1.0f / (1.0f + k * (1.2f - p.bright));
        }
    }
    float rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (seed_ >> 8) * (1.0f / 16777216.0f); }
    Voice v_[kVoices]; float sr_ = 48000; unsigned clk_ = 0; uint32_t seed_ = 3; Noise nz_;
};

// ================================================================== WEST COAST
struct WestParams {
    float ratio = 2, fine = 0, fmIndex = 0.3f, am = 0; int carShape = 0, modShape = 0; float fold = 0.3f, symmetry = 0, foldEnv = 0.4f;
    int lpgMode = 2; float lpgDecay = 0.4f, res = 0.2f, A = 0.002f, D = 0.4f, S = 0.0f, R = 0.3f, vel = 0.7f, volume = 0.8f;
};
class WestCoast {
public:
    static constexpr int kVoices = 6;
    WestParams p;
    void prepare(float sr) { sr_ = sr; for (auto& v : v_) v.dec.setup(sr); }
    void noteOn(int note, int vel) {
        Voice& v = allocVoice(v_, note, clk_);
        if (!v.on) { v.cph = v.mph = 0; v.vac = 0; v.f.reset(); }
        v.on = true; v.held = true; v.note = note; v.vel = vel / 127.0f; v.env.on();
    }
    void noteOff(int note) { for (auto& v : v_) if (v.on && v.held && v.note == note) { v.held = false; v.env.off(); } }
    void allNotesOff() { for (auto& v : v_) if (v.on) { v.held = false; v.env.off(); } }
    void render(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
        const float sr2 = 2 * sr_;
        const float vacUp = 1.0f - std::exp(-1.0f / (0.002f * sr_)), vacDn = 1.0f - std::exp(-1.0f / ((0.03f + p.lpgDecay * 1.5f) * sr_));
        for (auto& v : v_) {
            if (!v.on) continue;
            v.env.set(p.A, p.D, p.S, p.R, sr_);
            const float f0 = mtof(static_cast<float>(v.note)), fm = f0 * p.ratio * std::exp2(p.fine / 1200.0f), vs = vsens(v.vel, p.vel);
            float peak = 0;
            for (int i = 0; i < n; ++i) {
                const float e = v.env.next() * vs;
                v.vac += (e - v.vac) * (e > v.vac ? vacUp : vacDn);            // vactrol: sube rápido, cae lento
                const float vac = v.vac;
                float os[2];
                for (float& o : os) {
                    v.mph += fm / sr2; if (v.mph >= 1) v.mph -= 1;
                    const float mod = p.modShape == 0 ? fsin(v.mph) : 4 * std::fabs(v.mph - 0.5f) - 1;
                    v.cph += f0 * (1.0f + p.fmIndex * 4.0f * mod) / sr2; v.cph -= std::floor(v.cph);
                    float car = p.carShape == 0 ? fsin(v.cph) : 4 * std::fabs(v.cph - 0.5f) - 1;
                    car *= 1.0f - p.am + p.am * (0.5f + 0.5f * mod);
                    // wavefolder: pliega la onda tantas veces como suba la ganancia
                    const float g = 1.0f + (p.fold + p.foldEnv * vac) * 9.0f;
                    o = fsin((car * g + p.symmetry) * 0.25f);
                }
                float y = v.dec.process(os[0], os[1]);
                // lowpass gate
                if (p.lpgMode != 0) { if ((i & 7) == 0) v.f.set(30.0f * std::exp2(vac * 9.5f), p.res, sr_); y = v.f.lp(y); }
                if (p.lpgMode != 1) y *= vac; else y *= std::min(1.0f, vac * 4.0f);
                const float o = y * p.volume * p.volume * 0.32f;
                peak = std::max(peak, std::fabs(o));
                L[i] += o; R[i] += o;
            }
            if (!v.env.active() && peak < 1e-4f) v.on = false;
        }
    }
private:
    struct Voice : VBase { float cph = 0, mph = 0, vac = 0; Adsr env; Svf2 f; Decim2 dec; };
    Voice v_[kVoices]; float sr_ = 48000; unsigned clk_ = 0;
};

// ================================================================== ÓRGANO DE DRAWBARS
struct OrganParams {
    float bar[9] = {8, 8, 8, 0, 0, 0, 0, 0, 0}; int perc = 0; float percDecay = 0.3f, percLevel = 0.6f, click = 0.4f;
    int vib = 0, leslie = 1; float drive = 0.2f, A = 0.004f, R = 0.05f, volume = 0.8f;
};
class Organ {
public:
    static constexpr int kVoices = 10;
    OrganParams p;
    void prepare(float sr) { sr_ = sr; vib_.assign(static_cast<size_t>(sr * 0.02f), 0.0f); for (auto& d : ld_) d.assign(static_cast<size_t>(sr * 0.02f), 0.0f); xo_[0].set(800, sr); xo_[1].set(800, sr); }
    void noteOn(int note, int vel) {
        (void)vel;
        bool anyHeld = false; for (auto& v : v_) anyHeld |= v.held;
        Voice& v = allocVoice(v_, note, clk_);
        if (!v.on) v.ph = 0;
        v.on = true; v.held = true; v.note = note; v.env.on();
        v.perc = anyHeld ? 0.0f : 1.0f;      // percusión de disparo único, como en el original
        v.click = 1.0f;
    }
    void noteOff(int note) { for (auto& v : v_) if (v.on && v.held && v.note == note) { v.held = false; v.env.off(); } }
    void allNotesOff() { for (auto& v : v_) if (v.on) { v.held = false; v.env.off(); } }
    void render(float* L, float* R, int n) {
        static const int MUL[9] = {1, 3, 2, 4, 6, 8, 10, 12, 16};   // en múltiplos de f/2 (16')
        float amp[9]; for (int k = 0; k < 9; ++k) amp[k] = p.bar[k] < 0.5f ? 0.0f : std::pow(10.0f, (p.bar[k] - 8.0f) * 3.0f / 20.0f);
        const float pd = std::exp(-1.0f / ((p.percDecay < 0.5f ? 0.2f : 1.0f) * sr_ * (0.5f + p.percDecay))), cd = std::exp(-1.0f / (0.002f * sr_));
        const int pk = p.perc == 1 ? 3 : p.perc == 2 ? 4 : -1;   // 2.º (4') o 3.º (2⅔') armónico
        float mono[512];
        for (int i = 0; i < n; ++i) mono[i] = 0;
        for (auto& v : v_) {
            if (!v.on) continue;
            v.env.set(p.A, 0.01f, 1.0f, p.R, sr_);
            const float half = mtof(static_cast<float>(v.note)) * 0.5f / sr_;
            for (int i = 0; i < n; ++i) {
                const float e = v.env.next();
                v.ph += half; if (v.ph >= 1) v.ph -= 1;
                float s = 0;
                for (int k = 0; k < 9; ++k) { if (amp[k] == 0 || half * MUL[k] > 0.45f) continue; s += fsin(v.ph * MUL[k]) * amp[k]; }
                if (pk >= 0 && v.perc > 1e-4f) { s += fsin(v.ph * MUL[pk]) * v.perc * p.percLevel * 2.0f; v.perc *= pd; }
                if (v.click > 1e-3f) { s += nz_.next() * v.click * p.click * 0.8f; v.click *= cd; }
                mono[i] += s * e * 0.07f;
            }
            if (!v.env.active()) v.on = false;
        }
        // vibrato / chorus (escáner): V1-V3 solo vibrato, C1-C3 mezclado con la señal directa
        const int N = static_cast<int>(vib_.size());
        if (p.vib > 0) {
            const float depth = (p.vib - 1) % 3 * 0.0004f + 0.0004f; const bool chorus = p.vib > 3;
            for (int i = 0; i < n; ++i) {
                vib_[static_cast<size_t>(vw_)] = mono[i];
                vph_ += 6.9f / sr_; if (vph_ >= 1) vph_ -= 1;
                const float d = (0.001f + depth * (1 + fsin(vph_))) * sr_; float rp = static_cast<float>(vw_) - d; if (rp < 0) rp += static_cast<float>(N);
                const size_t i0 = static_cast<size_t>(rp) % static_cast<size_t>(N), i1 = (i0 + 1) % static_cast<size_t>(N); const float fr = rp - std::floor(rp);
                const float w = vib_[i0] + (vib_[i1] - vib_[i0]) * fr;
                if (++vw_ >= N) vw_ = 0;
                mono[i] = chorus ? 0.6f * (mono[i] + w) : w;
            }
        }
        // saturación y Leslie (bocina y tambor girando, cada uno a su velocidad)
        const float dg = 1.0f + p.drive * 6.0f, dn = 1.0f / std::tanh(dg);
        const float tH = p.leslie == 2 ? 6.7f : p.leslie == 1 ? 0.8f : 0.0f, tD = p.leslie == 2 ? 5.8f : p.leslie == 1 ? 0.7f : 0.0f;
        for (int i = 0; i < n; ++i) {
            const float x = std::tanh(mono[i] * dg) * dn * p.volume * p.volume;
            if (p.leslie == 0) { L[i] = R[i] = x; continue; }
            sH_ += (tH - sH_) * (tH > sH_ ? 0.6f : 0.35f) / sr_; sD_ += (tD - sD_) * (tD > sD_ ? 0.25f : 0.15f) / sr_;
            hph_ += sH_ / sr_; if (hph_ >= 1) hph_ -= 1; dph_ += sD_ / sr_; if (dph_ >= 1) dph_ -= 1;
            const float lo = xo_[0].process(x), hi = x - lo;
            float out[2] = {0, 0};
            for (int c = 0; c < 2; ++c) {
                const float hs = fsin(hph_ + c * 0.25f), ds = fsin(dph_ + c * 0.25f);
                // retardo modulado (doppler) para la bocina y amplitud para los dos
                auto& d = ld_[c]; const int M = static_cast<int>(d.size()); d[static_cast<size_t>(lw_)] = hi;
                float rp = static_cast<float>(lw_) - (0.002f + 0.0006f * hs) * sr_; if (rp < 0) rp += static_cast<float>(M);
                const size_t i0 = static_cast<size_t>(rp) % static_cast<size_t>(M), i1 = (i0 + 1) % static_cast<size_t>(M); const float fr = rp - std::floor(rp);
                const float hd = d[i0] + (d[i1] - d[i0]) * fr;
                out[c] = hd * (0.75f + 0.25f * hs) + lo * (0.85f + 0.15f * ds);
            }
            if (++lw_ >= static_cast<int>(ld_[0].size())) lw_ = 0;
            L[i] = out[0]; R[i] = out[1];
        }
    }
private:
    struct Voice : VBase { float ph = 0, perc = 0, click = 0; Adsr env; };
    Voice v_[kVoices]; float sr_ = 48000; unsigned clk_ = 0; Noise nz_;
    std::vector<float> vib_; int vw_ = 0; float vph_ = 0;
    std::vector<float> ld_[2]; int lw_ = 0; float hph_ = 0, dph_ = 0, sH_ = 0.8f, sD_ = 0.7f; OnePoleLP xo_[2];
};

}  // namespace gb

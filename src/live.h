// live.h — fase 3: sampler de 16 pads, breaks cortados a tempo, granular y procesador de voz.
// También lectura y escritura de WAV y los sonidos de serie.
#pragma once
#include <dirent.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dsp.h"
#include "fx.h"
#include "params.h"

namespace gb {

// ------------------------------------------------------------------ muestras y archivos WAV
struct Sample { std::vector<float> L, R; float sr = 48000; std::string name; size_t size() const { return L.size(); } };

inline Sample* loadWav(const std::string& path, float maxSec = 90) {
    FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return nullptr;
    // archivos enormes fuera (más de 300 MB no caben en la memoria de la Pi junto a todo lo demás)
    if (std::fseek(f, 0, SEEK_END) == 0) { const long sz = std::ftell(f); if (sz < 0 || sz > 300L * 1024 * 1024) { std::fclose(f); return nullptr; } std::rewind(f); }
    std::vector<unsigned char> d; unsigned char buf[65536]; size_t r;
    while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + r);
    std::fclose(f);
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return nullptr;
    auto u16 = [&](size_t p) { return static_cast<unsigned>(d[p] | d[p + 1] << 8); };
    auto u32 = [&](size_t p) { return static_cast<uint32_t>(d[p] | d[p + 1] << 8 | d[p + 2] << 16 | static_cast<uint32_t>(d[p + 3]) << 24); };
    unsigned fmt = 0, ch = 0, bits = 0; uint32_t rate = 0; size_t dpos = 0, dlen = 0;
    for (size_t p = 12; p + 8 <= d.size();) {
        const uint32_t len = u32(p + 4);
        if (!std::memcmp(&d[p], "fmt ", 4) && p + 24 <= d.size()) { fmt = u16(p + 8); ch = u16(p + 10); rate = u32(p + 12); bits = u16(p + 22); if (fmt == 0xFFFE && p + 34 <= d.size()) fmt = u16(p + 32); }
        else if (!std::memcmp(&d[p], "data", 4)) { dpos = p + 8; dlen = std::min<size_t>(len, d.size() - dpos); }
        p += 8 + len + (len & 1);
    }
    if (!dpos || !ch || !rate || (fmt != 1 && fmt != 3)) return nullptr;
    const unsigned bps = bits / 8; if (!bps) return nullptr;
    size_t n = dlen / (bps * ch); n = std::min(n, static_cast<size_t>(maxSec * rate));
    auto* s = new Sample; s->sr = static_cast<float>(rate); s->L.resize(n); s->R.resize(n);
    for (size_t i = 0; i < n; ++i) for (unsigned c = 0; c < std::min(ch, 2u); ++c) {
        const unsigned char* q = &d[dpos + (i * ch + c) * bps]; float v = 0;
        if (fmt == 3 && bits == 32) std::memcpy(&v, q, 4);
        else if (bits == 16) v = static_cast<int16_t>(q[0] | q[1] << 8) / 32768.0f;
        else if (bits == 24) v = static_cast<int32_t>(static_cast<uint32_t>(q[0] << 8 | q[1] << 16 | q[2] << 24)) / 2147483648.0f;
        else if (bits == 32) v = static_cast<int32_t>(q[0] | q[1] << 8 | q[2] << 16 | static_cast<uint32_t>(q[3]) << 24) / 2147483648.0f;
        else if (bits == 8) v = (q[0] - 128) / 128.0f;
        (c == 0 ? s->L : s->R)[i] = v;
    }
    if (ch == 1) s->R = s->L;
    const size_t sl = path.find_last_of('/'); std::string nm = path.substr(sl == std::string::npos ? 0 : sl + 1);
    s->name = nm.substr(0, nm.find_last_of('.'));
    return s;
}
inline bool saveWav(const std::string& path, const Sample& s) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    const uint32_t n = static_cast<uint32_t>(s.size()), data = n * 6, sr = static_cast<uint32_t>(s.sr);
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16(2); w32(sr); w32(sr * 6); w16(6); w16(24);
    std::fwrite("data", 1, 4, f); w32(data);
    for (uint32_t i = 0; i < n; ++i) for (float x : {s.L[i], s.R[i]}) {
        const int32_t v = static_cast<int32_t>(std::lrint(std::clamp(x, -1.0f, 1.0f) * 8388607.0f));
        const unsigned char b[3] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v >> 16)}; std::fwrite(b, 1, 3, f);
    }
    std::fclose(f); return true;
}
inline std::vector<std::string> listWavs(const std::string& dir) {
    std::vector<std::string> v; DIR* d = opendir(dir.c_str()); if (!d) return v;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name; if (n.empty() || n[0] == '.' || n.size() < 5) continue;
        std::string ext = n.substr(n.size() - 4); for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".wav") v.push_back(n);
    }
    closedir(d); std::sort(v.begin(), v.end()); return v;
}

// Utilidades de color: filtro DJ por voz (SVF), drive y reducción de bits
struct Svf {
    float ic1 = 0, ic2 = 0;
    // f: -1..0 paso bajo, 0..1 paso alto. res 0..1
    static void coefs(float f, float res, float sr, float& g, float& k) {
        const float fc = f < 0 ? 20000.0f * std::exp2(f * 9.5f) : 12.0f * std::exp2(f * 10.5f);
        g = std::tan(kPi * std::clamp(fc, 10.0f, 0.45f * sr) / sr); k = 2.0f - 1.9f * std::clamp(res, 0.0f, 1.0f);
    }
    float process(float x, float g, float k, bool hp) {
        const float v3 = x - ic2, a1 = 1.0f / (1.0f + g * (g + k)), v1 = a1 * (ic1 + g * v3), v2 = ic2 + g * v1;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        return hp ? x - k * v1 - v2 : v2;
    }
};
inline float crush(float x, float bits) { if (bits >= 15.5f) return x; const float q = std::exp2(bits - 1.0f); return std::round(x * q) / q; }

// ------------------------------------------------------------------ especificaciones (las lee la pantalla)
enum SampP { SA_TUNE, SA_FINE, SA_START, SA_END, SA_MODE, SA_REV, SA_ATT, SA_REL, SA_FILT, SA_RES, SA_DRIVE, SA_BITS, SA_LVL, SA_PAN, SA_CHOKE, SA_VEL, SA_COUNT };
inline const PSpec kSampSpec[SA_COUNT] = {
    {"Tune", -24, 24, 1, 0, 't', false, nullptr, "Tone"}, {"Fine", -100, 100, 1, 0, 'c', false, nullptr, "Tone"},
    {"Start", 0, 1, 0.001f, 0, 'p', false, nullptr, "Sample"}, {"End", 0, 1, 0.001f, 1, 'p', false, nullptr, "Sample"},
    {"Mode", 0, 2, 1, 0, 'n', false, "One shot|Gate|Loop", "Sample"}, {"Reverse", 0, 1, 1, 0, 'n', false, "Off|On", "Sample"},
    {"Attack", 0.001f, 2, 0, 0.001f, 's', true, nullptr, "Envelope"}, {"Release", 0.005f, 4, 0, 0.03f, 's', true, nullptr, "Envelope"},
    {"Filter", -1, 1, 0.01f, 0, 'd', false, nullptr, "Color"}, {"Resonance", 0, 1, 0.01f, 0.2f, 'p', false, nullptr, "Color"},
    {"Drive", 0, 1, 0.01f, 0, 'p', false, nullptr, "Color"}, {"Bits", 2, 16, 0, 16, 'f', false, "bits", "Color"},
    {"Level", 0, 1.5f, 0.01f, 0.8f, 'p', false, nullptr, "Output"}, {"Pan", -1, 1, 0.02f, 0, 'o', false, nullptr, "Output"},
    {"Choke", 0, 4, 1, 0, 'n', false, "Off|A|B|C|D", "Output"}, {"Velocity", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "Output"}};

enum BrkP { BK_BARS, BK_TUNE, BK_GATE, BK_RAND, BK_REV, BK_STUT, BK_FILT, BK_RES, BK_DRIVE, BK_BITS, BK_LVL, BK_RSIZE, BK_COUNT };
inline const PSpec kBrkSpec[BK_COUNT] = {
    {"Bars", 0, 3, 1, 1, 'n', false, "1|2|4|8", "Break"}, {"Tune", -12, 12, 0, 0, 't', false, nullptr, "Break"}, {"Length", 0.1f, 1, 0.01f, 1, 'p', false, nullptr, "Break"},
    {"Random", 0, 1, 0.01f, 0, 'p', false, nullptr, "Chaos"}, {"Reverse", 0, 1, 0.01f, 0, 'p', false, nullptr, "Chaos"}, {"Stutter", 0, 1, 0.01f, 0, 'p', false, nullptr, "Chaos"},
    {"Filter", -1, 1, 0.01f, 0, 'd', false, nullptr, "Color"}, {"Resonance", 0, 1, 0.01f, 0.2f, 'p', false, nullptr, "Color"}, {"Drive", 0, 1, 0.01f, 0, 'p', false, nullptr, "Color"},
    {"Bits", 2, 16, 0, 16, 'f', false, "bits", "Color"}, {"Level", 0, 1.5f, 0.01f, 0.9f, 'p', false, nullptr, "Color"},
    {"Repeat size", 0, 5, 1, 1, 'n', false, "1/32|1/16|1/8|1/4|1/2|1 bar", "Repeat"}};
inline const char* kBrkHolds[8] = {"Repeat", "Repeat 1/32", "Reverse", "Half time", "Random", "Octave down", "Chop", "Backwards"};
constexpr int kBrkHold0 = 20, kOnIdx = 30, kFrzIdx = 31;

enum GrP { GR_SRC, GR_MODE, GR_POS, GR_SIZE, GR_DENS, GR_PITCH, GR_SPRAY, GR_SPREAD, GR_REV, GR_FB, GR_LVL, GR_COUNT };
inline const char* kGrSrcNames = "Mix|In 1|In 2|In 3|In 4|Drums|Track 2|Track 3|Track 4|Sampler|Breaks|Voice";
inline const PSpec kGrSpec[GR_COUNT] = {
    {"Source", 0, 11, 1, 0, 'n', false, kGrSrcNames, "Source"}, {"Mode", 0, 1, 1, 0, 'n', false, "Free|MIDI notes", "Source"},
    {"Position", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Grains"}, {"Size", 0.01f, 0.6f, 0, 0.12f, 's', true, nullptr, "Grains"}, {"Density", 1, 80, 0, 18, 'f', true, "/s", "Grains"},
    {"Tune", -24, 24, 0, 0, 't', false, nullptr, "Grains"}, {"Spray", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Grains"}, {"Stereo", 0, 1, 0.01f, 0.6f, 'p', false, nullptr, "Grains"},
    {"Reverse", 0, 1, 0.01f, 0, 'p', false, nullptr, "Grains"}, {"Feedback", 0, 0.9f, 0.01f, 0, 'p', false, nullptr, "Output"}, {"Level", 0, 1.5f, 0.01f, 0.9f, 'p', false, nullptr, "Output"}};




// ------------------------------------------------------------------ sampler: 16 pads, 16 voces
constexpr int kPads = 16;
class Sampler {
public:
    float p[kPads][SA_COUNT];
    std::atomic<const Sample*> smp[kPads];
    int sel = 0;
    Sampler() { for (int i = 0; i < kPads; ++i) { smp[i] = nullptr; for (int j = 0; j < SA_COUNT; ++j) p[i][j] = kSampSpec[j].def; } }
    void prepare(float sr) { sr_ = sr; }
    // semis: desplazamiento cromático (teclado)
    void trigger(int pad, int vel, int note, float semis = 0) {
        const Sample* s = smp[pad].load(); if (!s || s->size() < 64) return;
        const float* q = p[pad];
        const int grp = static_cast<int>(q[SA_CHOKE]);
        for (auto& v : v_) if (v.on && grp > 0 && v.pad != pad && static_cast<int>(p[v.pad][SA_CHOKE]) == grp) v.choke();
        Voice* v = nullptr; for (auto& x : v_) if (!x.on) { v = &x; break; }
        if (!v) { unsigned b = ~0u; for (auto& x : v_) if (x.age < b) { b = x.age; v = &x; } }
        const double len = static_cast<double>(s->size());
        double a = std::clamp(static_cast<double>(q[SA_START]), 0.0, 1.0) * len, b = std::clamp(static_cast<double>(q[SA_END]), 0.0, 1.0) * len;
        if (b < a) std::swap(a, b);
        if (b - a < 64) b = std::min(len, a + 64);
        if (v->on) { tailL_ += v->lastL; tailR_ += v->lastR; }   // voz robada: su última muestra se apaga en ~2 ms (sin clic)
        *v = Voice{}; v->on = true; v->s = s; v->pad = pad; v->note = note; v->age = ++clock_; v->a = a; v->b = b;
        v->rev = q[SA_REV] >= 0.5f; v->mode = static_cast<int>(q[SA_MODE]);
        v->inc = (s->sr / sr_) * std::exp2((q[SA_TUNE] + q[SA_FINE] / 100.0f + semis) / 12.0f) * (v->rev ? -1.0 : 1.0);
        v->pos = v->rev ? b - 1 : a;
        const float vs = q[SA_VEL]; v->vel = 1.0f - vs + vs * std::pow(vel / 127.0f, 1.3f);
    }
    void release(int note) { for (auto& v : v_) if (v.on && v.note == note && v.stage < 2 && v.mode != 0) v.stage = 2; }
    void allOff() { for (auto& v : v_) if (v.on) v.stage = 2; }
    bool active() const { for (auto& v : v_) if (v.on) return true; return false; }
    void render(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
        for (auto& v : v_) {
            if (!v.on) continue;
            const float* q = p[v.pad]; float g, k; Svf::coefs(q[SA_FILT], q[SA_RES], sr_, g, k);
            const bool filt = std::fabs(q[SA_FILT]) > 0.02f, hp = q[SA_FILT] > 0;
            const float dg = 1.0f + q[SA_DRIVE] * 8.0f, dn = 1.0f / std::tanh(dg), bits = q[SA_BITS];
            const float pa = (q[SA_PAN] + 1.0f) * 0.25f * kPi, gl = std::cos(pa) * 1.4142f, gr = std::sin(pa) * 1.4142f;
            const float attInc = 1.0f / std::max(1.0f, q[SA_ATT] * sr_), relMul = v.choked ? std::exp(-1.0f / (0.004f * sr_)) : std::exp(-1.0f / (std::max(q[SA_REL], 0.003f) * 0.3f * sr_));
            const float lvl = q[SA_LVL] * v.vel;
            const Sample& s = *v.s; const size_t N = s.size();
            for (int i = 0; i < n && v.on; ++i) {
                // envolvente
                if (v.stage == 0) { v.env += attInc; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
                else if (v.stage == 2) { v.env *= relMul; if (v.env < 1e-4f) { v.on = false; break; } }
                // lectura con interpolación lineal
                const size_t ix = std::min(static_cast<size_t>(std::max(0.0, v.pos)), N - 1); const float fr = static_cast<float>(std::clamp(v.pos - static_cast<double>(ix), 0.0, 1.0));
                const size_t ix2 = std::min(ix + 1, N - 1);
                float xl = s.L[ix] + (s.L[ix2] - s.L[ix]) * fr, xr = s.R[ix] + (s.R[ix2] - s.R[ix]) * fr;
                // fundido al final para no hacer clic
                const double rem = v.inc > 0 ? (v.b - v.pos) / v.inc : (v.pos - v.a) / -v.inc;
                float fe = v.mode == 2 ? 1.0f : static_cast<float>(std::min(1.0, rem / 96.0));
                v.pos += v.inc;
                if (v.inc > 0 ? v.pos >= v.b : v.pos < v.a) {
                    if (v.mode == 2) { const double span = v.b - v.a; v.pos = v.a + std::fmod(v.pos - v.a, span); if (v.pos < v.a) v.pos += span; }
                    else v.on = false;
                }
                if (filt) { xl = v.fl.process(xl, g, k, hp); xr = v.fr.process(xr, g, k, hp); }
                if (q[SA_DRIVE] > 0.005f) { xl = std::tanh(xl * dg) * dn; xr = std::tanh(xr * dg) * dn; }
                const float e = v.env * fe * lvl;
                v.lastL = crush(xl, bits) * e * gl; v.lastR = crush(xr, bits) * e * gr;
                L[i] += v.lastL; R[i] += v.lastR;
            }
            if (!v.on) { v.lastL = v.lastR = 0; }
        }
        if (tailL_ != 0.0f || tailR_ != 0.0f) {
            for (int i = 0; i < n; ++i) { L[i] += tailL_; R[i] += tailR_; tailL_ *= 0.9896f; tailR_ *= 0.9896f; }
            if (std::fabs(tailL_) < 1e-6f && std::fabs(tailR_) < 1e-6f) tailL_ = tailR_ = 0.0f;
        }
    }
private:
    struct Voice {
        bool on = false, rev = false, choked = false; int pad = 0, note = -1, mode = 0, stage = 0; unsigned age = 0;
        const Sample* s = nullptr; double pos = 0, inc = 1, a = 0, b = 0; float env = 0, vel = 1, lastL = 0, lastR = 0; Svf fl, fr;
        void choke() { choked = true; stage = 2; }
    };
    Voice v_[16]; float sr_ = 48000, tailL_ = 0, tailR_ = 0; unsigned clock_ = 0;
};

// ------------------------------------------------------------------ breaks: un loop cortado en semicorcheas que sigue el tempo
class Breaks {
public:
    float p[BK_COUNT]; bool hold[8] = {}; bool on = false;
    std::atomic<const Sample*> smp{nullptr};
    std::atomic<int> lastSlice{-1};
    Breaks() { for (int j = 0; j < BK_COUNT; ++j) p[j] = kBrkSpec[j].def; }
    void prepare(float sr) { sr_ = sr; }
    int bars() const { static const int b[4] = {1, 2, 4, 8}; return b[std::clamp(static_cast<int>(p[BK_BARS]), 0, 3)]; }
    void setHold(int k, bool v) { hold[k] = v; if (v && (k == 0 || k == 1)) repIdx_ = -1; if (v && k == 3) halfStart_ = -1; }
    // nota MIDI: salta a ese corte (y lo toca si el break no está en marcha)
    bool startPending_ = false;
    void jump(int k16, float bpm, bool playing) {
        const Sample* s = smp.load(); if (!s) return;
        const int n = bars() * 16, target = (k16 * n / 16) % n;
        jumpTo_ = target;
        if (!playing) { const double spb = sr_ * 60.0 / bpm / 4.0; startSlice(*s, target, spb, bpm, 1, false); }
    }
    // pos0: posición en semicorcheas al empezar el bloque, dp: avance por muestra
    void render(float* L, float* R, int n, double pos0, double dp, float bpm, bool playing) {
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f;
        const Sample* s = smp.load();
        const double spb = sr_ * 60.0 / bpm / 4.0;
        if (s && playing && on) {
            // al encenderlo o al arrancar el Oxi, el break espera al 1 del compás y empieza desde su principio
            if (lastStep_ == -1000000) startPending_ = true;
            for (int i = 0; i < n; ++i) {
                const long st = static_cast<long>(std::floor(pos0 + dp * i));
                if (st == lastStep_) continue;
                lastStep_ = st;
                if (startPending_) {
                    if (((st % 16) + 16) % 16 != 0) continue;   // aún no es el 1 del compás (4/4: 16 semicorcheas)
                    const int nb = bars() * 16; offset_ = static_cast<int>(((-st) % nb + nb) % nb); jumpTo_ = -1; startPending_ = false;
                }
                step(*s, st, spb, bpm, i);
            }
        } else lastStep_ = -1000000;
        if (!s) { for (auto& v : v_) v.on = false; return; }
        float g, k; Svf::coefs(p[BK_FILT], p[BK_RES], sr_, g, k);
        const bool filt = std::fabs(p[BK_FILT]) > 0.02f, hp = p[BK_FILT] > 0;
        const float dg = 1.0f + p[BK_DRIVE] * 8.0f, dn = 1.0f / std::tanh(dg), lvl = p[BK_LVL];
        for (auto& v : v_) {
            if (!v.on) continue;
            if (v.delay >= n) { v.delay -= n; continue; }
            const size_t N = s->size();
            for (int i = std::max(0, v.delay); i < n; ++i) {
                if (v.left <= 0) { v.on = false; break; }
                const float fe = std::min({1.0f, v.age / 96.0f, v.left / 96.0f});
                const long ix = static_cast<long>(std::floor(v.pos));
                if (ix >= 0 && ix + 1 < static_cast<long>(N)) {
                    const float fr = static_cast<float>(v.pos - ix);
                    L[i] += (s->L[ix] + (s->L[ix + 1] - s->L[ix]) * fr) * fe;
                    R[i] += (s->R[ix] + (s->R[ix + 1] - s->R[ix]) * fr) * fe;
                }
                v.pos += v.inc; ++v.age; --v.left;
            }
            v.delay = 0;
        }
        for (int i = 0; i < n; ++i) {
            float xl = L[i], xr = R[i];
            if (filt) { xl = fl_.process(xl, g, k, hp); xr = fr_.process(xr, g, k, hp); }
            if (p[BK_DRIVE] > 0.005f) { xl = std::tanh(xl * dg) * dn; xr = std::tanh(xr * dg) * dn; }
            L[i] = crush(xl, p[BK_BITS]) * lvl; R[i] = crush(xr, p[BK_BITS]) * lvl;
        }
    }
private:
    struct V { bool on = false; double pos = 0, inc = 1; long left = 0, age = 0; int delay = 0; };
    void step(const Sample& s, long st, double spb, float bpm, int at) {
        const int n = bars() * 16;
        if (jumpTo_ >= 0) { offset_ = static_cast<int>(((jumpTo_ - st) % n + n) % n); jumpTo_ = -1; }
        int idx = static_cast<int>(((st + offset_) % n + n) % n);
        double rate = 1, len = 1; int reps = 1; bool rev = false;
        if (hold[7]) idx = n - 1 - idx;
        if (hold[4] || rnd() < p[BK_RAND]) idx = static_cast<int>(rnd() * n) % n;
        if (hold[3]) {
            if (halfStart_ < 0) { halfStart_ = st; halfIdx_ = idx; }
            const long k = st - halfStart_; if (k % 2) return;
            idx = static_cast<int>((halfIdx_ + k / 2) % n); rate *= 0.5; len = 2;
        }
        if (hold[0] || hold[1]) {
            // repetir: vuelve una y otra vez sobre un trozo del tamaño elegido (o 1/32 con el segundo botón)
            static const int SZ[6] = {0, 1, 2, 4, 8, 16};
            const int sz = hold[1] ? 0 : SZ[std::clamp(static_cast<int>(p[BK_RSIZE]), 0, 5)];
            if (repIdx_ < 0) { repIdx_ = idx; repStart_ = st; }
            if (sz == 0) { idx = repIdx_; reps = 2; }
            else idx = static_cast<int>((repIdx_ + ((st - repStart_) % sz)) % n);
        }
        else if (rnd() < p[BK_STUT]) { idx = lastIdx_ >= 0 ? lastIdx_ : idx; reps = rnd() < 0.5f ? 2 : 1; }
        if (hold[5]) rate *= 0.5;
        if (hold[6]) len *= 0.35;
        rev = hold[2] || rnd() < p[BK_REV];
        lastIdx_ = idx; lastSlice = idx % 16;
        for (int r = 0; r < reps; ++r) startSlice(s, idx, spb, bpm, len / reps * p[BK_GATE], rev, at + static_cast<int>(r * spb / reps), rate);
    }
    void startSlice(const Sample& s, int idx, double spb, float bpm, double lenSteps, bool rev, int delay = 0, double rate = 1) {
        const int n = bars() * 16; const double N = static_cast<double>(s.size()), sl = N / n;
        const double srcBpm = bars() * 4 * 60.0 / (N / s.sr);
        const double inc = (bpm / srcBpm) * (s.sr / sr_) * rate * std::exp2(p[BK_TUNE] / 12.0);
        V* v = nullptr; for (auto& x : v_) if (!x.on) { v = &x; break; } if (!v) v = &v_[0];
        v->on = true; v->delay = delay; v->age = 0; v->left = static_cast<long>(lenSteps * spb);
        v->inc = rev ? -inc : inc; v->pos = rev ? (idx + 1) * sl - 1 : idx * sl;
    }
    float rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (seed_ >> 8) * (1.0f / 16777216.0f); }
    V v_[6]; float sr_ = 48000; long lastStep_ = -1000000, halfStart_ = -1, repStart_ = 0; int offset_ = 0, jumpTo_ = -1, repIdx_ = -1, halfIdx_ = 0, lastIdx_ = -1; uint32_t seed_ = 12345;
    Svf fl_, fr_;
};

// ------------------------------------------------------------------ granular: graba 8 s de la fuente y los reparte en granos
class Granular {
public:
    float p[GR_COUNT]; bool on = false, freeze = false;
    static constexpr int kSeg = 96, kMaxG = 64;
    std::atomic<float> segPk[kSeg]; std::atomic<float> gpos[16]; std::atomic<int> ngr{0};
    std::atomic<float> headFrac{0};
    Granular() { for (int j = 0; j < GR_COUNT; ++j) p[j] = kGrSpec[j].def; for (auto& s : segPk) s = 0; for (auto& g : gpos) g = -1; }
    void prepare(float sr) { sr_ = sr; N_ = static_cast<int>(sr * 8); ring_.assign(static_cast<size_t>(N_), 0.0f); g_.reserve(kMaxG + 8); notes_.reserve(128); }
    void noteOn(int n) { notes_.erase(std::remove(notes_.begin(), notes_.end(), n), notes_.end()); if (notes_.size() < 64) notes_.push_back(n); }
    void noteOff(int n) { notes_.erase(std::remove(notes_.begin(), notes_.end(), n), notes_.end()); }
    void allOff() { notes_.clear(); }
    // escribe la fuente (mono) en el anillo
    void record(const float* L, const float* R, int n) {
        if (!on || freeze) return;
        const int segLen = N_ / kSeg;
        for (int i = 0; i < n; ++i) {
            const float x = 0.5f * (L[i] + R[i]) + p[GR_FB] * fbBuf_[i];
            ring_[static_cast<size_t>(w_)] = x;
            const int sg = w_ / segLen; if (w_ % segLen == 0) segAcc_ = 0; segAcc_ = std::max(segAcc_, std::fabs(x)); if (sg < kSeg) segPk[sg] = segAcc_;
            if (++w_ >= N_) w_ = 0;
        }
        headFrac = static_cast<float>(w_) / N_;
    }
    void render(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) { L[i] = R[i] = 0.0f; fbBuf_[i] = 0.0f; }
        if (!on) { g_.clear(); ngr = 0; return; }
        bool gate = true; float semi = 0;
        if (p[GR_MODE] >= 0.5f) { gate = !notes_.empty(); if (gate) semi = static_cast<float>(notes_.back() - 60); }
        const int size = std::max(64, static_cast<int>(p[GR_SIZE] * sr_));
        const float rate = std::min(8.0f, std::exp2((p[GR_PITCH] + semi) / 12.0f)), dens = p[GR_DENS];
        const float norm = 1.5f / std::sqrt(std::max(1.0f, dens * p[GR_SIZE]));
        if (gate) {
            acc_ += dens * n / sr_;
            while (acc_ >= 1 && static_cast<int>(g_.size()) < kMaxG) {
                acc_ -= 1;
                const float back = (0.05f + p[GR_POS] * 7.0f) * sr_ + size * std::max(rate, 1.0f) + rnd() * p[GR_SPRAY] * sr_ * 1.5f;
                G g; g.pos = std::fmod(w_ - static_cast<double>(back), static_cast<double>(N_)); if (g.pos < 0) g.pos += N_; g.len = size; g.age = 0;
                g.inc = rnd() < p[GR_REV] ? -rate : rate;
                const float pan = 0.5f + (rnd() - 0.5f) * p[GR_SPREAD]; g.gl = std::cos(pan * kPi * 0.5f); g.gr = std::sin(pan * kPi * 0.5f);
                g.delay = static_cast<int>(rnd() * n);
                g_.push_back(g);
            }
        } else acc_ = 0;
        const float lvl = p[GR_LVL];
        for (size_t k = g_.size(); k-- > 0;) {
            G& g = g_[k];
            for (int i = g.delay; i < n && g.age < g.len; ++i) {
                const int ix = static_cast<int>(g.pos); const float fr = static_cast<float>(g.pos - ix);
                const float a = ring_[static_cast<size_t>(ix)], b = ring_[static_cast<size_t>(ix + 1 >= N_ ? 0 : ix + 1)];
                const float ph = static_cast<float>(g.age) / g.len, win = 0.5f - 0.5f * fsin(ph + 0.25f);   // Hann
                const float v = (a + (b - a) * fr) * win * norm;
                L[i] += v * g.gl * lvl; R[i] += v * g.gr * lvl; fbBuf_[i] += v;
                g.pos += g.inc; if (g.pos >= N_) g.pos -= N_; if (g.pos < 0) g.pos += N_; ++g.age;
            }
            g.delay = 0;
            if (g.age >= g.len) { g_[k] = g_.back(); g_.pop_back(); }
        }
        ngr = static_cast<int>(g_.size());
        for (int j = 0; j < 16; ++j) gpos[j] = j < static_cast<int>(g_.size()) ? static_cast<float>(g_[static_cast<size_t>(j)].pos / N_) : -1.0f;
    }
private:
    struct G { double pos, inc; int len, age, delay; float gl, gr; };
    float rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (seed_ >> 8) * (1.0f / 16777216.0f); }
    std::vector<float> ring_; std::vector<G> g_; std::vector<int> notes_;
    float sr_ = 48000, acc_ = 0, segAcc_ = 0, fbBuf_[1024] = {}; int N_ = 1, w_ = 0; uint32_t seed_ = 777;
};



}  // namespace gb

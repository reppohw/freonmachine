// freon-machine.cpp — Freon Machine: el motor de sonido (todo menos el secuenciador, que es el Oxi One).
//
// El Oxi One (o cualquier fuente MIDI) toca 4 pistas por canales MIDI configurables:
//   pista 1 batería (8 sonidos, 5 kits) · pistas 2-4 sintes (Juno, DX, Moog, 303, Game Boy) · Sub
// La pantalla es una página web que sirve el propio programa: http://localhost:8080
//
//   ./freon-machine --list                    interfaces de audio y puertos MIDI
//   ./freon-machine [--audio N] [--midi N] [--out N] [--rate 48000] [--buffer 128] [--port 8080] [--host 127.0.0.1]
//   ./freon-machine --sin-audio               sin tarjeta de sonido (para probar la pantalla)
//   ./freon-machine --render prueba.wav       genera un WAV de prueba con todos los motores y kits

#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <climits>
#include <algorithm>
#include <fstream>
#include <functional>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <unistd.h>
#if defined(__linux__)
#include <sys/mman.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "RtAudio.h"
#include "RtMidi.h"
#include "drums.h"
#include "fx.h"
#include "par.h"
#include "live.h"
#include "voice.h"
#include "params.h"
#include "server.h"
#ifdef __linux__
#include <ifaddrs.h>
#include <netinet/in.h>
#endif

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

using namespace gb;

namespace {

// ------------------------------------------------------------------ colas sin bloqueos
template <class T, unsigned N>
class Spsc {
public:
    bool push(const T& e) { const unsigned w = w_.load(std::memory_order_relaxed), nx = (w + 1) & (N - 1); if (nx == r_.load(std::memory_order_acquire)) { drops.fetch_add(1, std::memory_order_relaxed); return false; } b_[w] = e; w_.store(nx, std::memory_order_release); return true; }
    std::atomic<unsigned> drops{0};   // mensajes perdidos por cola llena
    bool pop(T& e) { const unsigned r = r_.load(std::memory_order_relaxed); if (r == w_.load(std::memory_order_acquire)) return false; e = b_[r]; r_.store((r + 1) & (N - 1), std::memory_order_release); return true; }
private:
    T b_[N]; std::atomic<unsigned> w_{0}, r_{0};
};

enum Kind : uint8_t { K_ENGP, K_ENG, K_CHN, K_STRIP, K_GRP, K_FX, K_MST, K_KIT, K_LANE, K_SUB, K_NOTE, K_ALLOFF, K_LIVE, K_LCH, K_REC };
// Módulos de directo (K_LIVE: a = módulo, b = pad, c = parámetro). En las notas, t = 10 + módulo
enum LiveMod { LM_SAMP = 0, LM_BRK, LM_GRAN, LM_VOZ, LM_COUNT };
struct Cmd { Kind k; int16_t a, b, c; float v; };
struct Midi { unsigned char s, d1, d2; double t; };          // t: instante de llegada (s, reloj monotónico)
struct CtlMidi { unsigned char s, d1, d2; int16_t port; };     // CC y Program Change: los atiende el hilo de control
inline double monoNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Feedback { int16_t t, e, i; float v; };   // CC del Oxi que movió un knob → pantalla

constexpr int NT = 4, BLK = 256, NCH = 15, NGRP = 2, NSEND = 4, NDIR = 7, NBUS = 3 + NDIR;   // buses: máster, grupo A, grupo B y 7 pares de salidas directas
// Canales: 0 Bombo, 1 Caja, 2 Resto bat, 3 Sub, 4-6 pistas 2-4, 7-10 directo (fase 3), 11-14 entradas de audio
constexpr int CH_SUB = 3, CH_T0 = 4, CH_GRAN = 7, CH_SAMP = 8, CH_BRK = 9, CH_VOZ = 10, CH_IN = 11;
const char* kChNames[NCH] = {"Kick", "Snare", "Drums rest", "Sub", "Track 2", "Track 3", "Track 4", "Granular", "Sampler", "Breaks", "Voice", "Input 1", "Input 2", "Input 3", "Input 4"};
constexpr int kNLane = 16;
const char* kLaneKeys[kNLane] = {"tune", "decay", "snap", "filt", "reso", "sweep", "level", "pan", "vel", "tone", "color", "shape", "contour", "mute", "note", "out"};
// Los 8 knobs de cada sonido según el kit (el 9.º va en la fila extra, junto al pan). Mismo orden en la web y en la controladora.
inline const char* const* drumSlots(int uiKit, const char*& extra) {
    // Misma posición, misma función en todos los kits: nivel, afinación, duración, timbre, golpe, barrido, filtro, resonancia.
    // Regla de Jorge: Filter y Reso siempre contiguos (knobs 7 y 8)
    static const char* kPl[8] = {"level", "tune", "decay", "tone", "snap", "sweep", "filt", "reso"};
    static const char* kCy[8] = {"level", "tune", "decay", "color", "shape", "sweep", "filt", "reso"};
    static const char* kRc[8] = {"level", "tune", "decay", "vel", "snap", "sweep", "filt", "reso"};
    if (uiKit <= 1) { extra = "vel"; return kPl; }
    if (uiKit == 2) { extra = "contour"; return kCy; }
    extra = nullptr; return kRc;
}
const char* kSubKeys[8] = {"ch", "on", "oct", "tone", "glide", "vol", "mode", "src"};
// Parámetros de canal y grupo (mismo orden en las claves y en el audio)
enum SP { SP_LVL, SP_PAN, SP_MUTE, SP_SOLO, SP_CUE, SP_OUT, SP_DTYPE, SP_DRIVE, SP_DTONE, SP_DMIX, SP_LOW, SP_MID, SP_HIGH, SP_LIM, SP_SC, SP_SCAMT, SP_SCREL, SP_S0, SP_S1, SP_S2, SP_S3, SP_FILT, SP_RES, SP_TRIM, SP_COUNT };
const char* kSP[SP_COUNT] = {"lvl", "pan", "mute", "solo", "cue", "out", "dtype", "drive", "dtone", "dmix", "low", "mid", "high", "lim", "sc", "scAmt", "scRel", "s0", "s1", "s2", "s3", "filt", "res", "trim"};
enum MP { MP_VOL, MP_LIM, MP_CUELVL, MP_CUEMODE, MP_DTYPE, MP_DRIVE, MP_DTONE, MP_DMIX, MP_FILT, MP_RES, MP_REP, MP_GDIV, MP_GATE, MP_RISER, MP_SPACE, MP_SPSIZE, MP_THROW, MP_TDIV, MP_TFB, MP_TAPE, MP_BPM, MP_CLOCK, MP_RDECAY, MP_RNOISE, MP_TAPESM, MP_RSIZE, MP_FENV, MP_RPITCH, MP_GSHAPE, MP_GPAN, MP_SPDAMP, MP_SPFRZ, MP_TTONE, MP_COMB, MP_CBPITCH, MP_CBFB, MP_CBDAMP, MP_SWEEP, MP_SWRATE, MP_SWDEPTH, MP_SWFB, MP_SWMODE, MP_SUM, MP_BCOMP, MP_BCRAT, MP_BCATK, MP_BCREL, MP_BCMIX, MP_BCAIR, MP_BCMKP, MP_CLIP, MP_COUNT };   // MP_SUM ya no se usa
const char* kMP[MP_COUNT] = {"vol", "lim", "cueLvl", "cueMode", "dtype", "drive", "dtone", "dmix", "filt", "res", "rep", "gdiv", "gate", "riser", "space", "spSize", "throw", "throwDiv", "throwFb", "tape", "bpm", "clock", "rDecay", "rNoise", "tapeSm", "rSize", "fEnv", "rPitch", "gShape", "gPan", "spDamp", "spFrz", "tTone", "comb", "cbPitch", "cbFb", "cbDamp", "sweep", "swRate", "swDepth", "swFb", "swMode", "sum", "bcomp", "bcRatio", "bcAtk", "bcRel", "bcMix", "bcAir", "bcMkp", "clip"};
const float kMpDef[MP_COUNT] = {0.85f, -1.5f, 0.8f, 0, 0, 0, 1, 1, 0, 1.4f, 0, 2, 0, 0, 0, 0.7f, 0, 6, 0.55f, 0, 120, 1, 0, 0.5f, 0.5f, 2, 0, 0, 0, 0, 0.53f, 0, 0.7f, 0, 48, 0.8f, 0.3f, 0, 2, 0.7f, 0.4f, 0, 0, 0, 1, 10, 0.3f, 1, 0, 0, 1};
// Distorsión: Hard, Fuzz y Bitcrusher
const int kDistMap[2] = {D_FUZZ, D_BITS};
inline int distType(float v) { return kDistMap[std::clamp(static_cast<int>(v), 0, 1)]; }
// Kits: 808, Chip FM, Game Boy y Experimental (el tipo DRM queda solo para los breaks de serie)
const int kKitMap[5] = {KIT_P808, KIT_P909, KIT_CYC, KIT_GB, KIT_EXP};   // 808, 909, FM (Cycles), Chip, Experimental
const float kRepDivs[4] = {4, 2, 1, 0.5f};    // 1/4, 1/8, 1/16, 1/32 en semicorcheas (gater)

// Notas pulsadas de una pista, sin reservar memoria en el audio
struct NoteSet {
    int v[32] = {}; int n = 0;
    void add(int x) { remove(x); if (n < 32) v[n++] = x; }
    void remove(int x) { int j = 0; for (int i = 0; i < n; ++i) if (v[i] != x) v[j++] = v[i]; n = j; }
    void clear() { n = 0; }
    bool empty() const { return n == 0; }
    int lowest() const { int lo = v[0]; for (int i = 1; i < n; ++i) lo = std::min(lo, v[i]); return lo; }
};

// ------------------------------------------------------------------ Sub: sigue la nota más grave de su pista
struct SubVoice {
    float sr = 48000, ph = 0, semi = 36, target = 36, amp = 0, lp = 0;
    bool on = false;
    void render(float* o, int n, float tone, float glide, float vol) {
        const float gl = 1.0f - std::exp(-1.0f / (std::max(glide, 0.002f) * 0.33f * sr));
        const float dg = 1.0f + tone * 7.0f, norm = 1.0f / std::tanh(dg), lpc = 1.0f - std::exp(-2 * kPi * (160 + tone * 1400) / sr);
        for (int i = 0; i < n; ++i) {
            semi += (target - semi) * gl;
            amp += ((on ? 1.0f : 0.0f) - amp) * (on ? 0.004f : 0.0015f);
            ph += mtof(semi) / sr; if (ph >= 1) ph -= 1;
            const float x = std::tanh(fsin(ph) * dg) * norm;
            lp += (x - lp) * lpc;
            o[i] = lp * amp * 0.158f * vol;
        }
    }
};

// ------------------------------------------------------------------ tira de canal / grupo
struct Strip {
    float p[SP_COUNT] = {1.0f, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, -1, 0.5f, 0.25f, 0, 0, 0, 0, 0, 1.4f, 0};
    float trimG = 1;   // trim de entrada del canal
    Dist dist; Shelf lo[2], mi[2], hi[2]; bool eqDirty = true; Follower fol; Limiter lim; DJFilter dj;
    float L[BLK], R[BLK], pre[2][BLK], g[BLK];
    float sr = 48000;
    float glP = 1, grP = 1, lvP = 0, sP[4] = {};   // valores del bloque anterior: rampas sin escalones ni clics
    void prepare(float s) { sr = s; dist.prepare(s); lim.sr = s; dj.sr = s; }
    void set(int i, float v) {
        p[i] = v;
        if (i == SP_DTYPE) dist.type = distType(v); else if (i == SP_DRIVE) dist.drive = v; else if (i == SP_DTONE) dist.tone = v; else if (i == SP_DMIX) dist.mix = v;
        else if (i == SP_LOW || i == SP_MID || i == SP_HIGH) eqDirty = true; else if (i == SP_LIM) lim.thrDb = v; else if (i == SP_FILT) dj.f = v; else if (i == SP_RES) dj.res = v; else if (i == SP_TRIM) trimG = std::pow(10.0f, v / 20.0f);
    }
    // distorsión + EQ (o filtro en los grupos). Lo que sale aquí es la señal que alimenta los sidechains
    bool silent = false;
    void stageA(int n, bool group) {
        if (trimG != 1.0f) for (int i = 0; i < n; ++i) { L[i] *= trimG; R[i] *= trimG; }
        // canal en silencio: nada que procesar (los filtros no tienen cola audible)
        float pkIn = 0, acc = 0; for (int i = 0; i < n; ++i) { pkIn = std::max(pkIn, std::fabs(L[i]) + std::fabs(R[i])); acc += L[i] + R[i]; }
        if (!std::isfinite(acc) || !(pkIn < 128.0f)) { std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f); pkIn = 0; }   // un motor averiado (o un filtro desbocado, +36 dB) no contagia al resto de la mezcla
        silent = pkIn < 1e-6f;
        if (silent) { std::fill(pre[0], pre[0] + n, 0.0f); std::fill(pre[1], pre[1] + n, 0.0f); return; }
        dist.process(L, R, n);
        if (group) dj.process(L, R, n);
        else if (std::fabs(p[SP_LOW]) > 0.01f || std::fabs(p[SP_MID]) > 0.01f || std::fabs(p[SP_HIGH]) > 0.01f) {
            if (eqDirty) { for (int c = 0; c < 2; ++c) { lo[c].set(0, 120, p[SP_LOW], 0.7f, sr); mi[c].set(2, 1000, p[SP_MID], 0.8f, sr); hi[c].set(1, 6000, p[SP_HIGH], 0.7f, sr); } eqDirty = false; }
            for (int i = 0; i < n; ++i) { L[i] = hi[0].process(mi[0].process(lo[0].process(L[i]))); R[i] = hi[1].process(mi[1].process(lo[1].process(R[i]))); }
        }
        std::copy(L, L + n, pre[0]); std::copy(R, R + n, pre[1]);
    }
};

// ------------------------------------------------------------------ motor de audio (solo lo toca el hilo de audio)
struct Audio {
    float sr = 48000;
    DrumMachine drums;
    MeloEngines mel[NT];          // pistas 2-4 (índices 1..3); la 0 es la batería
    int ch[NT] = {10, 2, 3, 4};
    SubVoice sub; NoteSet subHeld; int subCh = 9; bool subOn = true; float subOct = -2, subTone = 0.2f, subGlide = 0.03f, subVol = 1; int subMode = 1, subSrc = 1;   // modo 1: sigue la nota más grave de la pista subSrc (como antes); 0: su propio canal
    NoteSet held[NT];
    Strip st[NCH], grp[NGRP];
    SendFx fx[NSEND]; float fxTail[NSEND] = {};
    MasterFx mfx; Dist mdist; DJFilter mdj; LookLimiter mlim; BusComp bcomp; std::atomic<float> bcGrDb{0};
    float mp[MP_COUNT]; float sumG = 0, fEnvS = 0;
    // reparto entre núcleos: cada tarea escribe solo en lo suyo
    Parallel par; float monoT[NT][BLK]; float fxOutL[NSEND][BLK], fxOutR[NSEND][BLK]; bool fxRun[NSEND] = {}; std::atomic<bool> fxBad[NSEND];
    float fxIdle[NSEND] = {}; int jobK = 0; double jobP0 = 0, jobDp = 0; bool jobPlay = false;
    static constexpr int kParMin = 32, kNSrc = 7;   // trozos muy cortos: no compensa repartir
    void renderSource(int i) {
        const int k = jobK;
        switch (i) {
            case 0: { float* lp[8]; for (int l = 0; l < 8; ++l) lp[l] = lanes[l]; drums.render(lp, k); break; }
            case 1: case 2: case 3: mel[i].render(st[CH_T0 + i - 1].L, st[CH_T0 + i - 1].R, monoT[i], k); break;
            case 4: samp.render(st[CH_SAMP].L, st[CH_SAMP].R, k); break;
            case 5: brk.render(st[CH_BRK].L, st[CH_BRK].R, k, jobP0, jobDp, bpm, jobPlay); break;
            default: gran.render(st[CH_GRAN].L, st[CH_GRAN].R, k); break;
        }
    }
    void renderSend(int f) {
        if (!fxRun[f]) return;
        const int k = jobK; fx[f].process(sendL[f], sendR[f], fxOutL[f], fxOutR[f], k, bpm);
        float acc = 0; for (int i = 0; i < k; ++i) acc += fxOutL[f][i] + fxOutR[f][i];
        if (!std::isfinite(acc)) { std::fill(fxOutL[f], fxOutL[f] + k, 0.0f); std::fill(fxOutR[f], fxOutR[f] + k, 0.0f); fxBad[f].store(true, std::memory_order_relaxed); }
    }
    static void srcTask(void* u, int i) { static_cast<Audio*>(u)->renderSource(i); }
    static void sendTask(void* u, int i) { static_cast<Audio*>(u)->renderSend(i); }
    float lanes[8][BLK], tmpL[BLK], tmpR[BLK], mono[BLK];
    float busL[NBUS][BLK], busR[NBUS][BLK], sendL[NSEND][BLK], sendR[NSEND][BLK], cueL[BLK], cueR[BLK];
    // reloj
    double pos = 0; long ticks = 0; long sinceTick = 1 << 30; bool running = false; float bpm = 120;
    std::atomic<float> clkBpm{0};
    // medidores
    std::atomic<float> peak[NCH + NGRP + 2]; std::atomic<float> grDb{0};
    std::atomic<float> cpu{0}, cpuPeak{0};
    std::atomic<uint64_t> calls{0}; std::atomic<unsigned> xruns{0}, late{0};      // vigilante: el audio sigue vivo
    std::atomic<float> bpmPub{120}; std::atomic<bool> clkPub{false}, runPub{false};   // copias seguras para el hilo de control
    std::atomic<int> act[NT]; std::atomic<int> dact[8]; std::atomic<int> sact[kPads];
    int outCh = 2, inCh = 0;
    // rutas de la interfaz (de Ajustes): par de la salida principal, par del cue (-2 automático, -1 ninguno) y canal de la interfaz de cada entrada (-1 ninguno)
    // se cambian en marcha desde Ajustes (sin reiniciar): el audio las lee una vez por bloque
    std::atomic<int> mainPair{0}, cuePair{-2}, inMap[4] = {{0}, {1}, {2}, {3}};
    // directo
    Sampler samp; Breaks brk; Granular gran; VoiceFx voz;
    int lch[LM_COUNT] = {5, 6, 7, 8};
    float inRaw[4][BLK] = {}, mixL[BLK], mixR[BLK], srcL[BLK], srcR[BLK];
    double lastPos = 0; float volP = 0.85f;
    // grabación al sampler: 0 parado, 1 grabando, 2 terminada (el hilo de control la recoge)
    std::vector<float> recL, recR; std::atomic<int> recState{0}; std::atomic<long> recLen{0}; int recSrc = 0;
    Spsc<Cmd, 16384> cmds; Spsc<Midi, 4096> midi; std::mutex midiMx;   // la cola de MIDI es de un solo escritor: los que escriben fuera del hilo de MIDI pasan por pushMidi
    void pushMidi(const Midi& m) { std::lock_guard<std::mutex> lk(midiMx); midi.push(m); }
    Spsc<Feedback, 4096> fb; Spsc<CtlMidi, 4096> ctlq;

    void prepare(float rate) {
        std::copy(kMpDef, kMpDef + MP_COUNT, mp); fsin(0);
        sr = rate; drums.prepare(sr); for (auto& m : mel) m.prepare(sr); sub.sr = sr;
        for (auto& s : st) s.prepare(sr);
        for (auto& g : grp) g.prepare(sr);
        for (auto& f : fx) f.prepare(sr);
        mfx.prepare(sr); mdist.prepare(sr); mdj.sr = sr; mlim.prepare(sr); bcomp.prepare(sr);
        for (auto& p : peak) p = 0;
        for (auto& a : act) a = 0;
        for (auto& b : fxBad) b = false;
        for (auto& a : dact) a = 0;
        for (auto& a : sact) a = 0;
        mel[1].select(E_JUNO); mel[2].select(E_ACID); mel[3].select(E_MOOG);
        samp.prepare(sr); brk.prepare(sr); gran.prepare(sr); voz.prepare(sr);
        recL.assign(static_cast<size_t>(sr * 30), 0.0f); recR.assign(static_cast<size_t>(sr * 30), 0.0f);
    }
    // notas de los módulos de directo
    void liveNote(int m, int n, int v) {
        switch (m) {
        case LM_SAMP:
            if (v > 0) { if (n >= 36 && n < 36 + kPads) { samp.trigger(n - 36, v, n); sact[n - 36] = 1; } else if (n >= 60) samp.trigger(samp.sel, v, n, static_cast<float>(n - 72)); }
            else samp.release(n);
            break;
        case LM_BRK:
            if (n >= 36 && n < 52) { if (v > 0) brk.jump(n - 36, bpm, brkPlaying()); }
            else if (n >= 60 && n < 68) brk.setHold(n - 60, v > 0);
            break;
        case LM_GRAN: if (v > 0) gran.noteOn(n); else gran.noteOff(n); break;
        default: if (v > 0) voz.noteOn(n); else voz.noteOff(n); break;
        }
    }
    bool clkOn() const { return mp[MP_CLOCK] >= 0.5f && sinceTick < static_cast<long>(sr * 0.5f) && clkBpm.load() > 20; }
    bool brkPlaying() const { return clkOn() ? running : true; }
    void liveSet(int m, int pad, int i, float v) {
        const bool b = v >= 0.5f;
        switch (m) {
        case LM_SAMP: if (pad < 0) samp.sel = std::clamp(static_cast<int>(v), 0, kPads - 1); else if (pad < kPads && i >= 0 && i < SA_COUNT) samp.p[pad][i] = std::clamp(v, kSampSpec[i].min, kSampSpec[i].max); break;
        case LM_BRK: if (i < 0) break; if (i < BK_COUNT) brk.p[i] = std::clamp(v, kBrkSpec[i].min, kBrkSpec[i].max); else if (i >= kBrkHold0 && i < kBrkHold0 + 8) brk.setHold(i - kBrkHold0, b); else if (i == kOnIdx) brk.on = b; break;
        case LM_GRAN: if (i < 0) break; if (i < GR_COUNT) gran.p[i] = std::clamp(v, kGrSpec[i].min, kGrSpec[i].max); else if (i == kOnIdx) gran.on = b; else if (i == kFrzIdx) gran.freeze = b; break;
        default: if (i < 0) break; if (i < VZ_COUNT) voz.p[i] = std::clamp(v, kVzSpec[i].min, kVzSpec[i].max); else if (i == kOnIdx) voz.on = b; break;
        }
    }
    void subUpdate() {
        auto& h = subHeld;
        if (!subOn || h.empty()) { sub.on = false; return; }
        const int lo = h.lowest();
        const float t = static_cast<float>(lo) + subOct * 12.0f;
        if (!sub.on && sub.amp < 0.01f) sub.semi = t;
        sub.target = t; sub.on = true;
    }
    void noteOn(int t, int n, int v) {
        if (t < 0 || t >= NT) return;
        act[t] = 1;
        if (t == 0) { const int l = drums.noteOn(n, v); if (l >= 0 && !drums.lane[static_cast<size_t>(l)].mute) dact[l] = 1; return; }
        held[t].add(n);
        mel[t].noteOn(n, v);
        if (subMode == 1 && t == subSrc) { subHeld.add(n); subUpdate(); }
    }
    void noteOff(int t, int n) {
        if (t <= 0 || t >= NT) return;
        held[t].remove(n);
        mel[t].noteOff(n);
        if (subMode == 1 && t == subSrc) { subHeld.remove(n); subUpdate(); }
    }
    void master(int i, float v) {
        mp[i] = v;
        switch (i) {
            case MP_LIM: mlim.thrDb = std::min(v, -0.06f); break;
            case MP_DTYPE: mdist.type = distType(v); break; case MP_DRIVE: mdist.drive = v; break; case MP_DTONE: mdist.tone = v; break; case MP_DMIX: mdist.mix = v; break;
            case MP_FILT: mdj.f = v; break; case MP_RES: mdj.res = v; break;
            case MP_REP: mfx.rep = v; break; case MP_GDIV: mfx.gateDiv = kRepDivs[std::clamp(static_cast<int>(v), 0, 3)]; break;
            case MP_GATE: mfx.gate = v; break; case MP_RISER: mfx.riser = v; break; case MP_SPACE: mfx.space = v; break; case MP_SPSIZE: mfx.spaceSize = v; break;
            case MP_THROW: mfx.thr = v; break; case MP_TDIV: mfx.throwDiv = v; break; case MP_TFB: mfx.throwFb = v; break; case MP_TAPE: mfx.tape = v; break; case MP_RDECAY: mfx.repDecay = v; break; case MP_RNOISE: mfx.riserNoise = v; break; case MP_TAPESM: mfx.tapeInertia = v; break; case MP_RSIZE: mfx.repSize = v; break;
            case MP_RPITCH: mfx.repPitch = v; break; case MP_GSHAPE: mfx.gShape = v; break; case MP_GPAN: mfx.gPan = v; break; case MP_SPDAMP: mfx.spDamp = v; break; case MP_SPFRZ: mfx.spFreeze = v >= 0.5f; break; case MP_TTONE: mfx.thTone = v; break;
            case MP_COMB: mfx.comb = v; break; case MP_CBPITCH: mfx.cbPitch = v; break; case MP_CBFB: mfx.cbFb = v; break; case MP_CBDAMP: mfx.cbDamp = v; break;
            case MP_BCOMP: bcomp.amount = v; break; case MP_BCRAT: bcomp.ratio = v; break; case MP_BCATK: bcomp.attack = v; break; case MP_BCREL: bcomp.release = v; break; case MP_BCMIX: bcomp.mix = v; break; case MP_BCAIR: bcomp.air = v; break; case MP_BCMKP: bcomp.makeup = v; break;
            case MP_SWEEP: mfx.sweep = v; break; case MP_SWRATE: mfx.swRate = v; break; case MP_SWDEPTH: mfx.swDepth = v; break; case MP_SWFB: mfx.swFb = v; break; case MP_SWMODE: mfx.swMode = v; break;
            default: break;
        }
    }
    void apply(const Cmd& c) {
        switch (c.k) {
            case K_ENGP: if (c.a >= 1 && c.a < NT && c.b >= 0 && c.b < E_COUNT && std::isfinite(c.v)) { const auto& E = kEng[c.b]; mel[c.a].set(c.b, c.c, c.c >= 0 && c.c < E.np ? std::clamp(c.v, E.p[c.c].min, E.p[c.c].max) : std::clamp(c.v, 0.0f, 1.0f)); } break;
            case K_ENG: if (c.a >= 1 && c.a < NT) { mel[c.a].select(static_cast<int>(c.v)); held[c.a].clear(); } break;
            case K_CHN: if (c.a >= 0 && c.a < NT) ch[c.a] = static_cast<int>(c.v); break;
            case K_STRIP: if (c.a >= 0 && c.a < NCH && c.b >= 0 && c.b < SP_COUNT) st[c.a].set(c.b, c.v); break;
            case K_GRP: if (c.a >= 0 && c.a < NGRP && c.b >= 0 && c.b < SP_COUNT) grp[c.a].set(c.b, c.v); break;
            case K_FX: if (c.a >= 0 && c.a < NSEND) { auto& f = fx[c.a]; if (c.b == 5) { const int t = std::clamp(static_cast<int>(c.v), 0, FX_COUNT - 1); if (f.type != t) { f.type = t; f.startClear(); } } else if (c.b == 6) f.ret = c.v; else if (c.b >= 0 && c.b < 5) f.p[c.b] = c.v; } break;
            case K_MST: if (c.b >= 0 && c.b < MP_COUNT) master(c.b, c.v); break;
            case K_KIT: drums.kit = kKitMap[std::clamp(static_cast<int>(c.v), 0, 4)]; break;
            case K_LANE: { if (c.a < 0 || c.a > 7) break; auto& L = drums.lane[static_cast<size_t>(c.a)];
                switch (c.b) { case 0: L.tune = c.v; break; case 1: L.decay = c.v; break; case 2: L.snap = c.v; break; case 3: L.filt = std::clamp(c.v, -1.0f, 1.0f); break; case 4: L.reso = c.v; break;
                               case 5: L.sweep = c.v; break; case 6: L.level = c.v; break; case 7: L.pan = c.v; break; case 8: L.vel = c.v; break; case 9: L.tone = c.v; break; case 10: L.color = c.v; break;
                               case 11: L.shape = c.v; break; case 12: L.contour = c.v; break; case 13: L.mute = c.v >= 0.5f; break; case 14: L.note = std::clamp(static_cast<int>(c.v), 0, 127); break;
                               case 15: L.out = std::clamp(static_cast<int>(c.v), 0, 2); break; }
                break; }
            case K_SUB: switch (c.b) { case 0: subCh = static_cast<int>(c.v); subHeld.clear(); subUpdate(); break; case 1: subOn = c.v >= 0.5f; subUpdate(); break; case 2: subOct = c.v; subUpdate(); break;
                                       case 3: subTone = c.v; break; case 4: subGlide = c.v; break; case 5: subVol = c.v; break; case 6: subMode = c.v >= 0.5f ? 1 : 0; subHeld.clear(); subUpdate(); break; case 7: subSrc = std::clamp(static_cast<int>(c.v), 1, NT - 1); subHeld.clear(); subUpdate(); break; } break;
            case K_NOTE: {
                const int n = std::clamp<int>(c.b, 0, 127), v = std::clamp<int>(c.c, 0, 127);
                if (c.a >= 10 && c.a < 10 + LM_COUNT) liveNote(c.a - 10, n, v);
                else if (c.a == 20) { if (v > 0) subHeld.add(n); else subHeld.remove(n); subUpdate(); } else if (c.a >= 0 && c.a < NT) { if (v > 0) noteOn(c.a, n, v); else noteOff(c.a, n); }
                break; }
            case K_ALLOFF: allNotesOff(); break;
            case K_LIVE: if (c.a >= 0 && c.a < LM_COUNT) liveSet(c.a, c.b, c.c, c.v); break;
            case K_LCH: if (c.a >= 0 && c.a < LM_COUNT) lch[c.a] = static_cast<int>(c.v); break;
            case K_REC: if (c.v >= 0.5f) { recSrc = c.b; recLen = 0; recState = 1; } else if (recState == 1) recState = 2; break;
        }
    }
    void handleMidi(const Midi& m) {
        // Reloj: tras Start, el primer pulso es la posición 0
        if (m.s == 0xF8) { ++ticks; sinceTick = 0; if (running && mp[MP_CLOCK] >= 0.5f) pos = ticks / 6.0; return; }
        if (m.s == 0xFA) { ticks = -1; pos = 0; running = true; return; }
        if (m.s == 0xFB) { running = true; return; }
        if (m.s == 0xFC) {   // Stop: se sueltan las notas (no se cortan de golpe), por si el secuenciador no manda sus note off
            running = false; subHeld.clear(); subUpdate();
            for (int t = 1; t < NT; ++t) { while (!held[t].empty()) noteOff(t, held[t].v[0]); }
            return;
        }
        if (m.s == 0xF2) { const long spp = static_cast<long>(m.d1) | static_cast<long>(m.d2) << 7; ticks = spp * 6 - 1; pos = spp; return; }   // Song Position Pointer
        if (m.s >= 0xF0) return;
        const int type = m.s & 0xF0, chn = (m.s & 0x0F) + 1;
        if (type != 0x90 && type != 0x80 && type != 0xB0) return;   // el resto (CC, Program Change) lo atiende el hilo de control
        for (int lm = 0; lm < LM_COUNT; ++lm) {
            if (lch[lm] != chn) continue;
            if (type == 0x90 || type == 0x80) liveNote(lm, m.d1, type == 0x90 ? m.d2 : 0);
            else if ((m.d1 == 120 || m.d1 == 123) && lm == LM_SAMP) samp.allOff();
        }
        // Sub: módulo aparte con su canal (el Oxi le manda la nota grave del secuenciador de acordes)
        if (subMode == 0 && subCh == chn) {
            if (type == 0x90 && m.d2 > 0) { subHeld.add(m.d1); subUpdate(); }
            else if (type == 0x80 || type == 0x90) { subHeld.remove(m.d1); subUpdate(); }
            else if (m.d1 == 120 || m.d1 == 123) { subHeld.clear(); subUpdate(); }
        }
        for (int t = 0; t < NT; ++t) {
            if (ch[t] != chn) continue;
            if (type == 0x90 && m.d2 > 0) noteOn(t, m.d1, m.d2);
            else if (type == 0x80 || type == 0x90) noteOff(t, m.d1);
            else if (m.d1 == 120 || m.d1 == 123) { if (t > 0) { mel[t].allOff(); held[t].clear(); } }
        }
    }
    void allNotesOff() { for (int t = 1; t < NT; ++t) { mel[t].allOff(); held[t].clear(); } subHeld.clear(); sub.on = false; samp.allOff(); for (int k = 0; k < 8; ++k) brk.setHold(k, false); gran.allOff(); voz.allOff(); }
    static void panGains(float pan, float& gl, float& gr) { const float a = (pan + 1.0f) * 0.25f * kPi; gl = std::cos(a) * 1.4142f; gr = std::sin(a) * 1.4142f; }

    // out: intercalado con «oc» canales; in: intercalado con «ic» canales (puede ser nulo)
    void process(float* out, int oc, const float* in, int ic, int n) {
        static thread_local bool ftz = false;
        if (!ftz) { flushDenormals(); if (par.helpers() > 0) pinToCore(0); ftz = true; }   // los ayudantes van en los núcleos 1, 2 y 3
        const auto t0 = std::chrono::steady_clock::now();
        const double now = monoNow(), blockStart = now - n / static_cast<double>(sr);
        calls.fetch_add(1, std::memory_order_relaxed);
        Cmd c; while (cmds.pop(c)) apply(c);
        // MIDI con su momento exacto: cada mensaje se aplica en la muestra que le corresponde dentro del bloque
        // (va un bloque por detrás, pero sin el «temblor» de agruparlo todo al principio)
        Midi evs[512]; int ne = 0; Midi m;
        while (ne < 512 && midi.pop(m)) evs[ne++] = m;
        // con una ráfaga de eventos, rejilla más ancha: trozos diminutos no se pueden repartir entre núcleos
        const int grid = ne > 8 ? ~31 : ~7;
        int done = 0;
        for (int e = 0; e <= ne; ++e) {
            int at = n;
            if (e < ne) { at = static_cast<int>((evs[e].t - blockStart) * sr); at = std::clamp(at & grid, done, n); }
            while (done < at) { const int k = std::min(BLK, at - done); chunk(out + static_cast<size_t>(done) * oc, oc, in ? in + static_cast<size_t>(done) * ic : nullptr, ic, k); done += k; }
            if (e < ne) handleMidi(evs[e]);
        }
        bpmPub.store(bpm, std::memory_order_relaxed); clkPub.store(clkOn(), std::memory_order_relaxed); runPub.store(running, std::memory_order_relaxed);
        // consumo: tiempo de cálculo frente al tiempo que dura el bloque
        const float load = static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * sr / n * 100.0);
        cpu = cpu.load() + (load - cpu.load()) * 0.05f;
        if (load > cpuPeak.load()) cpuPeak = load;
        if (load > 100.0f) late.fetch_add(1, std::memory_order_relaxed);   // el bloque tardó más de lo que dura: se oye como un corte
    }
    void chunk(float* out, int oc, const float* in, int ic, int k) {
        // --- tempo: reloj del Oxi si llega; si no, el de la pantalla
        sinceTick += k;
        const bool clk = clkOn();
        bpm = clk ? clkBpm.load() : mp[MP_BPM];
        const double spb = sr * 60.0 / bpm / 4.0;
        if (!clk || !running) pos += k / spb;
        else pos = ticks / 6.0 + std::min(1.0, sinceTick / spb);   // entre pulsos, interpola
        // --- fuentes
        for (auto& s : st) { std::fill(s.L, s.L + k, 0.0f); std::fill(s.R, s.R + k, 0.0f); }
        {   // posición para los breaks
            const double dpos = pos - lastPos, sp1 = 1.0 / spb;
            const bool cont = dpos >= 0 && dpos < 3.0 * k * sp1;
            jobP0 = cont ? lastPos : pos; jobDp = cont ? dpos / k : sp1; lastPos = pos;
        }
        jobK = k; jobPlay = brkPlaying();
        // batería, las 3 pistas, sampler, breaks y granular no comparten nada: pueden ir cada una en su núcleo
        if (k >= kParMin) par.enter();   // los ayudantes solo están despiertos en la parte repartida: el resto del bloque, esos núcleos quedan libres
        if (k >= kParMin) par.run(srcTask, this, kNSrc); else for (int i = 0; i < kNSrc; ++i) renderSource(i);
        for (int l = 0; l < 8; ++l) {
            const DrumLane& L = drums.lane[static_cast<size_t>(l)]; float gl, gr; panGains(L.pan, gl, gr);
            Strip& s = st[std::clamp(L.out, 0, 2)];
            for (int i = 0; i < k; ++i) { s.L[i] += lanes[l][i] * gl; s.R[i] += lanes[l][i] * gr; }
        }
        sub.render(mono, k, subTone, subGlide, subVol); std::copy(mono, mono + k, st[CH_SUB].L); std::copy(mono, mono + k, st[CH_SUB].R);
        for (int q = 0; q < 4; ++q) { const int c = inMap[q].load(std::memory_order_relaxed); if (in && c >= 0 && c < ic) for (int i = 0; i < k; ++i) inRaw[q][i] = in[i * ic + c]; else std::fill(inRaw[q], inRaw[q] + k, 0.0f); }
        for (int q = 0; q < std::min(ic, 4); ++q) { Strip& s = st[CH_IN + q]; std::copy(inRaw[q], inRaw[q] + k, s.L); std::copy(inRaw[q], inRaw[q] + k, s.R); }
        {   // la voz va después: lee las pistas
            const float* ins[4] = {inRaw[0], inRaw[1], inRaw[2], inRaw[3]};
            const float* trk[3] = {st[CH_T0].L, st[CH_T0 + 1].L, st[CH_T0 + 2].L};
            voz.render(ins, std::min(ic, 4), trk, st[CH_VOZ].L, st[CH_VOZ].R, k, bpm);
        }
        // --- solo: qué suena
        bool anySolo = false, chSolo[NCH], grSolo[NGRP];
        for (int i = 0; i < NCH; ++i) { chSolo[i] = st[i].p[SP_SOLO] >= 0.5f; anySolo |= chSolo[i]; }
        for (int g = 0; g < NGRP; ++g) { grSolo[g] = grp[g].p[SP_SOLO] >= 0.5f; anySolo |= grSolo[g]; }
        auto gOf = [&](int i) { const int o = static_cast<int>(st[i].p[SP_OUT]); return o == 1 ? 0 : o == 2 ? 1 : -1; };
        // --- etapa A de todos (los sidechains leen de aquí)
        for (auto& s : st) s.stageA(k, false);
        for (int b = 0; b < NBUS; ++b) { std::fill(busL[b], busL[b] + k, 0.0f); std::fill(busR[b], busR[b] + k, 0.0f); }
        for (int f = 0; f < NSEND; ++f) { std::fill(sendL[f], sendL[f] + k, 0.0f); std::fill(sendR[f], sendR[f] + k, 0.0f); }
        std::fill(cueL, cueL + k, 0.0f); std::fill(cueR, cueR + k, 0.0f);
        std::fill(mixL, mixL + k, 0.0f); std::fill(mixR, mixR + k, 0.0f);
        float pk[NCH + NGRP + 2] = {};
        const float rk = 1.0f / k;
        auto stageB = [&](Strip& s, int idx, bool audible, int bus) {
            float gl, gr; panGains(s.p[SP_PAN], gl, gr);
            const float lv = audible ? s.p[SP_LVL] : 0.0f;
            if (s.silent) {   // en silencio: nada que hacer, pero las rampas quedan al día
                pk[idx] = 0; s.lim.gr = 1.0f; s.glP = gl; s.grP = gr; s.lvP = lv; for (int f = 0; f < NSEND; ++f) s.sP[f] = s.p[SP_S0 + f]; return;
            }
            // pan: rampa a lo largo del bloque
            { const float dl = (gl - s.glP) * rk, dr = (gr - s.grP) * rk; float a = s.glP, b = s.grP;
              for (int i = 0; i < k; ++i) { a += dl; b += dr; s.L[i] *= a; s.R[i] *= b; } s.glP = gl; s.grP = gr; }
            const int src = static_cast<int>(s.p[SP_SC]);
            if (src >= 0 && src < NCH && &st[src] != &s && s.p[SP_SCAMT] > 0.001f) {
                s.fol.process(st[src].pre[0], st[src].pre[1], s.g, k, s.p[SP_SCAMT], s.p[SP_SCREL], sr);
                for (int i = 0; i < k; ++i) { s.L[i] *= s.g[i]; s.R[i] *= s.g[i]; }
            }
            s.lim.process(s.L, s.R, k);
            if (s.p[SP_CUE] >= 0.5f) for (int i = 0; i < k; ++i) { cueL[i] += s.L[i]; cueR[i] += s.R[i]; }
            float p = 0;
            if (idx < NCH && idx != CH_GRAN) { const float fl = s.p[SP_LVL]; for (int i = 0; i < k; ++i) { mixL[i] += s.L[i] * fl; mixR[i] += s.R[i] * fl; } }   // fuente «Mezcla» del granular (sin solos ni mutes)
            // nivel (fader, mute y solo): rampa a lo largo del bloque
            { const float d = (lv - s.lvP) * rk; float a = s.lvP;
              for (int i = 0; i < k; ++i) { a += d; s.L[i] *= a; s.R[i] *= a; p = std::max(p, std::max(std::fabs(s.L[i]), std::fabs(s.R[i]))); busL[bus][i] += s.L[i]; busR[bus][i] += s.R[i]; }
              s.lvP = lv; }
            pk[idx] = p;
            for (int f = 0; f < NSEND; ++f) {
                const float a1 = s.p[SP_S0 + f], a0 = s.sP[f]; s.sP[f] = a1;
                if (a1 < 0.0005f && a0 < 0.0005f) continue;
                const float d = (a1 - a0) * rk; float a = a0;
                for (int i = 0; i < k; ++i) { a += d; sendL[f][i] += s.L[i] * a; sendR[f][i] += s.R[i] * a; }
            }
        };
        for (int i = 0; i < NCH; ++i) {
            const int g = gOf(i);
            const bool muted = st[i].p[SP_MUTE] >= 0.5f, audible = !muted && (!anySolo || chSolo[i] || (g >= 0 && grSolo[g]));
            const int o = static_cast<int>(st[i].p[SP_OUT]);
            stageB(st[i], i, audible, o >= 3 ? std::min(NBUS - 1, o) : g < 0 ? 0 : 1 + g);
        }
        for (int g = 0; g < NGRP; ++g) {
            Strip& s = grp[g];
            std::copy(busL[1 + g], busL[1 + g] + k, s.L); std::copy(busR[1 + g], busR[1 + g] + k, s.R);
            s.stageA(k, true);
            bool chInSolo = false; for (int i = 0; i < NCH; ++i) chInSolo |= chSolo[i] && gOf(i) == g;
            const bool audible = s.p[SP_MUTE] < 0.5f && (!anySolo || grSolo[g] || chInSolo);
            const int o = static_cast<int>(s.p[SP_OUT]);
            stageB(s, NCH + g, audible, o >= 3 ? std::min(NBUS - 1, o) : 0);
        }
        // --- efectos de envío: cada uno en su núcleo. Siguen despiertos mientras su cola se oiga (máximo 40 s sin
        // señal: un dub autooscilando no se queda sonando para siempre) y al dormirse se vacían, para que nada resucite
        for (int f = 0; f < NSEND; ++f) {
            float e = 0; for (int i = 0; i < k; ++i) e = std::max(e, std::fabs(sendL[f][i]) + std::fabs(sendR[f][i]));
            const bool wasOn = fxTail[f] > 0;
            if (e > 1e-5f) { fxTail[f] = 4.5f; fxIdle[f] = 0; } else { fxTail[f] -= k / sr; fxIdle[f] += k / sr; }
            fxRun[f] = false;
            if (fx[f].clearing()) { fx[f].clearStep(); continue; }
            if (wasOn && fxTail[f] <= 0) { fx[f].startClear(); continue; }
            fxRun[f] = fxTail[f] > 0 && fx[f].ret >= 0.001f;
        }
        if (k >= kParMin) { par.run(sendTask, this, NSEND); par.leave(); } else for (int f = 0; f < NSEND; ++f) renderSend(f);
        for (int f = 0; f < NSEND; ++f) {
            if (!fxRun[f]) continue;
            if (fxBad[f].exchange(false, std::memory_order_relaxed)) fx[f].startClear();
            const float* a = fxOutL[f]; const float* b = fxOutR[f]; float o = 0;
            for (int i = 0; i < k; ++i) { o = std::max(o, std::fabs(a[i]) + std::fabs(b[i])); busL[0][i] += a[i]; busR[0][i] += b[i]; }
            if (o > 2e-5f && fxIdle[f] < 40.0f) fxTail[f] = std::max(fxTail[f], 4.5f);
        }
        // --- máster
        float* L = busL[0]; float* R = busR[0];
        mdist.process(L, R, k);
        // filtro con envolvente: los golpes lo abren hacia el centro
        { const float base = mp[MP_FILT], ef = mp[MP_FENV]; mdj.f = base;
          if (ef > 0.001f && std::fabs(base) >= 0.04f) { float e = 0; for (int i = 0; i < k; ++i) e = std::max(e, std::fabs(L[i]) + std::fabs(R[i]));
              fEnvS += (0.5f * e - fEnvS) * (0.5f * e > fEnvS ? 0.6f : 0.03f); const float m = std::min(fEnvS * ef, std::fabs(base) - 0.03f); mdj.f = base + (base < 0 ? m : -m); } }
        mdj.process(L, R, k);
        mfx.process(L, R, k, bpm, pos);
        // avería: un valor imposible o desmesurado (más de +36 dB) deja el bloque en silencio y reinicia los efectos, nunca ruido
        { float acc = 0, top = 0; for (int i = 0; i < k; ++i) { acc += L[i] + R[i]; top = std::max(top, std::max(std::fabs(L[i]), std::fabs(R[i]))); }
          if (!std::isfinite(acc) || !(top < 64.0f)) { std::fill(L, L + k, 0.0f); std::fill(R, R + k, 0.0f); mfx.reset(); mdist.reset(); mdj.reset(); bcomp.reset(); } }
        bcomp.process(L, R, k); bcGrDb.store(bcomp.lastGrDb, std::memory_order_relaxed);   // compresor de bus: une la mezcla antes del volumen y el limitador
        { const float vol = mp[MP_VOL], d = (vol - volP) * rk; float a = volP;
          for (int i = 0; i < k; ++i) { a += d; L[i] *= a; R[i] *= a; } volP = vol; }
        mlim.process(L, R, k);
        { float acc = 0; for (int i = 0; i < k; ++i) acc += L[i] + R[i];   // última red: nada imposible llega a la salida, y el máster no se queda mudo
          if (!std::isfinite(acc)) { std::fill(L, L + k, 0.0f); std::fill(R, R + k, 0.0f); bcomp.reset(); mlim.reset(); } }
        float mpk = 0;
        // saturación final: «Color» = curva suave de siempre (algo de densidad); «Clean» = lineal hasta −1 dBFS y red de seguridad encima
        const bool soft = mp[MP_CLIP] >= 0.5f; auto oc1 = [soft](float x) { return soft ? softClip(x) : safeClip(x); };
        for (int i = 0; i < k; ++i) { L[i] = oc1(L[i]); R[i] = oc1(R[i]); mpk = std::max(mpk, std::max(std::fabs(L[i]), std::fabs(R[i]))); }
        pk[NCH + NGRP] = mpk;
        // --- pre-escucha: salidas 3-4 si las hay; si no, según el modo
        bool anyCue = false; for (auto& s : st) anyCue |= s.p[SP_CUE] >= 0.5f; for (auto& g : grp) anyCue |= g.p[SP_CUE] >= 0.5f;
        const float cl = mp[MP_CUELVL]; const int mode = static_cast<int>(mp[MP_CUEMODE]);
        float cpk = 0;
        // salidas directas (para un mezclador analógico): la pre-escucha solo usa 3-4 si nadie las ocupa
        bool pairUsed[NDIR + 1] = {};
        for (auto& s : st) { const int o = static_cast<int>(s.p[SP_OUT]); if (o >= 3) pairUsed[std::min(NDIR, o - 2)] = true; }
        for (auto& g : grp) { const int o = static_cast<int>(g.p[SP_OUT]); if (o >= 3) pairUsed[std::min(NDIR, o - 2)] = true; }
        // salida principal y cue: los pares que se eligen en Ajustes; una salida directa ocupada gana al cue
        const int np = std::max(1, oc / 2), mpair = std::clamp(mainPair.load(std::memory_order_relaxed), 0, np - 1), cq = cuePair.load(std::memory_order_relaxed);
        int cpair = cq == -2 ? (mpair == 0 ? 1 : -1) : cq;
        if (cpair >= np || cpair == mpair || (cpair >= 1 && cpair <= NDIR && pairUsed[cpair])) cpair = -1;
        for (int i = 0; i < k; ++i) {
            const float a = oc1(cueL[i] * cl), b = oc1(cueR[i] * cl); cpk = std::max(cpk, std::max(std::fabs(a), std::fabs(b)));
            float* o = out + static_cast<size_t>(i) * oc;
            for (int q = 0; q < oc; ++q) o[q] = 0;
            if (oc < 2) { o[0] = L[i]; continue; }
            float* m = o + 2 * mpair;
            if (cpair >= 0) { m[0] = L[i]; m[1] = R[i]; o[2 * cpair] = a; o[2 * cpair + 1] = b; }
            else if (anyCue && mode == 0) { m[0] = 0.5f * (L[i] + R[i]); m[1] = 0.5f * (a + b); }
            else if (anyCue && mode == 2) { m[0] = a; m[1] = b; }
            else { m[0] = L[i]; m[1] = R[i]; }
            for (int pr = 1; pr <= NDIR && 2 * pr + 1 < oc; ++pr) if (pairUsed[pr] && pr != mpair && pr != cpair) { o[2 * pr] = oc1(busL[2 + pr][i]); o[2 * pr + 1] = oc1(busR[2 + pr][i]); }
        }
        pk[NCH + NGRP + 1] = cpk;
        // granular: graba su fuente (sin el propio granular)
        if (gran.on && !gran.freeze) {
            const int src = std::clamp(static_cast<int>(gran.p[GR_SRC]), 0, 11);
            auto addPre = [&](int c) { for (int i = 0; i < k; ++i) { srcL[i] += st[c].pre[0][i]; srcR[i] += st[c].pre[1][i]; } };
            std::fill(srcL, srcL + k, 0.0f); std::fill(srcR, srcR + k, 0.0f);
            if (src == 0) { std::copy(mixL, mixL + k, srcL); std::copy(mixR, mixR + k, srcR); }
            else if (src <= 4) { std::copy(inRaw[src - 1], inRaw[src - 1] + k, srcL); std::copy(inRaw[src - 1], inRaw[src - 1] + k, srcR); }
            else if (src == 5) { addPre(0); addPre(1); addPre(2); }
            else if (src <= 8) addPre(CH_T0 + src - 6);
            else addPre(src == 9 ? CH_SAMP : src == 10 ? CH_BRK : CH_VOZ);
            gran.record(srcL, srcR, k);
        }
        // grabación al sampler: Entrada 1 | Entrada 2 | Entradas 1+2 | Mezcla | Máster
        if (recState == 1) {
            long w = recLen; const long cap = static_cast<long>(recL.size());
            for (int i = 0; i < k && w < cap; ++i, ++w) {
                float a, b;
                switch (recSrc) { case 0: a = b = inRaw[0][i]; break; case 1: a = b = inRaw[1][i]; break; case 2: a = inRaw[0][i]; b = inRaw[1][i]; break;
                                  case 3: a = mixL[i]; b = mixR[i]; break; default: a = L[i]; b = R[i]; }
                recL[static_cast<size_t>(w)] = a; recR[static_cast<size_t>(w)] = b;
            }
            recLen = w; if (w >= cap) recState = 2;
        }
        for (int p = 0; p < NCH + NGRP + 2; ++p) if (pk[p] > peak[p].load()) peak[p] = pk[p];
        grDb = mlim.lastGrDb;
    }
};

// ------------------------------------------------------------------ descripción del mezclador para la pantalla
struct FxP { const char* l; float min, max, step, def; char fmt; const char* names; };
struct FxSpec { const char* name; int np; FxP p[5]; };
const char* kDivNames = "1/32|1/16|1/8T|1/16D|1/8|1/4T|1/8D|1/4|1/4D|1/2";
const FxSpec kFx[FX_COUNT] = {
    {"Hall reverb", 5, {{"Size", 0, 1, 0.01f, 0.55f, 'p', nullptr}, {"Damping", 0, 1, 0.01f, 0.4f, 'p', nullptr}, {"Pre-delay", 0, 0.25f, 0.005f, 0.02f, 's', nullptr}, {"Mod", 0, 1, 0.01f, 0.3f, 'p', nullptr}, {"Low cut", 0, 1, 0.01f, 0.15f, 'p', nullptr}}},
    {"Plate reverb", 5, {{"Size", 0, 1, 0.01f, 0.4f, 'p', nullptr}, {"Damping", 0, 1, 0.01f, 0.15f, 'p', nullptr}, {"Pre-delay", 0, 0.25f, 0.005f, 0.01f, 's', nullptr}, {"Mod", 0, 1, 0.01f, 0.25f, 'p', nullptr}, {"Low cut", 0, 1, 0.01f, 0.2f, 'p', nullptr}}},
    {"Black hole", 5, {{"Size", 0, 1, 0.01f, 0.7f, 'p', nullptr}, {"Darkness", 0, 1, 0.01f, 0.6f, 'p', nullptr}, {"Input", 0, 1, 0.01f, 0.5f, 'p', nullptr}, {"Mod", 0, 1, 0.01f, 0.5f, 'p', nullptr}, {"Low cut", 0, 1, 0.01f, 0.2f, 'p', nullptr}}},
    {"Delay ping-pong", 5, {{"Time", 0, 9, 1, 6, 'n', kDivNames}, {"Feedback", 0, 0.95f, 0.01f, 0.45f, 'p', nullptr}, {"Tone", 0, 1, 0.01f, 0.6f, 'p', nullptr}, {"Low cut", 0, 1, 0.01f, 0.25f, 'p', nullptr}, {"Drive", 0, 1, 0.01f, 0.2f, 'p', nullptr}}},
    {"Tape delay", 5, {{"Time", 0, 9, 1, 6, 'n', kDivNames}, {"Feedback", 0, 1, 0.01f, 0.5f, 'p', nullptr}, {"Tone", 0, 1, 0.01f, 0.4f, 'p', nullptr}, {"Wow", 0, 1, 0.01f, 0.3f, 'p', nullptr}, {"Saturation", 0, 1, 0.01f, 0.3f, 'p', nullptr}}},
    {"Delay dub", 4, {{"Time", 0, 9, 1, 7, 'n', kDivNames}, {"Feedback", 0, 1.15f, 0.01f, 0.75f, 'p', nullptr}, {"Band", 0, 1, 0.01f, 0.5f, 'p', nullptr}, {"Saturation", 0, 1, 0.01f, 0.5f, 'p', nullptr}}},
    {"Multitap", 4, {{"Time", 0, 9, 1, 4, 'n', kDivNames}, {"Pattern", 0, 3, 1, 0, 'n', "Straight|Swing|Gallop|Echoes"}, {"Feedback", 0, 0.9f, 0.01f, 0.25f, 'p', nullptr}, {"Tone", 0, 1, 0.01f, 0.6f, 'p', nullptr}}},
    {"Chorus / flanger", 3, {{"Rate", 0, 1, 0.01f, 0.3f, 'p', nullptr}, {"Depth", 0, 1, 0.01f, 0.5f, 'p', nullptr}, {"Feedback", 0, 0.9f, 0.01f, 0, 'p', nullptr}}},
};
std::string mixSpecJson() {
    std::string o = "\"channels\":[";
    for (int i = 0; i < NCH; ++i) { if (i) o += ","; o += jstr(kChNames[i]); }
    o += "],\"fx\":[";
    for (int f = 0; f < FX_COUNT; ++f) {
        if (f) o += ",";
        o += "{\"name\":" + jstr(kFx[f].name) + ",\"params\":[";
        for (int i = 0; i < kFx[f].np; ++i) { const FxP& p = kFx[f].p[i]; if (i) o += ","; o += "{\"l\":" + jstr(p.l) + ",\"min\":" + jnum(p.min) + ",\"max\":" + jnum(p.max) + ",\"step\":" + jnum(p.step) + ",\"def\":" + jnum(p.def) + ",\"fmt\":\"" + std::string(1, p.fmt) + "\"" + (p.names ? ",\"names\":" + jstr(p.names) : "") + "}"; }
        o += "]}";
    }
    return o + "],\"divs\":" + jstr(kDivNames);
}

std::string pspecJson(const PSpec* ps, int n) {
    std::string o = "[";
    for (int i = 0; i < n; ++i) { const PSpec& p = ps[i]; if (i) o += ",";
        o += "{\"l\":" + jstr(p.label) + ",\"min\":" + jnum(p.min) + ",\"max\":" + jnum(p.max) + ",\"step\":" + jnum(p.step) + ",\"def\":" + jnum(p.def) +
             ",\"fmt\":\"" + std::string(1, p.fmt) + "\",\"log\":" + (p.log ? "true" : "false") + (p.names ? ",\"names\":" + jstr(p.names) : "") + ",\"g\":" + jstr(p.grp) + "}"; }
    return o + "]";
}
std::string liveSpecJson() {
    std::string h = "["; for (int i = 0; i < 8; ++i) { if (i) h += ","; h += jstr(kBrkHolds[i]); } h += "]";
    return "\"live\":{\"samp\":" + pspecJson(kSampSpec, SA_COUNT) + ",\"brk\":" + pspecJson(kBrkSpec, BK_COUNT) + ",\"brkHolds\":" + h +
           ",\"gran\":" + pspecJson(kGrSpec, GR_COUNT) + ",\"voz\":" + pspecJson(kVzSpec, VZ_COUNT) + ",\"pads\":" + std::to_string(kPads) + "}";
}

// ------------------------------------------------------------------ sonidos de serie del sampler y de los breaks (se generan al arrancar)
const char* kLiveKeys[LM_COUNT] = {"samp", "brk", "gran", "voz"};
Sample* newSample(float sr, float sec, const char* name) { auto* s = new Sample; s->sr = sr; s->name = name; s->L.assign(static_cast<size_t>(sr * sec), 0.0f); s->R = s->L; return s; }
void normalize(Sample& s, float to = 0.9f) { float pk = 1e-6f; for (size_t i = 0; i < s.size(); ++i) pk = std::max({pk, std::fabs(s.L[i]), std::fabs(s.R[i])}); const float g = to / pk; for (size_t i = 0; i < s.size(); ++i) { s.L[i] *= g; s.R[i] *= g; } }
void trimTail(Sample& s) { size_t n = s.size(); while (n > 64 && std::fabs(s.L[n - 1]) < 1e-4f && std::fabs(s.R[n - 1]) < 1e-4f) --n; s.L.resize(n); s.R.resize(n); normalize(s); }
// sinte: motor, preset de fábrica (o -1), notas, cuánto tiempo se mantienen
Sample* synthSample(float sr, const char* name, int eng, int preset, std::vector<int> notes, int vel, float hold, float sec) {
    auto m = std::make_unique<MeloEngines>(); m->prepare(sr); m->select(eng);
    for (int i = 0; i < kEng[eng].np; ++i) m->set(eng, i, kEng[eng].p[i].def);
    for (int i = 0; i < kEng[eng].nb; ++i) m->set(eng, kBtn0 + i, kEng[eng].b[i].def ? 1.0f : 0.0f);
    if (preset >= 0) for (auto& [i, v] : factoryPresets(eng)[static_cast<size_t>(preset)].v) m->set(eng, i, v);
    Sample* s = newSample(sr, sec, name); float L[256], R[256], mono[256];
    for (int n : notes) m->noteOn(n, vel);
    const size_t N = s->size(); bool off = false;
    for (size_t o = 0; o < N; o += 256) {
        if (!off && o >= hold * sr) { for (int n : notes) m->noteOff(n); off = true; }
        const int k = static_cast<int>(std::min<size_t>(256, N - o)); m->render(L, R, mono, k);
        for (int i = 0; i < k; ++i) { s->L[o + static_cast<size_t>(i)] = L[i]; s->R[o + static_cast<size_t>(i)] = R[i]; }
    }
    trimTail(*s); return s;
}
Sample* drumSample(float sr, const char* name, int kit, int lane, float sec, float tune = 0, float decay = 0.5f) {
    DrumMachine d; d.prepare(sr); d.kit = kit; d.lane[static_cast<size_t>(lane)].tune = tune; d.lane[static_cast<size_t>(lane)].decay = decay; d.hit(lane, 120);
    Sample* s = newSample(sr, sec, name); float b[8][256]; float* o[8]; for (int i = 0; i < 8; ++i) o[i] = b[i];
    for (size_t p = 0; p < s->size(); p += 256) { const int k = static_cast<int>(std::min<size_t>(256, s->size() - p)); d.render(o, k); for (int i = 0; i < k; ++i) s->L[p + static_cast<size_t>(i)] = s->R[p + static_cast<size_t>(i)] = b[lane][i]; }
    trimTail(*s); return s;
}
std::vector<Sample*> factorySamples(float sr) {
    std::vector<Sample*> v;
    v.push_back(drumSample(sr, "Long kick", KIT_808, 0, 2.0f, -2, 0.9f));
    v.push_back(drumSample(sr, "Dry snare", KIT_DRM, 1, 0.8f));
    v.push_back(drumSample(sr, "Clap", KIT_808, 2, 0.8f, 0, 0.7f));
    v.push_back(drumSample(sr, "Hihat chip", KIT_CHIP, 5, 0.4f));
    v.push_back(synthSample(sr, "Minor stab", E_JUNO, 4, {57, 60, 64}, 110, 0.15f, 1.5f));
    v.push_back(synthSample(sr, "Pad Juno", E_JUNO, 0, {57, 60, 64, 67}, 100, 2.5f, 5.0f));
    v.push_back(synthSample(sr, "FM bell", E_DX, 2, {72}, 110, 0.3f, 4.0f));
    v.push_back(synthSample(sr, "E.piano chord", E_DX, 0, {57, 60, 64, 69}, 100, 1.2f, 3.0f));
    v.push_back(synthSample(sr, "Moog bass", E_MOOG, 0, {33}, 115, 0.6f, 1.5f));
    v.push_back(synthSample(sr, "Lead Moog", E_MOOG, 1, {69}, 110, 1.0f, 2.0f));
    v.push_back(synthSample(sr, "Acid", E_ACID, 1, {45}, 120, 0.25f, 1.0f));
    v.push_back(synthSample(sr, "GB arp", E_GB, 1, {69}, 110, 1.2f, 1.5f));
    {   // subida de ruido filtrado
        Sample* s = newSample(sr, 4.0f, "Riser"); BQ f; Noise nz; const size_t N = s->size();
        for (size_t i = 0; i < N; ++i) { const float t = static_cast<float>(i) / N; if (i % 32 == 0) f.set(2, 200.0f * std::pow(60.0f, t), 2.5f, sr); const float x = f.process(nz.next()) * t * t * 1.4f * std::min(1.0f, (N - i) / 400.0f); s->L[i] = x; s->R[i] = f.process(nz.next()) * t * t * 1.4f * std::min(1.0f, (N - i) / 400.0f); }
        v.push_back(s);
    }
    {   // impacto: seno grave con caída + ruido
        Sample* s = newSample(sr, 3.0f, "Impact"); Noise nz; float ph = 0; BQ lp; lp.set(0, 1200, 0.7f, sr);
        for (size_t i = 0; i < s->size(); ++i) { const float t = i / sr; ph += (38.0f + 180.0f * std::exp(-t / 0.06f)) / sr; const float x = fsin(ph) * std::exp(-t / 0.9f) + lp.process(nz.next()) * 0.6f * std::exp(-t / 0.35f); s->L[i] = s->R[i] = std::tanh(x * 1.5f) * 0.8f; }
        v.push_back(s);
    }
    v.push_back(drumSample(sr, "Odd perc", KIT_EXP, 7, 0.6f));
    v.push_back(drumSample(sr, "Bleep GB", KIT_GB, 7, 0.5f, 5));
    return v;
}
struct FacBreak { const char* name; float bpm; int bars, kit; std::vector<int> k, s, g, h, o; };
std::vector<Sample*> factoryBreaks(float sr) {
    const FacBreak B[3] = {
        {"Jungle 170", 170, 2, KIT_DRM, {0, 10, 16, 26, 27}, {4, 12, 20, 28, 30}, {7, 9, 15, 23, 25, 31}, {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28}, {14}},
        {"Funk 112", 112, 2, KIT_808, {0, 3, 10, 16, 19, 22, 26}, {4, 12, 20, 28}, {7, 14, 23, 30}, {}, {10, 26}},
        {"Boom bap 92", 92, 2, KIT_808, {0, 7, 10, 16, 23, 26}, {4, 12, 20, 28}, {15, 31}, {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30}, {}}};
    std::vector<Sample*> v;
    for (const auto& b : B) {
        const double step = sr * 60.0 / b.bpm / 4; const int steps = b.bars * 16;
        Sample* s = newSample(sr, static_cast<float>(steps * step / sr), b.name);
        DrumMachine d; d.prepare(sr); d.kit = b.kit; d.lane[1].decay = 0.45f; d.lane[5].decay = 0.35f;
        std::vector<int> hh = b.h; if (hh.empty()) for (int i = 0; i < 32; ++i) hh.push_back(i);
        float bl[8][256]; float* o[8]; for (int i = 0; i < 8; ++i) o[i] = bl[i];
        size_t pos = 0;
        for (int st = 0; st < steps; ++st) {
            const size_t next = static_cast<size_t>((st + 1) * step), swing = st % 2 ? static_cast<size_t>(step * 0.12) : 0;
            auto has = [&](const std::vector<int>& L) { return std::find(L.begin(), L.end(), st) != L.end(); };
            size_t at = static_cast<size_t>(st * step) + swing;
            while (pos < next) {
                const int k = static_cast<int>(std::min<size_t>(256, std::min(next, at > pos ? at : next) - pos));
                if (pos == at) { at = SIZE_MAX;
                    if (has(b.k)) d.hit(0, 122);
                    if (has(b.s)) d.hit(1, 110);
                    if (has(b.g)) d.hit(1, 45);
                    if (has(hh)) d.hit(5, st % 4 == 0 ? 85 : 60);
                    if (has(b.o)) d.hit(6, 80);
                    continue; }
                d.render(o, k);
                for (int i = 0; i < k; ++i) { float x = 0; for (int l = 0; l < 8; ++l) x += bl[l][i]; s->L[pos + static_cast<size_t>(i)] = s->R[pos + static_cast<size_t>(i)] = x * 0.8f; }
                pos += static_cast<size_t>(k);
            }
        }
        normalize(*s); v.push_back(s);
    }
    return v;
}

// último mensaje MIDI recibido (para el monitor de la pantalla)
std::atomic<bool> gReopen{false};
std::atomic<int> gLpdSamp{-1}, gLpdGran{-1};   // canales del sampler y del granular para el LPD8 (los escribe el hilo de control)   // SIGUSR1: reabrir el audio (ver onReopenSignal)
std::atomic<uint32_t> gMidiLast{0}; std::atomic<int> gMidiPort{-1}; std::atomic<unsigned> gMidiCount{0};
std::vector<std::string> gMidiNames; std::mutex gMidiNamesMx;
std::atomic<int> gMixCh{15};   // canal del mezclador (0-15): sus notas son mutes, solos... y no van al audio

std::string lower(std::string s);
// ------------------------------------------------------------------ estado y control (hilo de control)
struct Control {
    Audio& au; web::Server srv; std::map<std::string, float> st; std::string file = "freon-estado.json";
    std::map<std::string, std::string> ss;       // claves de texto: archivos de los pads y del break
    std::string base = ".";                      // carpeta del proyecto (samples/ y breaks/ cuelgan de aquí)
    std::vector<Sample*> facS, facB; std::map<std::string, Sample*> cache;
    std::string sampDir() const { return base + "/samples"; }
    std::string brkDir() const { return base + "/breaks"; }
    void initLive() {
        facS = factorySamples(au.sr); facB = factoryBreaks(au.sr);
    }
    Sample* fileSample(const std::string& dir, const std::string& name) {
        const std::string key = dir + "/" + name; auto it = cache.find(key); if (it != cache.end()) return it->second;
        Sample* s = loadWav(key); if (s) cache[key] = s; return s;   // nunca se liberan: el audio puede estar leyéndolas
    }
    void assignPad(int pad) {
        const std::string& f = ss["samp.p" + std::to_string(pad) + ".file"];
        Sample* s = f.empty() ? (pad < static_cast<int>(facS.size()) ? facS[static_cast<size_t>(pad)] : nullptr) : fileSample(sampDir(), f);
        if (!s && !f.empty()) { std::printf("No se pudo leer samples/%s\n", f.c_str()); s = pad < static_cast<int>(facS.size()) ? facS[static_cast<size_t>(pad)] : nullptr; }
        au.samp.smp[pad].store(s);
    }
    Sample* breakSample(const std::string& nm) {
        if (nm.rfind("fab:", 0) == 0) { const int i = std::atoi(nm.c_str() + 4); return i >= 0 && i < static_cast<int>(facB.size()) ? facB[static_cast<size_t>(i)] : nullptr; }
        return fileSample(brkDir(), nm);
    }
    // compases más probables para un loop: el tempo que más se acerque a 125
    static int guessBars(const Sample& s) {
        const double sec = s.size() / s.sr; int best = 1; double bd = 1e9;
        for (int i = 0; i < 4; ++i) { const double bpm = (1 << i) * 240.0 / sec, d = std::fabs(std::log(bpm / 125.0)); if (d < bd) { bd = d; best = i; } }
        return best;
    }
    void assignBreak(bool guess) {
        Sample* s = breakSample(ss["brk.name"]); if (!s && !facB.empty()) s = facB[0];
        au.brk.smp.store(s);
        if (guess && s) { const std::string nm = ss["brk.name"]; set("brk.0", nm.rfind("fab:", 0) == 0 ? 1.0f : static_cast<float>(guessBars(*s)), -1); }
    }
    std::string filesJson() {
        std::string o = "{\"samples\":[";
        auto sv = listWavs(sampDir()); for (size_t i = 0; i < sv.size(); ++i) { if (i) o += ","; o += jstr(sv[i].c_str()); }
        o += "],\"breaks\":["; auto bv = listWavs(brkDir()); for (size_t i = 0; i < bv.size(); ++i) { if (i) o += ","; o += jstr(bv[i].c_str()); }
        o += "],\"facSamples\":["; for (size_t i = 0; i < facS.size(); ++i) { if (i) o += ","; o += jstr(facS[i]->name.c_str()); }
        o += "],\"facBreaks\":["; for (size_t i = 0; i < facB.size(); ++i) { if (i) o += ","; o += jstr(facB[i]->name.c_str()); }
        return o + "]}";
    }
    void sset(const std::string& k, const std::string& v, int from) {
        if (!ss.count(k)) return;
        // nombres de archivo: sin carpetas ni «..» (solo se leen archivos de samples/ y breaks/)
        if ((k.size() > 5 && k.compare(k.size() - 5, 5, ".file") == 0) || k == "brk.name")
            if (v.find('/') != std::string::npos || v.find('\\') != std::string::npos || v.find("..") != std::string::npos || v.size() > 200) return;
        ss[k] = v; dirty = true; dirtyAt = std::chrono::steady_clock::now();
        if (k == "brk.name") assignBreak(true);
        else if (k.rfind("samp.p", 0) == 0) assignPad(std::atoi(k.c_str() + 6));
        srv.broadcast("sset " + k + " " + v, from);
    }
    // grabación terminada: se guarda en samples/ y se asigna al pad elegido
    void finishRec() {
        const long n = au.recLen.load();
        if (n > static_cast<long>(au.sr * 0.05f)) {
            auto* s = new Sample; s->sr = au.sr; s->L.assign(au.recL.begin(), au.recL.begin() + n); s->R.assign(au.recR.begin(), au.recR.begin() + n);
            char nm[64]; const std::time_t t = std::time(nullptr); std::strftime(nm, sizeof nm, "grabacion-%Y%m%d-%H%M%S.wav", std::localtime(&t));
            s->name = std::string(nm).substr(0, std::strlen(nm) - 4);
            std::error_code ec; std::filesystem::create_directories(sampDir(), ec);
            saveWav(sampDir() + "/" + nm, *s); cache[sampDir() + "/" + nm] = s;
            const int pad = static_cast<int>(st["samp.sel"]);
            srv.broadcast(std::string("files ") + filesJson());
            sset("samp.p" + std::to_string(pad) + ".file", nm, -1);
            set("samp.p" + std::to_string(pad) + ".2", 0, -1); set("samp.p" + std::to_string(pad) + ".3", 1, -1);
        }
        au.recState = 0; srv.broadcast("rec 0");
    }
    std::chrono::steady_clock::time_point dirtyAt{}; bool dirty = false;
    std::map<std::string, float> webPend_; std::chrono::steady_clock::time_point lastWeb{};
    unsigned lastXr = 0; bool wasDirty = false; std::chrono::steady_clock::time_point dirtySince{};
    std::function<std::string()> sysInfo, sysStatus; std::function<void(const std::string&)> sysApply, sysAction, sysRoute;
    std::function<void()> tick; std::chrono::steady_clock::time_point lastTick{};
    unsigned midiSeen_ = ~0u; std::string midiPorts_ = "-";

    explicit Control(Audio& a) : au(a) { std::fill(lastPre_, lastPre_ + 16, -1); }

    void defaults() {
        const int defEng[NT] = {0, E_JUNO, E_ACID, E_MOOG}, defCh[NT] = {10, 2, 3, 4};
        for (int t = 0; t < NT; ++t) {
            const std::string T = "t" + std::to_string(t + 1);
            st[T + ".ch"] = static_cast<float>(defCh[t]);
            if (t == 0) continue;
            st[T + ".eng"] = static_cast<float>(defEng[t]);
            for (int e = 0; e < E_COUNT; ++e) {
                for (int i = 0; i < kEng[e].np; ++i) st[T + "." + kEng[e].key + "." + std::to_string(i)] = kEng[e].p[i].def;
                for (int i = 0; i < kEng[e].nb; ++i) st[T + "." + kEng[e].key + "." + std::to_string(kBtn0 + i)] = kEng[e].b[i].def ? 1.0f : 0.0f;
            }
        }
        // presets: 8 huecos por motor; los de fábrica parten de los valores por defecto
        for (int e = 0; e < E_COUNT; ++e) {
            const auto& fp = factoryPresets(e);
            for (int s = 0; s < 8; ++s) {
                const std::string P = presetKey(e, s);
                const bool on = s < static_cast<int>(fp.size()) && fp[static_cast<size_t>(s)].name[0];
                st[P + "on"] = on ? 1.0f : 0.0f; st[P + "user"] = 0;
                for (int i : engIdx(e)) st[P + std::to_string(i)] = i < kBtn0 ? kEng[e].p[i].def : (kEng[e].b[i - kBtn0].def ? 1.0f : 0.0f);
                if (on) for (auto& [i, v] : fp[static_cast<size_t>(s)].v) st[P + std::to_string(i)] = v;
            }
        }
        st["ver"] = kStateVer;
        // directo
        const int lchDef[LM_COUNT] = {5, 6, 7, 8};
        for (int m = 0; m < LM_COUNT; ++m) st[std::string(kLiveKeys[m]) + ".ch"] = static_cast<float>(lchDef[m]);
        st["samp.sel"] = 0; st["rec.src"] = 0;
        for (int pad = 0; pad < kPads; ++pad) { for (int j = 0; j < SA_COUNT; ++j) st["samp.p" + std::to_string(pad) + "." + std::to_string(j)] = kSampSpec[j].def; ss["samp.p" + std::to_string(pad) + ".file"] = ""; }
        for (int j = 0; j < BK_COUNT; ++j) st["brk." + std::to_string(j)] = kBrkSpec[j].def;
        for (int j = 0; j < GR_COUNT; ++j) st["gran." + std::to_string(j)] = kGrSpec[j].def;
        for (int j = 0; j < VZ_COUNT; ++j) st["voz." + std::to_string(j)] = kVzSpec[j].def;
        for (int n = 0; n < 8; ++n) st["ms." + std::to_string(n) + ".on"] = 0;
        st["brk.on"] = 0; st["gran.on"] = 0; st["gran.frz"] = 0; st["voz.on"] = 0; ss["brk.name"] = "fab:0";
        st["d.kit"] = 0;
        st["cb.amt"] = 1; st["cb.sel"] = -1; for (int n = 0; n < kCombos; ++n) { st["cb." + std::to_string(n) + ".on"] = 0; st["cb." + std::to_string(n) + ".act"] = 0; }   // combinaciones de efectos
        st["solo.mode"] = 0;   // solos: 0 = se suman (latch), 1 = exclusivo (X-OR)
        st["midi.mixch"] = 16; st["midi.knob"] = 1; st["midi.fb"] = 1; ss["midi.skip"] = "oxi,mc-24";   // canal del mezclador, modo de knobs, devolver valores, a quién no
        const int N[8] = {36, 38, 39, 37, 45, 42, 46, 56};
        for (int l = 0; l < 8; ++l) {
            const std::string L = "d.l" + std::to_string(l) + ".";
            st[L + "tune"] = 0; st[L + "decay"] = 0.5f; st[L + "snap"] = 0.5f; st[L + "filt"] = 0; st[L + "reso"] = 0.2f; st[L + "sweep"] = 0.5f; st[L + "level"] = 0.8f; st[L + "pan"] = 0; st[L + "vel"] = 0.8f;
            st[L + "tone"] = 0.5f; st[L + "color"] = kCycColor[l]; st[L + "shape"] = kCycShape[l]; st[L + "contour"] = kCycContour[l];
            st[L + "mute"] = 0; st[L + "note"] = static_cast<float>(N[l]); st[L + "out"] = static_cast<float>(kLaneOut[l]);
        }
        st["sub.ch"] = 9; st["sub.on"] = 1; st["sub.oct"] = -2; st["sub.tone"] = 0.2f; st["sub.glide"] = 0.03f; st["sub.vol"] = 1; st["sub.mode"] = 1; st["sub.src"] = 1;
        // canales
        for (int i = 0; i < NCH; ++i) { Strip s; for (int p = 0; p < SP_COUNT; ++p) st["c" + std::to_string(i) + "." + kSP[p]] = s.p[p]; }
        // punto de partida limpio (lo pidió Jorge): envíos bajados, sin grupos, todo al máster, sin distorsión, EQ ni sidechain
        // faders a 0 dB: el equilibrio va en el Volume de cada instrumento (mezcla de Jorge, 7-11-2026)
        st["d.l1.level"] = 0.52f; st["c2.trim"] = -7.5f;
        // Volume por pista y motor, ajustado a oído por Jorge (una misma pista pesa distinto según lo que toque)
        st["t2.dx.7"] = 0.6f; st["t2.wt.21"] = 0.81f; st["t2.saw.20"] = 0.85f; st["t3.wt.21"] = 0.8f; st["t4.dx.7"] = 0.63f;
        for (int q = 0; q < 4; ++q) st["c" + std::to_string(CH_IN + q) + ".mute"] = 1;   // entradas en silencio hasta que las abras: evita acoples
        for (int g = 0; g < NGRP; ++g) { Strip s; for (int p = 0; p < SP_COUNT; ++p) st["g" + std::to_string(g) + "." + kSP[p]] = s.p[p]; st["g" + std::to_string(g) + ".lvl"] = 1.0f; }
        const int fxDef[NSEND] = {FX_HALL, FX_TAPE, FX_DARK, FX_PING};
        for (int f = 0; f < NSEND; ++f) {
            const std::string F = "fx" + std::to_string(f) + ".";
            st[F + "type"] = static_cast<float>(fxDef[f]); st[F + "ret"] = 0.6f;
            for (int i = 0; i < 5; ++i) st[F + "p" + std::to_string(i)] = i < kFx[fxDef[f]].np ? kFx[fxDef[f]].p[i].def : 0.0f;
        }
        for (int i = 0; i < MP_COUNT; ++i) st[std::string("m.") + kMP[i]] = kMpDef[i];
    }
    static constexpr float kStateVer = 14;
    static std::string presetKey(int e, int s) { return std::string("pre.") + kEng[e].key + "." + std::to_string(s) + "."; }
    static std::vector<int> engIdx(int e) { std::vector<int> v; for (int i = 0; i < kEng[e].np; ++i) v.push_back(i); for (int i = 0; i < kEng[e].nb; ++i) v.push_back(kBtn0 + i); return v; }
    static bool isEngParam(const std::string& k) {
        if (k.size() < 4 || k[0] != 't' || k[2] != '.') return false;
        for (int e = 0; e < E_COUNT; ++e) if (k.compare(3, std::strlen(kEng[e].key) + 1, std::string(kEng[e].key) + ".") == 0) return true;
        return false;
    }
    // ¿es la «Velocity» de algún motor (en una pista o en un preset)?
    static bool isVelParam(const std::string& k) {
        const size_t d = k.rfind('.'); if (d == std::string::npos) return false;
        const int i = std::atoi(k.c_str() + d + 1);
        for (int e = 0; e < E_COUNT; ++e) {
            if (i < 0 || i >= kEng[e].np || std::strcmp(kEng[e].p[i].label, "Velocity") != 0) continue;
            const std::string key = std::string(".") + kEng[e].key + ".";
            if (k.find(key) != std::string::npos && (k[0] == 't' || k.rfind("pre.", 0) == 0)) return true;
        }
        return false;
    }
    // Rango válido de cada clave numérica (para no aceptar valores imposibles de la red, del MIDI o de un archivo dañado)
    bool rangeOf(const std::string& k, float& lo, float& hi) const {
        lo = -1e9f; hi = 1e9f;
        if (k.size() > 3 && k[0] == 't' && k[2] == '.') {
            const std::string f = k.substr(3);
            if (f == "ch") { lo = 0; hi = 16; return true; }
            if (f == "eng") { lo = 0; hi = E_COUNT - 1; return true; }
            for (int e = 0; e < E_COUNT; ++e) {
                const std::string pre = std::string(kEng[e].key) + ".";
                if (f.rfind(pre, 0) != 0) continue;
                const int i = std::atoi(f.c_str() + pre.size());
                if (i >= kBtn0) { lo = 0; hi = 1; return true; }
                if (i >= 0 && i < kEng[e].np) { lo = kEng[e].p[i].min; hi = kEng[e].p[i].max; return true; }
            }
            return true;
        }
        if (k.rfind("d.l", 0) == 0) {
            if (k.size() < 6) return true;   // clave rota de un archivo dañado
            const std::string f = k.substr(5);
            static const std::map<std::string, std::pair<float, float>> R = {{"tune", {-12, 12}}, {"filt", {-1, 1}}, {"pan", {-1, 1}}, {"note", {0, 127}}, {"out", {0, 2}}};
            const auto it = R.find(f); if (it != R.end()) { lo = it->second.first; hi = it->second.second; } else { lo = 0; hi = 1; }
            return true;
        }
        if (k == "d.kit") { lo = 0; hi = 4; return true; }
        if (k.size() > 3 && k.compare(k.size() - 3, 3, ".ch") == 0) { lo = 0; hi = 16; return true; }
        auto spec = [&](const PSpec* ps, int n, int j) { if (j >= 0 && j < n) { lo = ps[j].min; hi = ps[j].max; } else { lo = 0; hi = 1; } return true; };
        auto tail = [&](size_t from) { return std::atoi(k.c_str() + from); };
        if (k == "samp.sel") { lo = 0; hi = kPads - 1; return true; }
        if (k == "rec.src") { lo = 0; hi = 20; return true; }
        if (k.rfind("samp.p", 0) == 0) { const size_t d = k.find('.', 6); return spec(kSampSpec, SA_COUNT, d == std::string::npos ? -1 : tail(d + 1)); }
        if (k.rfind("brk.", 0) == 0) return spec(kBrkSpec, BK_COUNT, std::isdigit(static_cast<unsigned char>(k[4])) ? tail(4) : -1);
        if (k.rfind("gran.", 0) == 0) return spec(kGrSpec, GR_COUNT, std::isdigit(static_cast<unsigned char>(k[5])) ? tail(5) : -1);
        if (k.rfind("voz.", 0) == 0) return spec(kVzSpec, VZ_COUNT, std::isdigit(static_cast<unsigned char>(k[4])) ? tail(4) : -1);
        if (k.rfind("sub.", 0) == 0) { const std::string f = k.substr(4); if (f == "src") { lo = 1; hi = NT - 1; } else if (f == "oct") { lo = -3; hi = 0; } else { lo = 0; hi = 1; } return true; }
        if (k == "solo.mode" || k == "cb.amt") { lo = 0; hi = 1; return true; }
        if (k.rfind("midi.", 0) == 0) { if (k == "midi.knob") { lo = 0; hi = 3; } else { lo = 0; hi = 1; } return true; }
        if (k.rfind("fx", 0) == 0 && k.size() > 4) {
            const std::string f = k.substr(4);
            if (f == "type") { lo = 0; hi = FX_COUNT - 1; } else if (f == "ret") { lo = 0; hi = 1; } else { lo = 0; hi = 9; }   // parámetros: el más grande es la división de tiempo (0-9)
            return true;
        }
        if ((k[0] == 'c' || k[0] == 'g') && k.size() > 2 && std::isdigit(static_cast<unsigned char>(k[1]))) {
            const size_t d = k.find('.'); if (d == std::string::npos) return true;
            static const std::map<std::string, std::pair<float, float>> R = {{"pan", {-1, 1}}, {"out", {0, 2 + NDIR}}, {"dtype", {0, 1}}, {"low", {-15, 15}}, {"mid", {-15, 15}}, {"high", {-15, 15}},
                {"lim", {-24, 0}}, {"sc", {-1, NCH - 1}}, {"trim", {-24, 12}}, {"scRel", {0.02f, 1.0f}}, {"filt", {-1, 1}}, {"res", {0.7f, 15}}, {"lvl", {0, 1.5f}}};
            const auto it = R.find(k.substr(d + 1)); if (it != R.end()) { lo = it->second.first; hi = it->second.second; } else { lo = 0; hi = 1; }
            return true;
        }
        if (k.rfind("m.", 0) == 0) {
            static const std::map<std::string, std::pair<float, float>> R = {{"vol", {0, 1.5f}}, {"lim", {-12, 0}}, {"cueLvl", {0, 1.2f}}, {"cueMode", {0, 2}}, {"dtype", {0, 1}}, {"filt", {-1, 1}}, {"res", {0.7f, 15}},
                {"gdiv", {0, 3}}, {"throwDiv", {0, 9}}, {"bpm", {40, 220}}, {"rSize", {0, 5}}, {"rPitch", {-1, 1}}, {"bcRatio", {0, 2}}, {"bcAtk", {0.1f, 30}}, {"bcRel", {0.05f, 1.2f}}, {"bcMkp", {-6, 12}}, {"cbPitch", {24, 96}}, {"swRate", {0, 5}}};
            const auto it = R.find(k.substr(2)); if (it != R.end()) { lo = it->second.first; hi = it->second.second; } else { lo = 0; hi = 1; }
            return true;
        }
        return true;   // el resto (presets, snapshots...) se acota al usarse
    }
    bool valid(const std::string& k, float& v) const {
        if (!std::isfinite(v)) return false;
        float lo, hi; rangeOf(k, lo, hi); v = std::clamp(v, lo, hi); return true;
    }
    void load() {
        std::ifstream f(file);
        if (!f) { f.open(file + ".bak"); if (f) { std::printf("Aviso: no se pudo leer %s; uso la copia %s.bak\n", file.c_str(), file.c_str()); dirty = true; } else return; }
        std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
        // archivo cortado (apagón a mitad de escritura): se usa la copia anterior
        auto complete = [](const std::string& t) { const size_t e = t.find_last_not_of(" \r\n\t"); return e != std::string::npos && t[e] == '}' && t[0] == '{'; };
        if (!complete(s)) {
            std::ifstream b(file + ".bak"); std::stringstream bs; if (b) bs << b.rdbuf();
            if (complete(bs.str())) { std::printf("Aviso: %s estaba dañado; uso la copia %s.bak\n", file.c_str(), file.c_str()); s = bs.str(); dirty = true; }
            else if (s.empty()) return;
        }
        std::vector<std::pair<std::string, float>> kv;
        size_t p = 0;
        while ((p = s.find('"', p)) != std::string::npos) {
            const size_t q = s.find('"', p + 1); if (q == std::string::npos) break;
            const std::string k = s.substr(p + 1, q - p - 1); const size_t colon = s.find(':', q); if (colon == std::string::npos) break;
            size_t vs = colon + 1; while (vs < s.size() && s[vs] == ' ') ++vs;
            if (vs < s.size() && s[vs] == '"') {   // valor de texto
                std::string v; size_t e = vs + 1; for (; e < s.size() && s[e] != '"'; ++e) { if (s[e] == '\\' && e + 1 < s.size()) ++e; v += s[e]; }
                if (this->ss.count(k)) this->ss[k] = v;
                p = s.find_first_of(",}", e + 1); if (p == std::string::npos) break; continue;
            }
            kv.emplace_back(k, std::strtof(s.c_str() + colon + 1, nullptr));
            p = s.find_first_of(",}", colon); if (p == std::string::npos) break;
        }
        float ver = 0; for (auto& [k, v] : kv) if (k == "ver") ver = v;
        if (ver < kStateVer) dirty = true;   // se guarda ya en el formato nuevo
        // los parámetros de los sintes cambiaron de orden en la versión 3: los antiguos se ignoran
        for (auto& [k, v] : kv) {
            if (k == "ver" || (ver < 3 && isEngParam(k))) continue;
            if (k.rfind("d.l", 0) == 0 && k.size() > 5 && k.compare(k.size() - 5, 5, ".mute") == 0) continue;   // los mutes de la batería ya no existen
            if (ver < 4 && (k.rfind("c11.", 0) == 0 || k.rfind("c12.", 0) == 0 || k.rfind("c13.", 0) == 0 || k.rfind("c14.", 0) == 0)) continue;   // entradas: nuevos valores de partida
            if (ver < 5 && (k.rfind("m.", 0) == 0 && k != "m.vol" && k != "m.bpm" && k != "m.clock" && k != "m.cueMode")) continue;   // efectos del máster rehechos
            if (ver < 6 && k.size() > 6 && k.compare(k.size() - 6, 6, ".dtype") == 0) continue;   // distorsión: Fuzz y Bitcrusher
            if (ver < 6 && k == "d.kit") continue;                                                   // kits: sin el tipo DRM
            if (ver < 7 && isVelParam(k)) continue;   // velocidad: nuevos valores de partida, más sensibles
            if (ver < 9 && k.find(".juno.") != std::string::npos) continue;
            if (ver < 11 && k.find(".dx.") != std::string::npos) continue;     // DX7 nuevo (MSFA): parámetros distintos   // Juno nuevo (motor KR-106): parámetros distintos
            if (ver < 8 && k.find(".juno.") != std::string::npos && k.compare(k.size() - 2, 2, ".9") == 0) continue;   // HPF del Juno: ahora continuo
            if (ver < 10 && k.size() > 4 && k.compare(k.size() - 4, 4, ".eng") == 0 && v >= 8) v = 0;   // West Coast y Organ quitados: pasan a Juno
            if (ver < 10 && k == "d.kit") { static const float M[4] = {0, 2, 3, 4}; v = M[std::clamp(static_cast<int>(v), 0, 3)]; }   // kits nuevos: 808, 909, FM, Chip, Experimental
            // una sola clave rara no puede impedir arrancar
            float x = v; try { if (!valid(k, x)) continue; } catch (const std::exception&) { continue; }
            if (st.count(k) || k.rfind("ms.", 0) == 0 || k.rfind("cb.", 0) == 0) st[k] = x;
        }
        if (ver > 0 && ver < 14) {   // bajó el nivel base de motores y Sub: el Volume guardado sube lo mismo y lo tuyo suena igual
            for (int e = 0; e < E_COUNT; ++e) {
                const int vi = kEngVol[e]; if (vi < 0) continue; const float m = std::pow(10.0f, -kEngShift14[e] / 40.0f), hi = kEng[e].p[vi].max;
                for (int t = 2; t <= NT; ++t) { auto it = st.find("t" + std::to_string(t) + "." + kEng[e].key + "." + std::to_string(vi)); if (it != st.end()) it->second = std::min(it->second * m, hi); }
                for (int sl = 0; sl < 8; ++sl) { const std::string P = presetKey(e, sl); if (st[P + "user"] >= 0.5f) { auto it = st.find(P + std::to_string(vi)); if (it != st.end()) it->second = std::min(it->second * m, hi); } }
            }
            st["sub.vol"] = std::min(st["sub.vol"] / 0.316f, 1.0f);
        }
        if (ver > 0 && ver < 14) for (int e = 0; e < E_COUNT; ++e) for (int sl = 0; sl < 8; ++sl) {   // presets de fábrica igualados de nivel; los tuyos no se tocan
            const std::string P = presetKey(e, sl); if (st[P + "user"] >= 0.5f) continue;
            for (int i : engIdx(e)) { const auto d = defs.find(P + std::to_string(i)); if (d != defs.end()) st[P + std::to_string(i)] = d->second; }
        }
        if (ver < 12) for (int f = 0; f < NSEND; ++f) {   // envíos con knobs nuevos: valores de partida
            const std::string F = "fx" + std::to_string(f) + "."; const int t = std::clamp(static_cast<int>(st[F + "type"]), 0, FX_COUNT - 1);
            if (t <= FX_PING) for (int i = 3; i < kFx[t].np; ++i) st[F + "p" + std::to_string(i)] = kFx[t].p[i].def;
        }
        std::printf("Estado cargado de %s%s\n", file.c_str(), ver < 3 ? " (sintes a sus valores nuevos)" : "");
    }
    // --- mezcla: reset por canal, reset general y 8 snapshots
    std::map<std::string, float> defs;   // valores de partida (se copian tras defaults())
    static bool isMixKey(const std::string& k) {
        if (k.size() > 2 && (k[0] == 'c' || k[0] == 'g') && std::isdigit(static_cast<unsigned char>(k[1]))) return true;
        if (k.rfind("fx", 0) == 0 && k.size() > 3 && std::isdigit(static_cast<unsigned char>(k[2]))) return true;
        return k.rfind("m.", 0) == 0 && k != "m.bpm" && k != "m.clock" && k != "m.cueMode" && k != "m.clip";
    }
    void mixReset(const std::string& what) {
        const std::string pre = what == "all" ? "" : what + ".";
        for (auto& [k, v] : defs) {
            if (pre.empty() ? !isMixKey(k) || k == "m.vol" : k.rfind(pre, 0) != 0) continue;
            if (st[k] != v) set(k, v, -1);
        }
    }
    void snapSave(int n) {
        if (n < 0 || n > 7) return;
        const std::string P = "ms." + std::to_string(n) + ".";
        std::vector<std::pair<std::string, float>> kv; for (auto& [k, v] : st) if (isMixKey(k)) kv.emplace_back(k, v);
        for (auto& [k, v] : kv) st[P + k] = v;   // sin difundir: la pantalla solo necesita saber que el hueco está lleno
        set(P + "on", 1, -1);
    }
    void snapLoad(int n) {
        if (n < 0 || n > 7) return;
        const std::string P = "ms." + std::to_string(n) + ".";
        if (st[P + "on"] < 0.5f) return;
        std::vector<std::pair<std::string, float>> kv;
        for (auto it = st.lower_bound(P); it != st.end() && it->first.rfind(P, 0) == 0; ++it) { const std::string k = it->first.substr(P.size()); if (k != "on" && st.count(k)) kv.emplace_back(k, it->second); }
        for (auto& [k, v] : kv) if (st[k] != v) set(k, v, -1);
    }
    // parámetros que mueve la prueba de estrés
    static bool isFxKey(const std::string& k) {
        if (k.rfind("m.", 0) == 0) { for (const char* x : {"m.vol", "m.lim", "m.clip", "m.cueLvl", "m.cueMode", "m.bpm", "m.clock", "m.sum"}) if (k == x) return false; return true; }
        if (k.rfind("fx", 0) == 0 && k.size() > 3 && std::isdigit(static_cast<unsigned char>(k[2]))) return k.compare(3, 5, ".type") != 0;
        if (k.size() > 2 && (k[0] == 'c' || k[0] == 'g') && std::isdigit(static_cast<unsigned char>(k[1]))) {
            const std::string f = k.substr(k.find('.') + 1);
            return f == "s0" || f == "s1" || f == "s2" || f == "s3" || f == "drive" || (k[0] == 'g' && (f == "filt" || f == "res"));
        }
        return false;
    }
    // --- 8 combinaciones de efectos de máster: cada una junta los efectos que tú elijas (por ejemplo filtro, delay
    // y repeat), se dispara con un botón y un crossfader corto la lleva de seco (Dry) a efecto (Wet). Guardar: pones
    // los efectos como quieras y guardas en un hueco; entran los que tengan cantidad. Pulsar = se queda; mantener =
    // solo mientras se mantiene. Con una activa, lo que muevas en sus efectos se guarda en ella, y si subes otro
    // efecto se añade. Al apagarla, todo vuelve a como estaba. Los demás efectos siguen libres.
    static constexpr int kCombos = 8;
    struct CbFx { const char* amt; std::vector<const char*> params; };
    static const std::vector<CbFx>& cbFx() {
        static const std::vector<CbFx> F = {{"m.filt", {"m.res", "m.fEnv"}}, {"m.rep", {"m.rSize", "m.rDecay", "m.rPitch"}}, {"m.gate", {"m.gdiv", "m.gShape", "m.gPan"}}, {"m.riser", {"m.rNoise"}},
            {"m.space", {"m.spSize", "m.spDamp", "m.spFrz"}}, {"m.throw", {"m.throwDiv", "m.throwFb", "m.tTone"}}, {"m.tape", {"m.tapeSm"}}, {"m.comb", {"m.cbPitch", "m.cbFb", "m.cbDamp"}},
            {"m.sweep", {"m.swRate", "m.swDepth", "m.swFb", "m.swMode"}}, {"m.drive", {"m.dtype", "m.dtone", "m.dmix"}}};
        return F;
    }
    struct CbItem { std::string k; float base, to; bool amt; };
    std::vector<CbItem> cbLive_; int cbCur_ = -1; bool cbDirty = false; std::map<int, std::chrono::steady_clock::time_point> cbHeld_; std::chrono::steady_clock::time_point lastCb{};
    void cbPublish(const std::string& k, float v) { st[k] = v; srv.broadcast("set " + k + " " + jnum(v)); }
    void comboSave(int n) {
        if (n < 0 || n >= kCombos) return;
        // se copia lo que suena ahora, también si hay otra combinación puesta; después se apaga
        std::map<std::string, float> now; for (const auto& f : cbFx()) { now[f.amt] = st[f.amt]; for (const char* p : f.params) now[p] = st[p]; }
        comboOff();
        const std::string P = "cb." + std::to_string(n) + "."; bool any = false;
        for (const auto& f : cbFx()) {
            const bool used = std::fabs(now[f.amt]) > 0.005f; any |= used;
            cbPublish(P + f.amt, used ? now[f.amt] : 0.0f);
            for (const char* p : f.params) cbPublish(P + p, now[p]);
        }
        set(P + "on", any ? 1.0f : 0.0f, -1);
    }
    // Con una combinación activa, lo que toca el músico (web o controladora) se guarda en ella
    void comboEdit(const std::string& k, float old, float v) {
        const std::string P = "cb." + std::to_string(cbCur_) + ".";
        for (auto& i : cbLive_) {
            if (i.k != k) continue;
            const float x = st["cb.amt"];
            i.to = x > 0.02f ? i.base + (v - i.base) / x : v;   // con el crossfader a medias, lo guardado es el valor a tope
            float lo, hi; rangeOf(k, lo, hi); i.to = std::clamp(i.to, lo, hi);
            cbPublish(P + k, i.to); return;
        }
        for (const auto& f : cbFx()) {
            if (k != f.amt || std::fabs(v) <= 0.005f) continue;
            cbLive_.push_back({k, old, v, true}); cbPublish(P + k, v);
            for (const char* p : f.params) { cbLive_.push_back({p, st[p], st[p], false}); cbPublish(P + p, st[p]); }
            return;
        }
    }
    void comboOff() {
        if (cbCur_ < 0) return;
        for (const auto& i : cbLive_) if (st[i.k] != i.base) set(i.k, i.base, -1);
        set("cb." + std::to_string(cbCur_) + ".act", 0, -1); set("cb.sel", -1, -1);
        cbLive_.clear(); cbCur_ = -1;
    }
    void comboOn(int n) {
        comboOff();
        const std::string P = "cb." + std::to_string(n) + ".";
        if (n < 0 || n >= kCombos || st[P + "on"] < 0.5f) return;
        for (const auto& f : cbFx()) {
            const auto a = st.find(P + f.amt); if (a == st.end() || std::fabs(a->second) <= 0.005f) continue;
            cbLive_.push_back({f.amt, st[f.amt], a->second, true});
            for (const char* p : f.params) { const auto o = st.find(P + p); if (o != st.end()) cbLive_.push_back({p, st[p], o->second, false}); }
        }
        cbCur_ = n; set(P + "act", 1, -1); set("cb.sel", static_cast<float>(n), -1); comboApply();
    }
    void comboApply() {
        cbDirty = false; if (cbCur_ < 0) return;
        const float x = st["cb.amt"];
        for (const auto& i : cbLive_) { const float v = i.base + (i.to - i.base) * x; if (st[i.k] != v) set(i.k, v, -1); }   // todos los knobs siguen al fader
    }
    // mismo gesto que los mutes: pulsar y soltar enseguida = se queda; mantener = solo mientras se mantiene
    void comboPress(int n, bool down) {
        const auto now = std::chrono::steady_clock::now();
        if (down) { if (cbCur_ == n) comboOff(); else { comboOn(n); if (cbCur_ == n) cbHeld_[n] = now; } return; }
        const auto it = cbHeld_.find(n); if (it == cbHeld_.end()) return;
        if (now - it->second > std::chrono::milliseconds(400) && cbCur_ == n) comboOff();
        cbHeld_.erase(it);
    }
    // Presets: cargar o guardar el motor actual de una pista (t = 1..3)
    void presetLoad(int t, int s, int from) {
        if (t < 1 || t >= NT || s < 0 || s > 7) return;
        const int e = static_cast<int>(st["t" + std::to_string(t + 1) + ".eng"]);
        const std::string P = presetKey(e, s); if (st[P + "on"] < 0.5f) return;
        for (int i : engIdx(e)) set("t" + std::to_string(t + 1) + "." + kEng[e].key + "." + std::to_string(i), st[P + std::to_string(i)], -1);
        (void)from;
    }
    void presetSave(int t, int s) {
        if (t < 1 || t >= NT || s < 0 || s > 7) return;
        const int e = static_cast<int>(st["t" + std::to_string(t + 1) + ".eng"]);
        const std::string P = presetKey(e, s);
        for (int i : engIdx(e)) set(P + std::to_string(i), st["t" + std::to_string(t + 1) + "." + kEng[e].key + "." + std::to_string(i)], -1);
        set(P + "on", 1, -1); set(P + "user", 1, -1);
    }
    // Guardado seguro: se escribe aparte, se fuerza al disco y se cambia de golpe; la versión anterior queda como .bak
    bool saveErr = false;
    void save() {
        dirty = false;
        const std::string tmp = file + ".tmp", js = stateJson();
        FILE* f = std::fopen(tmp.c_str(), "wb");
        bool ok = f && std::fwrite(js.data(), 1, js.size(), f) == js.size();
        if (f) { ok = std::fflush(f) == 0 && ok; ok = ::fsync(fileno(f)) == 0 && ok; ok = std::fclose(f) == 0 && ok; }
        if (ok) { std::rename(file.c_str(), (file + ".bak").c_str()); ok = std::rename(tmp.c_str(), file.c_str()) == 0; }
        if (!ok && !saveErr) std::printf("Aviso: no se pudo guardar el estado en %s\n", file.c_str());
        if (ok != !saveErr) { saveErr = !ok; srv.broadcast(std::string("saveerr ") + (saveErr ? "1" : "0")); }
        if (!ok) { dirty = true; dirtyAt = std::chrono::steady_clock::now() + std::chrono::seconds(5); }
    }
    std::string stateJson() {
        std::string o = "{"; bool first = true;
        for (auto& [k, v] : st) { if (!first) o += ","; first = false; o += "\"" + k + "\":" + jnum(v); }
        for (auto& [k, v] : ss) { o += ",\"" + k + "\":" + jstr(v.c_str()); }
        return o + "}";
    }

    // clave → orden para el audio
    static int find(const std::string& f, const char* const* names, int n) { for (int i = 0; i < n; ++i) if (f == names[i]) return i; return -1; }
    bool toCmd(const std::string& k, float v, Cmd& c) {
        c = {K_MST, 0, 0, 0, v};
        if (k == "d.kit") { c.k = K_KIT; return true; }
        for (int m = 0; m < LM_COUNT; ++m) {
            const std::string pre = std::string(kLiveKeys[m]) + ".";
            if (k.rfind(pre, 0) != 0) continue;
            const std::string f = k.substr(pre.size()); c.k = K_LIVE; c.a = static_cast<int16_t>(m);
            if (f == "ch") { c.k = K_LCH; return true; }
            if (f == "sel") { c.b = -1; return true; }
            if (f == "on") { c.c = kOnIdx; return true; } if (f == "frz") { c.c = kFrzIdx; return true; }
            if (f[0] == 'p') { const size_t d = f.find('.'); if (d == std::string::npos) return false; c.b = static_cast<int16_t>(std::atoi(f.c_str() + 1)); c.c = static_cast<int16_t>(std::atoi(f.c_str() + d + 1)); return true; }
            c.c = static_cast<int16_t>(std::atoi(f.c_str())); return true;
        }
        if (k == "rec.src") return false;
        if (k.rfind("d.l", 0) == 0) {
            if (k.size() < 6) return false;
            c.k = K_LANE; c.a = static_cast<int16_t>(k[3] - '0'); const std::string f = k.substr(5);
            const int i = find(f, kLaneKeys, kNLane); if (i < 0) return false; c.b = static_cast<int16_t>(i); return true;
        }
        if (k.rfind("sub.", 0) == 0) { const int i = find(k.substr(4), kSubKeys, 8); if (i < 0) return false; c.k = K_SUB; c.b = static_cast<int16_t>(i); return true; }
        if (k.rfind("m.", 0) == 0) { const int i = find(k.substr(2), kMP, MP_COUNT); if (i < 0) return false; c.k = K_MST; c.b = static_cast<int16_t>(i); return true; }
        if (k.rfind("fx", 0) == 0) {
            c.k = K_FX; c.a = static_cast<int16_t>(k[2] - '0'); const std::string f = k.substr(4);
            if (f == "type") { c.b = 5; return true; } if (f == "ret") { c.b = 6; return true; }
            if (f.size() == 2 && f[0] == 'p') { c.b = static_cast<int16_t>(f[1] - '0'); return true; }
            return false;
        }
        if (k[0] == 'c' || k[0] == 'g') {
            const size_t dot = k.find('.'); if (dot == std::string::npos) return false;
            const int idx = std::atoi(k.c_str() + 1), i = find(k.substr(dot + 1), kSP, SP_COUNT); if (i < 0) return false;
            c.k = k[0] == 'c' ? K_STRIP : K_GRP; c.a = static_cast<int16_t>(idx); c.b = static_cast<int16_t>(i); return true;
        }
        if (k.size() > 3 && k[0] == 't') {
            const int t = k[1] - '1'; if (t < 0 || t >= NT) return false; c.a = static_cast<int16_t>(t);
            const std::string f = k.substr(3);
            if (f == "ch") { c.k = K_CHN; return true; } if (f == "eng") { c.k = K_ENG; return true; }
            for (int e = 0; e < E_COUNT; ++e) {
                const std::string pre = std::string(kEng[e].key) + ".";
                if (f.rfind(pre, 0) == 0) { c.k = K_ENGP; c.b = static_cast<int16_t>(e); c.c = static_cast<int16_t>(std::atoi(f.c_str() + pre.size())); return true; }
            }
        }
        return false;
    }
    void set(const std::string& k, float v, int from) {
        if (k == "ver" || !st.count(k) || !valid(k, v)) return;
        const float old = st[k];
        st[k] = v; Cmd c; if (toCmd(k, v, c)) au.cmds.push(c);
        dirty = true; dirtyAt = std::chrono::steady_clock::now();
        // lo que llega por MIDI puede venir a cientos por segundo (un LFO del Oxi): a la web se le manda solo el último valor
        if (from == kMidiSrc) webPend_[k] = v; else srv.broadcast("set " + k + " " + jnum(v), from);
        queueFb(k);
        // solo exclusivo (X-OR): al encender un solo se apagan los demás
        if (v >= 0.5f && st["solo.mode"] >= 0.5f && k.size() > 5 && k.compare(k.size() - 5, 5, ".solo") == 0 && (k[0] == 'c' || k[0] == 'g'))
            for (auto& [o, ov] : st) if (o != k && ov >= 0.5f && o.size() > 5 && o.compare(o.size() - 5, 5, ".solo") == 0 && (o[0] == 'c' || o[0] == 'g') && std::isdigit(static_cast<unsigned char>(o[1]))) set(o, 0, -1);
        if ((k.size() > 3 && (k.compare(k.size() - 3, 3, ".ch") == 0 || k.compare(k.size() - 4, 4, ".eng") == 0)) || k == "d.kit" || k == "samp.sel" || k == "midi.mixch" || k == "midi.fb") mapDirty = true;
        if (k == "midi.mixch") gMixCh = mixCh();
        if (k == "cb.amt") cbDirty = true;
        if (from != -1 && cbCur_ >= 0 && k.rfind("m.", 0) == 0) comboEdit(k, old, v);
    }
    void pushAll() {
        // voz simplificada: entrada, ganancia, corte de graves, puerta, pitch y formant. Lo demás queda neutro
        // (sin compresor, de-esser, efectos, reverb ni eco; modo normal; todo señal procesada)
        for (const auto& [i, v] : std::initializer_list<std::pair<int, float>>{{VZ_COMP, 0}, {VZ_DESS, 0}, {VZ_MODE, VM_NORMAL}, {VZ_FX, 0}, {VZ_REVERB, 0}, {VZ_ECHO, 0}, {VZ_BAL, 1}})
            st["voz." + std::to_string(i)] = v;
        for (auto& [k, v] : st) { Cmd c; if (toCmd(k, v, c)) au.cmds.push(c); }
        for (int pad = 0; pad < kPads; ++pad) assignPad(pad);
        assignBreak(false);
    }

    void onMessage(int client, const std::string& m) {
        std::istringstream is(m); std::string op; is >> op;
        if (op == "set") { std::string k; float v; if (is >> k >> v) set(k, v, client); }
        else if (op == "hello") {
            std::string spec = specJson(); spec.pop_back(); spec += "," + mixSpecJson() + ",\"outs\":" + std::to_string(au.outCh) + ",\"ins\":" + std::to_string(au.inCh) + "}";
            spec.pop_back(); spec += "," + liveSpecJson() + "}";
            midiSeen_ = ~0u;   // que el monitor MIDI se reenvíe a la pantalla nueva
            srv.send(client, "spec " + spec); srv.send(client, "files " + filesJson()); srv.send(client, "state " + stateJson());
        }
        else if (op == "kb") { int t, n, v; if (is >> t >> n >> v) au.cmds.push({K_NOTE, static_cast<int16_t>(t), static_cast<int16_t>(n), static_cast<int16_t>(v), 0}); }
        else if (op == "panic") au.cmds.push({K_ALLOFF, 0, 0, 0, 0});
        else if (op == "midi") {   // mensaje MIDI simulado (pruebas): «midi estado d1 d2» en decimal
            int a0, a1, a2; if (is >> a0 >> a1 >> a2) { const unsigned char st0 = static_cast<unsigned char>(a0), ty = st0 & 0xF0;
                if (ty == 0xB0 || ty == 0xC0 || ((ty == 0x90 || ty == 0x80) && (st0 & 0x0F) == gMixCh.load())) onCtl({st0, static_cast<unsigned char>(a1), static_cast<unsigned char>(a2), -1});
                else au.pushMidi({st0, static_cast<unsigned char>(a1), static_cast<unsigned char>(a2), monoNow()}); } }
        else if (op == "mixreset") { std::string w; if (is >> w) mixReset(w); }
        else if (op == "drreset") {   // batería: un sonido o todos (no toca las notas MIDI ni el canal)
            std::string w; if (is >> w) for (int l = 0; l < 8; ++l) {
                if (w != "all" && std::atoi(w.c_str()) != l) continue;
                for (int fi = 0; fi < 14; ++fi) { const char* f = kLaneKeys[fi]; { const std::string k = "d.l" + std::to_string(l) + "." + f; if (defs.count(k) && st[k] != defs[k]) set(k, defs[k], -1); } }
            } }
        else if (op == "sys") { if (sysInfo) srv.send(client, "sys " + sysInfo()); }
        else if (op == "sysst") { if (sysStatus) srv.send(client, "sysst " + sysStatus()); }
        else if (op == "sysroute") { std::string rest; std::getline(is, rest); if (sysRoute) sysRoute(rest); }
        else if (op == "sysapply") { std::string rest; std::getline(is, rest); if (sysApply) sysApply(rest); }
        else if (op == "sysact") { std::string w; if (is >> w && sysAction) sysAction(w); }
        else if (op == "combo") { std::string w; int n; if (is >> w >> n) { if (w == "save") comboSave(n); else if (w == "down") comboPress(n, true); else if (w == "up") comboPress(n, false); else if (w == "off") comboOff(); } }
        else if (op == "msnap") { std::string w; int n; if (is >> w >> n) { if (w == "save") snapSave(n); else if (w == "load") snapLoad(n); } }
        else if (op == "sset") { std::string k; if (is >> k) { std::string v; std::getline(is, v); if (!v.empty() && v[0] == ' ') v.erase(0, 1); sset(k, v, client); } }
        else if (op == "files") srv.send(client, "files " + filesJson());
        else if (op == "wave") {   // forma de onda resumida para la pantalla
            std::string w; int i = 0; is >> w >> i; const Sample* sm = nullptr;
            if (w == "samp" && i >= 0 && i < kPads) sm = au.samp.smp[i].load(); else if (w == "brk") sm = au.brk.smp.load();
            std::string o = "wave " + w + " " + std::to_string(i) + " " + (sm ? jnum(static_cast<float>(sm->size() / sm->sr)) : "0");
            if (sm && sm->size()) { const size_t B = 240, N = sm->size();
                for (size_t b = 0; b < B; ++b) { float pk = 0; const size_t a0 = b * N / B, a1 = std::max(a0 + 1, (b + 1) * N / B); for (size_t j = a0; j < a1 && j < N; j += std::max<size_t>(1, (a1 - a0) / 64)) pk = std::max({pk, std::fabs(sm->L[j]), std::fabs(sm->R[j])}); o += " " + std::to_string(static_cast<int>(std::min(1.0f, pk) * 99)); } }
            srv.send(client, o);
        }
        else if (op == "rec") { int on; if (is >> on) { if (on) { au.cmds.push({K_REC, 0, static_cast<int16_t>(st["rec.src"]), 0, 1}); srv.broadcast("rec 1"); } else au.cmds.push({K_REC, 0, 0, 0, 0}); } }
        else if (op == "preset") { std::string w; int t, s; if (is >> w >> t >> s) { if (w == "load") presetLoad(t, s, client); else if (w == "save") presetSave(t, s); } }
        else if (op == "lhold") { std::string w; int i, on; if (is >> w >> i >> on && w == "brk" && i >= 0 && i < 8) au.cmds.push({K_LIVE, LM_BRK, 0, static_cast<int16_t>(kBrkHold0 + i), static_cast<float>(on)}); }
    }
    // ============================================================== control por MIDI (sin pantalla)
    // Mapa fijo: cada canal MIDI es un instrumento (el mismo que toca el Oxi) y el mezclador va en su propio canal.
    // Las controladoras se conectan a la Pi; los valores y las luces se les devuelven por MIDI.
    struct Tgt { std::string key; float min = 0, max = 1, step = 0; bool log = false; uint8_t type = 0; };   // type: 0 knob, 1 botón que alterna
    std::map<int, Tgt> ccMap;               // canal*128 + CC → destino
    std::map<std::string, int> keyAddr;     // clave → dirección (CC; 0x10000 + canal*128 + nota para luces de nota)
    std::map<int, float> lastIn, lastSet;   // último valor recibido y último puesto, por controladora y dirección (recogida)
    std::chrono::steady_clock::time_point lastWarn{};
    std::map<int, std::chrono::steady_clock::time_point> inAt;   // cuándo llegó (para no devolver el eco)
    std::map<int, int> pendFb, sentFb;      // devoluciones pendientes y ya enviadas
    std::function<void(const std::vector<unsigned char>&)> midiOut;
    bool mapDirty = true, dumpPending = true, forceDump = true, shift[16] = {};
    bool powerOffEnabled = false; std::chrono::steady_clock::time_point powerHeld{}; bool powerDown = false;
    int ledState[16]; bool ledInit = false;
    static constexpr int kMidiSrc = -2;     // «origen MIDI» en set()

    static int addrCC(int ch, int cc) { return ch * 128 + cc; }
    static int addrNote(int ch, int n) { return 0x10000 + ch * 128 + n; }
    int chOf(const std::string& k) const { auto it = st.find(k); if (it == st.end()) return -1; const int c = static_cast<int>(it->second); return c >= 1 && c <= 16 ? c - 1 : -1; }
    int mixCh() const { return chOf("midi.mixch"); }

    static float norm(const Tgt& t, float v) {
        if (t.max <= t.min) return 0;
        if (t.log && t.min > 0) return std::clamp(std::log(v / t.min) / std::log(t.max / t.min), 0.0f, 1.0f);
        return std::clamp((v - t.min) / (t.max - t.min), 0.0f, 1.0f);
    }
    static float denorm(const Tgt& t, float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        float y = t.log && t.min > 0 ? t.min * std::pow(t.max / t.min, x) : t.min + (t.max - t.min) * x;
        if (t.step > 0) y = std::round(y / t.step) * t.step;
        return std::clamp(y, t.min, t.max);
    }
    void mapKey(int ch, int cc, const std::string& key, const PSpec& p) {
        if (ch < 0 || !st.count(key)) return;
        Tgt t; t.key = key; t.min = p.min; t.max = p.max; t.step = p.step; t.log = p.log;
        ccMap[addrCC(ch, cc)] = t; keyAddr[key] = addrCC(ch, cc);
    }
    void mapRange(int ch, int cc, const std::string& key, float lo, float hi, float step = 0, uint8_t type = 0) {
        if (ch < 0 || !st.count(key)) return;
        Tgt t; t.key = key; t.min = lo; t.max = hi; t.step = step; t.type = type;
        ccMap[addrCC(ch, cc)] = t; keyAddr[key] = addrCC(ch, cc);
    }
    void buildMap() {
        ccMap.clear(); keyAddr.clear(); mapDirty = false;
        gLpdSamp = chOf("samp.ch"); gLpdGran = chOf("gran.ch");
        // sintes: 16 knobs de la capa principal (CC16-31), segunda página (CC40-63), botones (CC102-109)
        for (int t = 1; t < NT; ++t) {
            const std::string T = "t" + std::to_string(t + 1); const int ch = chOf(T + ".ch"); if (ch < 0) continue;
            const int e = std::clamp(static_cast<int>(st[T + ".eng"]), 0, E_COUNT - 1); const std::string K = T + "." + kEng[e].key + ".";
            // fila de arriba del MC-24 (CC 8–15): los 7 primeros knobs de la capa y el crossfader Dry–Wet
            for (int s = 0; s < 7; ++s) { const int i = kLayer[e][s]; if (i >= 0) mapKey(ch, 8 + s, K + std::to_string(i), kEng[e].p[i]); }
            mapRange(ch, 15, "cb.amt", 0, 1);
            for (int s = 0; s < 16; ++s) { const int i = kLayer[e][s]; if (i >= 0) mapKey(ch, 16 + s, K + std::to_string(i), kEng[e].p[i]); }
            if (e == E_DX) { const auto it = keyAddr.find(K + "0"); if (it != keyAddr.end()) ccMap[it->second].max = static_cast<float>(std::max(0, dx7::libCount() - 1)); }   // Patch: lo que hay en la biblioteca
            const auto p2 = page2(e); for (size_t s = 0; s < p2.size() && s < 24; ++s) mapKey(ch, 40 + static_cast<int>(s), K + std::to_string(p2[s]), kEng[e].p[p2[s]]);
            for (int b = 0; b < kEng[e].nb && b < 8; ++b) mapRange(ch, 102 + b, K + std::to_string(kBtn0 + b), 0, 1, 1, 1);
        }
        // batería: 8 knobs por sonido (CC 16 + knob*8 + sonido), pan (CC80-87), mutes (CC102-109)
        { const int ch = chOf("t1.ch");
          if (ch >= 0) {
            const char* extra = nullptr; const char* const* slots = drumSlots(static_cast<int>(st["d.kit"]), extra);
            // columnas en el orden de la pantalla y de la controladora: bombo, caja, HH cerrado, HH abierto, clap, rim, tom, perc
            static const int kCol[8] = {0, 1, 5, 6, 2, 3, 4, 7};
            for (int k = 0; k < 8; ++k) for (int c = 0; c < 8; ++c) {
                const int l = kCol[c];
                const std::string key = "d.l" + std::to_string(l) + "." + slots[k]; float lo, hi; rangeOf(key, lo, hi);
                mapRange(ch, 16 + k * 8 + c, key, lo, hi);
            }
            for (int c = 0; c < 8; ++c) { const std::string L = "d.l" + std::to_string(kCol[c]) + "."; mapRange(ch, 80 + c, L + "pan", -1, 1); if (static_cast<int>(st["d.kit"]) == 2) { mapRange(ch, 96 + c, L + "snap", 0, 1); mapRange(ch, 112 + c, L + "vel", 0, 1); } if (extra) { float lo, hi; rangeOf(L + extra, lo, hi); mapRange(ch, 88 + c, L + extra, lo, hi); } }
          } }
        // sampler: CC16-31 = parámetros del pad elegido (CC14 elige pad)
        { const int ch = chOf("samp.ch"); if (ch >= 0) { const int pad = std::clamp(static_cast<int>(st["samp.sel"]), 0, kPads - 1);
            for (int j = 0; j < SA_COUNT && j < 16; ++j) mapKey(ch, 16 + j, "samp.p" + std::to_string(pad) + "." + std::to_string(j), kSampSpec[j]);
            mapRange(ch, 14, "samp.sel", 0, kPads - 1, 1); } }
        { const int ch = chOf("brk.ch"); if (ch >= 0) { for (int j = 0; j < BK_COUNT; ++j) mapKey(ch, 16 + j, "brk." + std::to_string(j), kBrkSpec[j]); mapRange(ch, 110, "brk.on", 0, 1, 1, 1); } }
        { const int ch = chOf("gran.ch"); if (ch >= 0) { for (int j = 0; j < GR_COUNT; ++j) mapKey(ch, 16 + j, "gran." + std::to_string(j), kGrSpec[j]); mapRange(ch, 110, "gran.on", 0, 1, 1, 1); mapRange(ch, 111, "gran.frz", 0, 1, 1, 1); } }
        { const int ch = chOf("voz.ch"); if (ch >= 0) { for (int j = 0; j < VZ_COUNT; ++j) mapKey(ch, j < 16 ? 16 + j : 40 + j - 16, "voz." + std::to_string(j), kVzSpec[j]); mapRange(ch, 110, "voz.on", 0, 1, 1, 1); } }
        // Sub: octava, tono y glide; CC 110 lo enciende y apaga
        { const int ch = chOf("sub.ch"); if (ch >= 0) { mapRange(ch, 16, "sub.oct", -3, 0, 1); mapRange(ch, 17, "sub.tone", 0, 1); mapRange(ch, 19, "sub.vol", 0, 1); mapRange(ch, 18, "sub.glide", 0.005f, 0.3f); ccMap[addrCC(ch, 18)].log = true; mapRange(ch, 110, "sub.on", 0, 1, 1, 1); } }
        // mezclador
        const int mc = mixCh();
        if (mc >= 0) {
            for (int i = 0; i < NCH; ++i) {
                const std::string C = "c" + std::to_string(i) + ".";
                mapRange(mc, 16 + i, C + "lvl", 0, 1); mapRange(mc, 32 + i, C + "pan", -1, 1); mapRange(mc, 48 + i, C + "s0", 0, 1); mapRange(mc, 64 + i, C + "s1", 0, 1); mapRange(mc, 80 + i, C + "drive", 0, 1);
                keyAddr[C + "mute"] = addrNote(mc, i); keyAddr[C + "solo"] = addrNote(mc, 16 + i); keyAddr[C + "cue"] = addrNote(mc, 32 + i);
            }
            mapRange(mc, 31, "m.vol", 0, 1);
            static const char* kAmt[7] = {"m.rep", "m.gate", "m.riser", "m.space", "m.throw", "m.tape", "m.filt"};
            for (int f = 0; f < 7; ++f) mapRange(mc, 102 + f, kAmt[f], f == 6 ? -1.0f : 0.0f, 1);
            mapRange(mc, 111, "m.rSize", 0, 5, 1); mapRange(mc, 112, "m.gdiv", 0, 3, 1); mapRange(mc, 113, "m.rNoise", 0, 1); mapRange(mc, 114, "m.spSize", 0, 1);
            mapRange(mc, 115, "m.throwDiv", 0, 9, 1); mapRange(mc, 116, "m.tapeSm", 0, 1); mapRange(mc, 117, "m.res", 0.7f, 15); ccMap[addrCC(mc, 117)].log = true;
            mapRange(mc, 95, "cb.amt", 0, 1); for (int n = 0; n < kCombos; ++n) keyAddr["cb." + std::to_string(n) + ".act"] = addrNote(mc, 64 + n);
            mapRange(mc, 96, "m.comb", 0, 1); mapRange(mc, 97, "m.sweep", 0, 1);
            mapRange(mc, 1, "m.fEnv", 0, 1); mapRange(mc, 2, "m.rDecay", 0, 1); mapRange(mc, 3, "m.rPitch", -1, 1); mapRange(mc, 4, "m.gShape", 0, 1); mapRange(mc, 5, "m.gPan", 0, 1); mapRange(mc, 8, "m.spDamp", 0, 1); mapRange(mc, 9, "m.throwFb", 0, 1); mapRange(mc, 10, "m.tTone", 0, 1);
            mapRange(mc, 11, "m.cbPitch", 24, 96, 1); mapRange(mc, 12, "m.cbFb", 0, 1); mapRange(mc, 13, "m.cbDamp", 0, 1); mapRange(mc, 14, "m.swRate", 0, 5, 1); mapRange(mc, 15, "m.swDepth", 0, 1); mapRange(mc, 47, "m.swFb", 0, 1); mapRange(mc, 63, "m.swMode", 0, 1, 1, 1); mapRange(mc, 79, "m.spFrz", 0, 1, 1, 1);
            for (int g = 0; g < NGRP; ++g) keyAddr["g" + std::to_string(g) + ".mute"] = addrNote(mc, 60 + g);
            for (int n = 0; n < 8; ++n) keyAddr["ms." + std::to_string(n) + ".on"] = addrNote(mc, 48 + n);
            keyAddr["solo.mode"] = addrNote(mc, 62);
        }
        dumpPending = true;
    }
    // valor que se devuelve a la controladora (0-127)
    int fbValue(const std::string& k, int addr) {
        const float v = st[k];
        if (addr >= 0x10000) return v >= 0.5f ? 127 : 0;
        const auto it = ccMap.find(addr); if (it == ccMap.end()) return -1;
        return static_cast<int>(std::lround(norm(it->second, v) * 127.0f));
    }
    void queueFb(const std::string& k) {
        if (st["midi.fb"] < 0.5f) return;
        const auto it = keyAddr.find(k); if (it == keyAddr.end()) return;
        const int a = it->second;
        // eco: con potenciómetros normales no se devuelve lo que acaba de llegar de ellos (temblaría)
        const int mode = static_cast<int>(st["midi.knob"]);
        if (mode <= 1 && a < 0x10000) { const auto t = inAt.find(a); if (t != inAt.end() && std::chrono::steady_clock::now() - t->second < std::chrono::milliseconds(300)) return; }
        const int v = fbValue(k, a); if (v >= 0) pendFb[a] = v;
    }
    void sendMidi(std::vector<unsigned char> m) { if (midiOut) midiOut(m); }
    // envía lo pendiente; como mucho 48 mensajes cada 15 ms (~3000/s: USB sin problema; el resto espera a la siguiente vuelta)
    void flushFb(bool force) {
        int sent = 0;
        for (auto it = pendFb.begin(); it != pendFb.end();) {
            const int a = it->first, v = it->second;
            if (!force) { const auto s = sentFb.find(a); if (s != sentFb.end() && s->second == v) { it = pendFb.erase(it); continue; } }
            if (sent >= 48) { ++it; continue; }
            ++sent; it = pendFb.erase(it); sentFb[a] = v; outAt_[a] = {v, std::chrono::steady_clock::now()};
            if (a >= 0x10000) { const int ch = (a - 0x10000) / 128, n = (a - 0x10000) % 128; sendMidi({static_cast<unsigned char>(0x90 | ch), static_cast<unsigned char>(n), static_cast<unsigned char>(v)}); }
            else sendMidi({static_cast<unsigned char>(0xB0 | (a / 128)), static_cast<unsigned char>(a % 128), static_cast<unsigned char>(v)});
        }
    }
    void dumpAll() {
        dumpPending = false; if (st["midi.fb"] < 0.5f) return;
        // controladora nueva: todo; cambio de motor, kit o pad: solo lo que cambia
        if (forceDump) sentFb.clear();
        for (auto& [k, a] : keyAddr) { const int v = fbValue(k, a); if (v >= 0) pendFb[a] = v; }
        flushFb(false); if (forceDump) ledInit = false; forceDump = false;
    }
    void toggle(const std::string& k) { if (st.count(k)) set(k, st[k] >= 0.5f ? 0.0f : 1.0f, kMidiSrc); }
    // Botones: pulsar y soltar enseguida = cambia y se queda (latch); mantener más de 0,4 s = solo mientras se mantiene.
    // Así funcionan igual los botones momentáneos y los que la controladora manda como interruptor (127 y luego 0).
    struct Held { std::chrono::steady_clock::time_point t; float prev; };
    std::map<int, Held> held_;
    void button(int addr, const std::string& k, bool down) {
        if (!st.count(k)) return;
        const auto now = std::chrono::steady_clock::now();
        if (down) { const float prev = st[k]; held_[addr] = {now, prev}; set(k, prev >= 0.5f ? 0.0f : 1.0f, kMidiSrc); return; }
        const auto it = held_.find(addr); if (it == held_.end()) return;
        if (now - it->second.t > std::chrono::milliseconds(400)) set(k, it->second.prev, kMidiSrc);
        held_.erase(it);
    }
    // eco: lo que acabamos de mandar y vuelve por un bucle MIDI (IAC, red, un thru) no se toma como un gesto
    std::map<int, std::pair<int, std::chrono::steady_clock::time_point>> outAt_;
    bool isEcho(int addr, int v) { const auto it = outAt_.find(addr); return it != outAt_.end() && it->second.first == v && std::chrono::steady_clock::now() - it->second.second < std::chrono::milliseconds(120); }
    // Mensajes de las controladoras (CC, cambios de programa y notas del canal del mezclador)
    // Akai LPD8: un programa por módulo, cada uno en su canal (ver CONTROLADORAS.md). Pads = notas 36-43, knobs = CC 70-77.
    //   canal 11 sampler: pads disparan los samples 1-8 y eligen el que editan los knobs
    //   canal 12 breaks: pads = los 8 efectos (mientras se pulsan)
    //   canal 13 granular: pad 1 enciende, pad 2 congela, pads 3-8 notas; canal 14 máster: pads = combinaciones, knobs 7-8 = voz
    std::vector<std::string> lpdBrk_; std::chrono::steady_clock::time_point lpdBrkAt_{};
    bool lpd8(const CtlMidi& m) {
        const int prog = (m.s & 0x0F) - 10, ty = m.s & 0xF0; if (prog < 0 || prog > 3) return false;
        const bool isNote = ty == 0x90 || ty == 0x80, down = ty == 0x90 && m.d2 > 0;
        const int pad = m.d1 - 36, knob = m.d1 - 70, val = isNote ? (down ? 127 : 0) : m.d2;
        if (isNote && (pad < 0 || pad > 7)) return true;
        if (!isNote && (ty != 0xB0 || knob < 0 || knob > 7)) return true;
        const int16_t port = static_cast<int16_t>(m.port + 1000);   // fuera de la lista de puertos: no se vuelve a traducir
        auto cc = [&](int ch, int c, int v) { if (ch >= 0) onCtl({static_cast<unsigned char>(0xB0 | ch), static_cast<unsigned char>(c), static_cast<unsigned char>(v), port}); };
        auto note = [&](int ch, int n) { if (ch >= 0) au.midi.push({static_cast<unsigned char>((down ? 0x90 : 0x80) | ch), static_cast<unsigned char>(n), static_cast<unsigned char>(down ? std::max(1, static_cast<int>(m.d2)) : 0), monoNow()}); };
        static const int kSamp[8] = {SA_TUNE, SA_START, SA_END, SA_ATT, SA_REL, SA_FILT, SA_RES, SA_LVL};
        static const int kBrk[8] = {-1, BK_TUNE, BK_GATE, BK_RAND, BK_STUT, BK_FILT, BK_RES, BK_RSIZE};   // knob 1: qué break
        static const int kGr[8] = {GR_POS, GR_SIZE, GR_DENS, GR_PITCH, GR_SPRAY, GR_SPREAD, GR_FB, GR_LVL};
        static const int kGrNote[6] = {60, 62, 63, 65, 67, 72};
        static const int kMst[5] = {108, 102, 103, 105, 106};   // filtro, repeat, gater, space, throw
        switch (prog) {
        case 0: { const int ch = chOf("samp.ch");
            if (isNote) { if (down && static_cast<int>(st["samp.sel"]) != pad) set("samp.sel", static_cast<float>(pad), kMidiSrc); }   // la nota la manda el hilo de MIDI
            else cc(ch, 16 + kSamp[knob], val);
            break; }
        case 1: { const int ch = chOf("brk.ch");
            if (isNote) cc(ch, 102 + pad, val);
            else if (knob == 0) {   // elegir break: los de fábrica y luego los de la carpeta breaks/ (la lista se relee cada 5 s como mucho)
                const auto now = std::chrono::steady_clock::now();
                if (lpdBrk_.empty() || now - lpdBrkAt_ > std::chrono::seconds(5)) {
                    lpdBrk_.clear(); lpdBrkAt_ = now;
                    for (size_t i = 0; i < facB.size(); ++i) lpdBrk_.push_back("fab:" + std::to_string(i));
                    for (const auto& f : listWavs(brkDir())) lpdBrk_.push_back(f);
                }
                if (!lpdBrk_.empty()) { const std::string& nm = lpdBrk_[std::min(lpdBrk_.size() - 1, static_cast<size_t>(val) * lpdBrk_.size() / 128)]; if (ss["brk.name"] != nm) sset("brk.name", nm, kMidiSrc); }
            } else cc(ch, 16 + kBrk[knob], val);
            break; }
        case 2: { const int ch = chOf("gran.ch");
            if (!isNote) cc(ch, 16 + kGr[knob], val); else if (pad < 2) cc(ch, 110 + pad, val);   // notas 3-8: las manda el hilo de MIDI
            break; }
        default:
            if (isNote) onMixNote(mixCh(), 64 + pad, val);
            else if (knob < 5) cc(mixCh(), kMst[knob], val);
            else if (knob < 7) cc(chOf("voz.ch"), 16 + (knob == 5 ? VZ_PITCH : VZ_FORMANT), val);
            else cc(mixCh(), 95, val);   // knob 8: crossfader de las combinaciones (seco → combinación)
        }
        return true;
    }
    void onCtl(const CtlMidi& m) {
        if (m.port >= 0 && m.port < 1000 && portHas(m.port, "lpd8") && lpd8(m)) return;
        const int ty = m.s & 0xF0; int ch = m.s & 0x0F;
        if (ty == 0xC0) { onProgram(ch, m.d1); return; }
        if (ty == 0x90 || ty == 0x80) { onMixNote(ch, m.d1, ty == 0x90 ? m.d2 : 0); return; }
        if (ty != 0xB0) return;
        int cc = m.d1; const int val = m.d2;
        // MC-24: cada fila controla un sinte, sea cual sea el canal de esa pista. Filas 1 a 3 = T2 a T4: los 7 primeros knobs
        // de la capa y Volume; el último knob de la fila 3 es el crossfader Dry–Wet
        if (cc >= 8 && cc <= 31 && portHas(m.port, "mc-24")) {
            const int r = (cc - 8) / 8, s = (cc - 8) % 8; ch = chOf("t" + std::to_string(r + 2) + ".ch"); if (ch < 0) return;
            cc = s < 7 ? 8 + s : r < 2 ? 23 : 15;
        }
        if (isEcho(addrCC(ch, cc), val)) return;
        if (cc == 110 && (ch == mixCh() || isSynthCh(ch))) { shift[ch] = val >= 64; return; }   // shift
        // CC 3 y 4 con el número tal cual: motor y preset de una pista, kit de la batería. Es lo que usan los instrumentos del Oxi.
        // Solo actúan si el número cambia: una línea de modulación los repite sin parar y no debe cortar notas ni pisar knobs.
        if ((cc == 3 || cc == 4) && (isSynthCh(ch) || ch == chOf("t1.ch"))) {
            if (ch == chOf("t1.ch")) { if (cc == 3 && val < 5 && static_cast<int>(st["d.kit"]) != val) set("d.kit", static_cast<float>(val), kMidiSrc); return; }
            for (int t = 1; t < NT; ++t) if (chOf("t" + std::to_string(t + 1) + ".ch") == ch) {
                const std::string E = "t" + std::to_string(t + 1) + ".eng";
                if (cc == 3) { if (val < E_COUNT && static_cast<int>(st[E]) != val) { set(E, static_cast<float>(val), kMidiSrc); lastPre_[ch] = -1; } }
                else if (val < 8 && lastPre_[ch] != val) { lastPre_[ch] = val; presetLoad(t, val, -1); }
            }
            return;
        }
        if (ch == mixCh() && cc == 109) { if (val >= 64) comboOff(); if (val >= 64) for (const char* k : {"m.rep", "m.gate", "m.riser", "m.space", "m.throw", "m.tape", "m.filt", "m.comb", "m.sweep", "m.spFrz"}) if (st[k] != 0) set(k, 0, kMidiSrc); return; }   // todo el máster a cero
        if (ch == chOf("brk.ch") && cc >= 102 && cc < 110) { au.cmds.push({K_LIVE, LM_BRK, 0, static_cast<int16_t>(kBrkHold0 + cc - 102), val >= 64 ? 1.0f : 0.0f}); return; }   // holds: mientras se pulsa
        if (ch == chOf("samp.ch") && cc == 111) { if (val >= 64) { const bool on = au.recState.load() == 0; if (on) { au.cmds.push({K_REC, 0, static_cast<int16_t>(st["rec.src"]), 0, 1}); srv.broadcast("rec 1"); } else au.cmds.push({K_REC, 0, 0, 0, 0}); } return; }
        const int a = addrCC(ch, cc); const auto it = ccMap.find(a); if (it == ccMap.end()) return;
        const Tgt& t = it->second;
        const int pa = a + (m.port + 1) * 0x100000;   // cada controladora lleva su propia posición: dos en el mismo CC no se pisan
        if (t.type == 1) { button(a, t.key, val >= 64); return; }
        const float cur = st[t.key], cn = norm(t, cur);
        // el Oxi es un secuenciador: manda valores exactos y a saltos, así que con él siempre es absoluto
        const int mode = portHas(m.port, "oxi") || m.port >= 1000 ? 0 : static_cast<int>(st["midi.knob"]);   // Oxi y LPD8 (port+1000): absoluto
        const bool stepped = t.step >= 1.0f && (t.max - t.min) / t.step <= 1024;
        float nv;
        if (mode >= 2) {   // encoders sin fin: relativo
            const int d = mode == 2 ? (val < 64 ? val : val - 128) : val - 64;
            if (d == 0) return;
            // un salto grande no es un encoder: esa controladora está mandando valores absolutos. Se ignora y se avisa
            if (std::abs(d) > 24) { const auto now = std::chrono::steady_clock::now(); if (now - lastWarn > std::chrono::seconds(3)) { lastWarn = now; srv.broadcast("knobwarn"); } return; }
            if (stepped) nv = std::clamp(cur + (d > 0 ? t.step : -t.step), t.min, t.max);
            else nv = denorm(t, cn + d / 200.0f);
        } else {
            const float x = val / 127.0f;
            if (mode == 1 && !stepped) {   // recogida: el knob no salta; empieza a mandar cuando pasa por el valor actual
                const auto li = lastIn.find(pa); const auto ls = lastSet.find(pa);
                // ya lo llevaba este knob y nadie lo ha movido desde entonces: sigue enganchado, por rápido que se gire
                const bool held = ls != lastSet.end() && std::fabs(norm(t, ls->second) - cn) < 0.002f;
                const bool caught = held || std::fabs(x - cn) <= 0.03f || (li != lastIn.end() && (li->second - cn) * (x - cn) <= 0.0f);
                lastIn[pa] = x;
                if (!caught) return;
            } else lastIn[pa] = x;
            nv = denorm(t, x); lastSet[pa] = nv;
        }
        inAt[a] = std::chrono::steady_clock::now();
        if (nv != cur) set(t.key, nv, kMidiSrc);
        if (mode >= 2) { const int v = fbValue(t.key, a); if (v >= 0) pendFb[a] = v; }   // anillos de LED al día
    }
    int lastPre_[16];
    static bool portHas(int port, const char* what) { std::lock_guard<std::mutex> lk(gMidiNamesMx); return port >= 0 && port < static_cast<int>(gMidiNames.size()) && lower(gMidiNames[static_cast<size_t>(port)]).find(what) != std::string::npos; }
    bool isSynthCh(int ch) const { for (int t = 1; t < NT; ++t) if (chOf("t" + std::to_string(t + 1) + ".ch") == ch) return true; return false; }
    void onProgram(int ch, int pc) {
        if (ch == chOf("t1.ch")) { if (pc < 5) set("d.kit", static_cast<float>(pc), kMidiSrc); return; }
        for (int t = 1; t < NT; ++t) {
            if (ch != chOf("t" + std::to_string(t + 1) + ".ch")) continue;
            if (pc < E_COUNT) set("t" + std::to_string(t + 1) + ".eng", static_cast<float>(pc), kMidiSrc);
            else if (pc >= 10 && pc < 18) { if (shift[ch]) presetSave(t, pc - 10); else presetLoad(t, pc - 10, -1); }
            return;
        }
        if (ch == mixCh() && pc < 8) { if (shift[ch]) snapSave(pc); else snapLoad(pc); }
    }
    void onMixNote(int ch, int n, int vel) {
        if (ch != mixCh()) return;
        if (isEcho(addrNote(ch, n), vel)) return;
        if (n == 127) { if (vel > 0) powerHeld = std::chrono::steady_clock::now(); else powerHeld = {}; return; }
        const bool down = vel > 0; const int a = addrNote(ch, n);
        if (n < NCH) button(a, "c" + std::to_string(n) + ".mute", down);
        else if (n >= 16 && n < 16 + NCH) button(a, "c" + std::to_string(n - 16) + ".solo", down);
        else if (n >= 32 && n < 32 + NCH) button(a, "c" + std::to_string(n - 32) + ".cue", down);
        else if (n >= 60 && n < 60 + NGRP) button(a, "g" + std::to_string(n - 60) + ".mute", down);
        else if (n == 62) { if (down) toggle("solo.mode"); }
        else if (n >= 48 && n < 56 && down) { if (shift[ch]) snapSave(n - 48); else snapLoad(n - 48); }
        else if (n >= 64 && n < 64 + kCombos) { if (down && shift[ch]) comboSave(n - 64); else if (!shift[ch]) comboPress(n - 64, down); }
    }
    // Luces de estado en el canal del mezclador (notas 96-104): grabando, reloj, saturación, CPU, MIDI perdido,
    // error al guardar, breaks, granular y voz encendidos
    float clipHold = 0; bool hadClock = false;
    void statusLeds(float dt) {
        const int mc = mixCh(); if (mc < 0 || st["midi.fb"] < 0.5f) return;
        clipHold = std::max(0.0f, clipHold - dt);
        const bool clk = au.clkPub.load(); if (clk) hadClock = true;
        const bool lost = hadClock && !clk && au.runPub.load();
        const int v[9] = {au.recState.load() != 0, clk, clipHold > 0, au.cpu.load() > 70.0f, lost, saveErr || audioStalled, st["brk.on"] >= 0.5f, st["gran.on"] >= 0.5f, st["voz.on"] >= 0.5f};
        for (int i = 0; i < 9; ++i) {
            if (ledInit && ledState[i] == v[i]) continue;
            ledState[i] = v[i]; sendMidi({static_cast<unsigned char>(0x90 | mc), static_cast<unsigned char>(96 + i), static_cast<unsigned char>(v[i] ? 127 : 0)});
        }
        ledInit = true;
    }
    int audioCheckN = 0; bool audioStalled = false, watchAudio = false; std::function<void()> onStall, onAudioCheck, onReopen, onNetCheck;   // onAudioCheck: cada 2 s (¿ha aparecido la interfaz elegida?)
    std::chrono::steady_clock::time_point lastFb{}, lastWd{}, stallSince = std::chrono::steady_clock::now(); uint64_t lastCalls = 0;
    void run(std::atomic<bool>& quit) {
        srv.onMessage = [this](int c, const std::string& m) { onMessage(c, m); };
        auto last = std::chrono::steady_clock::now();
        while (!quit) {
            srv.poll(10);
            Feedback f;
            while (au.fb.pop(f)) {
                const std::string k = f.t >= 10 ? (f.t == 10 ? "samp.p" + std::to_string(f.e) + "." + std::to_string(f.i) : std::string(kLiveKeys[f.t - 10]) + "." + std::to_string(f.i))
                                                : "t" + std::to_string(f.t + 1) + "." + kEng[f.e].key + "." + std::to_string(f.i);
                st[k] = f.v; dirty = true; dirtyAt = std::chrono::steady_clock::now(); srv.broadcast("set " + k + " " + jnum(f.v));
            }
            // controladoras MIDI
            { CtlMidi m; int n = 0; while (n++ < 512 && au.ctlq.pop(m)) onCtl(m); }
            if (mapDirty) buildMap();
            if (dumpPending) dumpAll();
            const auto now = std::chrono::steady_clock::now();
            if (now - lastFb > std::chrono::milliseconds(15)) { lastFb = now; flushFb(false); }
            if (cbDirty && now - lastCb > std::chrono::milliseconds(10)) { lastCb = now; comboApply(); }
            // apagado desde el mezclador: nota 127 pulsada 3 s (solo con --apagado-midi)
            if (powerOffEnabled && powerHeld.time_since_epoch().count() && !powerDown && now - powerHeld > std::chrono::seconds(3)) {
                powerDown = true; std::printf("Apagando (nota 127 en el canal del mezclador)...\n"); if (dirty) save();
                if (std::system("sudo -n /sbin/poweroff || systemctl poweroff") != 0) std::printf("No se pudo apagar.\n");
            }
            // vigilante: si el audio deja de llamar más de 1 s, se intenta reabrir
            if (now - lastWd > std::chrono::milliseconds(500)) {
                lastWd = now; if (++audioCheckN % 4 == 0) { if (onAudioCheck) onAudioCheck(); if (onNetCheck) onNetCheck(); }
                if (gReopen.exchange(false) && onReopen) { std::printf("Reabro el audio (aviso de cambio de wifi).\n"); std::fflush(stdout); onReopen(); } const uint64_t c = au.calls.load();
                const unsigned xr = au.xruns.load(); if (xr != lastXr) { std::printf("Aviso: %u cortes de audio (el procesador no llegó a tiempo)\n", xr - lastXr); std::fflush(stdout); lastXr = xr; }
                if (c != lastCalls) { lastCalls = c; stallSince = now; if (audioStalled) { audioStalled = false; std::printf("Audio de vuelta.\n"); } }
                else if (watchAudio && now - stallSince > std::chrono::seconds(1) && !audioStalled) { audioStalled = true; std::printf("Aviso: el audio se ha parado; intento reabrirlo.\n"); if (onStall) onStall(); stallSince = now; }
                else if (watchAudio && audioStalled && now - stallSince > std::chrono::seconds(3)) { if (onStall) onStall(); stallSince = now; }
            }
            if (now - last > std::chrono::milliseconds(60)) {
                const float dt = std::chrono::duration<float>(now - last).count();
                last = now;
                float pk[NCH + NGRP + 2]; float pmax = 0; { int i = 0; for (auto& p : au.peak) { pk[i] = p.exchange(0.0f); pmax = std::max(pmax, pk[i]); ++i; } }
                if (pmax >= 0.99f) clipHold = 1.0f;
                statusLeds(dt);
                if (srv.anyClient()) {
                    std::string o = "lvl";
                    for (float p : pk) o += " " + jnum(p);
                    o += " act"; for (auto& a : au.act) o += " " + std::to_string(a.exchange(0));
                    o += " dact"; for (auto& a : au.dact) o += " " + std::to_string(a.exchange(0));
                    o += " sact"; for (auto& a : au.sact) o += " " + std::to_string(a.exchange(0));
                    o += " bpm " + jnum(std::round(au.bpmPub.load() * 10) / 10) + " clk " + std::to_string(au.clkPub.load() ? 1 : 0) + " gr " + jnum(au.grDb.load()) + " bcgr " + jnum(au.bcGrDb.load()) + " cpu " + jnum(std::round(au.cpu.load() * 10) / 10);
                    srv.broadcast(o);
                    // directo: corte del break, cabeza y granos del granular, compresión de la voz, grabación
                    std::string l = "live " + std::to_string(au.brk.lastSlice.load()) + " " + jnum(au.gran.headFrac.load()) + " " + jnum(au.voz.grDb.load()) + " " +
                                    std::to_string(au.recState.load()) + " " + jnum(au.recLen.load() / au.sr) + " " + jnum(std::round(au.voz.f0.load() * 10) / 10) + " g";
                    for (auto& g : au.gran.gpos) { const float v = g.load(); if (v >= 0) l += " " + jnum(std::round(v * 1000) / 1000); }
                    l += " s"; for (auto& sp : au.gran.segPk) l += " " + jnum(std::round(sp.load() * 100) / 100);
                    srv.broadcast(l);
                }
                if (au.recState.load() == 2) finishRec();
                // monitor MIDI: puertos abiertos y último mensaje
                { const unsigned c = gMidiCount.load();
                  std::string ports; { std::lock_guard<std::mutex> lk(gMidiNamesMx); for (size_t i = 0; i < gMidiNames.size(); ++i) { if (i) ports += " | "; ports += gMidiNames[i]; } }
                  if (c != midiSeen_ || ports != midiPorts_) {
                      midiSeen_ = c; midiPorts_ = ports; const uint32_t v = gMidiLast.load(); const int pi = gMidiPort.load();
                      std::string pn; { std::lock_guard<std::mutex> lk(gMidiNamesMx); if (pi >= 0 && pi < static_cast<int>(gMidiNames.size())) pn = gMidiNames[static_cast<size_t>(pi)]; }
                      srv.broadcast("midimon " + std::to_string(c) + " " + std::to_string(v >> 16) + " " + std::to_string((v >> 8) & 0xFF) + " " + std::to_string(v & 0xFF) + " " + pn + "\t" + ports);
                  } }
            }
            if (!webPend_.empty() && now - lastWeb > std::chrono::milliseconds(40)) { lastWeb = now; for (auto& [k, v] : webPend_) srv.broadcast("set " + k + " " + jnum(v), kMidiSrc); webPend_.clear(); }
            // se guarda tras 800 ms de calma; con CC moviéndose sin parar, como mucho cada minuto: escribir sin parar desgasta la tarjeta y frena el sistema
            if (dirty && !wasDirty) dirtySince = now;
            wasDirty = dirty;
            if (dirty && (now - dirtyAt > std::chrono::milliseconds(800) || now - dirtySince > std::chrono::seconds(60))) { save(); wasDirty = dirty; }
            if (tick && now - lastTick > std::chrono::seconds(2)) { lastTick = now; tick(); }
        }
        if (dirty) save();
    }
};

// ------------------------------------------------------------------ utilidades de línea de órdenes
int argInt(int argc, char** argv, const char* name, int def) { for (int i = 1; i + 1 < argc; ++i) if (!std::strcmp(argv[i], name)) return std::atoi(argv[i + 1]); return def; }
std::string argStr(int argc, char** argv, const char* name, const std::string& def = "") { for (int i = 1; i + 1 < argc; ++i) if (!std::strcmp(argv[i], name)) return argv[i + 1]; return def; }
bool hasArg(int argc, char** argv, const char* name) { for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], name)) return true; return false; }
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

void listDevices() {
    RtAudio audio;
    std::printf("\n== Interfaces de audio ==\n");
    const unsigned def = audio.getDefaultOutputDevice();
    for (unsigned id : audio.getDeviceIds()) { auto info = audio.getDeviceInfo(id); if (info.outputChannels) std::printf("  --audio %-4u %s  (%u salidas)%s\n", id, info.name.c_str(), info.outputChannels, id == def ? "  <- por defecto" : ""); }
    std::printf("\n== Entradas de audio ==\n");
    const unsigned defIn = audio.getDefaultInputDevice();
    for (unsigned id : audio.getDeviceIds()) { auto info = audio.getDeviceInfo(id); if (info.inputChannels) std::printf("  --entrada %-4u %s  (%u entradas)%s\n", id, info.name.c_str(), info.inputChannels, id == defIn ? "  <- por defecto" : ""); }
    try { RtMidiIn m; std::printf("\n== Entradas MIDI ==\n"); for (unsigned i = 0; i < m.getPortCount(); ++i) std::printf("  --midi %-4u %s\n", i, m.getPortName(i).c_str()); if (!m.getPortCount()) std::printf("  (ninguna: conecta el Oxi por USB)\n"); }
    catch (RtMidiError& e) { std::printf("MIDI no disponible: %s\n", e.getMessage().c_str()); }
    std::printf("\n");
}

void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return;
    const uint32_t n = static_cast<uint32_t>(L.size()), data = n * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2); u32(static_cast<uint32_t>(sr)); u32(static_cast<uint32_t>(sr) * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(data);
    for (uint32_t i = 0; i < n; ++i) for (float x : {L[i], R[i]}) { const int16_t v = static_cast<int16_t>(std::lrint(std::clamp(x, -1.0f, 1.0f) * 32767.0f)); std::fwrite(&v, 2, 1, f); }
    std::fclose(f);
}

// Prueba sin tarjeta: recorre los cinco kits y los cinco motores en las tres pistas y mide el consumo
int gNuc = 1;
// --- instrumentos para el Oxi: se generan desde las mismas tablas que usa el motor, así nunca se desincronizan
struct OxiP { std::string name, abbr; int cc, lo, hi, def; bool centered; };
std::string oxiAscii(const std::string& in) {
    std::string o; for (size_t i = 0; i < in.size(); ++i) { const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 128) { if (std::isalnum(c) || c == ' ' || c == '-' || c == '/' || c == '.' || c == '+') o += static_cast<char>(c); }
        else if (c == 0xE2) { o += '>'; i += 2; } else if (c == 0xC3 && i + 1 < in.size()) { const unsigned char d = static_cast<unsigned char>(in[++i]); o += d >= 0xA0 && d <= 0xA5 ? 'a' : d >= 0xA8 && d <= 0xAB ? 'e' : d >= 0xAC && d <= 0xAF ? 'i' : d >= 0xB2 && d <= 0xB6 ? 'o' : d >= 0xB9 && d <= 0xBC ? 'u' : d == 0xB1 ? 'n' : '_'; } }
    return o.substr(0, 16);
}
std::string oxiAbbr(const std::string& name, std::vector<std::string>& used) {   // una palabra: sus 4 primeras letras; dos: 2 + 2
    std::vector<std::string> w(1); for (char ch : name) { const unsigned char c = static_cast<unsigned char>(ch); if (std::isalnum(c)) w.back() += static_cast<char>(std::toupper(c)); else if (!w.back().empty()) w.emplace_back(); }
    if (w.back().empty()) w.pop_back();
    std::string a = w.empty() ? "PARM" : w.size() == 1 ? w[0].substr(0, 4) : w[0].substr(0, 2) + w[1].substr(0, 2);
    for (int n = 2; std::find(used.begin(), used.end(), a) != used.end(); ++n) a = a.substr(0, 3) + std::to_string(n % 10);
    used.push_back(a); return a;
}
void oxiWrite(const std::string& dir, const std::string& title, const std::string& abbr, const std::vector<OxiP>& ps) {
    std::string j = "{\"id\":\"\",\"name\":\"" + title + "\",\"abbr\":\"" + abbr + "\",\"manufacturer\":\"Freon Machine\",\"parameters\":[";
    std::string c = "manufacturer,device,section,parameter_name,parameter_description,cc_msb,cc_lsb,cc_min_value,cc_max_value,cc_default_value,nrpn_msb,nrpn_lsb,nrpn_min_value,nrpn_max_value,nrpn_default_value,orientation,notes,usage\n";
    for (size_t i = 0; i < ps.size(); ++i) { const OxiP& p = ps[i]; if (i) j += ",";
        j += "{\"type\":\"cc\",\"name\":\"" + p.name + "\",\"abbr\":\"" + p.abbr + "\",\"minimum\":" + std::to_string(p.lo) + ",\"maximum\":" + std::to_string(p.hi) + ",\"default_value\":" + std::to_string(p.def) + ",\"nr1\":" + std::to_string(p.cc) + ",\"nr2\":0,\"value_labels\":[]}";
        c += "Freon Machine," + title + ",," + p.name + ",," + std::to_string(p.cc) + ",," + std::to_string(p.lo) + "," + std::to_string(p.hi) + "," + std::to_string(p.def) + ",,,,,," + (p.centered ? "centered" : "0-based") + ",,\n"; }
    j += "],\"script\":null}";
    std::filesystem::create_directories(dir + "/oxiindef"); std::filesystem::create_directories(dir + "/csv");
    std::ofstream(dir + "/oxiindef/" + title + ".oxiindef") << j; std::ofstream(dir + "/csv/" + title + ".csv") << c;
}
int oxiExport(const std::string& dir) {
    const auto def127 = [](const PSpec& p) { float x = p.log && p.min > 0 ? std::log(p.def / p.min) / std::log(p.max / p.min) : (p.def - p.min) / std::max(1e-6f, p.max - p.min); return static_cast<int>(std::lround(std::clamp(x, 0.0f, 1.0f) * 127.0f)); };
    int files = 0;
    // sinte genérico: sirve para cualquier motor, porque la capa de 16 knobs tiene el mismo orden en todos
    { static const char* L[16] = {"Cutoff", "Resonance", "Filter env", "Macro A", "Macro B", "Macro C", "Macro D", "Volume", "Filter attack", "Filter decay", "Filter sustain", "Filter release", "Amp attack", "Amp decay", "Amp sustain", "Amp release"};
      std::vector<std::string> u; std::vector<OxiP> ps; ps.push_back({"Engine", "ENGN", 3, 0, E_COUNT - 1, 0, false}); ps.push_back({"Preset", "PRST", 4, 0, 7, 0, false}); u = {"ENGN", "PRST"};
      for (int k = 0; k < 16; ++k) ps.push_back({L[k], oxiAbbr(L[k], u), 16 + k, 0, 127, 64, false});
      oxiWrite(dir, "Freon Synth", "FSYN", ps); ++files; }
    for (int e = 0; e < E_COUNT; ++e) {
        std::vector<std::string> u = {"ENGN", "PRST"}; std::vector<OxiP> ps; ps.push_back({"Engine", "ENGN", 3, 0, E_COUNT - 1, e, false}); ps.push_back({"Preset", "PRST", 4, 0, 7, 0, false});
        const auto add = [&](int cc, int i) { const PSpec& p = kEng[e].p[i]; const std::string n = oxiAscii(p.label); ps.push_back({n, oxiAbbr(n, u), cc, 0, 127, def127(p), p.min < 0 && p.max > 0}); };
        for (int s = 0; s < 16; ++s) { const int i = kLayer[e][s]; if (i >= 0) add(16 + s, i); }
        const auto p2 = page2(e); for (size_t s = 0; s < p2.size() && s < 24; ++s) add(40 + static_cast<int>(s), p2[s]);
        for (int b = 0; b < kEng[e].nb && b < 8; ++b) { const std::string n = oxiAscii(kEng[e].b[b].label); ps.push_back({n, oxiAbbr(n, u), 102 + b, 0, 127, kEng[e].b[b].def ? 127 : 0, false}); }
        oxiWrite(dir, std::string("Freon ") + oxiAscii(kEng[e].name), "F" + oxiAscii(kEng[e].key).substr(0, 3), ps); ++files;
    }
    // batería: mismas columnas que en la pantalla
    static const char* kSnd[8] = {"Kick", "Snare", "HH closed", "HH open", "Clap", "Rim", "Tom", "Perc"}; static const char kPre[9] = "KSCOLRTP";
    struct KitDef { const char* title; const char* abbr; int uiKit; }; static const KitDef kKits[3] = {{"Freon Drums", "FDRM", 0}, {"Freon Drums FM", "FDFM", 2}, {"Freon Drums Chip", "FDCH", 3}};
    const auto lbl = [](const std::string& k, bool fm) -> std::pair<std::string, std::string> {
        static const std::map<std::string, std::pair<std::string, std::string>> A = {{"level", {"Level", "LVL"}}, {"tune", {"Pitch", "PIT"}}, {"decay", {"Decay", "DEC"}}, {"tone", {"Tone", "TON"}}, {"snap", {"Snap", "SNP"}}, {"sweep", {"Sweep", "SWP"}}, {"filt", {"Filter", "FLT"}}, {"reso", {"Reso", "RES"}}, {"vel", {"Velocity", "VEL"}}, {"color", {"FM", "FMA"}}, {"shape", {"Ratio", "RAT"}}, {"contour", {"FM decay", "FMD"}}, {"pan", {"Pan", "PAN"}}};
        if (fm && k == "snap") return {"Noise", "NOI"}; return A.at(k); };
    for (const auto& kit : kKits) {
        std::vector<OxiP> ps; ps.push_back({"Kit", "KIT", 3, 0, 4, kit.uiKit, false}); const char* extra = nullptr; const char* const* sl = drumSlots(kit.uiKit, extra); const bool fm = kit.uiKit == 2;
        for (int c = 0; c < 8; ++c) {
            const auto add = [&](const std::string& k, int cc) { const auto l = lbl(k, fm); std::string a = std::string(1, kPre[c]) + l.second; const bool ctr = k == "tune" || k == "filt" || k == "pan"; const int d = k == "level" || k == "vel" ? 102 : k == "reso" ? 25 : 64; ps.push_back({oxiAscii(std::string(kSnd[c]) + " " + l.first), a, cc, 0, 127, d, ctr}); };
            for (int k = 0; k < 8; ++k) add(sl[k], 16 + k * 8 + c);
            add("pan", 80 + c); if (extra) add(extra, 88 + c); if (fm) { add("snap", 96 + c); add("vel", 112 + c); }
        }
        oxiWrite(dir, kit.title, kit.abbr, ps); ++files;
    }
    {   // mezclador (canal 16)
        std::vector<std::string> u; std::vector<OxiP> ps; const auto add = [&](const std::string& n, int cc, int d, bool c = false) { const std::string a = oxiAscii(n); ps.push_back({a, oxiAbbr(a, u), cc, 0, 127, d, c}); };
        add("Master", 31, 108); add("Combo Dry-Wet", 95, 127);
        static const char* F[9] = {"Repeat", "Gater", "Riser", "Space", "Throw", "Tape stop", "Filter"}; for (int f = 0; f < 7; ++f) add(F[f], 102 + f, f == 6 ? 64 : 0, f == 6);
        add("Comb", 96, 0); add("Sweep", 97, 0); add("FX all off", 109, 0);
        for (int i = 0; i < 11; ++i) add(std::string(kChNames[i]) + " lvl", 16 + i, 102);
        for (int i = 0; i < 7; ++i) add(std::string(kChNames[i]) + " send A", 48 + i, 0);
        for (int i = 0; i < 7; ++i) add(std::string(kChNames[i]) + " send B", 64 + i, 0);
        oxiWrite(dir, "Freon Mixer", "FMIX", ps); ++files;
    }
    std::printf("%d instrumentos escritos en %s\n", files, dir.c_str());
    return 0;
}
// Nivel de cada motor y preset: nota sola y acorde de tres, velocidad 100, ya con el ajuste de nivel del motor
int levelsTest() {
    const float sr = 48000; dx7::libInit("");
    // sonoridad: ponderación K (como un medidor LUFS) y el mayor valor de una ventana de 400 ms
    const auto measure = [&](int e, int preset, const std::vector<int>& notes, double& lu, float& pk) {
        auto m = std::make_unique<MeloEngines>(); m->prepare(sr); m->select(e);
        for (int i = 0; i < kEng[e].np; ++i) m->set(e, i, kEng[e].p[i].def);
        for (int i = 0; i < kEng[e].nb; ++i) m->set(e, kBtn0 + i, kEng[e].b[i].def ? 1.0f : 0.0f);
        if (preset >= 0) for (auto& [i, v] : factoryPresets(e)[static_cast<size_t>(preset)].v) m->set(e, i, v);
        float L[256], R[256], mono[256]; for (int n : notes) m->noteOn(n, 100);
        Shelf sh[2]; BQ hp[2]; for (int c = 0; c < 2; ++c) { sh[c].set(1, 1500, 4, 0.7f, sr); hp[c].set(1, 60, 0.7f, sr); }
        std::vector<double> win; pk = 0;
        for (int b = 0; b < static_cast<int>(sr * 1.6f / 256); ++b) {
            m->render(L, R, mono, 256); double acc = 0;
            for (int i = 0; i < 256; ++i) { pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])}); const float a = hp[0].process(sh[0].process(L[i])), c = hp[1].process(sh[1].process(R[i])); acc += a * a + c * c; }
            win.push_back(acc / 512.0);
        }
        double best = 0, cur = 0; const size_t W = static_cast<size_t>(0.4f * sr / 256);
        for (size_t k = 0; k < win.size(); ++k) { cur += win[k]; if (k >= W) cur -= win[k - W]; if (k + 1 >= W) best = std::max(best, cur / static_cast<double>(W)); }
        lu = 10 * std::log10(best + 1e-12);
    };
    for (int e = 0; e < E_COUNT; ++e) {
        const auto& fp = factoryPresets(e); const bool mono = e == E_MOOG || e == E_ACID || e == E_GB; double sum = 0; int cnt = 0;
        for (int p = 0; p < static_cast<int>(fp.size()); ++p) {
            if (!fp[static_cast<size_t>(p)].name[0]) continue;
            double l; float pk; if (mono) measure(e, p, {45}, l, pk); else measure(e, p, {57, 60, 64}, l, pk);
            sum += l; ++cnt; std::printf("%-10s %-16s %6.1f dB pico %.2f\n", kEng[e].name, fp[static_cast<size_t>(p)].name, l, pk);
        }
        std::printf("%-10s MEDIA: %.1f dB\n\n", kEng[e].name, sum / std::max(cnt, 1));
    }
    // batería: un golpe a velocidad 110, ventana de 100 ms
    static const char* KN[5] = {"808", "909", "FM", "Chip", "Exp"}; static const int KI[5] = {KIT_P808, KIT_P909, KIT_CYC, KIT_GB, KIT_EXP}; static const char* SN[8] = {"Bombo", "Caja", "Clap", "Rim", "Tom", "HH cer", "HH abi", "Perc"};
    std::printf("%-6s", ""); for (auto* n : KN) std::printf("%14s", n); std::printf("\n");
    for (int l = 0; l < 8; ++l) {
        std::printf("%-6s", SN[l]);
        for (int k = 0; k < 5; ++k) {
            DrumMachine d; d.prepare(sr); d.kit = KI[k]; d.hit(l, 110);
            float b[8][256]; float* o[8]; for (int i = 0; i < 8; ++i) o[i] = b[i];
            Shelf sh; BQ hp; sh.set(1, 1500, 4, 0.7f, sr); hp.set(1, 60, 0.7f, sr);
            std::vector<double> win; float pk = 0;
            for (int q = 0; q < static_cast<int>(sr * 0.8f / 256); ++q) { d.render(o, 256); double acc = 0; for (int i = 0; i < 256; ++i) { pk = std::max(pk, std::fabs(b[l][i])); const float a = hp.process(sh.process(b[l][i])); acc += a * a; } win.push_back(acc / 256.0); }
            double best = 0, cur = 0; const size_t W = static_cast<size_t>(0.1f * sr / 256);
            for (size_t i = 0; i < win.size(); ++i) { cur += win[i]; if (i >= W) cur -= win[i - W]; best = std::max(best, cur / static_cast<double>(W)); }
            std::printf("  %6.1f (%.2f)", 10 * std::log10(best + 1e-12), pk);
        }
        std::printf("\n");
    }
    return 0;
}
// Prueba de estrés: el peor caso que se puede tocar, con los efectos moviéndose al azar. Sirve para medir la Pi.
int stressTest(int seconds) {
    auto au = std::make_unique<Audio>(); au->prepare(48000); const int got = au->par.start(gNuc - 1, 80);
#if defined(__linux__)
    {   // en la Pi se mide como en el directo: con tiempo real (hace falta sudo o el grupo audio)
        sched_param sp{}; sp.sched_priority = 80;
        const bool rt = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
        std::printf("Tiempo real: %s\n", rt ? "sí" : "no (el pico de reloj no será fiable)");
    }
#endif
    Control ctl(*au); ctl.initLive(); ctl.defaults(); ctl.defs = ctl.st; ctl.pushAll();
    constexpr int BUF = 128; const int sr = 48000; const double budget = BUF / static_cast<double>(sr);
    for (int t = 1; t < NT; ++t) ctl.set("t" + std::to_string(t + 1) + ".eng", E_JUNO, -1);
    const int types[NSEND] = {FX_HALL, FX_PLATE, FX_DARK, FX_TAPE};
    for (int f = 0; f < NSEND; ++f) { ctl.set("fx" + std::to_string(f) + ".type", static_cast<float>(types[f]), -1); for (int c = 0; c < 7; ++c) ctl.set("c" + std::to_string(c) + ".s" + std::to_string(f), 0.3f, -1); }
    ctl.set("m.bcomp", 0.5f, -1); ctl.set("brk.on", 1, -1); ctl.set("gran.on", 1, -1);
    std::vector<std::string> keys; for (auto& [k, v] : ctl.st) if (Control::isFxKey(k)) keys.push_back(k);
    std::vector<float> ib(BUF * 2); uint32_t rng = 12345; const auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
    const auto note = [&](int t, int n, int v) { au->cmds.push({K_NOTE, static_cast<int16_t>(t), static_cast<int16_t>(n), static_cast<int16_t>(v), 0}); };
    const auto pick = [&]() { for (const auto& k : keys) { float lo, hi; ctl.rangeOf(k, lo, hi); ctl.set(k, lo + (hi - lo) * (rnd() % 1000) / 999.0f, -1); } };
    pick(); ctl.comboSave(0); pick(); ctl.comboSave(1); ctl.comboOn(0);
    // reloj de CPU del hilo: lo que de verdad cuesta calcular, sin contar cuando el sistema le quita el procesador
    const auto threadCpu = []() { timespec ts{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return static_cast<double>(ts.tv_sec) + ts.tv_nsec * 1e-9; };
    double busy = 0, peak = 0, cpuPk = 0; long blocks = 0, over = 0, bad = 0; float top = 0; const long total = static_cast<long>(seconds) * sr / BUF;
    for (long b = 0; b < total; ++b) {
        if (b % 450 == 0) { const int root = 45 + static_cast<int>(rnd() % 12); for (int t = 1; t < NT; ++t) { for (int n = 30; n < 90; ++n) note(t, n, 0); for (int v = 0; v < 6; ++v) note(t, root + v * 3 + t, 100); } }
        if (b % 90 == 0) { note(0, 36 + static_cast<int>(rnd() % 21), 120); note(0, 42, 90); }
        if (b % 3 == 0) ctl.set("cb.amt", 0.5f + 0.5f * std::sin(b * 0.002f), -1);
        if (ctl.cbDirty) ctl.comboApply();
        if (b % 1500 == 0) ctl.comboOn(static_cast<int>(b / 1500 % 2));
        if (b % 40 == 0) { const auto& k = keys[rnd() % keys.size()]; float lo, hi; ctl.rangeOf(k, lo, hi); ctl.set(k, lo + (hi - lo) * (rnd() % 1000) / 999.0f, -1); }
        const double c0 = threadCpu(); const auto t0 = std::chrono::steady_clock::now();
        au->process(ib.data(), 2, nullptr, 0, BUF);
        const double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        busy += d; peak = std::max(peak, d); ++blocks; if (d > budget) ++over;
        cpuPk = std::max(cpuPk, threadCpu() - c0);
        for (float x : ib) { if (!std::isfinite(x)) ++bad; else top = std::max(top, std::fabs(x)); }
    }
    std::printf("Estrés %d s (buffer %d, %d núcleos en uso):\n  media %.1f %% · pico de reloj %.1f %% · pico de cálculo %.1f %% · bloques fuera de tiempo %ld de %ld\n  salida máxima %.3f · muestras rotas %ld\n",
                seconds, BUF, 1 + got, busy / blocks / budget * 100.0, peak / budget * 100.0, cpuPk / budget * 100.0, over, blocks, top, bad);
    return bad ? 1 : 0;
}
int renderTest(const std::string& path) {
    auto au = std::make_unique<Audio>(); au->prepare(48000); au->par.start(gNuc - 1, 0);
    Control ctl(*au); ctl.initLive(); ctl.defaults(); ctl.defs = ctl.st; ctl.pushAll();
    const int sr = 48000; const double bpm = 124, step = sr * 60.0 / bpm / 4;
    std::vector<float> L, R; std::vector<float> ib(512);
    const int Ch[4] = {57, 60, 64, 67}, Bass[8] = {33, 33, 45, 33, 36, 33, 40, 43};
    double busy = 0; long blocks = 0;
    auto run = [&](double steps, std::function<void(int)> at) {
        for (int s = 0; s < static_cast<int>(steps); ++s) {
            at(s);
            for (int done = 0; done < static_cast<int>(step); done += 256) {
                const int n = std::min(256, static_cast<int>(step) - done);
                const auto t0 = std::chrono::steady_clock::now();
                au->process(ib.data(), 2, nullptr, 0, n);
                busy += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); ++blocks;
                for (int i = 0; i < n; ++i) { L.push_back(ib[static_cast<size_t>(2 * i)]); R.push_back(ib[static_cast<size_t>(2 * i + 1)]); }
            }
        }
    };
    auto note = [&](int t, int n, int v) { au->cmds.push({K_NOTE, static_cast<int16_t>(t), static_cast<int16_t>(n), static_cast<int16_t>(v), 0}); };
    for (int kit = 0; kit < KIT_COUNT; ++kit) {
        au->cmds.push({K_KIT, 0, 0, 0, static_cast<float>(kit)});
        au->cmds.push({K_ENG, 1, 0, 0, static_cast<float>(kit)});
        au->cmds.push({K_ENG, 2, 0, 0, static_cast<float>((kit + 3) % 5)});
        au->cmds.push({K_ENG, 3, 0, 0, static_cast<float>((kit + 2) % 5)});
        run(32, [&](int s) {
            const int D[8] = {36, 38, 39, 37, 45, 42, 46, 56};
            if (s % 4 == 0) note(0, D[0], 120);
            if (s % 8 == 4) note(0, D[1], 110);
            if (s % 2 == 0) note(0, D[5], 80);
            if (s % 8 == 6) note(0, D[6], 90);
            if (s % 16 == 11) note(0, D[2], 100);
            if (s % 16 == 7) note(0, D[3], 90);
            if (s % 16 == 13) note(0, D[4], 100);
            if (s % 16 == 15) note(0, D[7], 90);
            if (s % 16 == 0) for (int n : Ch) note(1, n, 90);
            if (s % 16 == 12) for (int n : Ch) note(1, n, 0);
            const int b = Bass[s % 8]; note(2, b + 12, s % 4 == 0 ? 120 : 90);
            if (s % 2 == 1) note(2, Bass[(s - 1) % 8] + 12, 0);
            note(2, b + 12, 0);
            if (s % 8 == 0) note(3, 69 + (s / 8) % 3 * 2, 100);
            if (s % 8 == 6) note(3, 69 + (s / 8) % 3 * 2, 0);
        });
        std::printf("  kit %d listo\n", kit);
    }
    au->cmds.push({K_ALLOFF, 0, 0, 0, 0}); run(16, [](int) {});
    writeWav(path, L, R, sr);
    const double secs = L.size() / static_cast<double>(sr);
    std::printf("Escrito %s (%.1f s). Consumo medio: %.2f %% de un núcleo\n", path.c_str(), secs, busy / secs * 100.0);
    return 0;
}

// Entradas MIDI: todas a la vez. El reloj solo se acepta de una (la primera que lo mande).
struct MidiHub {
    Audio* au = nullptr; std::mutex mx; int only = -1; const void* clockOwner = nullptr; double clockAge = 1e9;
    std::function<void(const std::string&)> onPortsChanged;   // aviso al hilo de control (estado completo a las controladoras nuevas)
    struct Port { std::unique_ptr<RtMidiIn> in; std::string name; MidiHub* hub = nullptr; double acc = 0; ClockEst est; int idx = 0; bool lpd = false; };
    struct Out { std::unique_ptr<RtMidiOut> out; std::string name; };
    std::vector<std::unique_ptr<Port>> ports; std::vector<std::unique_ptr<Out>> outs; int nextIdx = 0;
    static void cb(double dt, std::vector<unsigned char>* m, void* u) {
        if (!m || m->empty()) return;
        auto* p = static_cast<Port*>(u); MidiHub& h = *p->hub; const unsigned char st = (*m)[0];
        const double t = monoNow();
        std::lock_guard<std::mutex> lk(h.mx);
        h.clockAge += dt;
        if ((st >= 0xF8 && st <= 0xFC) || st == 0xF2) {
            if (h.clockOwner != p && h.clockAge < 1.0) return;   // otro puerto ya manda el reloj
            h.clockOwner = p; h.clockAge = 0;
        }
        p->acc += dt;
        if (st == 0xF8) {
            const float b = p->est.tick(p->acc); if (b > 0) h.au->clkBpm = b;
            p->acc = 0;
        }
        const unsigned char d1 = m->size() > 1 ? (*m)[1] : 0, d2 = m->size() > 2 ? (*m)[2] : 0, ty = st & 0xF0;
        // Akai LPD8 (canales 11 a 14): todo, notas incluidas, lo traduce el hilo de control (Control::lpd8)
        if (p->lpd && (st & 0x0F) >= 10 && (st & 0x0F) <= 13 && (ty == 0x90 || ty == 0x80 || ty == 0xB0)) {
            const int prog = (st & 0x0F) - 10, pad = d1 - 36; const bool note = ty == 0x90 || ty == 0x80, down = ty == 0x90 && d2 > 0;
            static const int kGrNote[6] = {60, 62, 63, 65, 67, 72};
            const int sch = gLpdSamp.load(std::memory_order_relaxed), gch = gLpdGran.load(std::memory_order_relaxed);
            auto out = [&](int ch, int n) { h.au->pushMidi({static_cast<unsigned char>((down ? 0x90 : 0x80) | ch), static_cast<unsigned char>(n), static_cast<unsigned char>(down ? std::max<int>(1, d2) : 0), t}); };
            if (note && pad >= 0 && pad < 8 && prog == 0 && sch >= 0) out(sch, 36 + pad);                 // sampler: suena ya; el hilo de control solo elige el pad
            if (note && pad >= 2 && pad < 8 && prog == 2) { if (gch >= 0) out(gch, kGrNote[pad - 2]); }   // granular: notas
            else h.au->ctlq.push({st, d1, d2, static_cast<int16_t>(p->idx)});
        }
        else if (ty == 0xB0 || ty == 0xC0) {
            h.au->ctlq.push({st, d1, d2, static_cast<int16_t>(p->idx)});   // knobs, botones y cambios de programa: hilo de control
            if (ty == 0xB0 && (d1 == 120 || d1 == 123)) h.au->pushMidi({st, d1, d2, t});
        } else if ((ty == 0x90 || ty == 0x80) && (st & 0x0F) == gMixCh.load(std::memory_order_relaxed)) h.au->ctlq.push({st, d1, d2, static_cast<int16_t>(p->idx)});
        else h.au->pushMidi({st, d1, d2, t});
        if (st < 0xF8) { gMidiLast = static_cast<uint32_t>(st) << 16 | static_cast<uint32_t>(d1) << 8 | d2; gMidiPort = p->idx; ++gMidiCount; }
    }
    void publishNames() {
        std::lock_guard<std::mutex> lk(gMidiNamesMx); gMidiNames.clear();
        for (auto& p : ports) { if (static_cast<int>(gMidiNames.size()) <= p->idx) gMidiNames.resize(static_cast<size_t>(p->idx) + 1); gMidiNames[static_cast<size_t>(p->idx)] = p->name; }
    }
    // Busca puertos nuevos y cierra los que han desaparecido (cable suelto): al volver, se reabren solos
    // En Linux, los puertos que abre el propio programa también aparecen en la lista. Si se abrieran, cada búsqueda
    // crearía más y Freon acabaría conectada a sí misma sin parar. Solo se abren aparatos de verdad.
    static bool notADevice(const std::string& nm) { const std::string l = lower(nm); return l.find("through") != std::string::npos || l.find("rtmidi") != std::string::npos; }
    // nombre estable de un puerto: en Linux acaba en « 24:0», un número que cambia entre arranques
    static std::string portKey(std::string nm) { const size_t sp = nm.rfind(' '); if (sp != std::string::npos && nm.find(':', sp) != std::string::npos && std::isdigit(static_cast<unsigned char>(nm.back()))) nm.resize(sp); return nm; }
    std::vector<std::string> ignored;   // puertos desactivados en la pantalla de sistema
    bool isIgnored(const std::string& nm) const { return std::find(ignored.begin(), ignored.end(), portKey(nm)) != ignored.end(); }
    void scan(bool first) {
        bool changed = false;
        try {
            RtMidiIn probe; const unsigned n = probe.getPortCount();
            if (first && n == 0) std::printf("MIDI: no hay entradas por ahora (se buscan solas cada 2 s). Puedes tocar con el teclado del ordenador.\n");
            std::vector<std::string> now; for (unsigned i = 0; i < n; ++i) now.push_back(probe.getPortName(i));
            // fuera los que ya no están
            for (size_t k = 0; k < ports.size();) {
                if (std::find(now.begin(), now.end(), ports[k]->name) != now.end()) { ++k; continue; }
                std::printf("MIDI desconectado: %s\n", ports[k]->name.c_str());
                ports[k]->in->cancelCallback();
                { std::lock_guard<std::mutex> lk(mx); if (clockOwner == ports[k].get()) { clockOwner = nullptr; clockAge = 1e9; } }
                ports.erase(ports.begin() + static_cast<long>(k)); changed = true;
                au->cmds.push({K_ALLOFF, 0, 0, 0, 0});   // las notas de ese aparato no van a recibir su note off
            }
            for (unsigned i = 0; i < n; ++i) {
                const std::string& nm = now[i];
                if (only >= 0 && static_cast<int>(i) != only) continue;
                if (notADevice(nm) || isIgnored(nm)) continue;
                bool have = false; for (auto& p : ports) have |= p->name == nm;
                if (have) continue;
                auto p = std::make_unique<Port>(); p->name = nm; p->hub = this; p->idx = nextIdx++; p->lpd = lower(nm).find("lpd8") != std::string::npos;
                p->in = std::make_unique<RtMidiIn>(); p->in->openPort(i); p->in->ignoreTypes(true, false, true); p->in->setCallback(&MidiHub::cb, p.get());
                std::printf("MIDI:  %s\n", nm.c_str());
                ports.push_back(std::move(p)); changed = true;
            }
        } catch (RtMidiError& e) { if (first) std::printf("MIDI no disponible: %s\n", e.getMessage().c_str()); }
        // salidas: para devolver valores y luces a las controladoras
        try {
            RtMidiOut probe; const unsigned n = probe.getPortCount();
            std::vector<std::string> now; for (unsigned i = 0; i < n; ++i) now.push_back(probe.getPortName(i));
            for (size_t k = 0; k < outs.size();) { if (std::find(now.begin(), now.end(), outs[k]->name) != now.end()) { ++k; continue; } outs.erase(outs.begin() + static_cast<long>(k)); changed = true; }
            for (unsigned i = 0; i < n; ++i) {
                const std::string& nm = now[i];
                // al MC-24 no se le abre salida: solo tiene potenciómetros, y tras cerrarla deja de mandar hasta reenchufarlo
                if (notADevice(nm) || isIgnored(nm) || lower(nm).find("mc-24") != std::string::npos || lower(nm).find("lpd8") != std::string::npos) continue;
                bool have = false; for (auto& o : outs) have |= o->name == nm;
                if (have) continue;
                auto o = std::make_unique<Out>(); o->name = nm; o->out = std::make_unique<RtMidiOut>(); o->out->openPort(i);
                outs.push_back(std::move(o)); changed = true;
                if (onPortsChanged) onPortsChanged(nm);
            }
        } catch (RtMidiError&) {}
        if (changed) publishNames();
    }
    // Envía a todas las salidas menos las que contengan alguna de las palabras de «skip» (separadas por comas)
    void send(const std::vector<unsigned char>& msg, const std::string& skip, const std::string& only = "") {
        for (auto& o : outs) {
            const std::string ln = lower(o->name);
            if (!only.empty()) { if (o->name != only) continue; }
            else { bool sk = false; size_t a = 0; while (a <= skip.size()) { size_t b = skip.find(',', a); if (b == std::string::npos) b = skip.size(); std::string w = lower(skip.substr(a, b - a)); while (!w.empty() && w[0] == ' ') w.erase(0, 1); if (!w.empty() && ln.find(w) != std::string::npos) sk = true; a = b + 1; } if (sk) continue; }
            try { o->out->sendMessage(&msg); } catch (RtMidiError&) {}
        }
    }
    std::string outNames() const { std::string r; for (auto& o : outs) { if (!r.empty()) r += " | "; r += o->name; } return r; }
};

std::atomic<bool> gQuit{false};
void onSignal(int) { gQuit = true; }
// SIGUSR1: reabrir el audio (lo manda freon-wifi al cambiar de wifi: el USB puede perder datos y la interfaz quedar desalineada, «crusheada»)
void onReopenSignal(int) { gReopen = true; }

}  // namespace

// carpeta del programa (bin/), sin depender de desde dónde se lance
std::string exeDir(const char* argv0) {
    std::string p;
#if defined(__linux__)
    char b[4096]; const ssize_t n = ::readlink("/proc/self/exe", b, sizeof b - 1); if (n > 0) { b[n] = 0; p = b; }
#elif defined(__APPLE__)
    char b[4096]; uint32_t sz = sizeof b; if (_NSGetExecutablePath(b, &sz) == 0) { char r[PATH_MAX]; p = realpath(b, r) ? r : b; }
#endif
    if (p.empty()) p = argv0;
    const auto slash = p.rfind('/'); return slash == std::string::npos ? "." : p.substr(0, slash);
}
// --audio y --entrada aceptan un número o parte del nombre ("Scarlett", "MacBook")
// Algunos nombres de aparato no vienen en UTF-8 («Micrófono» en el Mac). Un byte inválido hace que el navegador cierre la conexión.
std::string utf8Clean(const std::string& s) {
    std::string o; for (size_t i = 0; i < s.size();) { const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0; bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
        if (ok) { o.append(s, i, n); i += n; } else { o += static_cast<char>(0xC0 | c >> 6); o += static_cast<char>(0x80 | (c & 0x3F)); ++i; } }   // se lee como Latin-1
    return o;
}
// --- ajustes de sistema: se eligen en la pantalla, no en la terminal. Un archivo de texto clave=valor junto al programa.
using SysCfg = std::map<std::string, std::string>;
SysCfg sysLoad(const std::string& path) {
    SysCfg c; std::ifstream f(path); std::string l;
    while (std::getline(f, l)) { const size_t e = l.find('='); if (e != std::string::npos) c[l.substr(0, e)] = l.substr(e + 1); }
    return c;
}
bool sysSave(const std::string& path, const SysCfg& c) {
    const std::string tmp = path + ".tmp"; { std::ofstream f(tmp); for (auto& [k, v] : c) f << k << '=' << v << '\n'; if (!f) return false; }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}
// Buffer mínimo seguro. En la Pi 3, con 64 y 128 se encadenaron cortes y el USB de la Clarett se quedó bloqueado en el
// sistema, en un estado que ningún programa puede interrumpir: solo se sale quitando la corriente.
int minBuffer() { std::ifstream f("/proc/device-tree/model"); std::string m; std::getline(f, m); return m.find("Raspberry Pi 3") != std::string::npos ? 256 : 64; }
int cpuTempC() { std::ifstream f("/sys/class/thermal/thermal_zone0/temp"); long t = 0; return f >> t ? static_cast<int>(t / 1000) : -1; }
std::string piThrottled() {
    { std::ifstream f("/sys/devices/platform/soc/soc:firmware/get_throttled"); std::string s; if (f >> s) return s; }
#ifdef __linux__
    // Pi OS actual: la alarma de tensión baja está en hwmon («rpi_volt»). Una vez vista, se recuerda hasta reiniciar
    static bool seen = false; bool found = false;
    for (int h = 0; h < 8; ++h) {
        const std::string d = "/sys/class/hwmon/hwmon" + std::to_string(h) + "/"; std::ifstream n(d + "name"); std::string nm;
        if (!(n >> nm) || nm != "rpi_volt") continue;
        std::ifstream a(d + "in0_lcrit_alarm"); int v = 0; if (a >> v) { found = true; if (v) seen = true; }
    }
    if (found) return seen ? "lowvolt" : "0";
#endif
    return "-";
}
// ¿Es una interfaz USB 1.1 (full speed)? En la Pi 3, con entradas y salidas a la vez suenan «crusheadas»: se abre solo la salida
bool usbFullSpeed(const std::string& devName) {
#ifdef __linux__
    std::ifstream cards("/proc/asound/cards"); std::string l; const std::string dn = lower(devName);
    while (std::getline(cards, l)) {
        const size_t u = l.find("]: USB-Audio - "); if (u == std::string::npos) continue;
        const std::string nm = lower(l.substr(u + 15)); if (nm.empty() || dn.find(nm) == std::string::npos) continue;
        const int card = std::atoi(l.c_str());
        std::ifstream st("/proc/asound/card" + std::to_string(card) + "/stream0"); std::string first; std::getline(st, first);
        return first.find("full speed") != std::string::npos;
    }
#else
    (void)devName;
#endif
    return false;
}
int findDevice(RtAudio& a, const std::string& want, bool input) {
    if (want.empty()) return -1;
    if (std::all_of(want.begin(), want.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        const unsigned id = static_cast<unsigned>(std::atoi(want.c_str()));
        for (unsigned d : a.getDeviceIds()) if (d == id) return static_cast<int>(d);
        return -2;
    }
    for (unsigned d : a.getDeviceIds()) { const auto inf = a.getDeviceInfo(d); if ((input ? inf.inputChannels : inf.outputChannels) && lower(utf8Clean(inf.name)).find(lower(want)) != std::string::npos) return static_cast<int>(d); }
    return -2;
}

int main(int argc, char** argv) {
#if defined(__SSE__) || defined(__x86_64__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    std::signal(SIGPIPE, SIG_IGN);
    if (hasArg(argc, argv, "--list")) { listDevices(); return 0; }
    if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h")) {
        std::printf("Uso: freon-machine [--list] [--audio N|nombre] [--entrada N|nombre] [--midi N] [--out N] [--rate N] [--buffer N]\n"
                    "                [--port 8080] [--host 127.0.0.1] [--estado archivo.json] [--esperar-audio S] [--apagado-midi] [--nucleos 1-4] [--salidas N] [--sin-audio] [--render prueba.wav] [--estres segundos] [--oxi carpeta]\n");
        return 0;
    }
    // Núcleos: en Linux (la Pi) hasta 4; en el Mac, 1, porque allí sobra potencia y no hay tiempo real para los ayudantes
    const int hw = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
#if defined(__linux__)
    const int nucDef = std::min(4, hw);
#else
    const int nucDef = 1;
#endif
    gNuc = std::clamp(argInt(argc, argv, "--nucleos", nucDef), 1, std::min(4, hw));
    if (hasArg(argc, argv, "--niveles")) return levelsTest();
    if (hasArg(argc, argv, "--oxi")) return oxiExport(argStr(argc, argv, "--oxi", "oxi-instrumentos"));
    if (hasArg(argc, argv, "--estres")) return stressTest(std::max(1, argInt(argc, argv, "--estres", 30)));
    if (hasArg(argc, argv, "--render")) return renderTest(argStr(argc, argv, "--render", "prueba.wav"));

    // ajustes de sistema: lo elegido en la pantalla manda sobre las opciones de arranque
    const std::string base = exeDir(argv[0]) + "/..", sysFile = base + "/freon-sistema.txt", sysPrev = sysFile + ".previo", sysPend = sysFile + ".probando", sysBack = sysFile + ".revertido";
    SysCfg cfg = sysLoad(sysFile); bool forceDefault = false;
    std::string autoOut;   // interfaz USB tomada sola porque la elegida no está (no se guarda en los ajustes)
    const auto cfgS = [&](const char* k, const char* arg) { const auto it = cfg.find(k); return it != cfg.end() && !it->second.empty() ? it->second : argStr(argc, argv, arg); };
    const auto cfgI = [&](const char* k, const char* arg, int def) { const std::string s = cfgS(k, arg); const int v = s.empty() ? def : std::atoi(s.c_str()); return v > 0 ? v : def; };
    gNuc = std::clamp(cfgI("nucleos", "--nucleos", nucDef), 1, std::min(4, hw));
    const unsigned rate = static_cast<unsigned>(cfgI("rate", "--rate", 48000));
    unsigned frames = static_cast<unsigned>(std::max(cfgI("buffer", "--buffer", 128), minBuffer()));
    const auto reexec = [&]() {   // reinicio limpio: mismo programa y mismas opciones
        std::fflush(stdout); for (int fd = 3; fd < 1024; ++fd) ::close(fd);
        execv(argv[0], argv); std::perror("No se pudo reiniciar"); std::_Exit(1);
    };
#if defined(__linux__)
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) std::printf("Aviso: no se pudo bloquear la memoria (mlockall); puede haber cortes si el sistema pagina.\n");
#endif
    // la carpeta del proyecto es la que contiene bin/ (así da igual desde dónde se abra)
    auto au = std::make_unique<Audio>(); au->prepare(static_cast<float>(rate));
    { const int got = au->par.start(gNuc - 1, 80); std::printf("Núcleos para el audio: %d de %d pedidos\n", 1 + got, gNuc); }
    dx7::libInit(base + "/dx7");   // sonidos del DX7: los de serie y los bancos .syx de la carpeta dx7/
    Control ctl(*au); ctl.base = base; ctl.initLive();
    ctl.file = argStr(argc, argv, "--estado", base + "/freon-estado.json");
    ctl.defaults(); ctl.defs = ctl.st;
    // primera vez con el nombre nuevo: se recoge el estado del groovebox (en esta carpeta o en la carpeta groovebox de al lado)
    if (!hasArg(argc, argv, "--estado") && !std::filesystem::exists(ctl.file))
        for (const std::string& old : {base + "/groovebox-estado.json", base + "/../groovebox/groovebox-estado.json"})
            if (std::filesystem::exists(old)) { const std::string nf = ctl.file; ctl.file = old; ctl.load(); ctl.file = nf; ctl.dirty = true; std::printf("Estado recogido de %s\n", old.c_str()); break; }
    if (!ctl.dirty) ctl.load();
    ctl.pushAll();
    gMixCh = ctl.mixCh();
    ctl.powerOffEnabled = hasArg(argc, argv, "--apagado-midi");
    ctl.srv.root = argStr(argc, argv, "--ui", base + "/ui");
    const int port = argInt(argc, argv, "--port", 8080);
    const std::string host = argStr(argc, argv, "--host", "127.0.0.1");
    if (!ctl.srv.listenOn(host, port)) { std::printf("No se pudo abrir el puerto %d (¿ya hay otra Freon Machine abierta?).\n", port); return 1; }

    // --- MIDI: se abren todas las entradas (USB, Bluetooth, controladores) y se buscan nuevas cada 2 s
    MidiHub hub; hub.au = au.get(); hub.only = argInt(argc, argv, "--midi", -1);
    { std::stringstream ms(cfg["midioff"]); std::string one; while (std::getline(ms, one, '|')) if (!one.empty()) hub.ignored.push_back(one); }
    hub.onPortsChanged = [&ctl](const std::string&) { ctl.dumpPending = true; ctl.forceDump = true; };   // controladora nueva: se le manda todo
    hub.scan(true);
    ctl.tick = [&hub]() { hub.scan(false); };
    const bool midiLog = std::getenv("GB_MIDILOG") != nullptr;   // pruebas: muestra lo que se devuelve a las controladoras
    ctl.midiOut = [&hub, &ctl, midiLog](const std::vector<unsigned char>& m) { hub.send(m, ctl.ss["midi.skip"]); if (midiLog && m.size() == 3) std::printf("MIDI> %02X %d %d\n", m[0], m[1], m[2]); };

    // --- Audio
    std::unique_ptr<RtAudio> audio;
    std::thread fake;
    const bool noAudio = hasArg(argc, argv, "--sin-audio");
    RtAudio::StreamParameters out, inp; RtAudio::StreamOptions opts; std::string outName, inName;
    auto cb = [](void* o, void* i, unsigned int n, double, RtAudioStreamStatus status, void* u) -> int {
        auto* a = static_cast<Audio*>(u);
        if (status) a->xruns.fetch_add(1, std::memory_order_relaxed);
        a->process(static_cast<float*>(o), a->outCh, static_cast<const float*>(i), i ? a->inCh : 0, static_cast<int>(n)); return 0;
    };
    // rutas: «principal» y «cue» son la primera salida del par (1, 3, 5…); «ent1»…«ent4» el canal de la interfaz (0 = ninguno)
    auto setRoutes = [&]() {
        const int pp = cfgI("principal", "--principal", 1); au->mainPair = std::max(0, (pp - 1) / 2);
        const std::string cq = cfgS("cue", "--cue"); au->cuePair = cq.empty() || cq == "auto" ? -2 : cq == "no" ? -1 : (std::max(1, std::atoi(cq.c_str())) - 1) / 2;
        for (int q = 0; q < 4; ++q) { const std::string e = "ent" + std::to_string(q + 1); const std::string v = cfgS(e.c_str(), ("--" + e).c_str()); au->inMap[q] = std::clamp(v.empty() ? q + 1 : std::atoi(v.c_str()), 0, 16) - 1; }
    };
    // abre (o reabre) el audio con lo elegido; con 'quiet' no repite los mensajes
    auto openAudio = [&](bool quiet) -> bool {
        if (audio && audio->isStreamOpen()) { if (audio->isStreamRunning()) audio->stopStream(); audio->closeStream(); }
        audio = std::make_unique<RtAudio>();
        if (audio->getDeviceIds().empty()) { if (!quiet) std::printf("No hay interfaces de audio. Prueba con --sin-audio.\n"); return false; }
        const std::string wantOut = !autoOut.empty() ? autoOut : forceDefault ? std::string() : cfgS("audio", "--audio"), wantIn = cfgS("entrada", "--entrada") == "none" ? std::string("none") : !autoOut.empty() ? autoOut : cfgS("entrada", "--entrada");
        int od = findDevice(*audio, wantOut, false);
#ifdef __linux__
        // de reserva, en la Pi: su salida de auriculares, directa y en estéreo (el «default» de ALSA pasa por conversiones y suena peor)
        if (wantOut.empty()) { od = findDevice(*audio, "Headphones", false); if (od < 0) od = findDevice(*audio, "bcm2835", false); if (od < 0) od = -1; }
#endif
        if (od == -2) { if (!quiet) std::printf("No encuentro la salida de audio '%s' (mira --list).\n", wantOut.c_str()); return false; }
        out.deviceId = od >= 0 ? static_cast<unsigned>(od) : audio->getDefaultOutputDevice();
        const int firstOut = std::max(1, argInt(argc, argv, "--out", 1));
        out.firstChannel = static_cast<unsigned>(firstOut - 1);
        const auto info = audio->getDeviceInfo(out.deviceId); outName = info.name;
        if (info.outputChannels < out.firstChannel + 2) { if (!quiet) std::printf("La interfaz '%s' tiene %u salidas; no se puede usar --out %d.\n", info.name.c_str(), info.outputChannels, firstOut); return false; }
        out.nChannels = std::min(16u, (info.outputChannels - out.firstChannel) & ~1u);   // todas las salidas (por pares), hasta 16
        // el jack de la Raspberry dice tener 8 canales, pero es estéreo
        if (lower(info.name).find("bcm2835") != std::string::npos || lower(info.name).find("default") != std::string::npos) out.nChannels = 2;
        out.nChannels = std::min(out.nChannels, static_cast<unsigned>(std::max(2, cfgI("salidas", "--salidas", 16))));
        au->outCh = static_cast<int>(out.nChannels);
        setRoutes();
        // entradas: la misma interfaz si tiene; un micro distinto de la salida solo si se pide con --entrada (dos relojes)
        int id = findDevice(*audio, wantIn == "none" ? std::string() : wantIn, true);
        if (id == -2) { if (!quiet) std::printf("No encuentro la entrada '%s'; sigo sin entradas.\n", wantIn.c_str()); id = -1; }
        inp.firstChannel = 0; inp.deviceId = id >= 0 ? static_cast<unsigned>(id) : out.deviceId;
        const auto iinfo = audio->getDeviceInfo(inp.deviceId); inName = iinfo.name;
        inp.nChannels = hasArg(argc, argv, "--sin-entradas") || wantIn == "none" || (id < 0 && (!wantIn.empty() || info.inputChannels == 0)) ? 0 : std::min(16u, iinfo.inputChannels);
        if (inp.nChannels > 0 && inp.deviceId == out.deviceId && usbFullSpeed(outName)) { inp.nChannels = 0; std::printf("%s es USB 1.1: solo salida (con entradas a la vez suena mal en la Pi).\n", outName.c_str()); }
        opts = {}; opts.flags = RTAUDIO_SCHEDULE_REALTIME; opts.priority = 80; opts.numberOfBuffers = static_cast<unsigned>(std::clamp(cfgI("bloques", "--bloques", 3), 2, 4));   // sin MINIMIZE_LATENCY: en el Mac elegía buffers de 15 muestras y crujía
        bool ok = false;
        if (inp.nChannels > 0) { au->inCh = static_cast<int>(inp.nChannels); ok = audio->openStream(&out, &inp, RTAUDIO_FLOAT32, rate, &frames, cb, au.get(), &opts) == RTAUDIO_NO_ERROR; }
        if (!ok) { au->inCh = 0; if (audio->isStreamOpen()) audio->closeStream(); ok = audio->openStream(&out, nullptr, RTAUDIO_FLOAT32, rate, &frames, cb, au.get(), &opts) == RTAUDIO_NO_ERROR; }
        if (ok && frames < 64) {   // buffer demasiado pequeño: se cuela en cortes y crujidos
            if (!quiet) std::printf("Aviso: la interfaz dio un buffer de %u muestras; se pide 256.\n", frames);
            audio->closeStream(); frames = 256;
            ok = au->inCh > 0 ? audio->openStream(&out, &inp, RTAUDIO_FLOAT32, rate, &frames, cb, au.get(), &opts) == RTAUDIO_NO_ERROR
                              : audio->openStream(&out, nullptr, RTAUDIO_FLOAT32, rate, &frames, cb, au.get(), &opts) == RTAUDIO_NO_ERROR;
        }
        if (!ok) { if (!quiet) std::printf("No se pudo abrir el audio: %s\n", audio->getErrorText().c_str()); return false; }
        if (audio->startStream() != RTAUDIO_NO_ERROR) { if (!quiet) std::printf("No se pudo arrancar el audio: %s\n", audio->getErrorText().c_str()); return false; }
        std::printf("Audio: %s  (salidas %d-%d%s, %u Hz, buffer %u = %.1f ms)\n", outName.c_str(), firstOut + 2 * au->mainPair.load(), firstOut + 2 * au->mainPair.load() + 1,
                    au->outCh >= 4 ? (" y " + std::to_string(au->outCh - 2) + " más para pre-escucha y salidas directas").c_str() : "", rate, frames, 1000.0 * frames / rate);
        if (au->inCh > 0) std::printf("Entradas: %d de %s\n", au->inCh, inName.c_str()); else if (!quiet) std::printf("Entradas: ninguna (elige una con --entrada N o --entrada nombre; mira --list)\n");
        return true;
    };
    std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal); std::signal(SIGUSR1, onReopenSignal);
    std::string audioNote; const bool rolledBack = std::filesystem::exists(sysBack);
    { std::error_code ec; std::filesystem::remove(sysBack, ec); }
    std::atomic<bool> restart{false};
    std::atomic<bool> fakeStop{false};
    const auto startFake = [&]() { fakeStop = false; fake = std::thread([&]() { std::vector<float> b(512); while (!gQuit && !fakeStop) { au->process(b.data(), 2, nullptr, 0, 256); std::this_thread::sleep_for(std::chrono::microseconds(256 * 1000000 / rate)); } }); };
    ctl.sysStatus = [&]() { return std::to_string(cpuTempC()) + " " + piThrottled() + " " + std::to_string(au->xruns.load() + au->late.load()) + " " + jnum(au->cpu.load()) + " " + jnum(au->cpuPeak.exchange(0.0f)); };
    ctl.sysInfo = [&]() {
        std::string o = "{\"outs\":["; std::string in = "\"ins\":[", mid = "\"midi\":[", off = "\"midioff\":["; bool fo = true, fi = true, fm = true;
        try { RtAudio a; for (unsigned id : a.getDeviceIds()) { const auto inf = a.getDeviceInfo(id); if (inf.outputChannels) { o += (fo ? "" : ",") + jstr(utf8Clean(inf.name).c_str()); fo = false; } if (inf.inputChannels) { in += (fi ? "" : ",") + jstr(utf8Clean(inf.name).c_str()); fi = false; } } } catch (...) {}
        try { RtMidiIn m; for (unsigned i = 0; i < m.getPortCount(); ++i) { const std::string n = m.getPortName(i); if (!MidiHub::notADevice(n)) { mid += (fm ? "" : ",") + jstr(utf8Clean(MidiHub::portKey(n)).c_str()); fm = false; } } } catch (...) {}
        for (const auto& n : hub.ignored) { if (!fm || mid.back() != '[') mid += ","; mid += jstr(n.c_str()); fm = false; off += (off.back() == '[' ? "" : ",") + jstr(n.c_str()); }
        return o + "]," + in + "]," + mid + "]," + off + "],\"audio\":" + jstr(cfgS("audio", "--audio").c_str()) + ",\"entrada\":" + jstr(cfgS("entrada", "--entrada").c_str()) +
               ",\"buffer\":" + std::to_string(frames) + ",\"minbuf\":" + std::to_string(minBuffer()) + ",\"bloques\":" + std::to_string(std::clamp(cfgI("bloques", "--bloques", 3), 2, 4)) + ",\"rate\":" + std::to_string(rate) + ",\"nucleos\":" + std::to_string(gNuc) + ",\"hw\":" + std::to_string(std::min(4, hw)) + ",\"salidas\":" + std::to_string(cfgI("salidas", "--salidas", 16)) +
               ",\"red\":" + jstr(cfgS("red", "--red").empty() ? "casa" : cfgS("red", "--red").c_str()) + ",\"redOk\":" + (std::filesystem::exists("/etc/freon-wifi.conf") ? "true" : "false") + ",\"nout\":" + std::to_string(au->outCh) + ",\"nin\":" + std::to_string(au->inCh) + ",\"principal\":" + std::to_string(2 * std::min(au->mainPair.load(), std::max(0, au->outCh / 2 - 1)) + 1) + ",\"cue\":" + jstr(au->cuePair == -2 ? "auto" : au->cuePair == -1 ? "no" : std::to_string(2 * au->cuePair + 1).c_str()) +
               ",\"ents\":[" + std::to_string(au->inMap[0] + 1) + "," + std::to_string(au->inMap[1] + 1) + "," + std::to_string(au->inMap[2] + 1) + "," + std::to_string(au->inMap[3] + 1) + "]" +
               ",\"cur\":" + jstr(utf8Clean(outName).c_str()) + ",\"curIn\":" + jstr(au->inCh > 0 ? utf8Clean(inName).c_str() : "") + ",\"note\":" + jstr(audioNote.c_str()) + ",\"rolled\":" + (rolledBack ? "true" : "false") + ",\"power\":" + (ctl.powerOffEnabled ? "true" : "false") + "}";
    };
    ctl.sysApply = [&](const std::string& line) {   // «clave=valor» separados por tabuladores
        static const std::vector<std::string> keys = {"audio", "entrada", "buffer", "bloques", "rate", "nucleos", "salidas", "midioff", "principal", "cue", "ent1", "ent2", "ent3", "ent4"};
        SysCfg next = cfg; std::stringstream ss(line); std::string kv;
        while (std::getline(ss, kv, '\t')) { const size_t e = kv.find('='); if (e == std::string::npos) continue; size_t b = kv.find_first_not_of(' '); const std::string k = kv.substr(b, e - b); std::string v = kv.substr(e + 1); v.erase(std::remove(v.begin(), v.end(), '\n'), v.end()); if (std::find(keys.begin(), keys.end(), k) != keys.end() && v.size() < 400) next[k] = v; }
        const auto num = [&](const char* k, std::initializer_list<int> ok) { const auto it = next.find(k); if (it == next.end() || it->second.empty()) return; const int v = std::atoi(it->second.c_str()); if (std::find(ok.begin(), ok.end(), v) == ok.end()) next.erase(k); };
        num("buffer", {64, 128, 256, 512, 1024, 2048}); if (next.count("buffer") && std::atoi(next["buffer"].c_str()) < minBuffer()) next["buffer"] = std::to_string(minBuffer()); num("rate", {44100, 48000}); num("nucleos", {1, 2, 3, 4}); num("bloques", {2, 3, 4}); num("salidas", {2, 4, 6, 8, 16}); num("principal", {1, 3, 5, 7, 9, 11, 13, 15}); for (const char* e : {"ent1", "ent2", "ent3", "ent4"}) num(e, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
        if (next.count("cue") && next["cue"] != "auto" && next["cue"] != "no") num("cue", {1, 3, 5, 7, 9, 11, 13, 15});
        { std::error_code ec; if (std::filesystem::exists(sysFile)) std::filesystem::copy_file(sysFile, sysPrev, std::filesystem::copy_options::overwrite_existing, ec); else std::filesystem::remove(sysPrev, ec); }
        if (!sysSave(sysFile, next)) { std::printf("Aviso: no se pudieron guardar los ajustes de sistema.\n"); return; }
        { std::ofstream f(sysPend); f << "1"; }
        restart = true; gQuit = true;
    };
    ctl.sysRoute = [&](const std::string& line) {   // salidas y entradas: al momento, sin reiniciar; se guardan para el próximo arranque
        std::stringstream ss(line); std::string kv; bool any = false;
        static const std::vector<std::string> odd = {"1", "3", "5", "7", "9", "11", "13", "15"};
        while (std::getline(ss, kv, '\t')) {
            const size_t e = kv.find('='); if (e == std::string::npos) continue; size_t b = kv.find_first_not_of(' '); const std::string k = kv.substr(b, e - b), v = kv.substr(e + 1);
            const int n = std::atoi(v.c_str());
            const bool ok = (k == "principal" && std::find(odd.begin(), odd.end(), v) != odd.end()) || (k == "cue" && (v == "auto" || v == "no" || std::find(odd.begin(), odd.end(), v) != odd.end())) ||
                            ((k == "ent1" || k == "ent2" || k == "ent3" || k == "ent4") && v == std::to_string(n) && n >= 0 && n <= 16);
            if (ok) { cfg[k] = v; any = true; }
            // red: «propia» = la Pi crea siempre su wifi; «casa» = se conecta a la de casa (y crea la propia si no la encuentra)
            if (k == "red" && (v == "propia" || v == "casa")) {
                cfg[k] = v; if (!sysSave(sysFile, cfg)) std::printf("Aviso: no se pudieron guardar los ajustes de sistema.\n");
                std::thread([v]() { if (std::system(("sudo -n /usr/local/sbin/freon-wifi " + v + " >/dev/null 2>&1").c_str()) != 0) std::printf("No se pudo cambiar la red (¿falta instalar deploy/wifi-propia.sh?).\n"); }).detach();
                std::printf("Red: %s\n", v.c_str()); std::fflush(stdout);
            }
        }
        if (!any) return;
        setRoutes();
        if (!sysSave(sysFile, cfg)) std::printf("Aviso: no se pudieron guardar los ajustes de sistema.\n");
    };
    ctl.sysAction = [&](const std::string& w) {
        if (w == "restart") { restart = true; gQuit = true; }
        else if (w == "poweroff" && ctl.powerOffEnabled) { std::printf("Apagando desde la pantalla...\n"); if (ctl.dirty) ctl.save(); if (std::system("sudo -n /sbin/poweroff || systemctl poweroff") != 0) std::printf("No se pudo apagar.\n"); }
    };
    if (noAudio) {
        startFake();
        std::printf("Audio: sin tarjeta de sonido (modo de prueba)\n");
    } else {
        // en la Pi la interfaz USB puede tardar en aparecer al arrancar: se espera hasta --esperar-audio segundos
        const int wait = std::min(8, argInt(argc, argv, "--esperar-audio", 0));   // como mucho 8 s: si la interfaz llega después, se pasa a ella sola
        const bool trying = std::filesystem::exists(sysPend);
        bool ok = openAudio(false);
        for (int t = 0; !ok && t < (trying ? 6 : wait) && !gQuit; t += 2) { std::this_thread::sleep_for(std::chrono::seconds(2)); ok = openAudio(true); }
        std::error_code ec;
        if (!ok && trying) {   // lo último elegido no funciona: se vuelve a lo anterior y se reinicia
            std::printf("Aviso: los ajustes nuevos no funcionan; vuelvo a los anteriores.\n");
            std::filesystem::remove(sysFile, ec); if (std::filesystem::exists(sysPrev)) std::filesystem::rename(sysPrev, sysFile, ec);
            std::filesystem::remove(sysPend, ec); { std::ofstream f(sysBack); f << "1"; }
            reexec();
        }
        std::filesystem::remove(sysPend, ec);
        // la interfaz elegida no está: mejor la de por defecto, y si tampoco hay, sin audio. La pantalla debe seguir accesible para arreglarlo
        if (!ok) { std::printf("Aviso: no encuentro la interfaz elegida; pruebo la de por defecto.\n"); forceDefault = true; ok = openAudio(true); audioNote = ok ? "default" : "none"; }
        if (!ok) { std::printf("Aviso: sin interfaz de audio. Elige otra en la pantalla.\n"); startFake(); } else ctl.watchAudio = true;
        // si la interfaz se cae en marcha: se intenta reabrir; tras 3 intentos, a la de reserva (y vuelve sola cuando reaparece)
        // cambio de wifi (de la de casa a la propia o al revés): el USB puede perder datos y la interfaz quedarse desalineada,
        // sonando «crusheada». Freon lo detecta sola (cambia la dirección IPv4 de wlan0) y reabre el audio 4 s después.
        // cambio entre la wifi de casa y la propia (hostapd reconfigura la antena y el USB puede perder datos: la interfaz
        // queda desalineada, «crusheada»). Solo al entrar o salir de la propia (10.42.0.1); una caída y vuelta de la wifi
        // o la conexión al arrancar no reabren nada. Se reabre 4 s después.
        ctl.onNetCheck = [ap = false, due = 0]() mutable {
#ifdef __linux__
            bool nowAp = false; ifaddrs* ifa = nullptr;
            if (getifaddrs(&ifa) == 0) { for (ifaddrs* i = ifa; i; i = i->ifa_next) if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && std::string(i->ifa_name) == "wlan0" &&
                ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr) == 0x0A2A0001u) nowAp = true; freeifaddrs(ifa); }
            if (nowAp != ap) { ap = nowAp; due = 2; }   // comprobación cada 2 s: 2 vueltas = 4 s
            if (due > 0 && --due == 0) gReopen = true;
#endif
        };
        ctl.onReopen = [&]() { if (ctl.watchAudio && !openAudio(true)) std::printf("No pude reabrir el audio; lo intenta el vigilante.\n"); };
        ctl.onStall = [&, fails = 0]() mutable {
            if (openAudio(true)) { fails = 0; return; }
            std::printf("Aún sin audio; lo vuelvo a intentar.\n");
            if (++fails < 3 || (forceDefault && autoOut.empty())) return;
            fails = 0; autoOut.clear(); forceDefault = true; audioNote = "default";
            std::printf("La interfaz no vuelve: paso a la salida de la Pi hasta que reaparezca.\n");
            if (!openAudio(true)) { audioNote = "none"; ctl.watchAudio = false; startFake(); }
        };
        // la elegida no está (suena por la de la Pi, o no suena): en cuanto aparece, se pasa a ella; y si mientras tanto
        // se enchufa otra interfaz USB, se usa esa (sin guardarla: la elegida sigue siendo la de Ajustes)
        ctl.onAudioCheck = [&, seen = 0, wait = 0, bad = std::string()]() mutable {
            if (!forceDefault && autoOut.empty()) return;
            const std::string want = cfgS("audio", "--audio");
            if (wait > 0) { --wait; return; }
            bool here = false; std::string other;
#ifdef __linux__
            {   // /proc/asound/cards: « 3 [CODEC ]: USB-Audio - USB Audio CODEC»
                std::ifstream f("/proc/asound/cards"); std::string l;
                while (std::getline(f, l)) {
                    if (!want.empty() && lower(l).find(lower(want)) != std::string::npos) here = true;
                    const size_t u = l.find("]: USB-Audio - "); if (u == std::string::npos) continue;
                    const std::string nm = l.substr(u + 15), ln = lower(nm);
                    if (!want.empty() && !ln.empty() && lower(want).find(ln) != std::string::npos) { here = true; continue; }   // «Clarett+ 2Pre (USB Audio)» guardado con el nombre largo
                    if (ln.find("oxi") != std::string::npos || ln.find("mc-24") != std::string::npos || nm == bad || nm == autoOut) continue;
                    if (other.empty()) other = nm;
                }
            }
#else
            if (!want.empty()) try { RtAudio a; here = findDevice(a, want, false) >= 0; } catch (...) {}
#endif
            if (!here && (other.empty() || !autoOut.empty())) { seen = 0; return; }
            if (++seen < 2) return;   // recién enchufada: se le da un momento para que esté lista
            if (fake.joinable()) { fakeStop = true; fake.join(); }
            const std::string prevAuto = autoOut;
            if (here) { std::printf("Ha aparecido la interfaz elegida (%s): paso a ella.\n", want.c_str()); autoOut.clear(); }
            else { std::printf("Ha aparecido otra interfaz (%s): paso a ella mientras no esté la elegida.\n", other.c_str()); autoOut = other; }
            forceDefault = false;
            if (openAudio(true)) { audioNote = autoOut.empty() ? "" : "auto"; ctl.watchAudio = true; seen = 0; std::printf("Audio: %s\n", outName.c_str()); return; }
            std::printf("Aún no responde; sigo con la de antes y lo vuelvo a intentar.\n");
            if (!here) bad = other;
            autoOut = prevAuto; forceDefault = autoOut.empty(); seen = 0; wait = 5;
            if (openAudio(true)) ctl.watchAudio = true; else { autoOut.clear(); forceDefault = true; if (openAudio(true)) ctl.watchAudio = true; else { ctl.watchAudio = false; startFake(); } }
        };
    }
    std::printf("\nPantalla: abre http://%s:%d en Chrome o Safari.\nPara salir: Ctrl+C.\n", host == "0.0.0.0" ? "localhost" : host.c_str(), port);
    ctl.run(gQuit);
    if (fake.joinable()) fake.join();
    if (audio) { if (audio->isStreamRunning()) audio->stopStream(); if (audio->isStreamOpen()) audio->closeStream(); }
    std::printf("\nEstado guardado en %s. Hasta luego.\n", ctl.file.c_str());
    if (restart) reexec();
    return 0;
}

// drm1.h — Emulación propia (desde cero, sin dependencias) de la Vermona DRM1 MKIII.
// Escrita por Fable (Claude) el 15-11-2026 en drm-fable/ sin mirar el código de Freon; aquí se usa tal cual como kit «DRM».
//
// Ocho canales, cada uno con los mismos mandos que el panel: KICK, DRUM 1, DRUM 2, MULTI, SNARE,
// HI HAT 1, HI HAT 2 y CLAP. Todos los mandos van de 0 a 1 (0 = tope a la izquierda, «las 7»;
// 0,5 = las 12; 1 = tope a la derecha, «las 5»). PAN va de 0 (izquierda) a 1 (derecha); VOLUME de 0 a 1.
// La velocidad de disparo va de 0 a 1 (el botón TRIG de la DRM dispara a 0,75).
//
// Lo que está documentado en el manual (qué mando hace qué) se sigue al pie de la letra; los rangos
// numéricos, las curvas y los detalles del circuito son estimaciones propias (ver README.md).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace drm1 {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;

inline float clamp01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
// Mando exponencial: p=0 → lo, p=1 → hi (sirve también con hi < lo)
inline float expo(float lo, float hi, float p) { return lo * std::pow(hi / lo, clamp01(p)); }
inline float bipolar(float p) { return (clamp01(p) - 0.5f) * 2.0f; }   // mando centrado: -1..1
inline float softClip(float x) { return std::tanh(x); }                 // etapa de salida analógica

// ---------------------------------------------------------------- piezas
struct Noise {   // ruido blanco, xorshift32
    uint32_t s;
    explicit Noise(uint32_t seed = 0x2545F491u) : s(seed) {}
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float>(s >> 8) * (2.0f / 16777216.0f) - 1.0f; }
};

struct Env {   // caída exponencial (un multiplicador por muestra)
    float e = 0.0f, k = 0.0f;
    void setTau(float tau, float sr) { k = std::exp(-1.0f / (std::max(tau, 1e-4f) * sr)); }
    void trig() { e = 1.0f; }
    float tick() { const float v = e; e *= k; return v; }
    bool quiet() const { return e < 1e-4f; }
};

// Filtro de estado variable (topología TPT): paso bajo resonante hasta la autooscilación
struct SVF {
    float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    static float damping(float reso) { return 2.0f * std::pow(1.0f - clamp01(reso), 1.6f) + 0.001f; }
    void set(float fc, float reso, float sr) {
        fc = std::clamp(fc, 10.0f, 0.45f * sr);
        g = std::tan(kPi * fc / sr); k = damping(reso);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float lp(float x) {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return v2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
};

struct OnePoleHP {
    float y = 0.0f, xp = 0.0f, a = 0.0f;
    void set(float fc, float sr) { const float w = kTwoPi * fc / sr; a = 1.0f / (1.0f + w); }
    float process(float x) { y = a * (y + x - xp); xp = x; return y; }
};

struct OnePoleLP {
    float y = 0.0f, a = 0.0f;
    void set(float fc, float sr) { const float w = kTwoPi * fc / sr; a = w / (1.0f + w); }
    float process(float x) { y += a * (x - y); return y; }
};

struct BandPass {   // bicuadrático paso banda (RBJ)
    float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    void set(float fc, float q, float sr) {
        fc = std::clamp(fc, 10.0f, 0.45f * sr);
        const float w = kTwoPi * fc / sr, al = std::sin(w) / (2.0f * q), a0 = 1.0f + al;
        b0 = al / a0; b2 = -al / a0; a1 = -2.0f * std::cos(w) / a0; a2 = (1.0f - al) / a0;
    }
    float process(float x) { const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
};

// «Impulso de aguja» (Nadelimpuls): pulso unipolar que la DRM suma al principio del sonido.
// Subida instantánea y caída exponencial de ~0,1 ms (medio milisegundo en total): un clic de banda ancha.
struct Needle {
    int n = 0, len = 0; float amp = 0.0f, k = 0.0f, v = 0.0f;
    void trig(float a, float sr) { amp = a; v = a; n = 0; len = static_cast<int>(0.0005f * sr); k = std::exp(-1.0f / (0.0001f * sr)); }
    float tick() { if (n >= len) return 0.0f; const float o = v; v *= k; ++n; return o; }
    bool done() const { return n >= len; }
};

// WAVE: de seno a cuadrada, con sobreexcitación en la mitad alta del mando (manual: «raw and deep» abajo, «overdrive» arriba)
inline float waveShape(float s, float w) {
    if (w < 1e-3f) return s;
    const float g = 1.0f + w * w * 40.0f;
    const float sq = std::tanh(s * g);
    return s + (sq - s) * w;
}

inline float tri(float ph) { return 4.0f * std::fabs(ph - 0.5f) - 1.0f; }   // triángulo -1..1 de una fase 0..1
inline float square(float ph) { return ph < 0.5f ? 1.0f : -1.0f; }

// ---------------------------------------------------------------- canales
struct Channel {
    float pan = 0.5f, volume = 0.8f;   // mandos comunes a todos los canales (PAN y VOLUME)
    float sr = 48000.0f;
    virtual ~Channel() = default;
    virtual void prepare(float s) { sr = s; }
    virtual void trigger(float vel, int mode = 0) = 0;   // mode: solo lo usan los hi-hats (0 cerrado, 1 abierto)
    virtual float process() = 0;
    virtual bool active() const = 0;
};

// KICK — DECAY, PITCH, BEND, TIME, WAVE, NOISE, ATTACK
//   Seno con envolvente de tono (TIME = su tiempo, BEND = su cantidad), conformador seno→cuadrada (WAVE),
//   ráfaga fija de ruido (NOISE, «la maza») e impulso de aguja (ATTACK), todo por un VCA con caída DECAY.
struct Kick : Channel {
    float decay = 0.35f, pitch = 0.25f, bend = 0.45f, time = 0.3f, wave = 0.3f, noise = 0.25f, attack = 0.35f;
    float ph = 0.0f, f0 = 50.0f, fb = 0.0f, gain = 0.0f; bool on = false;
    Env eA, eP, eN; Needle nd; Noise nz{0x1111u}; BandPass nbp;
    void prepare(float s) override { sr = s; nbp.set(3200.0f, 0.8f, sr); }
    void trigger(float vel, int) override {
        on = true; gain = vel; ph = 0.0f;
        eA.setTau(expo(0.04f, 2.0f, decay), sr); eA.trig();       // 40 ms (dance) … 2 s (dubstep)
        eP.setTau(expo(0.004f, 0.25f, time), sr); eP.trig();      // envolvente de tono: 4 ms … 250 ms
        eN.setTau(0.004f, sr); eN.trig();                          // ráfaga de ruido fija (~12 ms)
        f0 = expo(30.0f, 480.0f, pitch);                           // «las 9» ≈ 52 Hz, las 10 ≈ 60 Hz
        fb = bend * bend * 900.0f;                                 // Hz que se suman al inicio (lineal en Hz, como un CV sumado)
        nd.trig(attack, sr);
    }
    float process() override {
        if (!on) return 0.0f;
        const float a = eA.tick(), p = eP.tick();
        const float f = std::min(f0 + fb * p, 1500.0f);            // con PITCH al máximo, BEND ya no llega más arriba
        ph += f / sr; if (ph >= 1.0f) ph -= 1.0f;
        float out = waveShape(std::sin(kTwoPi * ph), wave) * a;
        out += nbp.process(nz.next()) * eN.tick() * noise * 1.5f;
        out += nd.tick() * 1.3f;
        if (eA.quiet() && nd.done()) on = false;
        return softClip(out * 1.3f * gain) * volume;
    }
    bool active() const override { return on; }
};

// DRUM 1 / DRUM 2 — DECAY, PITCH, BEND (centrado), ATTACK, FM INT, FM FREQ, WAVE
//   Seno cuya altura sigue la envolvente DECAY (BEND: a la derecha baja, a la izquierda sube), modulado por un
//   oscilador seno libre que va de LFO a frecuencia de audio (FM FREQ), conformador WAVE e impulso de aguja.
struct Drum : Channel {
    float decay = 0.45f, pitch = 0.35f, bend = 0.68f, attack = 0.3f, fmInt = 0.0f, fmFreq = 0.3f, wave = 0.2f;
    float ph = 0.0f, lfo = 0.0f, lfoInc = 0.0f, f0 = 100.0f, b = 0.0f, gain = 0.0f; bool on = false;
    Env eA; Needle nd; OnePoleHP dc;
    void prepare(float s) override { sr = s; dc.set(20.0f, sr); }
    static float fmRate(float p) { return p < 0.2f ? expo(0.3f, 20.0f, p / 0.2f) : expo(20.0f, 1500.0f, (p - 0.2f) / 0.8f); }   // «desde las 9 entra en audio»
    void trigger(float vel, int) override {
        on = true; gain = vel; ph = 0.0f;                           // el LFO no se reinicia: la FM lenta cambia de golpe a golpe
        eA.setTau(expo(0.005f, 3.0f, decay), sr); eA.trig();       // más amplio que el KICK: desde clics muy cortos
        f0 = expo(30.0f, 1920.0f, pitch); b = bipolar(bend);
        lfoInc = fmRate(fmFreq) / sr;
        nd.trig(attack, sr);
    }
    float process() override {
        if (!on) { lfo += lfoInc; if (lfo >= 1.0f) lfo -= 1.0f; return 0.0f; }
        const float a = eA.tick();
        lfo += lfoInc; if (lfo >= 1.0f) lfo -= 1.0f;
        float f = f0 * std::exp2(b * 3.0f * a);                     // hasta ±3 octavas al inicio, siguiendo DECAY
        f = std::clamp(f, 15.0f, 4000.0f);                         // en los extremos de PITCH el BEND no llega más lejos
        f *= std::exp2(fmInt * 2.5f * std::sin(kTwoPi * lfo));      // FM exponencial (entrada V/oct del VCO): hasta ±2,5 octavas; atonal y campanas a frecuencia de audio
        ph += std::min(f, 12000.0f) / sr; ph -= std::floor(ph);
        float out = dc.process(waveShape(std::sin(kTwoPi * ph), wave) * a);
        out += nd.tick();
        if (eA.quiet() && nd.done()) on = false;
        return softClip(out * 1.3f * gain) * volume;
    }
    bool active() const override { return on; }
};

// MULTI — DECAY, PITCH, BEND (centrado), ATTACK, PITCH 2, PITCH 3, HIGH PASS
//   Tres osciladores triangulares: PITCH es el primero y la base de los otros dos (PITCH 2/3: al tope izquierdo, apagados).
//   Todos siguen la envolvente DECAY según BEND. Suma con algo de saturación (metálico) y paso alto.
struct Multi : Channel {
    float decay = 0.4f, pitch = 0.63f, bend = 0.5f, attack = 0.3f, pitch2 = 0.61f, pitch3 = 0.0f, highpass = 0.3f;
    float ph[3] = {}, f1 = 500.0f, r2 = 0.0f, r3 = 0.0f, b = 0.0f, gain = 0.0f; bool on = false;
    Env eA; Needle nd; OnePoleHP hp;
    static float ratio(float p) { return p < 0.03f ? 0.0f : std::exp2((p - 0.5f) * 5.0f); }   // ×0.18 … ×5.7 respecto a PITCH
    void trigger(float vel, int) override {
        on = true; gain = vel; ph[0] = ph[1] = ph[2] = 0.0f;
        eA.setTau(expo(0.005f, 2.0f, decay), sr); eA.trig();
        f1 = expo(40.0f, 2560.0f, pitch); r2 = ratio(pitch2); r3 = ratio(pitch3); b = bipolar(bend);
        hp.set(expo(20.0f, 2560.0f, highpass), sr);
        nd.trig(attack, sr);
    }
    float process() override {
        if (!on) return 0.0f;
        const float a = eA.tick();
        const float f = std::clamp(f1 * std::exp2(b * 3.0f * a), 15.0f, 6000.0f);
        float x = 0.0f;
        ph[0] += f / sr; ph[0] -= std::floor(ph[0]); x += tri(ph[0]);
        if (r2 > 0.0f) { ph[1] += std::min(f * r2, 10000.0f) / sr; ph[1] -= std::floor(ph[1]); x += tri(ph[1]); }
        if (r3 > 0.0f) { ph[2] += std::min(f * r3, 10000.0f) / sr; ph[2] -= std::floor(ph[2]); x += tri(ph[2]); }
        float out = hp.process(std::tanh(x * 1.1f) * a * 0.9f);
        out += nd.tick();
        if (eA.quiet() && nd.done()) on = false;
        return softClip(out * 1.3f * gain) * volume;
    }
    bool active() const override { return on; }
};

// SNARE — DECAY R, REVERB, DECAY N, NOISE, ATTACK, RESO, FILTER
//   Cuatro piezas (manual): ruido, impulso de aguja, filtro resonante y pseudo-reverb.
//   El impulso excita el filtro paso bajo resonante (RESO alto: autooscila; FILTER es entonces su tono);
//   el «reverb» es ruido con caída DECAY R metido por el mismo filtro; el ruido «de la bordonera» (NOISE, DECAY N)
//   entra después del filtro. El camino del filtro pasa por un VCA con DECAY R (por eso, con RESO al máximo,
//   DECAY R también acorta la parte tonal, como dice el manual).
struct Snare : Channel {
    float decayR = 0.3f, reverb = 0.3f, decayN = 0.4f, noise = 0.6f, attack = 0.6f, reso = 0.55f, filter = 0.35f;
    float gain = 0.0f, drive = 1.0f; bool on = false;
    SVF svf; OnePoleHP nzHp; OnePoleLP nzLp; Env eR, eN; Needle nd; Noise nz{0x2222u}, nz2{0x3333u};
    void prepare(float s) override { sr = s; nzHp.set(900.0f, sr); nzLp.set(6500.0f, sr); }
    void trigger(float vel, int) override {
        on = true; gain = vel;
        eR.setTau(expo(0.05f, 1.2f, decayR), sr); eR.trig();
        eN.setTau(expo(0.015f, 0.5f, decayN), sr); eN.trig();
        svf.set(expo(120.0f, 2700.0f, filter), reso, sr);
        drive = 0.45f + 0.55f * std::min(1.0f, svf.k);                // con mucha resonancia el filtro «se va»: entra menos señal (como si topara con la alimentación)
        nd.trig(attack, sr);
    }
    float process() override {
        if (!on) return 0.0f;
        const float r = eR.tick(), n = eN.tick(), ndl = nd.tick();
        const float in = (ndl * 5.0f + nz.next() * r * reverb * 0.9f) * drive;
        const float tonal = svf.lp(in) * r;
        float out = tonal * 4.0f + ndl * 0.8f + nzLp.process(nzHp.process(nz2.next())) * n * noise * 2.6f;
        if (eR.quiet() && eN.quiet()) on = false;
        return softClip(out * gain) * volume;
    }
    bool active() const override { return on; }
};

// HI HAT 1 / HI HAT 2 — DECAY, FILTER, BEND (centrado), ATTACK, RESO, MIX, PITCH
//   Ruido y banco de osciladores metálicos (seis cuadradas desafinadas, PITCH las afina todas) mezclados con MIX
//   (izquierda: solo ruido; derecha: solo osciladores), por un paso bajo resonante cuyo corte sigue la envolvente
//   según BEND (más suave que en DRUM/MULTI), paso alto fijo y VCA. ATTACK: impulso afinado con PITCH.
//   Cerrado (mode 0): caída corta fija, independiente de DECAY. Abierto (mode 1): caída = DECAY.
//   Es una sola voz: el cerrado corta al abierto, como en la DRM.
struct HiHat : Channel {
    float decay = 0.35f, filter = 0.6f, bend = 0.5f, attack = 0.3f, reso = 0.3f, mix = 0.5f, pitch = 0.5f;
    float ph[6] = {}, inc[6] = {}, fc0 = 8000.0f, b = 0.0f, gain = 0.0f; int cnt = 0; bool on = false;
    SVF svf; OnePoleHP hp, hp2; BandPass abp; Env eA; Needle nd; Noise nz{0x4444u};
    static constexpr float kRatio[6] = {1.0f, 1.4837f, 1.8002f, 2.5461f, 2.6303f, 3.8968f};
    void trigger(float vel, int mode) override {
        on = true; gain = vel; cnt = 0;
        eA.setTau(mode == 1 ? expo(0.03f, 2.0f, decay) : 0.03f, sr); eA.trig();
        const float f = 640.0f * std::exp2((pitch - 0.5f) * 2.0f);   // 320 … 1280 Hz de base (las cuadradas llegan hasta ×3,9)
        for (int i = 0; i < 6; ++i) inc[i] = f * kRatio[i] / sr;
        fc0 = expo(1000.0f, 16000.0f, filter); b = bipolar(bend);
        svf.set(fc0, reso, sr);
        const float fhp = 3000.0f * std::exp2(pitch - 0.5f);        // paso alto fijo de dos polos (el canal solo trae paso bajo; el circuito quita los graves)
        hp.set(fhp, sr); hp2.set(fhp, sr);
        abp.set(f * 4.0f, 4.0f, sr);
        nd.trig(attack, sr);
    }
    float process() override {
        if (!on) return 0.0f;
        const float a = eA.tick();
        if (b != 0.0f && (++cnt & 15) == 0) svf.set(fc0 * std::exp2(b * 2.0f * a), reso, sr);
        float metal = 0.0f;
        for (int i = 0; i < 6; ++i) { ph[i] += inc[i]; if (ph[i] >= 1.0f) ph[i] -= 1.0f; metal += square(ph[i]); }
        metal *= 1.0f / 6.0f;
        const float x = nz.next() * (1.0f - mix) * 0.9f + metal * mix * 1.1f;
        float out = hp2.process(hp.process(svf.lp(x))) * a * 2.6f;
        out += abp.process(nd.tick()) * 2.5f;
        if (eA.quiet() && nd.done()) on = false;
        return softClip(out * gain) * volume;
    }
    bool active() const override { return on; }
};

// CLAP — DECAY R, REVERB, NOISE, RESO, CLAP, FILTER, HIGH PASS
//   Secuencia de cuatro ráfagas de ruido (CLAP = su velocidad), ruido añadido (NOISE) y pseudo-reverb (REVERB, DECAY R)
//   que arranca con la última palmada; todo por el paso bajo resonante (FILTER, RESO) y un paso alto (HIGH PASS).
struct Clap : Channel {
    float decayR = 0.35f, reverb = 0.4f, noise = 0.4f, reso = 0.45f, clap = 0.5f, filter = 0.55f, highpass = 0.3f;
    int nb = 0; float t = 0.0f, nextB = 0.0f, gap = 0.01f, gain = 0.0f; bool on = false;
    static constexpr int kClaps = 4;
    SVF svf; OnePoleHP hp; Env eB, eR, eN; Noise nz{0x5555u}, nz2{0x6666u};
    void trigger(float vel, int) override {
        on = true; gain = vel; nb = 0; t = 0.0f; nextB = 0.0f;
        gap = expo(0.025f, 0.006f, clap);                           // a la derecha, palmadas más rápidas
        eB.setTau(0.0025f, sr); eB.e = 0.0f;
        eR.setTau(expo(0.04f, 0.7f, decayR), sr); eR.e = 0.0f;
        eN.setTau(0.03f, sr); eN.trig();
        svf.set(expo(400.0f, 6400.0f, filter), reso, sr);
        hp.set(expo(60.0f, 1920.0f, highpass), sr);
    }
    float process() override {
        if (!on) return 0.0f;
        if (nb < kClaps && t >= nextB) { eB.trig(); ++nb; nextB += gap; if (nb == kClaps) eR.trig(); }
        const float bst = eB.tick(), r = eR.tick(), n = eN.tick();
        const float in = nz.next() * bst + nz.next() * r * reverb * 0.45f + nz2.next() * n * noise * 0.5f;
        float out = hp.process(svf.lp(in)) * 3.2f;
        t += 1.0f / sr;
        if (nb >= kClaps && eB.quiet() && eR.quiet() && eN.quiet()) on = false;
        return softClip(out * gain) * volume;
    }
    bool active() const override { return on; }
};

// ---------------------------------------------------------------- la máquina: 8 canales a estéreo
enum ChannelId { KICK = 0, DRUM1, DRUM2, MULTI, SNARE, HIHAT1, HIHAT2, CLAP, N_CHANNELS };
inline const char* channelName(int c) {
    static const char* N[N_CHANNELS] = {"KICK", "DRUM 1", "DRUM 2", "MULTI", "SNARE", "HI HAT 1", "HI HAT 2", "CLAP"};
    return c >= 0 && c < N_CHANNELS ? N[c] : "?";
}
// Notas MIDI de fábrica de la MKIII (manual, «Factory Defaults»). Los hi-hats tienen dos: cerrado y abierto/cymbal.
struct FactoryNotes { int kick = 36, drum1 = 48, drum2 = 41, multi = 58, snare = 40, hh1Closed = 49, hh1Open = 51, hh2Closed = 42, hh2Open = 44, clap = 39; };

struct Machine {
    Kick kick; Drum drum1, drum2; Multi multi; Snare snare; HiHat hihat1, hihat2; Clap clap;
    float master = 0.5f;
    Channel* ch[N_CHANNELS] = {&kick, &drum1, &drum2, &multi, &snare, &hihat1, &hihat2, &clap};
    void prepare(float sr) { for (auto* c : ch) c->prepare(sr); }
    void trigger(int c, float vel, int mode = 0) { if (c >= 0 && c < N_CHANNELS) ch[c]->trigger(clamp01(vel), mode); }
    // disparo por nota MIDI con las notas de fábrica
    void noteOn(int note, int vel127, const FactoryNotes& n = FactoryNotes()) {
        const float v = std::clamp(vel127, 1, 127) / 127.0f;
        if (note == n.kick) trigger(KICK, v); else if (note == n.drum1) trigger(DRUM1, v); else if (note == n.drum2) trigger(DRUM2, v);
        else if (note == n.multi) trigger(MULTI, v); else if (note == n.snare) trigger(SNARE, v); else if (note == n.clap) trigger(CLAP, v);
        else if (note == n.hh1Closed) trigger(HIHAT1, v, 0); else if (note == n.hh1Open) trigger(HIHAT1, v, 1);
        else if (note == n.hh2Closed) trigger(HIHAT2, v, 0); else if (note == n.hh2Open) trigger(HIHAT2, v, 1);
    }
    void process(float& L, float& R) {
        L = R = 0.0f;
        for (auto* c : ch) {
            if (!c->active() && !(c == &drum1 || c == &drum2)) continue;   // los DRUM siguen moviendo su LFO aunque callen
            const float x = c->process();
            if (x == 0.0f) continue;
            const float th = clamp01(c->pan) * (kPi * 0.5f);
            L += x * std::cos(th); R += x * std::sin(th);
        }
        L *= master; R *= master;
    }
};

}  // namespace drm1

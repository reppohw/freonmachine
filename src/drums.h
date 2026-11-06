// drums.h — kits de batería de Freon Machine.
// En directo: 808 y 909 (modelos de Mutable Instruments Plaits), FM al estilo Model:Cycles (propio),
// Chip (Game Boy) y Experimental. Las recetas 808/DRM/Chip FM antiguas quedan para los sonidos de serie del sampler.
//
// Cada sonido se describe como una «receta» de piezas sencillas (cuerpo con barrido de tono,
// modulador FM, ruido filtrado, banco metálico de 6 cuadradas, clics) y una voz las toca.
// Carriles: 0 bombo, 1 caja, 2 clap, 3 rim, 4 tom, 5 HH cerrado, 6 HH abierto, 7 perc.
#pragma once
#include <array>
#include "dsp.h"
#include "plaits_glue.h"

namespace gb {

enum DrumKit { KIT_808 = 0, KIT_DRM, KIT_CHIP, KIT_GB, KIT_EXP, KIT_P808, KIT_P909, KIT_CYC, KIT_COUNT };

struct DrumRecipe {
    // cuerpo: oscilador con barrido de tono. wave 0 seno, 1 triángulo, 2 cuadrada, 3 pulso estrecho
    float body = 0, f = 100, pitchMul = 1, pitchTau = 0.03f, bodyDec = 0.2f, drive = 0; int wave = 0;
    bool stepPitch = false; float stepMul = 0.8f;        // barrido por escalones (Game Boy)
    float fmRatio = 0, fmIndex = 0, fmTau = 0.02f;         // modulador FM sobre el cuerpo
    // ruido: tipo 0 blanco, 1 LFSR 15 bits, 2 LFSR 7 bits, 3 muestreo y retención
    float noise = 0, noiseDec = 0.1f, noiseRate = 1; int noiseType = 0;
    int filt = -1; float ff = 1000, fq = 0.7f;            // -1 sin filtro, 0 LP, 1 HP, 2 BP
    int bursts = 0; float burstGap = 0.01f;                // clap: ráfagas antes de la cola
    // banco metálico (hihats analógicos)
    float metal = 0, metalDec = 0.05f, metalBP = 10000, metalHP = 7000; float metalMul = 1;
    // clics (Experimental)
    float click = 0; int clicks = 1; float clickGap = 0.004f, clickBP = 0, clickQ = 1;
    bool stepped = false; float stepPer = 0.02f; int steps = 8;   // envolvente por escalones (Game Boy)
    float snap = 0.0f, attack = 0.0005f;                       // «golpe»: ráfaga de ataque y dureza
    float swAmt = 0.0f, swTau = 0.03f;                         // barrido extra para ruido, metal y filtro
    float fmFb = 0.0f;                                        // realimentación del modulador FM (kit FM)
};

inline float semi(float s) { return std::exp2(s / 12.0f); }
inline const float kMetal[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

// tune en semitonos; decay y tone de 0 a 1
inline DrumRecipe drumRecipe(int kit, int lane, float tune, float decay, float tone) {
    DrumRecipe r; const float s = semi(tune);
    switch (kit) {
    case KIT_808: case KIT_DRM: {
        const bool drm = kit == KIT_DRM;
        switch (lane) {
        case 0: r.body = 1; r.f = (drm ? 50 : 47) * s; r.pitchMul = drm ? 6 : 3.2f; r.pitchTau = drm ? 0.018f : 0.03f;
                r.bodyDec = drm ? 0.07f + decay * 0.35f : 0.08f + decay * 0.45f; r.drive = drm ? 2 + tone * 4 : 0;
                r.click = (drm ? 0.15f : 0.35f) * tone; r.clickBP = 1500; r.clickQ = 0.7f; break;
        case 1: r.body = drm ? 0.7f : 0.8f; r.f = (drm ? 185 : 180) * s; r.pitchMul = drm ? 1.3f : 1.0f; r.pitchTau = 0.01f; r.wave = drm ? 1 : 0;
                r.bodyDec = 0.03f + decay * 0.07f; r.fmRatio = drm ? 0 : 1.83f; r.fmIndex = drm ? 0 : 0.15f; r.fmTau = 1;
                r.noise = drm ? 0.35f + tone * 0.5f : 0.25f + tone * 0.6f; r.noiseDec = (drm ? 0.05f + decay * 0.18f : 0.04f + decay * 0.14f);
                r.filt = drm ? 2 : 1; r.ff = drm ? 3500 + tone * 3000 : 1800; r.fq = drm ? 0.7f : 0.7f; break;
        case 2: r.noise = 1; r.noiseDec = 0.04f + decay * 0.15f; r.filt = 2; r.ff = (1000 + tone * 900) * s; r.fq = 1.4f; r.bursts = 3; r.burstGap = drm ? 0.009f : 0.011f; break;
        case 3: r.body = 0.8f; r.f = 1700 * s; r.wave = drm ? 2 : 0; r.bodyDec = 0.006f + decay * 0.02f; r.fmRatio = 460.0f / 1700.0f; r.fmIndex = 0.3f; r.fmTau = 1;
                r.filt = -1; break;
        case 4: r.body = 1; r.f = 105 * s; r.pitchMul = 1.6f; r.pitchTau = 0.04f; r.bodyDec = 0.06f + decay * 0.3f;
                r.noise = tone > 0.5f || drm ? 0.15f * (0.4f + tone) : 0; r.noiseDec = 0.01f; r.filt = 0; r.ff = 3000; break;
        case 5: case 6: { const bool open = lane == 6;
                r.metal = drm ? 0.8f : 1.0f; r.metalDec = open ? 0.08f + decay * 0.35f : 0.008f + decay * 0.03f; r.metalBP = 9000 + tone * 3000; r.metalHP = 6500 + tone * 2000; r.metalMul = s;
                if (drm) { r.noise = 0.5f; r.noiseDec = r.metalDec; r.filt = 1; r.ff = 8000; }
                break; }
        default: r.metal = 0.6f; r.metalMul = s * 2.6f; r.metalDec = 0.03f + decay * 0.2f; r.metalBP = (1600 + tone * 1800) * s; r.metalHP = 400; break;
        }
        break; }
    case KIT_CHIP:
        switch (lane) {
        case 0: r.body = 0.95f; r.f = 48 * s; r.pitchMul = 4; r.pitchTau = 0.012f; r.fmRatio = 1; r.fmIndex = 6; r.fmTau = 0.012f; r.bodyDec = 0.08f + decay * 0.3f; break;
        case 1: r.body = 0.55f; r.f = 190 * s; r.pitchMul = 1.5f; r.pitchTau = 0.012f; r.fmRatio = 2.71f; r.fmIndex = 14; r.fmTau = 0.03f; r.bodyDec = 0.04f + decay * 0.12f;
                r.noise = 0.3f * (0.3f + tone); r.noiseDec = 0.05f + decay * 0.1f; r.filt = 1; r.ff = 2500; break;
        case 2: r.body = 0.45f; r.f = 1200 * s; r.fmRatio = 3.3f; r.fmIndex = 30; r.fmTau = 0.04f; r.bodyDec = 0.03f + decay * 0.12f; r.bursts = 3; r.burstGap = 0.011f; break;
        case 3: r.body = 0.6f; r.f = 950 * s; r.fmRatio = 1.5f; r.fmIndex = 4 + tone * 4; r.fmTau = 0.01f; r.bodyDec = 0.012f + decay * 0.03f; break;
        case 4: r.body = 0.8f; r.f = 115 * s; r.pitchMul = 1.8f; r.pitchTau = 0.04f; r.fmRatio = 1; r.fmIndex = 2 + tone * 4; r.fmTau = 0.04f; r.bodyDec = 0.08f + decay * 0.3f; break;
        case 5: case 6: r.body = 0.35f; r.f = 3900 * s; r.fmRatio = 1.43f; r.fmIndex = 40; r.fmTau = lane == 6 ? 0.2f : 0.05f;
                r.bodyDec = lane == 6 ? 0.09f + decay * 0.35f : 0.012f + decay * 0.04f; r.filt = -1; r.metalHP = 6000 + tone * 3000; break;
        default: r.body = 0.5f; r.f = 680 * s; r.fmRatio = 3.5f; r.fmIndex = 5 + tone * 5; r.fmTau = 0.15f; r.bodyDec = 0.08f + decay * 0.4f; break;
        }
        break;
    case KIT_GB:
        r.stepped = true;
        switch (lane) {
        case 0: r.body = 0.9f; r.wave = 2; r.f = 420 * s; r.stepPitch = true; r.stepMul = 0.78f; r.steps = 8; r.stepPer = 0.012f + decay * 0.05f; r.filt = 0; r.ff = 2500 + tone * 6000; break;
        case 1: r.noise = 0.6f; r.noiseType = 1; r.noiseRate = (1.5f + tone * 1.5f) * s; r.steps = 10; r.stepPer = 0.008f + decay * 0.03f;
                r.body = 0.4f; r.wave = 3; r.f = 220 * s; break;
        case 2: r.noise = 0.55f; r.noiseType = 1; r.noiseRate = 1.2f * s; r.bursts = 3; r.burstGap = 0.012f; r.steps = 8; r.stepPer = 0.008f + decay * 0.03f; break;
        case 3: r.body = 0.6f; r.wave = 3; r.f = 1700 * s; r.steps = 3; r.stepPer = 0.006f + decay * 0.01f; break;
        case 4: r.body = 0.9f; r.wave = 0; r.f = 260 * s; r.stepPitch = true; r.stepMul = 0.93f; r.steps = 8; r.stepPer = 0.02f + decay * 0.05f; break;
        case 5: r.noise = 0.45f; r.noiseType = 2; r.noiseRate = (6 + tone * 10) * s; r.steps = 3; r.stepPer = 0.006f + decay * 0.012f; break;
        case 6: r.noise = 0.4f; r.noiseType = 2; r.noiseRate = (6 + tone * 10) * s; r.steps = 12; r.stepPer = 0.015f + decay * 0.04f; break;
        default: r.body = 0.5f; r.wave = 2; r.f = 880 * s; r.stepPitch = true; r.stepMul = 1.5f; r.steps = 6; r.stepPer = 0.01f + decay * 0.04f; break;
        }
        break;
    default:  // Experimental: senos puros, clics y ruido digital
        switch (lane) {
        case 0: r.body = 0.95f; r.f = 44 * s; r.bodyDec = 0.04f + decay * 0.2f; r.click = 0.4f + tone * 0.6f; break;
        case 1: r.noise = 1.6f; r.noiseDec = 0.004f + decay * 0.03f; r.filt = 2; r.ff = 5000 * s; r.fq = 3; r.body = 0.45f * (0.3f + tone); r.f = 3150 * s; r.bodyDec = 0.01f + decay * 0.04f; break;
        case 2: r.click = 20; r.clicks = 6 + static_cast<int>(decay * 10); r.clickGap = 0.003f + tone * 0.006f; r.clickBP = 2400 * s; r.clickQ = 8; break;
        case 3: r.click = 90; r.clickBP = 2600 * s; r.clickQ = 25 + decay * 60; break;
        case 4: r.body = 0.7f; r.f = 55 * s; r.pitchMul = 320.0f / 55.0f; r.pitchTau = 0.03f + decay * 0.15f; r.bodyDec = 0.08f + decay * 0.3f; break;
        case 5: r.click = 2.5f; r.clickBP = -(7000 + tone * 6000); break;   // negativo = paso alto
        case 6: r.body = 0.12f; r.f = 11500 * semi(tune / 4); r.bodyDec = 0.08f + decay * 0.5f; r.click = 0.8f; break;
        default: r.noise = 0.5f; r.noiseType = 3; r.noiseRate = 8 + tone * 40; r.noiseDec = 0.005f + decay * 0.03f; break;
        }
        break;
    }
    return r;
}

class DrumVoice {
public:
    void prepare(float sr) { sr_ = sr; }
    void trigger(const DrumRecipe& r, float vel) {
        // si sonaba, su última muestra se apaga en ~2 ms en vez de cortarse (sin clic al redisparar)
        if (on_) tail_ += last_;
        r_ = r; t_ = 0; v_ = vel; ph_ = fph_ = 0; fbPrev_ = 0; for (float& m : mph_) m = 0; on_ = true; choke_ = 1.0f; chokeOn_ = false;
        lfsr_ = 0x7fff; swN_ = 0; nAcc_ = 0; sh_ = 0; stepF_ = r.f; stepT_ = 0; clickN_ = 0; clickT_ = 0;
        if (r.filt >= 0) nf_.set(r.filt, r.ff, r.fq, sr_);
        mbp_.set(2, r.metalBP, 0.9f, sr_); mhp_.set(1, r.metalHP, 0.7f, sr_);
        if (r.clickBP > 0) cf_.set(2, r.clickBP, r.clickQ, sr_); else if (r.clickBP < 0) cf_.set(1, -r.clickBP, 0.7f, sr_);
        hhp_.set(1, r.metalHP, 0.7f, sr_); sbp_.set(1, 2500, 0.7f, sr_);
        drvNorm_ = r.drive > 0 ? 1.0f / std::tanh(r.drive) : 1.0f;
        // envolventes exponenciales recursivas: un multiplicador por muestra en vez de un exp() por muestra
        auto k = [&](float tau) { return std::exp(-1.0f / (std::max(tau, 0.0005f) * sr_)); };
        kBody_ = k(r.bodyDec); kNoise_ = k(r.noiseDec); kMetal_ = k(r.metalDec); kPitch_ = k(r.pitchTau); kFm_ = k(r.fmTau); kSw_ = k(r.swTau); kSnap_ = k(0.005f);
        eBody_ = eNoise_ = eMetal_ = ePitch_ = eFm_ = eSw_ = eSnap_ = 1.0f;
        life_ = std::max({r.bodyDec * 8, r.noiseDec * 8, r.metalDec * 8, r.stepped ? r.stepPer * (r.steps + 1) : 0.0f, r.bursts * r.burstGap + r.noiseDec * 8, r.clicks * r.clickGap + 0.25f, 0.05f});
    }
    void choke() { if (on_) chokeOn_ = true; }
    bool active() const { return on_ || std::fabs(tail_) > 1e-5f; }
    float process() {
        tail_ *= 0.9896f;   // ~2 ms
        if (!on_) return tail_;
        const float dt = 1.0f / sr_, t = t_;
        float out = 0.0f;
        const float at = r_.attack; const bool inAtk = t < at, atkEnv = inAtk;
        const float atkV = inAtk ? t / at : 1.0f;
        float stepEnv = 1.0f;
        const float swF = r_.swAmt > 0 ? 1.0f + r_.swAmt * eSw_ : 1.0f;
        eSw_ *= kSw_;
        if (r_.swAmt > 0 && r_.filt >= 0 && (++swN_ & 31) == 0) nf_.set(r_.filt, std::min(r_.ff * swF, 18000.0f), r_.fq, sr_);
        if (r_.stepped) { const int k = static_cast<int>(t / r_.stepPer); stepEnv = k >= r_.steps ? 0.0f : 1.0f - static_cast<float>(k) / r_.steps; }
        if (r_.body > 0) {
            float f;
            if (r_.stepPitch) { stepT_ += dt; while (stepT_ >= 1.0f / 128.0f) { stepT_ -= 1.0f / 128.0f; stepF_ *= r_.stepMul; } f = std::clamp(stepF_, 20.0f, 12000.0f); }
            else f = r_.f * (1.0f + (r_.pitchMul - 1.0f) * ePitch_);
            ePitch_ *= kPitch_;
            float pm = 0.0f;
            if (r_.fmIndex > 0) {
                fph_ += f * r_.fmRatio * dt; fph_ -= std::floor(fph_);
                const float mo = fsin(fph_ + r_.fmFb * 0.25f * fbPrev_); fbPrev_ = mo;
                pm = mo * r_.fmIndex * eFm_ / (2 * kPi);
            }
            eFm_ *= kFm_;
            ph_ += f * dt; ph_ -= std::floor(ph_);
            const float q = ph_ + pm - std::floor(ph_ + pm);
            float o;
            switch (r_.wave) { case 1: o = 1 - 4 * std::fabs(q - 0.5f); break; case 2: o = q < 0.5f ? 0.8f : -0.8f; break; case 3: o = q < 0.125f ? 0.8f : -0.2f; break; default: o = fsin(q); }
            if (r_.drive > 0) o = std::tanh(o * r_.drive) * drvNorm_;
            out += o * r_.body * (r_.stepped ? stepEnv : atkEnv ? atkV : eBody_);
        }
        if (!inAtk) eBody_ *= kBody_;
        if (r_.noise > 0) {
            float n;
            if (r_.noiseType == 0) n = noise_.next();
            else if (r_.noiseType == 3) { nAcc_ += r_.noiseRate * swF * 100.0f * dt; if (nAcc_ >= 1) { nAcc_ -= std::floor(nAcc_); sh_ = std::round(noise_.next() * 3) / 3; } n = sh_; }
            else { nAcc_ += r_.noiseRate * swF; while (nAcc_ >= 1) { nAcc_ -= 1; const unsigned b = (lfsr_ ^ (lfsr_ >> 1)) & 1u; lfsr_ = (lfsr_ >> 1) | (b << 14); if (r_.noiseType == 2) lfsr_ = (lfsr_ & ~0x40u) | (b << 6); } n = (lfsr_ & 1u) ? -0.7f : 0.7f; }
            if (r_.filt >= 0) n = nf_.process(n);
            float e;
            if (r_.bursts > 0) {
                const float tb = r_.bursts * r_.burstGap;
                if (t < tb) { const float u = std::fmod(t, r_.burstGap); e = std::exp(-u / 0.003f); eNoise_ = 1.0f; }
                else { e = 0.9f * eNoise_; eNoise_ *= kNoise_; }
            } else { e = r_.stepped ? stepEnv : atkEnv ? atkV : eNoise_; if (!inAtk) eNoise_ *= kNoise_; }
            out += n * r_.noise * e;
        }
        if (r_.metal > 0) {
            float m = 0.0f;
            for (int k = 0; k < 6; ++k) { mph_[k] += kMetal[k] * r_.metalMul * swF * dt; if (mph_[k] >= 1) mph_[k] -= 1; m += mph_[k] < 0.5f ? 1.0f : -1.0f; }
            out += mhp_.process(mbp_.process(m * 0.18f)) * r_.metal * 2.2f * (atkEnv ? atkV : eMetal_);
            if (!inAtk) eMetal_ *= kMetal_;
        }
        if (r_.click > 0) {
            float c = 0.0f;
            if (clickN_ < r_.clicks && t >= clickT_) { c = 1.0f * (1.0f - clickN_ / 18.0f); ++clickN_; clickT_ += r_.clickGap; }
            if (r_.clickBP != 0) c = cf_.process(c);
            out += c * r_.click;
        }
        if (r_.body > 0 && r_.metalHP > 0 && r_.metal == 0 && r_.f > 3000) out = hhp_.process(out);   // hihats FM
        // golpe: ráfaga de ruido agudo y realce de los primeros milisegundos
        if (r_.snap > 0 && t < 0.04f) { const float e = eSnap_; out = out * (1.0f + r_.snap * 1.8f * e) + sbp_.process(noise_.next()) * r_.snap * 0.9f * e; }
        eSnap_ *= kSnap_;
        if (chokeOn_) { choke_ *= 0.995f; if (choke_ < 0.001f) { on_ = false; chokeOn_ = false; } }
        t_ += dt;
        if (t_ > life_) on_ = false;
        last_ = on_ ? out * v_ * choke_ : 0.0f;
        return last_ + tail_;
    }
private:
    DrumRecipe r_; float sr_ = 48000, t_ = 0, v_ = 1, ph_ = 0, fph_ = 0, fbPrev_ = 0, mph_[6] = {}, nAcc_ = 0, sh_ = 0, stepF_ = 100, stepT_ = 0, clickT_ = 0, choke_ = 1, drvNorm_ = 1;
    float kBody_ = 0, kNoise_ = 0, kMetal_ = 0, kPitch_ = 0, kFm_ = 0, kSw_ = 0, kSnap_ = 0, eBody_ = 1, eNoise_ = 1, eMetal_ = 1, ePitch_ = 1, eFm_ = 1, eSw_ = 1, eSnap_ = 1, life_ = 1;
    float last_ = 0, tail_ = 0;
    int clickN_ = 0; unsigned swN_ = 0; bool on_ = false, chokeOn_ = false; unsigned lfsr_ = 0x7fff;
    BQ nf_, mbp_, mhp_, cf_, hhp_, sbp_; Noise noise_;
};

// sweep, snap: 0,5 = como viene el kit. out: canal de la mezcla (0 Bombo, 1 Caja, 2 Resto)
// Parámetros por sonido. filt: −1..0 paso bajo, 0..1 paso alto. vel: cuánto responde a la velocidad.
// ratio, index, idec, fb y noise solo los usa el kit FM.
// tone: el TONE de la 808/909. color, shape, contour: macros del kit FM (como en el Model:Cycles).
struct DrumLane {
    float tune = 0, decay = 0.5f, snap = 0.5f, filt = 0, reso = 0.2f, sweep = 0.5f, level = 0.8f, pan = 0.0f, vel = 0.8f;
    float tone = 0.5f, color = 0.5f, shape = 0.5f, contour = 0.5f;
    float drive = 0.0f;   // ya no se toca desde fuera
    bool mute = false; int note = 36, out = 0;
};
// Valores de partida del kit FM por sonido (bombo, caja, clap, rim, tom, HH cerrado, HH abierto, perc)
inline const float kCycColor[8] = {0.35f, 0.5f, 0.5f, 0.4f, 0.3f, 0.6f, 0.6f, 0.45f};
inline const float kCycShape[8] = {0.3f, 0.5f, 0.5f, 0.6f, 0.3f, 0.5f, 0.5f, 0.35f};
inline const float kCycSweep[8] = {0.55f, 0.3f, 0.4f, 0.2f, 0.45f, 0.2f, 0.2f, 0.25f};
inline const float kCycContour[8] = {0.4f, 0.4f, 0.5f, 0.2f, 0.45f, 0.2f, 0.6f, 0.35f};

// ---------------------------------------------------------------- clap 808/909: como el circuito
// Dos caminos del mismo ruido: las palmadas (3 a 6 golpes en diente de sierra, a unos 10 ms) y la «sala»,
// una cola que entra con la última palmada. Tone = filtro, Snap = manos, Sweep = separación, Decay = cola.
struct ClapVoice {
    float sr = 48000, gain = 0, gap = 0.01f, eb = 0, et = 0, ta = 0, rise = 0, kB = 0, kT = 0, kA = 0, t = 0, nextB = 0; int bursts = 4, nb = 0; bool on = false;
    BQ bp, bp2, hp; Noise nz, nz2;
    void trigger(float g, float decay, float tone, float snap, float sweep, bool is909) {
        on = true; t = 0; gain = g; nb = 0; nextB = 0; eb = 0; et = 0; ta = 1; rise = 0; nz2.s ^= 0x5bd1e995u;
        bursts = 3 + static_cast<int>(snap * 2.99f);
        gap = (is909 ? 0.0095f : 0.0115f) * (0.5f + sweep);
        const float d2 = decay * decay, tail = is909 ? 0.03f + d2 * 0.45f : 0.05f + d2 * 0.7f, fc = (is909 ? 650.0f : 500.0f) * std::exp2(tone * 2.0f);
        kB = std::exp(-1.0f / ((is909 ? 0.0028f : 0.0035f) * sr)); kT = std::exp(-1.0f / (tail * sr)); kA = 1.0f - std::exp(-1.0f / (0.001f * sr));
        bp.set(2, fc, is909 ? 1.2f : 1.6f, sr); bp2.set(2, fc * (is909 ? 1.4f : 1.0f), is909 ? 0.9f : 1.0f, sr); hp.set(1, is909 ? 700.0f : 350.0f, 0.7f, sr);
    }
    bool active() const { return on; }
    float process() {
        if (!on) return 0.0f;
        if (nb < bursts && t >= nextB) { ++nb; nextB += gap; eb = 1.0f; }
        const bool last = nb >= bursts;
        if (last) { rise += (1.0f - rise) * kA; et = ta * rise; ta *= kT; }
        eb *= kB; t += 1.0f / sr;
        if (last && ta < 1e-4f && eb < 1e-4f) on = false;
        const float x = bp.process(nz.next()) * eb * 3.2f + bp2.process(nz2.next()) * et * 1.5f;
        return softClip(hp.process(x) * gain * 2.0f) * 0.66f;
    }
};

// ---------------------------------------------------------------- hi-hats 808/909 (propio, sobre el circuito publicado)
// 808: las seis cuadradas del original, con sus frecuencias, sin aliasing, por dos bandas y el VCA asimétrico.
// 909: el original es una muestra de 6 bits; aquí, tres parejas en anillo (como Plaits) y ruido blanco de verdad.
// Tone = brillo, Snap = ruido, Sweep = el filtro cae, Decay = duración.
struct HatVoice {
    float sr = 48000, ph[6] = {}, inc[6] = {}, e = 0, k = 0, kF = 0, gain = 0, nm = 0, fc = 8000, sw = 0, eS = 0, kS = 0; bool on = false, k9 = false; int cnt = 0; float bqQ = 1;
    BQ bp, bp2, hp; Noise nz;
    void trigger(bool is909, float g, float accent, float tuneRatio, float tau, float tone, float snap, float sweep, float fMul) {
        k9 = is909; on = true; gain = g; e = 0.55f + 0.45f * accent; cnt = 0;
        static const float F8[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f}, F9[6] = {200, 7530, 510, 8075, 730, 10500};
        for (int i = 0; i < 6; ++i) inc[i] = (k9 ? F9[i] : F8[i]) * tuneRatio / sr;
        nm = 0.02f + snap * snap * 0.8f;
        fc = (k9 ? 9500.0f : 7100.0f) * fMul * std::exp2((tone - 0.5f) * 1.6f); bqQ = k9 ? 0.8f : 1.3f;
        sw = std::max(0.0f, sweep - 0.3f) * 1.4f; eS = 1; kS = std::exp(-1.0f / (0.05f * sr));
        bp.set(2, fc, bqQ, sr); bp2.set(2, 3440.0f * tuneRatio, 2.0f, sr); hp.set(1, (k9 ? 6500.0f : 5500.0f) * fMul, 0.8f, sr);
        k = std::exp(-1.0f / (tau * sr)); kF = std::exp(-1.0f / (0.012f * sr));
    }
    void choke() { if (on) k = std::min(k, std::exp(-1.0f / (0.004f * sr))); }
    bool active() const { return on; }
    float process() {
        if (!on) return 0.0f;
        for (int i = 0; i < 6; ++i) { ph[i] += inc[i]; if (ph[i] >= 1) ph[i] -= 1; }
        float m;
        if (k9) m = blepPulse(ph[0], inc[0], 0.5f) * blepSaw(ph[1], inc[1]) + blepPulse(ph[2], inc[2], 0.5f) * blepSaw(ph[3], inc[3]) + blepPulse(ph[4], inc[4], 0.5f) * blepSaw(ph[5], inc[5]);
        else { m = 0; for (int i = 0; i < 6; ++i) m += blepPulse(ph[i], inc[i], 0.5f); m *= 0.5f; }
        const float x = m * (1.0f - nm) + nz.next() * 2.0f * nm;
        if (sw > 0.01f && (++cnt & 31) == 0) { eS *= std::pow(kS, 32.0f); bp.set(2, fc * (1.0f + sw * eS * 0.8f) / (1.0f + sw * 0.8f), bqQ, sr); }
        float y = bp.process(x);
        if (k9) { y *= e; e *= e > 0.4f ? kF : k; }
        else {   // VCA en clase A: deja pasar más los positivos; de ahí sale el chisporroteo del 808
            float s = (y + 0.35f * bp2.process(x)) * e * 2.2f; s *= s > 0 ? 2.2f : 0.35f; y = s / (1.0f + std::fabs(s)) * 1.15f; e *= k;
        }
        if (e < 1.5e-4f) on = false;
        return hp.process(y) * gain * (k9 ? 0.6f : 0.85f);
    }
};

// ---------------------------------------------------------------- cencerro 808: dos cuadradas, paso banda y doble caída
struct CowbellVoice {
    float sr = 48000, ph1 = 0, ph2 = 0, f1 = 540, f2 = 800, e = 0, k1 = 0, k2 = 0, gain = 0; bool on = false; BQ bp;
    void trigger(float g, float tune, float decay, float tone) {
        on = true; e = 1; gain = g; f1 = 540.0f * semi(tune); f2 = 800.0f * semi(tune);
        k1 = std::exp(-1.0f / (0.012f * sr)); k2 = std::exp(-1.0f / ((0.06f + decay * 0.5f) * sr));
        bp.set(2, 1500.0f * std::exp2(tone * 1.6f), 1.2f, sr);
    }
    bool active() const { return on; }
    float process() {
        if (!on) return 0.0f;
        ph1 += f1 / sr; if (ph1 >= 1) ph1 -= 1; ph2 += f2 / sr; if (ph2 >= 1) ph2 -= 1;
        const float x = (ph1 < 0.5f ? 0.5f : -0.5f) + (ph2 < 0.5f ? 0.5f : -0.5f);
        e *= e > 0.4f ? k1 : k2;
        if (e < 1e-4f) on = false;
        return bp.process(x) * e * gain * 1.6f;
    }
};

// ---------------------------------------------------------------- kit FM al estilo Model:Cycles
// Cada sonido es una «máquina» FM (bombo, caja, metal, perc, clap) con cuatro macros:
//   Color (brillo: cantidad de FM), Shape (carácter: relación del modulador / balance), Sweep (barrido) y Contour (forma).
// Cada macro mueve varios parámetros internos a la vez, como en el Cycles.
enum CycMachine { CY_KICK = 0, CY_SNARE, CY_METAL, CY_PERC, CY_CLAP };
struct CycVoice {
    float sr = 48000; bool on = false; int m = CY_KICK;
    float f = 50, ratio = 1, idx = 0, fb = 0, pDepth = 0, phC = 0, phM = 0, phC2 = 0, phM2 = 0, prev = 0;
    float eA = 0, kA = 0, eP = 0, kP = 0, eI = 0, kI = 0, eN = 0, kN = 0, nMix = 0, gain = 0, hold = 0, holdT = 0, t = 0, burstGap = 0.01f; int bursts = 0, nb = 0; float nextB = 0;
    BQ nbp, nhp, mhp, swf; Noise nz; float swDepth = 0, eS = 0, kS = 0, swBase = 0;
    static float kf(float tau, float sr) { return std::exp(-1.0f / (std::max(tau, 0.0004f) * sr)); }
    // FM = cantidad, Ratio = relación del modulador, Sweep = barrido, FM decay = lo que dura la FM, Noise = ruido.
    // En el clap no hay FM: FM es el tono, Ratio las manos y FM decay la separación.
    void trigger(int machine, float hz, float decay, float color, float shape, float sweep, float contour, float noise, float g) {
        m = machine; on = true; gain = g; t = 0; eA = 1; eP = 1; eI = 1; eN = 1; eS = 1; prev = 0; nb = 0; nextB = 0; hold = 0;
        phC = phM = phC2 = phM2 = 0; f = hz;
        const float d2 = decay * decay, hiN = std::max(0.0f, noise - 0.5f) * 2.0f;
        switch (m) {
        case CY_KICK:
            ratio = 0.5f + shape * 2.5f; idx = color * color * 6.0f; fb = color * 0.3f;
            pDepth = sweep * 4.0f;                                         // hasta 4 octavas de caída
            kP = kf(0.01f + sweep * 0.06f, sr); kI = kf(0.004f + contour * 0.25f, sr);
            kA = kf(0.06f + d2 * 1.6f, sr); nMix = hiN * 0.6f; kN = kf(0.008f, sr); nbp.set(2, 3500.0f, 0.8f, sr); nhp.set(1, 700.0f, 0.7f, sr); break;
        case CY_SNARE:
            ratio = 1.4f + shape * 0.9f; idx = 0.5f + color * 4.0f; fb = 0.1f;
            pDepth = sweep * 1.5f; kP = kf(0.008f, sr); kI = kf(0.005f + contour * 0.15f, sr);
            kA = kf(0.04f + d2 * 0.4f, sr); nMix = noise * 1.2f;
            kN = kf(0.04f + d2 * 0.5f, sr);
            nbp.set(2, 3000.0f, 0.8f, sr); nhp.set(1, 700.0f, 0.7f, sr); break;
        case CY_METAL:
            ratio = 1.41f + shape * 3.1f; idx = 2.0f + color * 9.0f; fb = 0.2f + color * 0.3f;
            pDepth = 0; kI = kf(0.02f + contour * 0.2f, sr);
            holdT = 0;
            kA = kf(0.012f + d2 * 1.2f, sr);
            swDepth = sweep; kS = kf(0.08f, sr); swBase = 5000.0f; mhp.set(1, swBase, 0.7f, sr); nMix = hiN; kN = kf(0.012f + d2 * 1.2f, sr); nhp.set(1, 7000.0f, 0.7f, sr); break;
        case CY_PERC:
            ratio = 0.5f + shape * 6.5f; idx = color * 5.0f; fb = 0.05f;
            pDepth = sweep * 2.0f; kP = kf(0.01f + sweep * 0.04f, sr); kI = kf(0.005f + contour * 0.3f, sr);
            kA = kf(0.03f + d2 * 0.9f, sr); nMix = hiN * 0.6f; kN = kf(0.012f, sr); nbp.set(2, 3500.0f, 0.8f, sr); nhp.set(1, 700.0f, 0.7f, sr); break;
        default:   // CY_CLAP: ráfagas de ruido modulado
            bursts = 2 + static_cast<int>(shape * 4.0f); burstGap = 0.005f + contour * 0.012f;
            kN = kf(0.0025f, sr); kA = kf(0.04f + d2 * 0.5f, sr); nMix = 1;
            nbp.set(2, 800.0f + color * 2400.0f, 1.0f + color, sr); nhp.set(1, 400.0f, 0.7f, sr);
            swDepth = sweep; break;
        }
    }
    bool active() const { return on; }
    float process() {
        if (!on) return 0.0f;
        const float dt = 1.0f / sr; float out = 0;
        if (m == CY_CLAP) {
            if (nb < bursts && t >= nextB) { ++nb; nextB += burstGap; eN = 1; }
            const bool tailOn = nb >= bursts;
            float n = nhp.process(nbp.process(nz.next()));
            const float bE = eN; eN *= kN;
            out = n * (tailOn ? eA : bE) * 2.2f;
            if (tailOn) eA *= kA;
            if (tailOn && eA < 1e-4f) on = false;
            t += dt; return out * gain;
        }
        const float fc = f * std::exp2(pDepth * eP);
        eP *= kP;
        if (m == CY_METAL) {
            // dos parejas FM inarmónicas, como un platillo
            phM += fc * ratio * dt; phM -= std::floor(phM); phM2 += fc * 1.47f * ratio * 1.13f * dt; phM2 -= std::floor(phM2);
            const float mI = idx * (0.3f + 0.7f * eI); eI *= kI;
            const float mo = fsin(phM + fb * prev), mo2 = fsin(phM2);
            prev = mo;
            phC += fc * dt; phC -= std::floor(phC); phC2 += fc * 1.47f * dt; phC2 -= std::floor(phC2);
            float x = fsin(phC + mI * mo * 0.16f) + fsin(phC2 + mI * mo2 * 0.16f);
            if (swDepth > 0.01f && (static_cast<int>(t * sr) & 15) == 0) mhp.set(1, swBase * std::exp2(-swDepth * 2.0f * (1.0f - eS)), 0.7f, sr);
            eS *= kS;
            x = mhp.process(x);
            if (nMix > 0) { x += nhp.process(nz.next()) * nMix * eN; eN *= kN; }
            if (hold < holdT) hold += dt; else eA *= kA;
            out = x * eA * 0.8f;
        } else {
            phM += fc * ratio * dt; phM -= std::floor(phM);
            const float mo = fsin(phM + fb * prev); prev = mo;
            const float mI = idx * eI; eI *= kI;
            phC += fc * dt; phC -= std::floor(phC);
            float x = fsin(phC + mI * mo * 0.16f);
            if (nMix > 0) { const float n = nhp.process(nbp.process(nz.next())); x = x * (1.0f - nMix * 0.5f) + n * nMix * eN * 1.5f; eN *= kN; }
            eA *= kA;
            out = x * eA;
        }
        t += dt;
        if (eA < 1e-4f && t > 0.01f) on = false;
        return out * gain;
    }
};
inline const int kLaneOut[8] = {0, 1, 1, 2, 2, 2, 2, 2};
// Igualación medida con --niveles: dB por kit y sonido (bombo, caja, clap, rim, tom, HH cerrado, HH abierto, perc)
inline float kitTrim(int kit, int l) {
    static const float D[5][8] = {{1.9f, -2.4f, 0.0f, 6.2f, -2.3f, -0.6f, -3.1f, -0.5f}, {1.6f, -1.0f, -1.9f, -3.6f, 0.7f, 1.1f, 0.8f, 1.5f}, {-3.2f, -0.1f, -2.6f, -10.2f, -6.3f, -3.9f, -4.1f, -13.0f}, {-3.3f, -4.8f, -2.5f, 2.6f, -5.0f, -3.0f, -4.9f, -8.5f}, {3.8f, 11.2f, 5.1f, 3.7f, -1.9f, -1.3f, 6.0f, 3.7f}};
    const int k = kit == KIT_P808 ? 0 : kit == KIT_P909 ? 1 : kit == KIT_CYC ? 2 : kit == KIT_GB ? 3 : kit == KIT_EXP ? 4 : -1;
    return k < 0 ? 1.0f : std::pow(10.0f, D[k][std::clamp(l, 0, 7)] / 20.0f);
}

class DrumMachine {
public:
    int kit = KIT_P808;
    std::array<DrumLane, 8> lane;
    DrumMachine() {
        const int N[8] = {36, 38, 39, 37, 45, 42, 46, 56};
        for (int i = 0; i < 8; ++i) { auto& L = lane[static_cast<size_t>(i)]; L.note = N[i]; L.out = kLaneOut[i];
            L.color = kCycColor[i]; L.shape = kCycShape[i]; L.sweep = 0.5f; L.contour = kCycContour[i]; }
    }
    void prepare(float sr) { sr_ = sr; for (auto& v : v_) v.prepare(sr); for (auto& c : cy_) c.sr = sr; for (auto& c : cl_) c.sr = sr; for (auto& c : cb_) c.sr = sr; for (auto& h : ht_) h.sr = sr; }
    // devuelve el carril que suena con esa nota (o -1)
    int noteOn(int note, int vel) {
        for (int l = 0; l < 8; ++l) if (lane[static_cast<size_t>(l)].note == note) { hit(l, vel); return l; }
        return -1;
    }
    void hit(int l, int vel) {
        const DrumLane& L = lane[static_cast<size_t>(l)];
        const float v01 = std::clamp(vel, 1, 127) / 127.0f, vs = std::clamp(L.vel, 0.0f, 1.0f);
        const float g = (1.0f - vs + vs * std::pow(v01, 1.6f)) * L.level * kitTrim(kit, l);   // velocidad → volumen
        lastVel_[l] = v01;
        if (l == 5) { v_[6].choke(); pchoke_ = true; cy_[6].on = false; ht_[6].choke(); }   // el HH cerrado corta el abierto
        if (kit == KIT_P808 || kit == KIT_P909) { hitPlaits(l, L, v01, vs, g); return; }
        if (kit == KIT_CYC) { hitCycles(l, L, v01, vs, g); return; }
        DrumRecipe r = drumRecipe(kit, l, L.tune, L.decay, 0.5f);
        // barrido: escala la caída de tono (o la añade si el sonido no la tiene)
        // barrido: 0 = sin caída de tono, 0,5 = como viene el kit, 1 = caída mucho mayor y más larga.
        // Por encima de 0,5 también barre el ruido, el metal y el filtro (hihats, caja, clap).
        const float up = std::max(0.0f, L.sweep - 0.5f) * 2.0f;
        if (L.sweep <= 0.5f) {
            const float k = L.sweep * 2.0f;
            if (r.stepPitch) r.stepMul = std::pow(r.stepMul, k);
            else r.pitchMul = 1.0f + (r.pitchMul - 1.0f) * k;
        } else {
            if (r.stepPitch) r.stepMul = std::pow(r.stepMul, 1.0f + up * 2.0f);
            else if (r.pitchMul != 1.0f) { r.pitchMul = 1.0f + (r.pitchMul - 1.0f) * (1.0f + up * 3.0f); r.pitchTau *= 1.0f + up * 2.0f; }
            else if (r.body > 0) { r.pitchMul = 1.0f + up * 5.0f; r.pitchTau = 0.015f + up * 0.04f; }
            r.swAmt = up * 3.0f; r.swTau = 0.01f + up * 0.05f;
        }
        // golpe: más clic y ráfaga de ataque por encima de 0,5; ataque más blando por debajo
        r.click *= L.snap * 2.0f;
        r.snap = std::max(0.0f, L.snap - 0.5f) * 2.0f;
        r.attack = 0.0005f + std::max(0.0f, 0.5f - L.snap) * 0.05f;
        // velocidad: más fuerte = más volumen, más golpe y más caída de tono
        r.click *= 1.0f - vs * 0.7f * (1.0f - v01);
        r.snap += vs * std::max(0.0f, v01 - 0.6f) * 0.8f;
        if (!r.stepPitch && r.pitchMul > 1.0f) r.pitchMul = 1.0f + (r.pitchMul - 1.0f) * (1.0f - vs * 0.5f * (1.0f - v01));
        lastVel_[l] = v01;
        v_[static_cast<size_t>(l)].trigger(r, g);
    }
    // escribe cada carril por separado (para mandarlos a los canales Bombo / Caja / Resto)
    void prepareFilters(float sr) { sr_ = sr; }
    void render(float* const* outs, int n) {
        for (int l = 0; l < 8; ++l) {
            float* o = outs[l]; auto& v = v_[static_cast<size_t>(l)];
            for (int i = 0; i < n; ++i) o[i] = 0.0f;
            bool any = false;
            if (v.active()) { any = true; for (int i = 0; i < n; ++i) o[i] += v.process() * 0.8f; }
            if (plife_[l] > 0) { any = true; renderPlaits(l, o, n); }
            if (cy_[l].active()) { any = true; for (int i = 0; i < n; ++i) o[i] += cy_[l].process(); }
            if (ht_[l].active()) { any = true; for (int i = 0; i < n; ++i) o[i] += ht_[l].process(); }
            if (cl_[l].active()) { any = true; for (int i = 0; i < n; ++i) o[i] += cl_[l].process(); }
            if (cb_[l].active()) { any = true; for (int i = 0; i < n; ++i) o[i] += cb_[l].process(); }
            if (!any) continue;
            // filtro por sonido: paso bajo (a la izquierda) o paso alto (a la derecha), con resonancia;
            // con velocidad, el paso bajo se abre más al tocar fuerte
            const DrumLane& L = lane[static_cast<size_t>(l)];
            if (std::fabs(L.filt) > 0.02f) {
                const bool hp = L.filt > 0;
                float fc = hp ? 12.0f * std::exp2(L.filt * 10.5f) : 20000.0f * std::exp2(L.filt * 9.5f);
                if (!hp) fc *= std::exp2(L.vel * (lastVel_[l] - 0.7f) * 2.0f);
                const float gg = std::tan(kPi * std::clamp(fc, 12.0f, 0.45f * sr_) / sr_), k = 2.0f - 1.9f * std::clamp(L.reso, 0.0f, 1.0f), a1 = 1.0f / (1.0f + gg * (gg + k));
                float& c1 = fs_[l][0]; float& c2 = fs_[l][1];
                for (int i = 0; i < n; ++i) {
                    const float x = o[i], v3 = x - c2, v1 = a1 * (c1 + gg * v3), v2 = c2 + gg * v1;
                    c1 = 2 * v1 - c1; c2 = 2 * v2 - c2;
                    o[i] = hp ? x - k * v1 - v2 : v2;
                }
            }
        }
    }
private:
    // ---- 808 y 909 (Plaits). Snap y Sweep según el sonido: ver hitPlaits.
    struct PSpec { int model; float hz, decMul, gain; };
    static PSpec plaitsSpec(int kit, int l) {
        const bool k9 = kit == KIT_P909;
        switch (l) {
            // ganancias medidas para que los kits suenen a niveles parecidos
            case 0: return {k9 ? pd::BD909 : pd::BD808, k9 ? 52.0f : 48.0f, 1.0f, k9 ? 1.1f : 2.0f};
            case 1: return {k9 ? pd::SD909 : pd::SD808, k9 ? 200.0f : 185.0f, 1.0f, k9 ? 1.25f : 1.4f};
            case 3: return {k9 ? pd::BD909 : pd::BD808, k9 ? 1800.0f : 1700.0f, 0.25f, k9 ? 0.7f : 1.4f};   // rim: cuerpo agudo y corto
            case 4: return {k9 ? pd::BD909 : pd::BD808, k9 ? 140.0f : 125.0f, 0.8f, k9 ? 0.9f : 1.8f};      // tom
            case 5: return {k9 ? pd::HH909 : pd::HH808, k9 ? 260.0f : 205.0f, 0.45f, k9 ? 0.63f : 0.8f};    // HH cerrado
            case 6: return {k9 ? pd::HH909 : pd::HH808, k9 ? 260.0f : 205.0f, 1.0f, k9 ? 0.63f : 0.8f};     // HH abierto
            case 7: return {k9 ? pd::HH909 : pd::HH808, k9 ? 380.0f : 300.0f, 1.0f, 0.56f};                 // 909: ride (en la 808 es el cencerro)
            default: return {pd::SD808, 1000.0f, 1.0f, 1.0f};                                  // clap: aparte
        }
    }
    void hitPlaits(int l, const DrumLane& L, float v01, float vs, float g) {
        const bool k9 = kit == KIT_P909;
        if (l == 2) { cl_[l].trigger(g * (k9 ? 0.63f : 0.7f), L.decay, L.tone, L.snap, L.sweep, k9); return; }
        if (l == 5 || l == 6 || (l == 7 && k9)) {   // hats y ride con voz propia: Decay es tiempo real
            const float d2 = L.decay * L.decay, tau = l == 5 ? 0.005f + d2 * 0.07f : l == 6 ? 0.03f + d2 * 0.5f : 0.2f + d2 * 3.0f;
            ht_[l].trigger(k9, g * (l == 7 ? 0.8f : 1.0f), std::clamp((1.0f - vs) * 0.6f + vs * v01, 0.0f, 1.0f), semi(L.tune) * (l == 7 ? 1.3f : 1.0f), tau, L.tone, L.snap, L.sweep, l == 7 ? 0.8f : 1.0f);
            return;
        }
        if (l == 7 && !k9) { cb_[l].trigger(g * 0.7f, L.tune, L.decay, L.tone); return; }
        const PSpec ps = plaitsSpec(kit, l);
        pv_[l].setModel(ps.model);
        pv_[l].trigger(std::clamp((1.0f - vs) * 0.6f + vs * v01, 0.0f, 1.0f));   // velocidad → acento de Plaits (más golpe y brillo)
        pg_[l] = g * ps.gain; pchoke6_ = l == 6 ? false : pchoke6_;
        plife_[l] = static_cast<int>(sr_ * (0.15f + 6.0f * L.decay * ps.decMul));
        if (l == 6) { pchoke_ = false; pcg_ = 1.0f; }
    }
    void renderPlaits(int l, float* o, int n) {
        const DrumLane& L = lane[static_cast<size_t>(l)];
        const PSpec ps = plaitsSpec(kit == KIT_P909 ? KIT_P909 : KIT_P808, l);
        const float hz = ps.hz * semi(L.tune);
        float buf[256]; float peak = 0.0f;
        for (int off = 0; off < n; off += 256) {
            const int k = std::min(256, n - off);
            // bombos y toms: Snap = golpe de ataque, Sweep = barrido de tono. Cajas: Snap = snappy. Hihats: Snap = ruido.
            pv_[l].render(buf, k, sr_, hz, L.tone, std::clamp(L.decay * ps.decMul, 0.0f, 1.0f), L.snap, L.sweep);
            for (int i = 0; i < k; ++i) {
                float x = buf[i] * pg_[l];
                if (l == 6 && pchoke_) { pcg_ *= 0.995f; x *= pcg_; }
                o[off + i] += x; peak = std::max(peak, std::fabs(x));
            }
        }
        plife_[l] -= n;
        if ((l == 6 && pchoke_ && pcg_ < 1e-3f) || (plife_[l] < static_cast<int>(sr_ * 0.1f) && peak < 1e-5f)) plife_[l] = 0;
    }
    // ---- FM al estilo Model:Cycles
    void hitCycles(int l, const DrumLane& L, float v01, float vs, float g) {
        static const int M[8] = {CY_KICK, CY_SNARE, CY_CLAP, CY_PERC, CY_KICK, CY_METAL, CY_METAL, CY_PERC};
        static const float F[8] = {52, 190, 1000, 820, 110, 420, 420, 560};
        static const float DM[8] = {1.0f, 1.0f, 1.0f, 0.35f, 0.9f, 0.4f, 1.0f, 0.9f};
        // velocidad: más fuerte = más brillo (Color) además de más volumen
        const float col = std::clamp(L.color * (1.0f - vs * 0.5f * (1.0f - v01)), 0.0f, 1.0f);
        static const float G[8] = {0.9f, 0.63f, 0.72f, 0.9f, 0.9f, 0.36f, 0.32f, 0.9f};
        cy_[l].trigger(M[l], F[l] * semi(L.tune), std::clamp(L.decay * DM[l], 0.0f, 1.0f), col, L.shape, L.sweep, L.contour, L.snap, g * G[l]);
    }
    std::array<DrumVoice, 8> v_; float sr_ = 48000, lastVel_[8] = {0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f}, fs_[8][2] = {};
    pd::Drum pv_[8]; float pg_[8] = {}; int plife_[8] = {}; bool pchoke_ = false, pchoke6_ = false; float pcg_ = 1.0f;
    CycVoice cy_[8]; ClapVoice cl_[8]; CowbellVoice cb_[8]; HatVoice ht_[8];
};

}  // namespace gb

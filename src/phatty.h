// phatty.h — sinte mono al estilo del Moog Slim Phatty / Little Phatty para Freon Machine.
//
// Lo que tiene el Slim Phatty (manual de Moog y reseñas): 2 osciladores con forma de onda continua (triángulo → sierra →
// cuadrada → pulso estrecho), octava 16'/8'/4'/2' en cada uno, osc 2 desplazable ±7 semitonos y con sync al osc 1; filtro
// Moog en escalera con 1 a 4 polos (6/12/18/24 dB), Overload (la entrada del filtro se satura), seguimiento de teclado y
// cantidad de envolvente bipolar (a las 12 = 0); dos ADSR (filtro y volumen) y glide; modulación: una fuente (las 4 formas
// del LFO, envolvente del filtro o S&H, osc 2 o ruido) hacia un destino (afinación, filtro, forma de onda u osc 2), con
// Mod Amount como profundidad máxima que la rueda de modulación recorre de 0 a ese máximo (sin rueda, no hay modulación).
//
// Rangos del original que se conocen: corte 20 Hz–16 kHz, LFO 0,2–50 Hz. Las curvas exactas de cada knob no están
// publicadas: aquí son estimaciones (exponenciales) para ajustar a oído con un Slim Phatty al lado.
// El camino osciladores → filtro corre a 2× y se diezma, como el motor Moog.
#pragma once
#include "dsp.h"

namespace gb {

struct PhattyParams {
    int   oct1 = 1, oct2 = 1;                  // 0 = 16', 1 = 8', 2 = 4', 3 = 2'
    float wave1 = 0.33f, wave2 = 0.33f;        // 0 triángulo · 1/3 sierra · 2/3 cuadrada · 1 pulso estrecho
    float lvl1 = 1.0f, lvl2 = 0.8f;
    float freq2 = 0.0f;                        // osc 2: −7…+7 semitonos
    float cutoff = 800.0f, reso = 0.2f, egAmt = 0.3f, kbd = 0.5f, overload = 0.2f;   // egAmt: −1…1
    int   poles = 3;                           // 0..3 = 1..4 polos
    float fA = 0.002f, fD = 0.3f, fS = 0.3f, fR = 0.3f, A = 0.002f, D = 0.5f, S = 0.8f, R = 0.2f;
    float glide = 0.05f, lfoRate = 3.0f, modAmt = 0.0f, modWheel = 0.0f, vel = 0.3f, volume = 0.75f;
    int   modSrc = 0, modDst = 0;              // fuente: 0 tri, 1 cuadrada, 2 sierra, 3 rampa, 4 env. filtro, 5 osc 2, 6 S&H, 7 ruido
                                               // destino: 0 afinación, 1 filtro, 2 forma de onda, 3 osc 2
    int   ladder = 0;                          // 0 clásico (Huovilainen), 1 mejorado (D'Angelo-Välimäki)
    bool  sync = false, glideOn = false;
};

// escalera de Huovilainen (como en moog.h) con salida en la etapa que toque: 1 a 4 polos. La realimentación sale siempre de
// la cuarta etapa, como en el Phatty (por eso la resonancia suena igual con cualquier número de polos)
struct PhattyLadder {
    float sr = 96000.0f, st[4] = {}, stT[3] = {}, d[6] = {}, tune = 0.0f, resQ = 0.0f;
    static constexpr float kTh = 1.22070313f;
    static float th(float x) { x = std::clamp(x, -3.0f, 3.0f); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }
    void reset() { for (float& v : st) v = 0; for (float& v : stT) v = 0; for (float& v : d) v = 0; }
    void set(float fc, float res) {
        const float f = std::clamp(fc, 10.0f, 0.45f * sr) / sr, f2 = f * f, f3 = f2 * f;
        tune = (1.0f - std::exp(-2.0f * kPi * f * (1.8730f * f3 + 0.4955f * f2 - 0.6490f * f + 0.9988f))) / kTh;
        resQ = 4.0f * res * (-3.9364f * f2 + 1.8409f * f + 0.9968f);
    }
    float process(float x, int poles) {
        float in = x - resQ * d[5];
        d[0] = st[0] = d[0] + tune * (th(in * kTh) - stT[0]);
        for (int k = 1; k < 4; ++k) { in = st[k - 1]; stT[k - 1] = th(in * kTh); st[k] = d[k] + tune * (stT[k - 1] - (k != 3 ? stT[k] : th(d[k] * kTh))); d[k] = st[k]; }
        d[5] = (st[3] + d[4]) * 0.5f; d[4] = st[3];
        return poles >= 3 ? d[5] : st[std::clamp(poles, 0, 2)];
    }
};

class PhattyVoice {
public:
    PhattyParams p;
    void prepare(float s) {
        sr_ = s; srOs_ = 2.0f * s; lad_.sr = dvl_.sr = srOs_; lad_.reset(); dvl_.reset(); dec_.setup(sr_); fenv_.sr = aenv_.sr = sr_;
        for (Smoother* m : {&sCut_, &sRes_, &sVol_, &sOl_}) m->setup(sr_, 6.0f);
        sCut_.y = p.cutoff; sRes_.y = p.reso; sVol_.y = p.volume; sOl_.y = p.overload; held_ = 0;
    }
    void noteOn(int note, int vel) {
        const bool legato = held_ > 0 && aenv_.active();
        push(note); target_ = static_cast<float>(note); vel_ = vel / 127.0f;
        if (!p.glideOn || !played_) semi_ = target_;
        played_ = true;
        if (!legato) { fenv_.gateOn(); aenv_.gateOn(); }
    }
    void noteOff(int note) { remove(note); if (held_ > 0) { target_ = static_cast<float>(stack_[held_ - 1]); return; } fenv_.gateOff(); aenv_.gateOff(); }
    void allNotesOff() { held_ = 0; fenv_.gateOff(); aenv_.gateOff(); }
    bool active() const { return aenv_.active(); }

    void render(float* out, int n) {
        const float gl = p.glideOn ? 1.0f - std::exp(-1.0f / (std::max(p.glide, 0.001f) * 0.33f * sr_)) : 1.0f;
        static const float kOct[4] = {-12, 0, 12, 24};
        const float o1 = kOct[std::clamp(p.oct1, 0, 3)], o2 = kOct[std::clamp(p.oct2, 0, 3)] + p.freq2;
        const float vs = velGain(vel_, p.vel), lfoInc = p.lfoRate / sr_;
        const float depth = std::clamp(p.modAmt * p.modWheel, 0.0f, 1.0f);   // la rueda recorre de 0 al Mod Amount
        const float kt = p.kbd;
        for (int i = 0; i < n; ++i) {
            semi_ += (target_ - semi_) * gl;
            // LFO y fuente de modulación (−1…1)
            lfo_ += lfoInc; if (lfo_ >= 1.0f) { lfo_ -= 1.0f; shVal_ = noise_.next(); }
            const float fe = fenv_.process(p.fA, p.fD, p.fS, p.fR), ae = aenv_.process(p.A, p.D, p.S, p.R);
            float src = 0;
            switch (p.modSrc) {
                case 0: src = 4.0f * std::fabs(lfo_ - 0.5f) - 1.0f; break;   // triángulo
                case 1: src = lfo_ < 0.5f ? 1.0f : -1.0f; break;              // cuadrada
                case 2: src = 1.0f - 2.0f * lfo_; break;                      // sierra (baja)
                case 3: src = 2.0f * lfo_ - 1.0f; break;                      // rampa (sube)
                case 4: src = fe; break;                                      // envolvente del filtro
                case 5: src = o2last_; break;                                 // osc 2 (a frecuencia de audio)
                case 6: src = shVal_; break;                                  // S&H
                default: src = noise_.next(); break;                          // ruido
            }
            const float m = src * depth;
            const float pm = p.modDst == 0 ? m * 12.0f : 0.0f, om = p.modDst == 3 ? m * 12.0f : 0.0f, wm = p.modDst == 2 ? m : 0.0f;
            const float note1 = semi_ + o1 + pm, note2 = semi_ + o2 + pm + om;
            if (note1 != n1K_) { n1K_ = note1; f1K_ = mtof(note1); } if (note2 != n2K_) { n2K_ = note2; f2K_ = mtof(note2); }
            const float dt1 = f1K_ / srOs_, dt2 = f2K_ / srOs_;
            // filtro: corte × seguimiento de teclado × envolvente (bipolar, ±5 octavas) × modulación (±4 octavas)
            const float cut = sCut_.process(p.cutoff), res = sRes_.process(p.reso), ol = sOl_.process(p.overload), vol = sVol_.process(p.volume);
            const float oct = (note1 - 60.0f) / 12.0f * kt + p.egAmt * 5.0f * fe * (0.6f + 0.4f * vs) + (p.modDst == 1 ? m * 4.0f : 0.0f);
            const bool dv = p.ladder == 1; if (dv != dvK_) { dvK_ = dv; lad_.reset(); dvl_.reset(); cutK_ = -1; }   // al cambiar de modelo, sin restos del otro
            if (oct != octK_ || cut != cutK_ || res != resK_) { octK_ = oct; cutK_ = cut; resK_ = res; if (dv) dvl_.set(cut * std::exp2(oct), res * 1.08f); else lad_.set(cut * std::exp2(oct), res * 1.08f); }
            const float drive = 1.0f + ol * 9.0f, comp = 1.0f / std::sqrt(drive);
            const float w1 = std::clamp(p.wave1 + wm, 0.0f, 1.0f), w2 = std::clamp(p.wave2 + wm, 0.0f, 1.0f);
            float os[2];
            for (float& o : os) {
                ph1_ += dt1; bool wrap = false; if (ph1_ >= 1.0f) { ph1_ -= 1.0f; wrap = true; }
                ph2_ += dt2; if (ph2_ >= 1.0f) ph2_ -= 1.0f;
                if (p.sync && wrap) ph2_ = ph1_ * (dt2 / dt1);   // sync duro: el osc 2 vuelve a empezar con el 1
                const float a = wave(w1, ph1_, dt1), b = wave(w2, ph2_, dt2); o2last_ = b;
                const float mix = (p.lvl1 * a + p.lvl2 * b) * 0.5f;
                const float x = softClip(mix * drive) * comp * 1.4f;   // Overload: la entrada del filtro se satura
                o = (dv ? dvl_.process(x, p.poles) * (1.0f + 0.7f * dvl_.resQ) : lad_.process(x, p.poles) * (1.0f + 0.7f * lad_.resQ)) * 0.8f;
            }
            float y = dec_.process(os[0], os[1]) * ae * vol * vol * vs;
            const float hp = y - hpX_ + 0.9995f * hpY_; hpX_ = y; hpY_ = hp;
            out[i] = hp;
        }
    }

private:
    // forma continua: triángulo → sierra → cuadrada → pulso estrecho (el ancho baja del 50 % al 8 %)
    static float wave(float w, float ph, float dt) {
        if (w < 1.0f / 3.0f) { const float x = w * 3.0f; return (4.0f * std::fabs(ph - 0.5f) - 1.0f) * (1.0f - x) + blepSaw(ph, dt) * x; }
        if (w < 2.0f / 3.0f) { const float x = (w - 1.0f / 3.0f) * 3.0f; return blepSaw(ph, dt) * (1.0f - x) + blepPulse(ph, dt, 0.5f) * x; }
        return blepPulse(ph, dt, 0.5f - (w - 2.0f / 3.0f) * 3.0f * 0.42f);
    }
    void push(int note) { remove(note); if (held_ < 16) stack_[held_++] = note; }
    void remove(int note) { int j = 0; for (int i = 0; i < held_; ++i) if (stack_[i] != note) stack_[j++] = stack_[i]; held_ = j; }
    float sr_ = 48000.0f, srOs_ = 96000.0f, ph1_ = 0, ph2_ = 0.37f, lfo_ = 0, shVal_ = 0, o2last_ = 0;
    float semi_ = 36.0f, target_ = 36.0f, vel_ = 0.8f, n1K_ = -1e9f, n2K_ = -1e9f, f1K_ = 0, f2K_ = 0, octK_ = -1e9f, cutK_ = -1, resK_ = -1;
    float hpX_ = 0, hpY_ = 0; bool played_ = false; int stack_[16] = {}, held_ = 0;
    PhattyLadder lad_; DVLadder dvl_; bool dvK_ = false; Decim2 dec_; Env fenv_, aenv_; Noise noise_; Smoother sCut_, sRes_, sVol_, sOl_;
};

}  // namespace gb

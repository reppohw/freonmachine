// moog.h — sinte mono estilo Minimoog para Freon Machine.
//
// 3 osciladores (PolyBLEP) con su octava, afinación y onda; el 3 puede ir libre como LFO.
// Mezclador que se puede saturar, filtro ladder de 24 dB con resonancia saturada,
// envolventes ADSR de filtro y de amplitud, modulación del osc 3 al tono o al filtro, y glide.
// El camino oscilador → filtro corre a 2× y se diezma para no generar aliasing.
#pragma once
#include "dsp.h"

namespace gb {

struct MoogParams {
    int   oct = 0;                           // octava general (-2..+2)
    int   wave1 = 1, wave2 = 1, wave3 = 1;   // 0 triángulo, 1 sierra, 2 cuadrada, 3 pulso ancho, 4 pulso estrecho
    float semi2 = 0, fine2 = 7;              // osc 2: semitonos y cents
    float semi3 = -12, fine3 = -3;           // osc 3
    float lvl1 = 1.0f, lvl2 = 0.7f, lvl3 = 0.4f, noise = 0.0f;
    float drive = 0.3f;                      // saturación del mezclador
    float glide = 0.06f;                     // s
    float cutoff = 420.0f, emph = 0.45f, contour = 0.55f;
    int   kbd = 2;                           // seguimiento de teclado: 0 off, 1 1/3, 2 2/3, 3 completo
    float fA = 0.003f, fD = 0.35f, fS = 0.2f, fR = 0.25f;
    float A = 0.003f, D = 0.5f, S = 0.8f, R = 0.2f;
    float mod = 0.0f;                        // cantidad de modulación del osc 3
    float vel = 0.3f;                        // velocidad → filtro y volumen
    float volume = 0.8f;
    bool glideOn = true, o3lfo = false, modFilt = false, modPitch = false, retrig = false;
};

// Filtro ladder de Huovilainen (DAFX, 2004/2010): el modelo digital clásico del filtro Moog.
// Cada una de las 4 etapas satura como los transistores del circuito real, la realimentación
// autooscila con la resonancia al máximo y, como en el Minimoog, al subir la resonancia se pierden
// graves. Basado en la implementación de Victor Lazzarini para Csound.
struct HuovLadder {
    float sr = 96000.0f, st[4] = {}, stT[3] = {}, d[6] = {}, tune = 0.0f, resQ = 0.0f;
    static constexpr float kTh = 1.22070313f;   // escala térmica (el valor experimental del modelo)
    static float th(float x) { x = std::clamp(x, -3.0f, 3.0f); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }
    void reset() { for (float& v : st) v = 0; for (float& v : stT) v = 0; for (float& v : d) v = 0; }
    void set(float fc, float res) {
        const float f = std::clamp(fc, 10.0f, 0.45f * sr) / sr, f2 = f * f, f3 = f2 * f;
        const float fcr = 1.8730f * f3 + 0.4955f * f2 - 0.6490f * f + 0.9988f;   // corrección de la afinación
        const float acr = -3.9364f * f2 + 1.8409f * f + 0.9968f;                 // corrección de la resonancia
        tune = (1.0f - std::exp(-2.0f * kPi * f * fcr)) / kTh;
        resQ = 4.0f * res * acr;
    }
    float process(float x) {
        float in = x - resQ * d[5];
        d[0] = st[0] = d[0] + tune * (th(in * kTh) - stT[0]);
        for (int k = 1; k < 4; ++k) {
            in = st[k - 1];
            stT[k - 1] = th(in * kTh);
            st[k] = d[k] + tune * (stT[k - 1] - (k != 3 ? stT[k] : th(d[k] * kTh)));
            d[k] = st[k];
        }
        d[5] = (st[3] + d[4]) * 0.5f;   // medio ciclo de retardo para compensar la fase
        d[4] = st[3];
        return d[5];
    }
};

class MoogVoice {
public:
    MoogParams p;

    void prepare(float sampleRate) {
        sr_ = sampleRate; srOs_ = 2.0f * sr_;
        lad_.sr = srOs_; lad_.reset(); dec_.setup(sr_);
        fenv_.sr = aenv_.sr = sr_;
        for (Smoother* s : {&sCut_, &sEmph_, &sDrv_, &sVol_}) s->setup(sr_, 6.0f);
        sCut_.y = p.cutoff; sEmph_.y = p.emph; sDrv_.y = p.drive; sVol_.y = p.volume;
        driftC_ = 1.0f - std::exp(-1.0f / (0.8f * sr_));
        held_ = 0;
    }

    void noteOn(int note, int vel) {
        const bool legato = held_ > 0 && aenv_.active();
        push(note);
        target_ = static_cast<float>(note);
        vel_ = vel / 127.0f;
        // Como en el Minimoog: con glide, el tono se desliza desde la última nota tocada
        if (!p.glideOn || !played_) semi_ = target_;
        played_ = true;
        if (!legato || p.retrig) { fenv_.gateOn(); aenv_.gateOn(); }
    }
    void noteOff(int note) {
        remove(note);
        if (held_ > 0) { target_ = static_cast<float>(stack_[held_ - 1]); return; }
        fenv_.gateOff(); aenv_.gateOff();
    }
    void allNotesOff() { held_ = 0; fenv_.gateOff(); aenv_.gateOff(); }
    bool active() const { return aenv_.active(); }

    void render(float* out, int n) {
        const float gl = p.glideOn ? 1.0f - std::exp(-1.0f / (std::max(p.glide, 0.002f) * 0.33f * sr_)) : 1.0f;
        const float m2 = std::exp2((p.semi2 + p.fine2 / 100.0f) / 12.0f), m3 = std::exp2((p.semi3 + p.fine3 / 100.0f) / 12.0f);
        const float octm = std::exp2(static_cast<float>(p.oct));
        const float kt = p.kbd == 0 ? 0.0f : p.kbd == 1 ? 0.33f : p.kbd == 2 ? 0.67f : 1.0f;
        const float lfoF = 0.2f * std::exp2((p.semi3 + 24.0f) / 6.0f);   // osc 3 como LFO: de 0,2 a ~50 Hz
        const float vs = velGain(vel_, p.vel);   // la velocidad solo cambia entre notas
        for (int i = 0; i < n; ++i) {
            semi_ += (target_ - semi_) * gl;
            for (float& d : drift_) d += (noise_.next() * 0.6f - d) * driftC_;
            // modulación del osc 3 (va un ciclo por detrás; suficiente para modular)
            const float mod = p.mod * o3last_;
            const float pm = p.modPitch ? std::exp2(mod * 2.0f / 12.0f) : 1.0f;
            if (semi_ != semiK_) { semiK_ = semi_; hzK_ = mtof(semi_); }   // potencias: solo si cambia lo que entra (es lo más caro por muestra)
            const float f = hzK_ * octm * pm;
            const float dt1 = f * (1.0f + drift_[0] * 0.0012f) / srOs_;
            const float dt2 = f * m2 * (1.0f + drift_[1] * 0.0012f) / srOs_;
            const float dt3 = (p.o3lfo ? lfoF : f * m3 * (1.0f + drift_[2] * 0.0012f)) / srOs_;

            const float fe = fenv_.process(p.fA, p.fD, p.fS, p.fR);
            const float ae = aenv_.process(p.A, p.D, p.S, p.R);
            const float cut = sCut_.process(p.cutoff), emph = sEmph_.process(p.emph), drv = sDrv_.process(p.drive), vol = sVol_.process(p.volume);
            if (f != fK_ || kt != ktK_) { fK_ = f; ktK_ = kt; trackK_ = std::pow(f / 261.6f, kt); }
            const float cx = p.contour * 5.0f * fe * (0.6f + 0.4f * vs);
            if (cx != cxK_) { cxK_ = cx; cgK_ = std::exp2(cx); }
            float fc = cut * trackK_ * cgK_;
            if (p.modFilt) fc *= std::exp2(mod * 3.0f);
            // Emphasis: 0..1 → resonancia del ladder; pasado ~90 % autooscila, como el Minimoog
            const float k = emph * 1.12f, dg = 1.0f + drv * 5.0f, dcomp = 1.0f / std::sqrt(dg);
            if (fc != lfcK_ || k != lkK_) { lfcK_ = fc; lkK_ = k; lad_.set(fc, k); }

            float os[2];
            for (float& o : os) {
                ph_[0] += dt1; if (ph_[0] >= 1.0f) ph_[0] -= 1.0f;
                ph_[1] += dt2; if (ph_[1] >= 1.0f) ph_[1] -= 1.0f;
                ph_[2] += dt3; if (ph_[2] >= 1.0f) ph_[2] -= 1.0f;
                const float o3 = wave(p.wave3, ph_[2], dt3);
                o3last_ = o3;
                float m = p.lvl1 * wave(p.wave1, ph_[0], dt1) + p.lvl2 * wave(p.wave2, ph_[1], dt2) + (p.o3lfo ? 0.0f : p.lvl3 * o3);
                if (p.noise > 0.001f) m += p.noise * noise_.next();
                m = softClip(m * 0.45f * dg) * dcomp * 1.6f;
                o = lad_.process(m) * 2.2f;
            }
            float y = dec_.process(os[0], os[1]);
            y *= ae * vol * vol * 0.9f * vs;
            const float hp = y - hpX_ + 0.9995f * hpY_; hpX_ = y; hpY_ = hp;
            out[i] = hp;
        }
    }

private:
    static float wave(int w, float p, float dt) {
        switch (std::clamp(w, 0, 4)) {
            case 0: return 4.0f * std::fabs(p - 0.5f) - 1.0f;
            case 1: return blepSaw(p, dt);
            case 2: return blepPulse(p, dt, 0.5f);
            case 3: return blepPulse(p, dt, 0.3f);
            default: return blepPulse(p, dt, 0.12f);
        }
    }
    void push(int note) { remove(note); if (held_ < 16) stack_[held_++] = note; }
    void remove(int note) { int j = 0; for (int i = 0; i < held_; ++i) if (stack_[i] != note) stack_[j++] = stack_[i]; held_ = j; }

    float sr_ = 48000.0f, srOs_ = 96000.0f;
    float ph_[3] = {0.0f, 0.33f, 0.71f}, drift_[3] = {}, driftC_ = 0.0f, o3last_ = 0.0f;
    float semi_ = 45.0f, target_ = 45.0f, vel_ = 0.8f;
    float semiK_ = -1e9f, hzK_ = 0, fK_ = -1, ktK_ = -1, trackK_ = 1, cxK_ = -1e9f, cgK_ = 1, lfcK_ = -1, lkK_ = -1;
    bool played_ = false;
    float hpX_ = 0.0f, hpY_ = 0.0f;
    int stack_[16] = {}, held_ = 0;
    HuovLadder lad_; Decim2 dec_; Env fenv_, aenv_; Noise noise_;
    Smoother sCut_, sEmph_, sDrv_, sVol_;
};

}  // namespace gb

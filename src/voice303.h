// voice303.h — el 303 de Freon Machine.
// El sonido lo hace Open303 (Robin Schmidt), una emulación del TB-303 medida sobre el aparato real:
// oscilador sierra/cuadrada como el original, filtro de diodos «TeeBee» con 4× de sobremuestreo,
// envolventes y accent como la 303. Aquí solo se traducen los knobs de Freon a sus unidades y se
// añaden el drive y el tono de salida (extras tipo Devilfish).
//
// Parámetros (normalizados como vienen de un encoder o un CC):
//   cutoff, resonance, envmod, decay, accent, wave, drive, volume, y los extras de abajo.
// Comportamiento 303:
//   - Velocidad >= 100 => nota con accent.
//   - Nota nueva con otra aún pulsada (legato) => slide.

#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include "open303_glue.h"

namespace acid {

constexpr float kPi = 3.14159265358979f;

struct Params {
    float cutoff = 0.35f;
    float resonance = 0.65f;
    float envmod = 0.5f;
    float decay = 0.35f;
    float accent = 0.6f;
    float wave = 0.0f;       // 0 = sierra, 1 = cuadrada (continuo)
    float drive = 0.25f;
    float volume = 0.8f;
    // extras tipo Devilfish
    float tune = 0.0f;       // semitonos (continuo, ±36)
    float slide = 0.045f;    // tiempo de slide (s)
    float accDecay = 0.18f;  // decay del filtro en notas con accent (s)
    float ampDecay = 2.5f;   // caída del volumen con la nota pulsada (s)
    float softAtk = 0.0f;    // ataque de la envolvente del filtro: 0 = 3 ms (303), 1 = 30 ms
    float sub = 0.0f;        // oscilador una octava abajo, entra en el filtro
    float dtone = 8000.0f;   // tono tras el drive (Hz)
    float vel = 0.3f;        // velocidad → volumen (además del accent a partir de 100)
};

class Voice303 {
public:
    Params p;
    Voice303() : o_(std::make_unique<o303::Synth>()) {}

    void prepare(float sampleRate) {
        sr_ = sampleRate;
        o_->setSampleRate(sampleRate);
        o_->setVolume(0.0);
        o_->setAmpSustain(-60.0);
        o_->setAmpRelease(1.0);
        // Todo distinto de cualquier valor real: el primer bloque manda todos los parámetros a Open303. Antes solo se forzaba
        // el cutoff, y lo que estaba en su valor por defecto (accent, resonancia, decay...) no llegaba hasta mover el knob.
        last_.cutoff = last_.resonance = last_.envmod = last_.decay = last_.accent = last_.wave = last_.tune = last_.slide = last_.accDecay = last_.ampDecay = last_.softAtk = -999.0f;
        allNotesOff();
    }
    void noteOn(int note, int velocity) {
        const float v = velocity / 127.0f;
        velG_ = std::exp2(-5.0f * p.vel * (1.0f - v));
        o_->noteOn(std::clamp(note, 0, 127), std::clamp(velocity, 1, 127));
    }
    void noteOff(int note) { o_->noteOn(std::clamp(note, 0, 127), 0); }
    void allNotesOff() { o_->allNotesOff(); }

    // Rellena 'out' con n muestras mono.
    void render(float* out, int n) {
        apply();
        const float dtc = 1.0f - std::exp(-2.0f * kPi * std::min(p.dtone, 0.45f * sr_) / sr_);
        const float a = 1.0f - std::exp(-1.0f / (0.01f * sr_));   // suavizado de 10 ms para drive y volumen
        o_->render(out, n);
        for (int i = 0; i < n; ++i) {
            drv_ += (p.drive - drv_) * a; vol_ += (p.volume - vol_) * a; vg_ += (velG_ - vg_) * a;
            float s = out[i] * 0.87f;   // al mismo nivel que el 303 anterior (medido)
            // drive tras el filtro, con ganancia compensada, y tono de salida
            const float g = 1.0f + drv_ * 9.0f;
            if (drv_ > 0.002f) s = softClip(s * g) / std::sqrt(g);
            toneY_ += (s - toneY_) * dtc;
            out[i] = toneY_ * vol_ * vol_ * vg_;
        }
    }

private:
    static float softClip(float x) { x = std::clamp(x, -3.0f, 3.0f); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }
    // Knobs de Freon → unidades de Open303 (solo se recalcula lo que cambia)
    void apply() {
        auto ch = [](float a, float b) { return std::fabs(a - b) > 1e-6f; };
        if (ch(p.cutoff, last_.cutoff)) o_->setCutoff(200.0 * std::pow(20.0, static_cast<double>(p.cutoff)));   // 200 Hz .. 4 kHz
        if (ch(p.resonance, last_.resonance)) o_->setResonance(100.0 * p.resonance);
        if (ch(p.envmod, last_.envmod)) o_->setEnvMod(100.0 * p.envmod);
        if (ch(p.decay, last_.decay)) o_->setDecay(200.0 * std::pow(10.0, static_cast<double>(p.decay)));     // 200 ms .. 2 s, como el knob de la 303
        if (ch(p.accent, last_.accent)) o_->setAccent(100.0 * p.accent);
        if (ch(p.wave, last_.wave)) o_->setWaveform(p.wave);
        if (ch(p.tune, last_.tune)) o_->setPitchBend(p.tune);
        if (ch(p.slide, last_.slide)) o_->setSlideTime(1000.0 * std::max(p.slide, 0.005f));
        if (ch(p.accDecay, last_.accDecay)) o_->setAccentDecay(1000.0 * std::max(p.accDecay, 0.02f));
        if (ch(p.ampDecay, last_.ampDecay)) o_->setAmpDecay(1000.0 * std::max(p.ampDecay, 0.016f));
        if (ch(p.softAtk, last_.softAtk)) o_->setNormalAttack(3.0 + 27.0 * p.softAtk);
        o_->setSubLevel(p.sub);
        last_ = p;
    }
    std::unique_ptr<o303::Synth> o_;
    Params last_;
    float sr_ = 48000.0f, velG_ = 1.0f, vg_ = 1.0f, drv_ = 0.25f, vol_ = 0.8f, toneY_ = 0.0f;
};

}  // namespace acid

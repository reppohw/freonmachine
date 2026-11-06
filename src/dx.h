// dx.h — el DX7 de Freon Machine (motor MSFA, ver dx7_glue.h).
// Toca los sonidos de la biblioteca (8 de serie + bancos .syx de la carpeta dx7/) y los retoca con
// 16 knobs: brillo, caída del brillo, feedback, desafinación, vibrato, tono, volumen, ADSR, transposición,
// velocidad del vibrato, sensibilidad a la velocidad y algoritmo.
#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include "dsp.h"
#include "dx7_glue.h"

namespace gb {

struct DxParams {
    int patch = 0;
    dx7::Macros m;
    float tone = 1.0f, volume = 0.8f;
};

class DxSynth {
public:
    DxParams p;
    DxSynth() : s_(std::make_unique<dx7::Synth>()) {}
    void prepare(float sr) { sr_ = sr; s_->prepare(sr); cur_ = -1; }
    void noteOn(int note, int vel) { syncPatch(); s_->noteOn(note, vel, p.m); }
    void noteOff(int note) { s_->noteOff(note); }
    void allNotesOff() { s_->allOff(); }
    void render(float* L, float* R, int n) {
        syncPatch();
        s_->render(L, n);
        // Tone: paso bajo suave de salida (el DX7 no tiene filtro; ayuda a domar los agudos en directo)
        const float fc = 400.0f * std::pow(50.0f, std::clamp(p.tone, 0.0f, 1.0f)), a = 1.0f - std::exp(-2.0f * kPi * std::min(fc, 0.45f * sr_) / sr_);
        const float vk = 1.0f - std::exp(-1.0f / (0.01f * sr_));
        for (int i = 0; i < n; ++i) {
            vol_ += (p.volume - vol_) * vk;
            lp_ += (L[i] - lp_) * a;
            const float y = (p.tone >= 0.995f ? L[i] : lp_) * vol_ * vol_ * 0.9f;
            L[i] = R[i] = y;
        }
    }
private:
    void syncPatch() { if (p.patch != cur_) { cur_ = p.patch; s_->select(cur_); } }
    std::unique_ptr<dx7::Synth> s_; float sr_ = 48000, lp_ = 0, vol_ = 0.8f; int cur_ = -1;
};

}  // namespace gb

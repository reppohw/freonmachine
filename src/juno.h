// juno.h — el Juno de Freon Machine.
// El sonido lo hace el motor DSP de KR-106 (Ultramaster / Kayrock, GPL v3), una emulación de la
// familia Juno calibrada con medidas de los aparatos reales. Aquí va en modo Juno-6:
// DCO con sierra, pulso con PWM y sub, filtro IR3109 con las curvas medidas del Juno-6, paso alto
// continuo como el deslizador del Juno-6, una sola ADSR (filtro y VCA, o VCA en modo gate),
// LFO con retardo y el chorus BBD (I, II e I+II).
// Los knobs son los deslizadores del panel, de 0 a 1, como en el aparato.
#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include "dsp.h"
#include "../third_party/kr106/KR106_DSP.h"

#ifndef JUNO_OS
#define JUNO_OS 2   // sobremuestreo del Juno (1, 2 o 4)
#endif

namespace gb {

struct JunoParams {
    float lfoRate = 0.45f, lfoDelay = 0.0f;           // LFO
    float dcoLfo = 0.0f, pwm = 0.4f; int pwmMode = 1; // DCO: LFO→pitch, ancho de pulso, fuente (0 LFO, 1 manual, 2 env)
    float sub = 0.4f, noise = 0.0f;
    float hpf = 0.0f;                                  // paso alto continuo (Juno-6)
    float cutoff = 0.55f, res = 0.15f, env = 0.3f, vcfLfo = 0.0f, kbd = 0.5f;   // VCF
    float A = 0.05f, D = 0.45f, S = 0.6f, R = 0.35f;   // ENV
    int   chorus = 1;                                  // 0 off, 1 I, 2 II, 3 I+II
    float vel = 0.7f;                                  // sensibilidad a la velocidad (el Juno-6 no tiene: 0 = como el original)
    float drift = 0.5f;                                // inestabilidad analógica entre voces
    float volume = 0.8f;
    int   range = 0;                                   // -1 = 16', 0 = 8', 1 = 4'
    bool saw = true, pulse = false, inv = false, gate = false;
};

class JunoSynth {
public:
    JunoParams p;
    JunoSynth() : k_(std::make_unique<::KR106DSP<float>>(6)) {}

    void prepare(float sr) {
        auto& k = *k_;
        // Juno-6: mismo arreglo que hace el plugin al pasar a J60, y luego el modelo J6
        k.SetParam(kAdsrMode, 0);
        k.mSynthModel = kr106::kJ6;
        k.ForEachVoice([](kr106::Voice<float>& v) { v.mModel = kr106::kJ6; v.mADSR.mModel = kr106::kJ6; v.mVCF.mJ106Res = false; });
        k.mLFO.mModel = kr106::kJ6; k.mHPF.mModel = kr106::kJ6;
        k.Reset(sr, 256);
        k.SetOversample(JUNO_OS);
        k.SetActiveVoices(6);
        k.mIgnoreVelocity = false;
        k.mNoiseFloorMul = 0.0f; k.mMainsMul = 0.0f;   // sin ruido de fondo ni zumbido de red
        k.SetParam(kPortaMode, 1); k.SetParam(kLfoMode, 0); k.SetParam(kVcaLevel, 0.5);
        k.SetParam(kBenderDco, 0); k.SetParam(kBenderVcf, 0); k.SetParam(kBender, 0);
        dirty_ = true; apply();
        k.mMasterVolSmooth = k.mMasterVol;
    }
    void noteOn(int note, int vel) {
        // velocidad: el KR-106 multiplica la VCA por la raíz de lo que recibe; se le pasa el cuadrado de nuestra curva
        const float g = velGain(std::clamp(vel, 1, 127) / 127.0f, p.vel);
        k_->NoteOn(std::clamp(note, 0, 127), std::clamp(static_cast<int>(std::lround(127.0f * g * g)), 1, 127));
    }
    void noteOff(int note) { k_->NoteOff(std::clamp(note, 0, 127)); }
    void allNotesOff() { k_->AllNotesOff(); }

    void render(float* L, float* R, int n) {
        apply();
        float* outs[2] = {L, R};
        k_->ProcessBlock(nullptr, outs, 2, n);
        for (int i = 0; i < n; ++i) { L[i] *= kGain; R[i] *= kGain; }
    }

private:
    // índices de parámetro del KR-106 (mismo orden que su EParams)
    enum {
        kBenderDco = 0, kBenderVcf, kArpRate, kLfoRate, kLfoDelay, kDcoLfo, kDcoPwm, kDcoSub, kDcoNoise, kHpfFreq,
        kVcfFreq, kVcfRes, kVcfEnv, kVcfLfo, kVcfKbd, kVcaLevel, kEnvA, kEnvD, kEnvS, kEnvR,
        kTranspose, kHold, kArpeggio, kDcoPulse, kDcoSaw, kDcoSubSw, kChorusOff, kChorusI, kChorusII,
        kOctTranspose, kArpMode, kArpRange, kLfoMode, kPwmMode, kVcfEnvInv, kVcaMode,
        kBender, kTuning, kPower, kPortaMode, kPortaRate, kTransposeOffset, kBenderLfo, kAdsrMode
    };
    static constexpr float kGain = 0.1f;
    // solo se manda al KR-106 lo que ha cambiado
    void apply() {
        auto& k = *k_;
        auto f = [&](float JunoParams::* m, int idx) { if (dirty_ || last_.*m != p.*m) k.SetParam(idx, p.*m); };
        f(&JunoParams::lfoRate, kLfoRate); f(&JunoParams::lfoDelay, kLfoDelay); f(&JunoParams::dcoLfo, kDcoLfo); f(&JunoParams::pwm, kDcoPwm);
        f(&JunoParams::sub, kDcoSub); f(&JunoParams::noise, kDcoNoise); f(&JunoParams::cutoff, kVcfFreq); f(&JunoParams::res, kVcfRes);
        f(&JunoParams::env, kVcfEnv); f(&JunoParams::vcfLfo, kVcfLfo); f(&JunoParams::kbd, kVcfKbd);
        f(&JunoParams::A, kEnvA); f(&JunoParams::D, kEnvD); f(&JunoParams::S, kEnvS); f(&JunoParams::R, kEnvR);
        if (dirty_ || last_.hpf != p.hpf) k.mHPF.SetContinuousJ6(p.hpf);
        if (dirty_ || last_.pwmMode != p.pwmMode) k.SetParam(kPwmMode, std::clamp(p.pwmMode, 0, 2));
        if (dirty_ || last_.chorus != p.chorus) { const int c = std::clamp(p.chorus, 0, 3); k.SetParam(kChorusI, (c & 1) ? 1 : 0); k.SetParam(kChorusII, (c & 2) ? 1 : 0); }
        if (dirty_ || last_.range != p.range) k.SetParam(kOctTranspose, std::clamp(p.range, -1, 1) + 1);
        if (dirty_ || last_.saw != p.saw) k.SetParam(kDcoSaw, p.saw ? 1 : 0);
        if (dirty_ || last_.pulse != p.pulse) k.SetParam(kDcoPulse, p.pulse ? 1 : 0);
        if (dirty_) k.SetParam(kDcoSubSw, 1);
        if (dirty_ || last_.inv != p.inv) k.SetParam(kVcfEnvInv, p.inv ? 1 : 0);
        if (dirty_ || last_.gate != p.gate) k.SetParam(kVcaMode, p.gate ? 1 : 0);
        if (dirty_ || last_.drift != p.drift) k.SetDriftAmount(p.drift);
        k.mMasterVol = p.volume * p.volume;
        last_ = p; dirty_ = false;
    }
    std::unique_ptr<::KR106DSP<float>> k_;
    JunoParams last_; bool dirty_ = true;
};

}  // namespace gb

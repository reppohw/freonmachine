// open303_glue.cpp — se compila aparte: Open303 define macros (PI, INLINE...) que no deben salir de aquí.
#include "open303_glue.h"
#include "../third_party/open303/rosic_Open303.h"
namespace o303 {
Synth::Synth() : o_(new rosic::Open303()) {}
Synth::~Synth() { delete o_; }
void Synth::setSampleRate(double sr) { o_->setSampleRate(sr); }
void Synth::setCutoff(double v) { o_->setCutoff(v); }
void Synth::setResonance(double v) { o_->setResonance(v); }
void Synth::setEnvMod(double v) { o_->setEnvMod(v); }
void Synth::setDecay(double v) { o_->setDecay(v); }
void Synth::setAccent(double v) { o_->setAccent(v); }
void Synth::setWaveform(double v) { o_->setWaveform(v); }
void Synth::setPitchBend(double v) { o_->setPitchBend(v); }
void Synth::setSlideTime(double v) { o_->setSlideTime(v); }
void Synth::setAccentDecay(double v) { o_->setAccentDecay(v); }
void Synth::setAmpDecay(double v) { o_->setAmpDecay(v); }
void Synth::setNormalAttack(double v) { o_->setNormalAttack(v); }
void Synth::setSubLevel(double v) { o_->subLevel = v; }
void Synth::setVolume(double v) { o_->setVolume(v); }
void Synth::setAmpSustain(double v) { o_->setAmpSustain(v); }
void Synth::setAmpRelease(double v) { o_->setAmpRelease(v); }
void Synth::noteOn(int note, int vel) { o_->noteOn(note, vel, 0.0); }
void Synth::allNotesOff() { o_->allNotesOff(); }
void Synth::render(float* out, int n) { for (int i = 0; i < n; ++i) out[i] = static_cast<float>(o_->getSample()); }
}

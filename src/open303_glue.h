// open303_glue.h — puente con Open303 sin meter sus cabeceras (y sus macros) en el resto del programa.
#pragma once
namespace rosic { class Open303; }
namespace o303 {
struct Synth {
    Synth(); ~Synth();
    Synth(const Synth&) = delete; Synth& operator=(const Synth&) = delete;
    void setSampleRate(double sr);
    void setCutoff(double hz); void setResonance(double pct); void setEnvMod(double pct); void setDecay(double ms);
    void setAccent(double pct); void setWaveform(double w); void setPitchBend(double semis); void setSlideTime(double ms);
    void setAccentDecay(double ms); void setAmpDecay(double ms); void setNormalAttack(double ms); void setSubLevel(double v);
    void setVolume(double db); void setAmpSustain(double db); void setAmpRelease(double ms);
    void noteOn(int note, int vel); void allNotesOff();
    void render(float* out, int n);   // n muestras mono
private:
    rosic::Open303* o_;
};
}

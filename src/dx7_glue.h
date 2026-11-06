// dx7_glue.h — el DX7 de Freon Machine: motor MSFA (Google, Apache 2.0; el mismo origen que Dexed).
// Se compila aparte porque MSFA define macros cortas (N, LG_N) que no deben salir de su código.
#pragma once
#include <string>
namespace dx7 {
// Knobs de Freon sobre el patch cargado: 0,5 = el patch tal cual
struct Macros {
    float bright = 0.5f, modDecay = 0.5f, feedback = 0.5f, detune = 0.5f, vibrato = 0.5f, vibRate = 0.5f;
    float attack = 0.5f, decay = 0.5f, sustain = 0.5f, release = 0.5f, velocity = 0.5f;
    int transpose = 0, algo = 0;   // algo 0 = el del patch; 1..32 lo cambia
};
// Biblioteca de sonidos: 8 de serie + los bancos .syx de la carpeta dx7/ (se leen al arrancar)
void libInit(const std::string& folder);
int libCount();
std::string libName(int i);
class Synth {
public:
    Synth(); ~Synth();
    Synth(const Synth&) = delete; Synth& operator=(const Synth&) = delete;
    void prepare(double sr);
    void select(int patch);
    void noteOn(int note, int vel, const Macros& m);
    void noteOff(int note);
    void allOff();
    void render(float* out, int n);   // mono
private:
    struct Impl; Impl* p_;
};
}

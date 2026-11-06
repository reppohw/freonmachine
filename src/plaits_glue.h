// plaits_glue.h — tambores de Mutable Instruments Plaits (808 y 909), sin meter sus cabeceras en el resto.
#pragma once
namespace pd {
enum Model { BD808 = 0, BD909, SD808, SD909, HH808, HH909 };
class Drum {
public:
    Drum(); ~Drum();
    Drum(const Drum&) = delete; Drum& operator=(const Drum&) = delete;
    void setModel(int m);
    void trigger(float accent) { trig_ = true; accent_ = accent; }
    // f0 en Hz; el resto de 0 a 1. a y b dependen del modelo:
    //   bombos: a = golpe de ataque (FM de ataque / suciedad 909), b = barrido (auto-FM / envolvente FM 909)
    //   cajas:  a = snappy (cuerdas), b = sin uso
    //   hihats: a = ruido, b = sin uso
    void render(float* out, int n, float sr, float f0, float tone, float decay, float a, float b);
private:
    struct Impl; Impl* p_; int model_ = 0; bool trig_ = false; float accent_ = 0.8f;
};
}

// Freon Machine: solo la tabla de seno que necesitan los tambores de Plaits (la original trae muchas más).
#ifndef PLAITS_RESOURCES_H_
#define PLAITS_RESOURCES_H_
namespace plaits {
extern float lut_sine[641];   // sin(2π·i/512), i = 0..640 (se rellena al arrancar)
void InitSineLut();
}
#endif

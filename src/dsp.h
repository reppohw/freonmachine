// dsp.h — piezas comunes de los sintes de Freon Machine (sin dependencias).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gb {

constexpr float kPi = 3.14159265358979f;

// Velocidad → volumen: con la sensibilidad al máximo, de 0 a −30 dB; a 0,7 (por defecto) unos −20 dB tocando muy flojo
inline float velGain(float v, float amt) { return std::exp2(-5.0f * amt * (1.0f - v)); }
inline float mtof(float n) { return 440.0f * std::exp2((n - 69.0f) / 12.0f); }

// Saturación suave barata (aprox. de tanh, monótona y acotada en ±1).
inline float softClip(float x) {
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// Corrección PolyBLEP para discontinuidades de osciladores.
inline float polyBlep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float blepSaw(float p, float dt) { return 2.0f * p - 1.0f - polyBlep(p, dt); }
inline float blepPulse(float p, float dt, float w) {
    float p2 = p + 1.0f - w; if (p2 >= 1.0f) p2 -= 1.0f;
    return (p < w ? 1.0f : -1.0f) + polyBlep(p, dt) - polyBlep(p2, dt);
}

// Suavizado de un polo (evita «zipper» al mover knobs).
struct Smoother {
    float y = 0.0f, a = 0.0f;
    void setup(float sr, float ms) { a = std::exp(-1.0f / (0.001f * ms * sr)); }
    float process(float x) { y = x + a * (y - x); return y; }
};

// Ruido blanco (xorshift).
struct Noise {
    uint32_t s = 0x9E3779B9u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (static_cast<int32_t>(s) * (1.0f / 2147483648.0f)); }
};

// Envolvente ADSR con curvas analógicas (exponenciales). Tiempos en segundos.
struct Env {
    enum Stage { Idle, Attack, Decay, Release } st = Idle;
    float y = 0.0f, sr = 48000.0f;
    void gateOn(bool retrigFromZero = false) { if (retrigFromZero) y = 0.0f; st = Attack; }
    void gateOff() { if (st != Idle) st = Release; }
    bool active() const { return st != Idle; }
    static float coef(float t, float sr) { return 1.0f - std::exp(-1.0f / (std::max(t, 0.0005f) * sr)); }
    float process(float a, float d, float s, float r) {
        switch (st) {
            case Attack: y += (1.3f - y) * coef(a * 0.6f, sr); if (y >= 1.0f) { y = 1.0f; st = Decay; } break;
            case Decay: y += (s - y) * coef(d * 0.3f, sr); break;
            case Release: y += (0.0f - y) * coef(r * 0.3f, sr); if (y < 1e-5f) { y = 0.0f; st = Idle; } break;
            default: break;
        }
        return y;
    }
};

// Biquad paso bajo (TDF-II), para el diezmado tras el sobremuestreo.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass(float fc, float q, float sr) {
        const float w = 2.0f * kPi * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0f * q), a0 = 1.0f + al;
        b0 = (1.0f - c) * 0.5f / a0; b1 = (1.0f - c) / a0; b2 = b0; a1 = -2.0f * c / a0; a2 = (1.0f - al) / a0;
    }
    float process(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
// Diezmador 2:1 con Butterworth de 4.º orden (corte en 0,45 de la frecuencia de salida).
struct Decim2 {
    Biquad f1, f2;
    void setup(float srOut) { f1.lowpass(0.45f * srOut, 0.5412f, 2.0f * srOut); f2.lowpass(0.45f * srOut, 1.3066f, 2.0f * srOut); }
    float process(float a, float b) { f2.process(f1.process(a)); return f2.process(f1.process(b)); }
};

// Filtro de 4 polos ZDF con realimentación saturada (ladder de Moog / OTA del Juno).
struct Ladder {
    float s[4] = {}, sr = 48000.0f;
    void reset() { for (float& v : s) v = 0.0f; }
    // k: resonancia 0..~4 (autooscila cerca de 4). comp: cuánto se compensa la pérdida de graves.
    float process(float x, float fc, float k, float comp) {
        fc = std::clamp(fc, 10.0f, 0.42f * sr);
        const float g = std::tan(kPi * fc / sr), G = g / (1.0f + g), inv = 1.0f / (1.0f + g);
        const float S = G * G * G * s[0] * inv + G * G * s[1] * inv + G * s[2] * inv + s[3] * inv;
        const float G4 = G * G * G * G;
        float u = softClip((x - k * S) / (1.0f + k * G4));
        for (float& st : s) { const float w = (u - st) * G, lp = w + st; st = lp + w; u = lp; }
        return u * (1.0f + comp * k);
    }
};

// Paso alto de un polo (TPT).
struct OnePoleHP {
    float s = 0.0f, G = 0.0f;
    void set(float fc, float sr) { const float g = std::tan(kPi * std::clamp(fc, 1.0f, 0.45f * sr) / sr); G = g / (1.0f + g); }
    float process(float x) { const float v = (x - s) * G, lp = v + s; s = lp + v; return x - lp; }
};
struct OnePoleLP {
    float s = 0.0f, G = 0.0f;
    void set(float fc, float sr) { const float g = std::tan(kPi * std::clamp(fc, 1.0f, 0.45f * sr) / sr); G = g / (1.0f + g); }
    float process(float x) { const float v = (x - s) * G, lp = v + s; s = lp + v; return lp; }
};

// Seno por tabla (4096 puntos, interpolación lineal). Entrada en ciclos.
struct SinTab {
    float t[4097];
    SinTab() { for (int i = 0; i <= 4096; ++i) t[i] = std::sin(2.0f * kPi * static_cast<float>(i) / 4096.0f); }
};
inline float fsin(float cyc) {
    static const SinTab T;
    cyc -= std::floor(cyc);
    const float x = cyc * 4096.0f; const int i = static_cast<int>(x); const float f = x - static_cast<float>(i);
    return T.t[i] + (T.t[i + 1] - T.t[i]) * f;
}

// Tempo del reloj MIDI. Por USB los pulsos llegan con mucho temblor (medido con el Oxi: entre 0,1 y 39 ms, cuando
// deberían ser 19). Aquí no se promedian intervalos: se traza la recta que mejor pasa por los instantes de hasta 8
// tiempos, así que ningún pulso tardío pesa más que otro. Si el tempo cambia de verdad se nota varios pulsos seguidos
// y se empieza a medir desde ahí. Lo publicado va en décimas y solo se mueve si la medida se aleja.
struct ClockEst {
    static constexpr int N = 192, SHORT = 24, CONFIRM = 6;
    double r[N] = {}, now = 0, pub = 0; int i = 0, n = 0, off = 0; bool settled = false;
    void reset() { i = n = off = 0; now = 0; settled = false; }
    double slope() const {   // segundos por pulso
        const double c = (n - 1) / 2.0; double sxy = 0;
        for (int k = 0; k < n; ++k) sxy += (c - k) * (r[(i - 1 - k + N) % N] - now);
        return sxy / (n * (n * static_cast<double>(n) - 1.0) / 12.0);
    }
    float tick(double dt) {
        if (dt > 0.2 || dt < 0) { reset(); return static_cast<float>(pub); }   // solo un parón borra la medida
        now += dt; r[i] = now; i = (i + 1) % N; if (n < N) ++n;
        if (n < 12) return static_cast<float>(pub);
        const double per = slope();
        if (per <= 0) return static_cast<float>(pub);
        if (n > SHORT) {   // ¿ha cambiado el tempo? debe notarse varios pulsos seguidos: un pulso tardío no basta
            const double s = (now - r[(i - 1 - SHORT + N) % N]) / SHORT;
            off = std::fabs(s / per - 1.0) > 0.03 ? off + 1 : 0;
            if (off >= CONFIRM) { n = SHORT; off = 0; settled = false; return static_cast<float>(pub = std::round(60.0 / (slope() * 24.0) * 10.0) / 10.0); }
        }
        const double est = 60.0 / (per * 24.0);
        // el margen crece con el tempo (a más tempo, el mismo temblor pesa más); con la ventana recién llena se afina una vez
        const bool full = n == N && !settled; if (n == N) settled = true;
        if (pub <= 0 || full || std::fabs(est - pub) >= 0.2 * (est / 120.0) * (est / 120.0)) pub = std::round(est * 10.0) / 10.0;
        return static_cast<float>(pub);
    }
};

// Biquad general (RBJ) para filtros de batería: tipo 0 paso bajo, 1 paso alto, 2 paso banda.
struct BQ {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(int type, float fc, float q, float sr) {
        fc = std::clamp(fc, 10.0f, 0.45f * sr);
        const float w = 2.0f * kPi * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0f * std::max(q, 0.05f)), a0 = 1.0f + al;
        if (type == 0) { b0 = (1 - c) * 0.5f; b1 = 1 - c; b2 = b0; }
        else if (type == 1) { b0 = (1 + c) * 0.5f; b1 = -(1 + c); b2 = b0; }
        else { b0 = al; b1 = 0; b2 = -al; }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void reset() { z1 = z2 = 0; }
    float process(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

}  // namespace gb

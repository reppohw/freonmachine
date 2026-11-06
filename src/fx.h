// fx.h — efectos del mezclador de Freon Machine: distorsión, EQ, limitador, sidechain,
// reverbs algorítmicas, delays, chorus y los efectos de directo del máster.
#pragma once
#include <array>
#include <vector>
#include "dsp.h"

namespace gb {

// ------------------------------------------------------------------ utilidades
struct Shelf {   // RBJ: 0 low shelf, 1 high shelf, 2 peaking
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(int type, float fc, float db, float q, float sr) {
        const float A = std::pow(10.0f, db / 40.0f), w = 2 * kPi * fc / sr, c = std::cos(w), sn = std::sin(w);
        float a0;
        if (type == 2) { const float al = sn / (2 * q); b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; }
        else {
            const float al = sn / 2 * std::sqrt(2.0f), sA = 2 * std::sqrt(A) * al;
            if (type == 0) { b0 = A * ((A + 1) - (A - 1) * c + sA); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sA); a0 = (A + 1) + (A - 1) * c + sA; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sA; }
            else { b0 = A * ((A + 1) + (A - 1) * c + sA); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sA); a0 = (A + 1) - (A - 1) * c + sA; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sA; }
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    float process(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// ------------------------------------------------------------------ distorsión (6 tipos, 2× sobremuestreo, nivel compensado)
enum DistType { D_SOFT = 0, D_TUBE, D_HARD, D_FUZZ, D_FOLD, D_BITS };
inline float shape(int type, float x, float drive) {
    const float g = 1.0f + drive * 24.0f;
    switch (type) {
        case D_TUBE: { const float y = x * g + 0.2f * drive; return softClip(y) - softClip(0.2f * drive); }
        case D_HARD: return std::clamp(x * g, -1.0f, 1.0f);
        case D_FUZZ: { const float y = x * g * 2.0f; return y / (1.0f + std::fabs(y)); }
        case D_FOLD: { const float y = x * (1.0f + drive * 6.0f); return fsin(y * 0.25f); }
        default: return softClip(x * g);
    }
}
struct Dist {
    int type = D_SOFT; float drive = 0, tone = 1, mix = 1;
    void prepare(float sr) { sr_ = sr; dec_[0].setup(sr); dec_[1].setup(sr); key_ = -1; }
    void reset() {   // solo tras una avería
        for (auto& d : dec_) { d.f1.z1 = d.f1.z2 = d.f2.z1 = d.f2.z2 = 0; }
        lp_[0] = OnePoleLP{}; lp_[1] = OnePoleLP{}; dcL_ = OnePoleHP{}; dcR_ = OnePoleHP{};
        for (int c = 0; c < 2; ++c) { prev_[c] = 0; eIn_[c] = eOut_[c] = 0; ag_[c] = 1; adX_[c] = 0; adF_[c] = 0; } held_ = 0; hc_ = 0; key_ = -1;
    }
    bool on() const { return drive > 0.001f; }
    void update() {
        const int key = type * 100000 + static_cast<int>(drive * 1000);
        if (key != key_) {
            key_ = key;
            // ganancia que mantiene el nivel de un seno de 0,3 igual a la entrada
            double si = 0, so = 0; for (int i = 0; i < 256; ++i) { const float x = 0.3f * std::sin(2 * kPi * i / 256.0f); si += x * x; const float y = type == D_BITS ? bits(x) : shape(type, x, drive); so += y * y; }
            comp_ = so > 1e-9 ? static_cast<float>(std::sqrt(si / so)) : 1.0f;
        }
        for (auto& l : lp_) l.set(400.0f * std::pow(50.0f, tone), sr_);
        dcL_.set(18.0f, sr_); dcR_.set(18.0f, sr_);
        envC_ = 1.0f - std::exp(-1.0f / (0.25f * sr_)); agC_ = 1.0f - std::exp(-1.0f / (0.15f * sr_));
    }
    void process(float* L, float* R, int n) {
        if (!on()) return;
        update();
        float* ch[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            float* x = ch[c];
            for (int i = 0; i < n; ++i) {
                const float in = x[i]; float w;
                if (type == D_BITS) w = bits(in);
                else if (type == D_FUZZ) { const float a = 0.5f * (prev_[c] + in), y1 = fuzzAA(c, a), y2 = fuzzAA(c, in); w = dec_[c].process(y1, y2); }
                else { const float a = 0.5f * (prev_[c] + in), y1 = shape(type, a, drive), y2 = shape(type, in, drive); w = dec_[c].process(y1, y2); }
                prev_[c] = in;
                w = lp_[c].process(w * comp_);
                w = c ? dcR_.process(w) : dcL_.process(w);
                // nivel autorregulado: el efecto sale con la misma energía que entra
                eIn_[c] += (in * in - eIn_[c]) * envC_; eOut_[c] += (w * w - eOut_[c]) * envC_;
                if (eIn_[c] > 1e-7f) { const float t = std::clamp(std::sqrt(eIn_[c] / std::max(eOut_[c], 1e-9f)), 0.1f, 4.0f); ag_[c] += (t - ag_[c]) * agC_; }
                w *= ag_[c];
                x[i] = in * (1.0f - mix) + w * mix;
            }
        }
    }
private:
    // Fuzz con antialiasing por antiderivada (Parker et al., DAFx 2016): en vez de la curva en cada punto se usa su media
    // entre dos puntos. Con el sobremuestreo 2× ya existente, quita buena parte del aliasing sin gastar 4×.
    float fuzzAA(int c, float x) {
        const float G = (1.0f + drive * 24.0f) * 2.0f;
        const auto F = [G](float v) { const float u = std::fabs(v * G); return (u - std::log1p(u)) / G; };
        if (G != adG_[c]) { adG_[c] = G; adF_[c] = F(adX_[c]); }
        const float dx = x - adX_[c], Fx = F(x);
        float y;
        if (std::fabs(dx) < 1e-3f) { const float u = 0.5f * (x + adX_[c]) * G; y = u / (1.0f + std::fabs(u)); }
        else y = (Fx - adF_[c]) / dx;
        adX_[c] = x; adF_[c] = Fx;
        return y;
    }
    float adX_[2] = {}, adF_[2] = {}, adG_[2] = {-1, -1};
    float bits(float x) {   // reductor de bits y de frecuencia de muestreo
        const float lv = std::exp2(10.0f - drive * 8.0f), hold = 1.0f + drive * 20.0f;
        if (++hc_ >= hold) { hc_ = 0; held_ = std::round(x * lv) / lv; }
        return held_;
    }
    float sr_ = 48000, comp_ = 1, prev_[2] = {}, held_ = 0, hc_ = 0, eIn_[2] = {}, eOut_[2] = {}, ag_[2] = {1, 1}, envC_ = 0.0001f, agC_ = 0.0001f; int key_ = -1;
    Decim2 dec_[2]; OnePoleLP lp_[2]; OnePoleHP dcL_, dcR_;
};

// ------------------------------------------------------------------ limitador de picos (sin lookahead, rápido)
struct Limiter {
    float thrDb = 0.0f, gr = 1.0f, sr = 48000, lastGrDb = 0;
    bool on() const { return thrDb < -0.05f; }
    void process(float* L, float* R, int n) {
        if (!on()) { lastGrDb = 0; return; }
        const float thr = std::pow(10.0f, thrDb / 20.0f), att = 1.0f - std::exp(-1.0f / (0.002f * sr)), rel = 1.0f - std::exp(-1.0f / (0.08f * sr));
        float minG = 1.0f;
        for (int i = 0; i < n; ++i) {
            const float pk = std::max(std::fabs(L[i]), std::fabs(R[i])), target = pk > thr ? thr / pk : 1.0f;
            gr += (target - gr) * (target < gr ? att : rel);
            L[i] *= gr; R[i] *= gr; minG = std::min(minG, gr);
        }
        lastGrDb = 20.0f * std::log10(std::max(minG, 1e-4f));
    }
};

// ------------------------------------------------------------------ limitador del máster con 1,5 ms de anticipación
// Ve el pico antes de que llegue y baja el nivel en esos 1,5 ms: el golpe pasa entero, sin distorsión ni sobrepasar el
// umbral. El mínimo deslizante y la media móvil son de coste fijo por muestra. Suma 1,5 ms de retardo al máster.
struct LookLimiter {
    static constexpr int MAXD = 128;
    float thrDb = 0.0f, sr = 48000, lastGrDb = 0;
    void prepare(float s) { sr = s; D = std::clamp(static_cast<int>(0.0015f * sr), 8, MAXD - 1); relC = 1.0f - std::exp(-1.0f / (0.08f * sr)); for (float& v : box) v = 1.0f; sum = D; }
    void process(float* L, float* R, int n) {
        const bool on = thrDb < -0.05f; const float thr = std::pow(10.0f, thrDb / 20.0f); float minG = 1.0f;
        for (int i = 0; i < n; ++i) {
            const float pk = std::max(std::fabs(L[i]), std::fabs(R[i])), tgt = on && pk > thr ? thr / pk : 1.0f;
            // mínimo de los últimos D valores (cola monótona)
            while (qN > 0 && qV[(qH + qN - 1) % MAXD] >= tgt) --qN;
            qV[(qH + qN) % MAXD] = tgt; qT[(qH + qN) % MAXD] = t; ++qN;
            while (t - qT[qH] >= D) { qH = (qH + 1) % MAXD; --qN; }
            const float held = qV[qH];
            env = held < env ? held : env + (held - env) * relC;
            sum += env - box[bw]; box[bw] = env; bw = (bw + 1) % D;
            const float g = static_cast<float>(sum / D);
            const float dl = dL[w], dr = dR[w]; dL[w] = L[i]; dR[w] = R[i]; w = (w + 1) % D; ++t;
            L[i] = dl * g; R[i] = dr * g; minG = std::min(minG, g);
        }
        lastGrDb = 20.0f * std::log10(std::max(minG, 1e-4f));
    }
private:
    int D = 72, w = 0, bw = 0, qH = 0, qN = 0; long t = 0; float env = 1, relC = 0.001f, dL[MAXD] = {}, dR[MAXD] = {}, box[MAXD] = {}, qV[MAXD] = {}; long qT[MAXD] = {}; double sum = 72;
};

// ------------------------------------------------------------------ seguidor de envolvente para el sidechain
struct Follower {
    float e1 = 0, e2 = 0;
    // devuelve la ganancia (1 = sin agachar) para cada muestra en g[]
    void process(const float* srcL, const float* srcR, float* g, int n, float amt, float rel, float sr) {
        const float c = 1.0f - std::exp(-1.0f / (std::max(0.006f, rel / 3.0f) * sr));
        for (int i = 0; i < n; ++i) {
            const float x = 0.5f * (std::fabs(srcL[i]) + std::fabs(srcR[i]));
            e1 += (x - e1) * c; e2 += (e1 - e2) * c;
            g[i] = 1.0f - std::clamp(e2 * amt * 2.5f, 0.0f, 0.95f);
        }
    }
};

// ------------------------------------------------------------------ filtro DJ (LP a la izquierda, HP a la derecha)
struct DJFilter {
    float f = 0, res = 1.4f, sr = 48000; int side_ = 0;
    Biquad lp[2], hp[2];
    void reset() { for (auto& b : lp) b.z1 = b.z2 = 0; for (auto& b : hp) b.z1 = b.z2 = 0; }
    void process(float* L, float* R, int n) {
        if (std::fabs(f) < 0.02f) { side_ = 0; return; }
        // al pasar de paso bajo a paso alto (o al revés) el filtro nuevo empieza limpio: sin el estado viejo, sin clic
        const int side = f < 0 ? -1 : 1;
        if (side != side_) { for (auto& b : lp) b.z1 = b.z2 = 0; for (auto& b : hp) b.z1 = b.z2 = 0; side_ = side; }
        const float q = 0.5f + res * 0.6f;
        float* ch[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            if (f < 0) lp[c].lowpass(20000.0f * std::exp2(f * 9.5f), q, sr);
            else hpSet(hp[c], 10.0f * std::exp2(f * 11.0f), q);
            for (int i = 0; i < n; ++i) ch[c][i] = f < 0 ? lp[c].process(ch[c][i]) : hp[c].process(ch[c][i]);
        }
    }
private:
    void hpSet(Biquad& b, float fc, float q) {
        fc = std::clamp(fc, 10.0f, 0.45f * sr);
        const float w = 2.0f * kPi * fc / sr, c = std::cos(w), al = std::sin(w) / (2.0f * q), a0 = 1.0f + al;
        b.b0 = (1.0f + c) * 0.5f / a0; b.b1 = -(1.0f + c) / a0; b.b2 = b.b0; b.a1 = -2.0f * c / a0; b.a2 = (1.0f - al) / a0;
    }
};

// ------------------------------------------------------------------ línea de retardo con lectura fraccional
// Línea de retardo con tamaño potencia de 2 (lecturas con máscara, sin divisiones)
struct DelayLine {
    std::vector<float> b; int w = 0, mask = 3, maxD = 2;
    void resize(int n) { int N = 4; while (N < n) N <<= 1; b.assign(static_cast<size_t>(N), 0.0f); w = 0; mask = N - 1; maxD = n - 2 > 1 ? n - 2 : 1; }
    void zero() { std::fill(b.begin(), b.end(), 0.0f); }
    void push(float x) { b[static_cast<size_t>(w)] = x; w = (w + 1) & mask; }
    float at(int d) const { return b[static_cast<size_t>((w - d) & mask)]; }   // retardo entero: mucho más barato que read()
    float read(float d) const {   // d en muestras (>= 1)
        const float pos = static_cast<float>(w) - std::clamp(d, 1.0f, static_cast<float>(maxD));
        const float fl = std::floor(pos); const int i0 = static_cast<int>(fl); const float fr = pos - fl;
        const float a = b[static_cast<size_t>(i0 & mask)], c = b[static_cast<size_t>((i0 + 1) & mask)];
        return a + (c - a) * fr;
    }
};

// ------------------------------------------------------------------ paso todo (difusión)
struct Allpass {
    std::vector<float> b; int w = 0, n = 1; float g = 0.5f;
    void resize(int N, float gain) { n = std::max(N, 1); b.assign(static_cast<size_t>(n), 0.0f); w = 0; g = gain; }
    void zero() { std::fill(b.begin(), b.end(), 0.0f); }
    float process(float x) {
        const float d = b[static_cast<size_t>(w)], v = x + g * d;
        b[static_cast<size_t>(w)] = v; if (++w >= n) w = 0;
        return d - g * v;
    }
};

// ------------------------------------------------------------------ reverb FDN de 8 líneas (Householder), estéreo
// Entrada con difusores (sin ecos sueltos al principio) y las 8 líneas moduladas (cola sin timbre metálico).
struct FDN {
    static constexpr int N = 8, ND = 4;
    float sr = 48000, t60 = 2, damp = 6000, size = 0.6f, pre = 0.02f, mod = 0.3f, lowcut = 0;
    bool freeze = false;
    void prepare(float s) {
        sr = s; for (auto& d : d_) d.resize(static_cast<int>(0.25f * sr));
        preL_.resize(static_cast<int>(0.3f * sr)); preR_.resize(static_cast<int>(0.3f * sr));
        static const float MSL[ND] = {3.1f, 4.7f, 11.3f, 16.9f}, MSR[ND] = {3.7f, 5.3f, 12.1f, 15.7f}; static const float G[ND] = {0.72f, 0.72f, 0.62f, 0.62f};
        for (int i = 0; i < ND; ++i) { apL_[i].resize(static_cast<int>(MSL[i] * 0.001f * sr), G[i]); apR_[i].resize(static_cast<int>(MSR[i] * 0.001f * sr), G[i]); }
        key_[0] = -1;
    }
    void zero() { for (auto& d : d_) d.zero(); preL_.zero(); preR_.zero(); for (auto& a : apL_) a.zero(); for (auto& a : apR_) a.zero(); for (auto& l : lp_) l.s = 0; hpL_.s = hpR_.s = 0; }
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        static const float MS[N] = {29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.3f, 79.1f};
        // el tamaño se desliza: mover el knob no da saltos en las líneas
        const float scT = 0.6f + size * 0.9f;
        sc_ = sc_ <= 0 ? scT : sc_ + (scT - sc_) * std::min(1.0f, n / (0.15f * sr));
        const float T = freeze ? 4000.0f : std::max(0.2f, t60), Dmp = freeze ? 0.45f * sr : damp;
        // los coeficientes solo se recalculan si algo ha cambiado (ocho potencias y tangentes por bloque se notan)
        if (key_[0] != sc_ || key_[1] != T) { key_[0] = sc_; key_[1] = T; for (int k = 0; k < N; ++k) { dl_[k] = MS[k] / 1000.0f * sc_ * sr; g_[k] = std::pow(10.0f, -3.0f * (dl_[k] / sr) / T); } }
        if (key_[2] != Dmp) { key_[2] = Dmp; for (auto& l : lp_) l.set(Dmp, sr); }
        if (key_[3] != lowcut) { key_[3] = lowcut; const float f = 20.0f * std::pow(30.0f, lowcut); hpL_.set(f, sr); hpR_.set(f, sr); }
        const float depth = (0.00015f + mod * 0.0009f) * sr;
        const float lfoInc = 32.0f * 0.31f / sr, in = freeze ? 0.0f : 0.35f;
        const bool useHp = lowcut > 0.005f;
        const int pd = std::clamp(static_cast<int>(pre * sr + 0.5f), 1, preL_.maxD);
        static const float A[N] = {1, -1, 0.7f, -0.7f, 0, 0.5f, -0.5f, 1}, B[N] = {0, 0.5f, 0.7f, 0.7f, 1, -1, 0.8f, -0.4f};
        for (int i = 0; i < n; ++i) {
            preL_.push(inL[i]); preR_.push(inR[i]);
            float xl = pre > 0.001f ? preL_.at(pd) : inL[i], xr = pre > 0.001f ? preR_.at(pd) : inR[i];
            if (useHp) { xl = hpL_.process(xl); xr = hpR_.process(xr); }
            for (auto& a : apL_) xl = a.process(xl);
            for (auto& a : apR_) xr = a.process(xr);
            // la modulación es muy lenta: basta recalcular las posiciones cada 32 muestras, y se ahorra casi todo el coste.
            // Dos senos en cuadratura bastan: cada línea usa una combinación distinta
            if ((i & 31) == 0) {
                lfo_ += lfoInc; if (lfo_ >= 1) lfo_ -= 1;
                const float m1 = fsin(lfo_) * depth, m2 = fsin(lfo_ + 0.25f) * depth;
                for (int k = 0; k < N; ++k) { const float p = std::clamp(dl_[k] + A[k] * m1 + B[k] * m2, 2.0f, static_cast<float>(d_[k].maxD - 2)); id_[k] = static_cast<int>(p); fr_[k] = p - static_cast<float>(id_[k]); }
            }
            float y[N], sum = 0;
            for (int k = 0; k < N; ++k) { const float c = d_[k].at(id_[k]), a = d_[k].at(id_[k] + 1); y[k] = lp_[k].process(c + (a - c) * fr_[k]) * g_[k]; sum += y[k]; }
            const float h = -2.0f / N * sum;
            for (int k = 0; k < N; ++k) d_[k].push(y[k] + h + (k < N / 2 ? xl : xr) * in);
            // cada lado escucha líneas distintas: estéreo ancho y sin que nada se cancele al sumar a mono
            outL[i] = 0.6f * (y[0] + y[2] - y[5] + y[7]);
            outR[i] = 0.6f * (y[1] - y[3] + y[4] + y[6]);
        }
    }
private:
    DelayLine d_[N], preL_, preR_; OnePoleLP lp_[N]; OnePoleHP hpL_, hpR_; Allpass apL_[ND], apR_[ND];
    float lfo_ = 0, sc_ = 0, dl_[N] = {}, g_[N] = {}, fr_[N] = {}, key_[4] = {-1, -1, -1, -1}; int id_[N] = {2, 2, 2, 2, 2, 2, 2, 2};
};

// ------------------------------------------------------------------ plate de Dattorro (AES 1997): el tanque clásico de plancha
struct Plate {
    float sr = 48000, decay = 0.6f, damp = 0.2f, pre = 0.01f, mod = 0.3f, lowcut = 0;
    void prepare(float s) {
        sr = s; sc_ = sr / 29761.0f; const auto S = [&](int x) { return static_cast<int>(x * sc_); };
        preD_.resize(static_cast<int>(0.3f * sr));
        in_[0].resize(S(142), 0.75f); in_[1].resize(S(107), 0.75f); in_[2].resize(S(379), 0.625f); in_[3].resize(S(277), 0.625f);
        // el tanque: dos ramas que se alimentan en cruz
        mL_.resize(S(672) + 40); mR_.resize(S(908) + 40);
        d1L_.resize(S(4453) + 4); d2L_.resize(S(3720) + 4); d1R_.resize(S(4217) + 4); d2R_.resize(S(3163) + 4);
        apL_.resize(S(1800), 0.5f); apR_.resize(S(2656), 0.5f);
        lpF_ = -1;
        static const int T[14] = {4453, 3720, 4217, 3163, 266, 2974, 1996, 1990, 1066, 353, 3627, 2673, 2111, 121};
        for (int i = 0; i < 14; ++i) tap_[i] = std::max(1, static_cast<int>(T[i] * sc_ + 0.5f));
    }
    void zero() { preD_.zero(); for (auto& a : in_) a.zero(); mL_.zero(); mR_.zero(); d1L_.zero(); d2L_.zero(); d1R_.zero(); d2R_.zero(); apL_.zero(); apR_.zero(); fbL_ = fbR_ = 0; lpL_.s = lpR_.s = bw_.s = 0; hp_.s = 0; }
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        const float dF = damp;
        if (lpF_ != dF) { lpF_ = dF; const float f = 1200.0f * std::pow(13.0f, 1.0f - dF); lpL_.set(f, sr); lpR_.set(f, sr); bw_.set(9000.0f, sr); }
        if (hpK_ != lowcut) { hpK_ = lowcut; hp_.set(20.0f * std::pow(30.0f, lowcut), sr); }
        const bool useHp = lowcut > 0.005f;
        const float dec = std::clamp(decay, 0.0f, 0.98f), dif1 = 0.7f, exc = (2.0f + mod * 14.0f) * sc_;
        const float tapS = sc_;
        for (int i = 0; i < n; ++i) {
            float x = 0.5f * (inL[i] + inR[i]);
            preD_.push(x); x = pre > 0.001f ? preD_.read(pre * sr) : x;
            if (useHp) x = hp_.process(x);
            x = bw_.process(x);
            for (auto& a : in_) x = a.process(x);
            lfo_ += 0.8f / sr; if (lfo_ >= 1) lfo_ -= 1;
            const float mA = fsin(lfo_) * exc, mB = fsin(lfo_ + 0.25f) * exc;
            // rama izquierda
            float a = x + fbR_;
            { const float dl = mL_.read(672.0f * tapS + mA), v = a + dif1 * dl; mL_.push(v); a = dl - dif1 * v; }
            d1L_.push(a); a = lpL_.process(d1L_.at(tap_[0])) * dec;
            a = apL_.process(a); d2L_.push(a); const float tl = d2L_.at(tap_[1]) * dec;
            // rama derecha
            float b = x + fbL_;
            { const float dl = mR_.read(908.0f * tapS + mB), v = b + dif1 * dl; mR_.push(v); b = dl - dif1 * v; }
            d1R_.push(b); b = lpR_.process(d1R_.at(tap_[2])) * dec;
            b = apR_.process(b); d2R_.push(b); const float tr = d2R_.at(tap_[3]) * dec;
            fbL_ = softClip(tl); fbR_ = softClip(tr);
            // las siete tomas de salida de cada lado, tal como las da el artículo
            outL[i] = 0.25f * (d1R_.at(tap_[4]) + d1R_.at(tap_[5]) + d2R_.at(tap_[6]) - d1L_.at(tap_[7]) - d2L_.at(tap_[8]));
            outR[i] = 0.25f * (d1L_.at(tap_[9]) + d1L_.at(tap_[10]) + d2L_.at(tap_[11]) - d1R_.at(tap_[12]) - d2R_.at(tap_[13]));
        }
    }
private:
    Allpass in_[4], apL_, apR_; DelayLine preD_, mL_, mR_, d1L_, d2L_, d1R_, d2R_; OnePoleLP lpL_, lpR_, bw_; OnePoleHP hp_;
    float sc_ = 1, lfo_ = 0, fbL_ = 0, fbR_ = 0, lpF_ = -1, hpK_ = -1; int tap_[14] = {};
};

// ------------------------------------------------------------------ efectos de envío
enum FxType { FX_HALL = 0, FX_PLATE, FX_DARK, FX_PING, FX_TAPE, FX_DUB, FX_MULTI, FX_CHORUS, FX_COUNT };

// Divisiones de tiempo: semicorcheas
inline const float kDivs[10] = {0.5f, 1, 4.0f / 3, 1.5f, 2, 8.0f / 3, 3, 4, 6, 8};

class SendFx {
public:
    int type = FX_HALL; float p[5] = {0.55f, 0.4f, 0.02f, 0, 0}; float ret = 0.6f;
    void prepare(float sr) { sr_ = sr; fdn_.prepare(sr); plate_.prepare(sr); for (auto& d : dl_) d.resize(static_cast<int>(4.2f * sr)); }
    void clear() { fdn_.zero(); plate_.zero(); for (auto& d : dl_) d.zero(); }
    // Al cambiar de efecto: el retorno calla y los búferes se vacían de uno en uno, un bloque cada uno
    // (vaciarlos de golpe son ~2 MB dentro del audio)
    void startClear() { clearStage_ = 0; dSm_ = 0; }
    bool clearing() const { return clearStage_ >= 0; }
    void clearStep() {
        constexpr int nd = static_cast<int>(sizeof(dl_) / sizeof(dl_[0]));
        if (clearStage_ < 0) return;
        if (clearStage_ < nd) dl_[clearStage_].zero(); else if (clearStage_ == nd) fdn_.zero(); else { plate_.zero(); for (auto& l : tlp_) l.s = 0; for (auto& h : thp_) h.s = 0; for (auto& b : bp_) b.reset(); }
        clearStage_ = clearStage_ > nd ? -1 : clearStage_ + 1;
    }
    // in: suma de envíos (estéreo). out: retorno (se suma al máster)
    void process(const float* inL, const float* inR, float* oL, float* oR, int n, float bpm) {
        const float s16 = 60.0f / std::max(20.0f, bpm) / 4.0f;
        switch (type) {
        case FX_HALL: fdn_.t60 = 0.6f + p[0] * 3.6f; fdn_.damp = 1500.0f * std::pow(8.0f, 1.0f - p[1]); fdn_.size = 0.25f + p[0] * 0.5f; fdn_.pre = p[2]; fdn_.mod = p[3]; fdn_.lowcut = p[4]; fdn_.process(inL, inR, oL, oR, n); break;
        case FX_PLATE: plate_.decay = 0.25f + p[0] * 0.72f; plate_.damp = p[1]; plate_.pre = p[2]; plate_.mod = p[3]; plate_.lowcut = p[4]; plate_.process(inL, inR, oL, oR, n); break;
        case FX_DARK: fdn_.t60 = 3.0f + p[0] * 12.0f; fdn_.damp = 900.0f * std::pow(12.0f, 1.0f - p[1]); fdn_.size = 0.4f + p[0] * 0.6f; fdn_.pre = 0.02f + p[2] * 0.18f; fdn_.mod = p[3]; fdn_.lowcut = p[4]; fdn_.process(inL, inR, oL, oR, n); break;
        case FX_PING: {
            const float dT = kDivs[idx(p[0])] * s16 * sr_, fb = std::min(p[1], 0.95f), sg = 1.0f + p[4] * 4.0f;
            setLoop(800.0f * std::pow(20.0f, p[2]), p[3]);
            for (int i = 0; i < n; ++i) {
                const float d = glide(dT);
                const float yl = loop(0, dl_[0].read(d), sg), yr = loop(1, dl_[1].read(d), sg);
                dl_[0].push(0.5f * (inL[i] + inR[i]) + yr * fb); dl_[1].push(yl * fb);
                oL[i] = yl; oR[i] = yr;
            }
            break; }
        case FX_TAPE: case FX_DUB: {
            const bool dub = type == FX_DUB;
            // el tiempo se desliza (unos 200 ms) al cambiar de división o de tempo: como una cinta, sin saltos.
            // Dos cabezas: cada lado con su propia cinta y un temblor distinto, para que el eco se abra en estéreo.
            const float dT = kDivs[idx(p[0])] * s16 * sr_, fb = std::min(p[1], dub ? 1.15f : 1.0f);
            if (dub) { const float f = 300.0f * std::pow(10.0f, p[2]); bp_[0].set(2, f, 0.9f, sr_); bp_[1].set(2, f * 1.06f, 0.9f, sr_); }
            setLoop(800.0f * std::pow(15.0f, dub ? 1.0f : p[2]), 0.1f);
            const float wow = dub ? 0.15f : p[3], sat = dub ? p[3] : p[4], sg = 1.0f + sat * 4.0f;
            const float maxW = 0.003f * sr_;
            for (int i = 0; i < n; ++i) {
                wph_ += 0.6f / sr_; if (wph_ >= 1) wph_ -= 1;
                flt_ += 5.3f / sr_; if (flt_ >= 1) flt_ -= 1;
                wnd_ += (noise_.next() - wnd_) * 0.0015f;   // deriva lenta, no ruido: la cinta no raspa
                const float d = glide(dT), w = std::min(d * 0.004f, maxW) * wow;
                const float mL = w * (fsin(wph_) + 0.25f * fsin(flt_) + 3.0f * wnd_), mR = w * (fsin(wph_ + 0.3f) + 0.25f * fsin(flt_ + 0.5f) - 3.0f * wnd_);
                float yl = dl_[0].read(d + mL), yr = dl_[1].read(d * 1.008f + mR);
                if (dub) { yl = bp_[0].process(yl) * 1.4f; yr = bp_[1].process(yr) * 1.4f; }
                yl = loop(0, yl, sg); yr = loop(1, yr, sg);
                dl_[0].push(inL[i] + yl * fb); dl_[1].push(inR[i] + yr * fb);
                oL[i] = yl; oR[i] = yr;
            }
            break; }
        case FX_MULTI: {
            static const float PAT[4][4] = {{1, 2, 3, 4}, {1, 1.66f, 3, 3.66f}, {0.75f, 1.5f, 2.25f, 3}, {1, 2, 4, 8}};
            const float dT = kDivs[idx(p[0])] * s16 * sr_; const int pt = std::clamp(static_cast<int>(p[1] + 0.5f), 0, 3); const float fb = std::min(p[2], 0.9f);
            setLoop(800.0f * std::pow(20.0f, p[3]), 0);
            for (int i = 0; i < n; ++i) {
                const float d = glide(dT) * 0.5f;
                const float t0 = dl_[0].read(d * PAT[pt][0]), t1 = dl_[0].read(d * PAT[pt][1]) * 0.8f, t2 = dl_[0].read(d * PAT[pt][2]) * 0.6f, t3 = dl_[0].read(d * PAT[pt][3]) * 0.4f;
                // las tomas se reparten a izquierda y derecha; la realimentación pasa por la saturación, así no puede crecer sin límite
                const float y = loop(0, (t0 + t1 + t2 + t3) * 0.4f, 1.0f);
                dl_[0].push(0.5f * (inL[i] + inR[i]) + y * fb);
                oL[i] = (t0 + 0.4f * t1 + t2 + 0.4f * t3) * 0.7f; oR[i] = (0.4f * t0 + t1 + 0.4f * t2 + t3) * 0.7f;
            }
            break; }
        default: {   // chorus / flanger estéreo
            const float rate = 0.05f * std::pow(160.0f, p[0]), depth = p[1], fb = std::min(p[2], 0.9f);
            for (int i = 0; i < n; ++i) {
                cph_ += rate / sr_; if (cph_ >= 1) cph_ -= 1;
                const float m = fsin(cph_), base = 0.004f * sr_, dep = depth * 0.0035f * sr_;
                const float yl = dl_[0].read(base + dep * m), yr = dl_[1].read(base - dep * m);
                dl_[0].push(softClip(inL[i] + yl * fb)); dl_[1].push(softClip(inR[i] + yr * fb));
                oL[i] = yl; oR[i] = yr;
            }
            break; }
        }
        for (int i = 0; i < n; ++i) { oL[i] *= ret; oR[i] *= ret; }
    }
private:
    static int idx(float v) { return std::clamp(static_cast<int>(v + 0.5f), 0, 9); }
    // filtros de la realimentación: tono (paso bajo) y corte de graves, para que las repeticiones no embarren
    void setLoop(float lpHz, float low) {
        if (lpHz != lpK_) { lpK_ = lpHz; tlp_[0].set(lpHz, sr_); tlp_[1].set(lpHz, sr_); }
        if (low != hpK_) { hpK_ = low; const float f = 20.0f * std::pow(40.0f, low); thp_[0].set(f, sr_); thp_[1].set(f, sr_); }
    }
    float loop(int c, float y, float sg) { y = thp_[c].process(tlp_[c].process(y)); return softClip(y * sg) / sg; }   // saturación de ganancia unidad
    float glide(float target) { if (dSm_ <= 0) dSm_ = target; dSm_ += (target - dSm_) * (1.0f - std::exp(-1.0f / (0.2f * sr_))); return dSm_; }
    float sr_ = 48000, wph_ = 0, flt_ = 0, wnd_ = 0, cph_ = 0, dSm_ = 0, lpK_ = -1, hpK_ = -1; int clearStage_ = -1;
    FDN fdn_; Plate plate_; DelayLine dl_[2]; OnePoleLP tlp_[2]; OnePoleHP thp_[2]; BQ bp_[2]; Noise noise_;
};

// ------------------------------------------------------------------ suma analógica (idea de Console, de Airwindows, MIT):
// cada canal entra al bus por un seno y el bus sale por el arcoseno. Un canal solo sale igual que entró;
// lo que cambia es cómo se juntan: los canales se empujan un poco entre sí, como en una mesa.
inline float sumIn(float x, float g) {
    const float v = std::clamp(x * g, -1.5707f, 1.5707f), v2 = v * v;
    return v * (1.0f + v2 * (-0.16666667f + v2 * (0.0083333f - v2 * 0.00019841f))) / g;
}
inline float sumOut(float x, float g) { return std::asin(std::clamp(x * g, -1.0f, 1.0f)) / g; }

// ------------------------------------------------------------------ efectos de directo del máster
struct MasterFx {
    float sr = 48000;
    // Cada efecto tiene una cantidad de 0 a 1: 0 = apagado, se puede llevar poco a poco
    float rep = 0, repSize = 2;               // repeat: cantidad = mezcla seco/repetición; tamaño 0..5 = 1 compás, 1/2, 1/4, 1/8, 1/16, 1/32
    float gate = 0, gateDiv = 1;              // gater: profundidad
    float riser = 0;                          // riser: posición del barrido
    float space = 0, spaceSize = 0.7f;        // space: envío a la reverb larga
    float thr = 0, throwDiv = 6, throwFb = 0.55f;   // delay throw: envío
    float tape = 0;                           // tape stop: 1 = cinta parada
    float repDecay = 0, riserNoise = 0.5f, tapeInertia = 0.5f;   // segundos parámetros
    float repPitch = 0;                       // repeat: cada vuelta sube o baja (−1..1 = hasta una octava por vuelta)
    float gShape = 0, gPan = 0;               // gater: de corte seco a trémolo suave; alternar izquierda y derecha
    float spDamp = 0.53f; bool spFreeze = false;   // space: tono de la cola; congelar lo que ya está sonando
    float thTone = 0.7f;                      // throw: tono de las repeticiones
    float comb = 0, cbPitch = 48, cbFb = 0.8f, cbDamp = 0.3f;              // comb: filtro afinado a una nota
    float sweep = 0, swRate = 2, swDepth = 0.7f, swFb = 0.4f, swMode = 0;  // sweep: phaser o flanger a tempo

    void prepare(float s) {
        sr = s; rep_[0].assign(static_cast<size_t>(sr * 4), 0); rep_[1].assign(static_cast<size_t>(sr * 4), 0);
        tape_[0].assign(static_cast<size_t>(sr * 6), 0); tape_[1].assign(static_cast<size_t>(sr * 6), 0);
        spaceRv_.prepare(sr); thL_.resize(static_cast<int>(sr * 4)); thR_.resize(static_cast<int>(sr * 4));
        cbL_.resize(static_cast<int>(sr / 15.0f)); cbR_.resize(static_cast<int>(sr / 15.0f));
        flL_.resize(static_cast<int>(sr * 0.02f)); flR_.resize(static_cast<int>(sr * 0.02f));
    }
    // Si algo se hubiera roto (un NaN), se limpia lo que tiene memoria. Solo ocurre en caso de avería.
    void reset() {
        spaceRv_.zero(); thL_.zero(); thR_.zero(); cbL_.zero(); cbR_.zero(); flL_.zero(); flR_.zero();
        for (auto& v : rep_) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& v : tape_) std::fill(v.begin(), v.end(), 0.0f);
        for (auto& a : apS_) for (auto& x : a) x = 0;
        cbLpL_ = OnePoleLP{}; cbLpR_ = OnePoleLP{}; thLp_ = OnePoleLP{}; thHp_ = OnePoleHP{}; swFbL_ = swFbR_ = 0;
        hpR_[0].reset(); hpR_[1].reset(); nbp_.reset(); tapeRate_ = 1; repState_ = 0; repGain_ = 1;
    }
    static int repBand(float a) { return a < 0.02f ? -1 : a < 0.25f ? 0 : a < 0.5f ? 1 : a < 0.75f ? 2 : 3; }
    // pos: posición en semicorcheas (para cuadrar repeat y gater con el tempo)
    void process(float* L, float* R, int n, float bpm, double pos) {
        const float s16 = 60.0f / std::max(20.0f, bpm) / 4.0f, spb = s16 * sr;
        const float sm = std::min(1.0f, n / (0.02f * sr));
        processComb(L, R, n, sm);
        processSweep(L, R, n, sm, spb, pos);
        // --- riser: paso alto que sube, ruido y algo de reverb
        riserS_ += (riser - riserS_) * sm;
        const float rk = riserS_;
        if (rk > 0.001f) {
            hpR_[0].set(1, 20.0f * std::pow(150.0f, rk), 0.7f, sr); hpR_[1].set(1, 20.0f * std::pow(150.0f, rk), 0.7f, sr); nbp_.set(2, 300.0f * std::pow(30.0f, rk), 1.5f, sr);
            for (int i = 0; i < n; ++i) { const float nz = nbp_.process(noise_.next()) * rk * rk * riserNoise; L[i] = hpR_[0].process(L[i]) + nz; R[i] = hpR_[1].process(R[i]) + nz; }
        }
        // --- gater: la profundidad es la cantidad. Shape lo lleva de corte seco a trémolo; Pan alterna los lados
        if (gate > 0.001f || gateLvl_ < 0.999f || gateLvlR_ < 0.999f) {
            const double per = gateDiv;
            const float tau = 0.002f + gShape * gShape * 0.25f * static_cast<float>(per) * s16;
            const float smooth = 1.0f - std::exp(-1.0f / (tau * sr));
            for (int i = 0; i < n; ++i) {
                const double ph = std::fmod(pos + i / spb, per) / per;
                const bool first = ph < 0.5;
                const float tL = first ? 1.0f : 1.0f - gate, tR = first ? 1.0f - gate * gPan : 1.0f - gate * (1.0f - gPan);
                gateLvl_ += (tL - gateLvl_) * smooth; gateLvlR_ += (tR - gateLvlR_) * smooth;
                L[i] *= gateLvl_; R[i] *= gateLvlR_;
            }
        }
        // --- repeat: captura un trozo del tamaño elegido desde el siguiente golpe y lo repite; la cantidad mezcla seco y repetición.
        // Con Pitch cada vuelta sube o baja: el redoble que trepa.
        static const float kLen[6] = {16, 8, 4, 2, 1, 0.5f};
        const int band = rep < 0.02f ? -1 : std::clamp(static_cast<int>(std::lround(repSize)), 0, 5);
        if (band != repBand_) { repBand_ = band; repState_ = 0; }
        repMix_ += (rep - repMix_) * sm;
        const int len = band < 0 ? 64 : std::clamp(static_cast<int>(kLen[band] * spb), 64, static_cast<int>(rep_[0].size()));
        const float turn = std::exp2(repPitch);
        for (int i = 0; i < n; ++i) {
            const double p = pos + i / spb;
            if (band >= 0 && repState_ == 0) { if (std::floor(p * 2) != std::floor((p - 1.0 / spb) * 2)) { repState_ = 1; repI_ = 0; } }
            if (band < 0) repState_ = 0;
            if (repState_ == 1) { rep_[0][static_cast<size_t>(repI_)] = L[i]; rep_[1][static_cast<size_t>(repI_)] = R[i]; if (++repI_ >= len) { repState_ = 2; repPos_ = 0; repRate_ = 1; repGain_ = 1; } }
            else if (repState_ == 2) {
                // en los bordes de cada vuelta la repetición cede al seco 2 ms: sin clics
                const double lim = len - 2.0;
                const float fade = std::min(1.0f, static_cast<float>(std::min(repPos_, lim - repPos_)) / (0.002f * sr)), w = repMix_ * std::max(fade, 0.0f);
                const size_t i0 = static_cast<size_t>(repPos_); const float fr = static_cast<float>(repPos_ - static_cast<double>(i0));
                const float a = rep_[0][i0] + (rep_[0][i0 + 1] - rep_[0][i0]) * fr, b = rep_[1][i0] + (rep_[1][i0 + 1] - rep_[1][i0]) * fr;
                L[i] = L[i] * (1.0f - w) + a * w * repGain_; R[i] = R[i] * (1.0f - w) + b * w * repGain_;
                repPos_ += repRate_;
                if (repPos_ >= lim) { repPos_ = 0; repGain_ *= 1.0f - repDecay * 0.45f; repRate_ = std::clamp(repRate_ * turn, 0.25f, 4.0f); }
            }
        }
        // --- space (reverb larga, con cola) y delay throw
        spaceSend_ += (std::max(space, 0.6f * rk) - spaceSend_) * sm;
        throwSend_ += (thr - throwSend_) * std::min(1.0f, n / (0.01f * sr));
        if (spaceSend_ > 0.001f || spaceTail_ > 0 || spFreeze) {
            spaceTail_ = spaceSend_ > 0.001f || spFreeze ? 14.0f : spaceTail_ - n / sr;
            spaceRv_.t60 = 3.0f + spaceSize * 12.0f; spaceRv_.damp = 1500.0f * std::pow(8.0f, 1.0f - spDamp); spaceRv_.size = 0.4f + spaceSize * 0.6f; spaceRv_.pre = 0.03f;
            spaceRv_.mod = 0.4f; spaceRv_.lowcut = 0.25f; spaceRv_.freeze = spFreeze;
            float sl[512], sr2[512], ol[512], orr[512];
            for (int o = 0; o < n; o += 512) {
                const int k = std::min(512, n - o);
                for (int i = 0; i < k; ++i) { sl[i] = L[o + i] * spaceSend_; sr2[i] = R[o + i] * spaceSend_; }
                spaceRv_.process(sl, sr2, ol, orr, k);
                for (int i = 0; i < k; ++i) { L[o + i] += ol[i] * 0.9f; R[o + i] += orr[i] * 0.9f; }
            }
        }
        if (throwSend_ > 0.001f || throwTail_ > 0) {
            throwTail_ = throwSend_ > 0.001f ? 15.0f : throwTail_ - n / sr;
            const float dT = kDivs[std::clamp(static_cast<int>(throwDiv + 0.5f), 0, 9)] * s16 * sr;
            if (thDsm_ <= 0) thDsm_ = dT;
            thDsm_ += (dT - thDsm_) * std::min(1.0f, n / (0.2f * sr));
            if (thToneK_ != thTone) { thToneK_ = thTone; thLp_.set(300.0f * std::pow(40.0f, thTone), sr); thHp_.set(250, sr); }
            const float fb = std::min(throwFb, 1.0f);
            for (int i = 0; i < n; ++i) {
                const float y = softClip(thHp_.process(thLp_.process(thL_.read(thDsm_))));
                const float yr = thR_.read(thDsm_ * 1.5f);
                thL_.push(0.5f * (L[i] + R[i]) * throwSend_ + y * fb); thR_.push(y * 0.8f);
                L[i] += y * 0.9f; R[i] += (0.5f * y + 0.5f * yr) * 0.9f;
            }
        }
        // --- tape stop: la cantidad frena la cinta; a 0 vuelve a tiempo real
        const float target = 1.0f - std::clamp(tape, 0.0f, 1.0f), tapeK = 0.004f * std::pow(0.02f, tapeInertia);
        for (int i = 0; i < n; ++i) {
            const size_t N = tape_[0].size();
            tape_[0][static_cast<size_t>(tw_)] = L[i]; tape_[1][static_cast<size_t>(tw_)] = R[i];
            tapeRate_ += (target - tapeRate_) * tapeK;
            if (tape < 0.005f && tapeRate_ > 0.97f) { tapeRate_ = 1.0f; tr_ = static_cast<double>(tw_); }
            if (tapeRate_ < 1.0f) {
                tr_ += tapeRate_; while (tr_ >= static_cast<double>(N)) tr_ -= static_cast<double>(N);
                const size_t i0 = static_cast<size_t>(tr_), i1 = (i0 + 1) % N; const float fr = static_cast<float>(tr_ - std::floor(tr_));
                const float g = std::min(1.0f, tapeRate_ * 3.0f);
                L[i] = (tape_[0][i0] + (tape_[0][i1] - tape_[0][i0]) * fr) * g;
                R[i] = (tape_[1][i0] + (tape_[1][i1] - tape_[1][i0]) * fr) * g;
            } else tr_ = static_cast<double>(tw_);
            if (++tw_ >= static_cast<long>(N)) tw_ = 0;
        }
    }
private:
    // --- comb: un eco tan corto que se convierte en nota. Amount mezcla, Pitch afina, Feedback da cuerpo, Damp oscurece
    void processComb(float* L, float* R, int n, float sm) {
        combS_ += (comb - combS_) * sm;
        if (combS_ < 0.001f && combTail_ <= 0) return;
        combTail_ = combS_ >= 0.001f ? 4.0f : combTail_ - n / sr;
        const float dT = sr / (440.0f * std::exp2((std::clamp(cbPitch, 24.0f, 96.0f) - 69.0f) / 12.0f));
        if (cbD_ <= 0) cbD_ = dT;
        if (cbDampK_ != cbDamp) { cbDampK_ = cbDamp; const float f = 18000.0f * std::pow(0.08f, cbDamp); cbLpL_.set(f, sr); cbLpR_.set(f, sr); }
        const float fb = std::clamp(cbFb, 0.0f, 0.97f), norm = 1.0f - fb * 0.85f, k = 1.0f - std::exp(-1.0f / (0.03f * sr));
        for (int i = 0; i < n; ++i) {
            cbD_ += (dT - cbD_) * k;
            const float yl = cbLpL_.process(cbL_.read(cbD_)), yr = cbLpR_.process(cbR_.read(cbD_ * 1.003f));
            cbL_.push(softClip(L[i] * norm + yl * fb)); cbR_.push(softClip(R[i] * norm + yr * fb));
            L[i] += (yl - L[i]) * combS_; R[i] += (yr - R[i]) * combS_;
        }
    }
    // --- sweep: phaser de 6 etapas o flanger, con la oscilación atada al tempo (sigue al Oxi)
    void processSweep(float* L, float* R, int n, float sm, float spb, double pos) {
        sweepS_ += (sweep - sweepS_) * sm;
        if (sweepS_ < 0.001f) return;
        static const float kBars[6] = {4, 8, 16, 32, 64, 128};   // en semicorcheas: 1/4, 1/2, 1, 2, 4 y 8 compases
        const float per = kBars[std::clamp(static_cast<int>(swRate + 0.5f), 0, 5)];
        const bool flanger = swMode >= 0.5f; const float depth = std::clamp(swDepth, 0.0f, 1.0f), fb = std::clamp(swFb, 0.0f, 0.9f), wet = sweepS_ * 0.5f;
        for (int i = 0; i < n; ++i) {
            const float ph = static_cast<float>(std::fmod(pos + i / spb, static_cast<double>(per)) / per);
            if (flanger) {
                const float ml = 0.5f + 0.5f * fsin(ph), mr = 0.5f + 0.5f * fsin(ph + 0.25f);
                const float dl = (0.0004f + 0.007f * depth * ml) * sr, dr = (0.0004f + 0.007f * depth * mr) * sr;
                const float yl = flL_.read(dl), yr = flR_.read(dr);
                flL_.push(softClip(L[i] + yl * fb)); flR_.push(softClip(R[i] + yr * fb));
                L[i] += (yl - L[i]) * wet; R[i] += (yr - R[i]) * wet;
            } else {
                if (i % 16 == 0) {   // coeficientes cada 16 muestras: la tangente es cara y el barrido, lento
                    const float low = 3.0f + (1.0f - depth) * 2.5f;
                    const float fl = 60.0f * std::exp(low + depth * 3.0f * (0.5f + 0.5f * fsin(ph))), fr = 60.0f * std::exp(low + depth * 3.0f * (0.5f + 0.5f * fsin(ph + 0.25f)));
                    const float tl = std::tan(kPi * std::min(fl, 0.4f * sr) / sr), tr = std::tan(kPi * std::min(fr, 0.4f * sr) / sr);
                    apA_[0] = (tl - 1.0f) / (tl + 1.0f); apA_[1] = (tr - 1.0f) / (tr + 1.0f);
                }
                float y[2] = {L[i] + swFbL_ * fb, R[i] + swFbR_ * fb};
                for (int c = 0; c < 2; ++c) for (int s = 0; s < 6; ++s) { const float x = y[c], o = apA_[c] * x + apS_[c][s]; apS_[c][s] = x - apA_[c] * o; y[c] = o; }
                swFbL_ = softClip(y[0]); swFbR_ = softClip(y[1]);
                L[i] += (swFbL_ - L[i]) * wet; R[i] += (swFbR_ - R[i]) * wet;
            }
        }
    }
    std::vector<float> rep_[2], tape_[2]; int repState_ = 0, repI_ = 0, repBand_ = -1; long tw_ = 0; double tr_ = 0, repPos_ = 0; float tapeRate_ = 1, repGain_ = 1, repMix_ = 0, repRate_ = 1;
    float gateLvl_ = 1, gateLvlR_ = 1, riserS_ = 0, spaceSend_ = 0, spaceTail_ = 0, throwSend_ = 0, throwTail_ = 0, thDsm_ = 0, thToneK_ = -1;
    float combS_ = 0, combTail_ = 0, cbD_ = 0, cbDampK_ = -1, sweepS_ = 0, apA_[2] = {}, apS_[2][6] = {}, swFbL_ = 0, swFbR_ = 0;
    BQ hpR_[2], nbp_; Noise noise_; FDN spaceRv_; DelayLine thL_, thR_, cbL_, cbR_, flL_, flR_; OnePoleLP thLp_, cbLpL_, cbLpR_; OnePoleHP thHp_;
};

}  // namespace gb

// voice.h — procesador de voz en vivo, pensado como un transformador de voz de escenario:
// limpieza (paso alto, puerta, compresor, de-esser), detección de tono, cambio de tono y de
// formantes por PSOLA, afinación automática, robot, armonías, vocoder de 16 bandas y efectos.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

#include "dsp.h"
#include "fx.h"
#include "params.h"

namespace gb { inline float vcrush(float x, float bits) { if (bits >= 15.5f) return x; const float q = std::exp2(bits - 1.0f); return std::round(x * q) / q; } }

namespace gb {

enum VzP { VZ_IN, VZ_GAIN, VZ_LOWCUT, VZ_GATE, VZ_COMP, VZ_DESS,
           VZ_MODE, VZ_PITCH, VZ_FORMANT, VZ_TUNE, VZ_KEY, VZ_SCALE,
           VZ_HARM, VZ_HLVL, VZ_ROBOT, VZ_CARRIER, VZ_VTONE,
           VZ_FX, VZ_FXAMT, VZ_REVERB, VZ_ECHO, VZ_ETIME, VZ_BAL, VZ_LVL, VZ_COUNT };
enum VzMode { VM_NORMAL = 0, VM_AUTO, VM_ROBOT, VM_HARM, VM_VOCODER };
inline const PSpec kVzSpec[VZ_COUNT] = {
    {"Input", 0, 4, 1, 0, 'n', false, "In 1|In 2|In 3|In 4|In 1+2", "Input"}, {"Gain", 0, 30, 0.5f, 6, 'f', false, "dB", "Input"},
    {"Low cut", 20, 400, 0, 90, 'h', true, nullptr, "Input"}, {"Gate", -80, -20, 1, -80, 'g', false, nullptr, "Input"},
    {"Comp", 0, 1, 0.01f, 0.4f, 'p', false, nullptr, "Dynamics"}, {"De-ess", 0, 1, 0.01f, 0.3f, 'p', false, nullptr, "Dynamics"},
    {"Mode", 0, 4, 1, 0, 'n', false, "Normal|Auto-pitch|Robot|Harmony|Vocoder", "Voice"}, {"Pitch", -12, 12, 1, 0, 't', false, nullptr, "Voice"},
    {"Formant", -1, 1, 0.01f, 0, 'b', false, nullptr, "Voice"}, {"Retune", 0, 1, 0.01f, 0.8f, 'p', false, nullptr, "Voice"},
    {"Key", 0, 11, 1, 0, 'n', false, "C|C#|D|D#|E|F|F#|G|G#|A|A#|B", "Voice"}, {"Scale", 0, 2, 1, 1, 'n', false, "Chromatic|Major|Minor", "Voice"},
    {"Harmony", 0, 6, 1, 0, 'n', false, "3rd+5th|3rd up|5th up|Oct down|Oct up|5th+oct|MIDI notes", "Harmony / robot"},
    {"Harm level", 0, 1, 0.01f, 0.7f, 'p', false, nullptr, "Harmony / robot"}, {"Robot note", 36, 72, 1, 48, 'm', false, nullptr, "Harmony / robot"},
    {"Carrier", 0, 4, 1, 0, 'n', false, "Saw|Pulse|Track 2|Track 3|Track 4", "Vocoder"}, {"Tone", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "Vocoder"},
    {"Effect", 0, 5, 1, 0, 'n', false, "None|Megaphone|Radio|Strobe|Lo-fi|Drive", "Effects"}, {"Amount", 0, 1, 0.01f, 0.5f, 'p', false, nullptr, "Effects"},
    {"Reverb", 0, 1, 0.01f, 0.15f, 'p', false, nullptr, "Effects"}, {"Echo", 0, 1, 0.01f, 0, 'p', false, nullptr, "Effects"},
    {"Echo time", 0, 9, 1, 6, 'n', false, "1/32|1/16|1/8T|1/16D|1/8|1/4T|1/8D|1/4|1/4D|1/2", "Effects"},
    {"Balance", 0, 1, 0.01f, 1, 'p', false, nullptr, "Output"}, {"Level", 0, 1.5f, 0.01f, 0.9f, 'p', false, nullptr, "Output"}};

class VoiceFx {
public:
    float p[VZ_COUNT]; bool on = false;
    std::atomic<float> grDb{0}, f0{0};
    VoiceFx() { for (int j = 0; j < VZ_COUNT; ++j) p[j] = kVzSpec[j].def; }
    void prepare(float sr) {
        sr_ = sr; rv_.prepare(sr); echo_.resize(static_cast<int>(sr * 3)); notes_.reserve(16);
        for (int b = 0; b < kB; ++b) {
            const float fc = 120.0f * std::pow(7500.0f / 120.0f, b / (kB - 1.0f));
            mod_[b].set(2, fc, 5.0f, sr); car_[b].set(2, fc, 5.0f, sr); vsib_.set(1, 6000, 0.7f, sr);
        }
        envA_ = 1.0f - std::exp(-1.0f / (0.004f * sr)); envR_ = 1.0f - std::exp(-1.0f / (0.04f * sr));
    }
    void noteOn(int n) { notes_.erase(std::remove(notes_.begin(), notes_.end(), n), notes_.end()); notes_.push_back(n); if (notes_.size() > 4) notes_.erase(notes_.begin()); }
    void noteOff(int n) { notes_.erase(std::remove(notes_.begin(), notes_.end(), n), notes_.end()); }
    void allOff() { notes_.clear(); }

    // in: entradas sin procesar; trk: pistas 2-4 (portadoras del vocoder); bpm para el eco
    void render(const float* const* in, int ins, const float* const* trk, float* L, float* R, int n, float bpm) {
        if (!on || ins == 0) { for (int i = 0; i < n; ++i) L[i] = R[i] = 0.0f; return; }
        const int src = std::clamp(static_cast<int>(p[VZ_IN]), 0, 4), mode = std::clamp(static_cast<int>(p[VZ_MODE]), 0, 4), fx = std::clamp(static_cast<int>(p[VZ_FX]), 0, 5);
        const float gain = std::pow(10.0f, p[VZ_GAIN] / 20.0f);
        hpf_.set(1, p[VZ_LOWCUT], 0.707f, sr_); dsf_.set(1, 5500, 0.7f, sr_);
        const float gateThr = p[VZ_GATE] <= -79.5f ? 0.0f : std::pow(10.0f, p[VZ_GATE] / 20.0f);
        const float ca = p[VZ_COMP], thrDb = -8.0f - 30.0f * ca, ratio = 1.0f + 7.0f * ca, makeup = std::pow(10.0f, (-thrDb * (1.0f - 1.0f / ratio)) * 0.5f / 20.0f);
        const float thrLin = std::pow(10.0f, thrDb / 20.0f);
        const float atk = std::exp(-1.0f / (0.004f * sr_)), rel = std::exp(-1.0f / (0.12f * sr_)), gA = std::exp(-1.0f / (0.002f * sr_)), gR = std::exp(-1.0f / (0.1f * sr_));
        const float fShift = std::exp2(p[VZ_FORMANT] * 0.6f);   // ±0,6 octavas de formantes
        voiceSetup(mode);
        float maxGr = 0;
        for (int i = 0; i < n; ++i) {
            // --- limpieza
            float x = src < 4 ? (src < ins ? in[src][i] : 0.0f) : 0.5f * (in[0][i] + (ins > 1 ? in[1][i] : 0.0f));
            x = hpf_.process(x * gain);
            const float ax = std::fabs(x); genv_ = ax > genv_ ? ax : genv_ * 0.9995f;
            const float gt = gateThr > 0 && genv_ < gateThr ? 0.0f : 1.0f; gg_ = gt > gg_ ? gt + (gg_ - gt) * gA : gt + (gg_ - gt) * gR; x *= gg_;
            if (ca > 0.01f) {
                const float lv = ax * gg_; cenv_ = lv > cenv_ ? lv + (cenv_ - lv) * atk : lv + (cenv_ - lv) * rel;
                // por debajo del umbral no hace falta calcular nada (lo habitual): ahorra un log y un pow por muestra
                float gr = 1.0f;
                if (cenv_ > thrLin) { const float over = 20.0f * std::log10(cenv_) - thrDb, grdb = over * (1.0f - 1.0f / ratio); maxGr = std::max(maxGr, grdb); gr = std::exp2(-grdb * 0.16609640f); }
                x *= gr * makeup;
            }
            if (p[VZ_DESS] > 0.01f) {
                const float hi = dsf_.process(x); denv_ = std::fabs(hi) > denv_ ? std::fabs(hi) : denv_ * 0.999f;
                const float thr = 0.25f - 0.22f * p[VZ_DESS], red = denv_ > thr ? thr / denv_ : 1.0f; x -= hi * (1.0f - red);
            }
            const float clean = x;
            // --- análisis de tono (a 12 kHz, cada 256 muestras)
            ring_[static_cast<size_t>(w_ & kRM)] = clean;
            dAcc_ += clean; if (++dCnt_ == 4) { dec_[static_cast<size_t>(dw_ & kDM)] = dAcc_ * 0.25f; ++dw_; dAcc_ = 0; dCnt_ = 0; }
            if ((w_ & 255) == 0) detect();
            // --- voz principal y extras
            float wl = 0, wr = 0;
            if (mode == VM_VOCODER) { const float v = vocoder(clean, trk, i); wl = wr = v; }
            else {
                for (int k = 0; k < nv_; ++k) {
                    Shifter& s = sh_[k];
                    if (s.next < static_cast<double>(w_) - 4096) s.next = static_cast<double>(w_);
                    while (w_ >= s.next) { grain(s, fShift); s.next += std::max(32.0, s.period); }
                }
                const float m = acc_[0][static_cast<size_t>(w_ & kRM)], h = acc_[1][static_cast<size_t>(w_ & kRM)], hr = acc_[2][static_cast<size_t>(w_ & kRM)];
                acc_[0][static_cast<size_t>(w_ & kRM)] = acc_[1][static_cast<size_t>(w_ & kRM)] = acc_[2][static_cast<size_t>(w_ & kRM)] = 0.0f;
                const float hl = p[VZ_HLVL];
                wl = m + (h * 0.8f + hr * 0.4f) * hl; wr = m + (h * 0.4f + hr * 0.8f) * hl;
            }
            ++w_;
            // --- efecto de color
            if (fx > 0) colorFx(fx, wl, wr);
            // seco (limpio, con el mismo retardo que el procesado) y procesado
            const float dry = ring_[static_cast<size_t>((w_ - 1 - kDry) & kRM)];
            const float b = p[VZ_BAL];
            L[i] = dry * (1.0f - b) + wl * b; R[i] = dry * (1.0f - b) + wr * b;
        }
        // --- eco y reverb
        const float s16 = 60.0f / std::max(20.0f, bpm) / 4.0f;
        static const float kD[10] = {0.5f, 1, 4.0f / 3, 1.5f, 2, 8.0f / 3, 3, 4, 6, 8};
        if (p[VZ_ECHO] > 0.005f || echoTail_ > 0) {
            echoTail_ = p[VZ_ECHO] > 0.005f ? 8.0f : echoTail_ - n / sr_;
            const float d = kD[std::clamp(static_cast<int>(p[VZ_ETIME]), 0, 9)] * s16 * sr_;
            for (int i = 0; i < n; ++i) { const float y = echoLp_.process(echo_.read(d)); echo_.push(0.5f * (L[i] + R[i]) * p[VZ_ECHO] + y * 0.45f); L[i] += y * 0.8f; R[i] += y * 0.6f; }
            echoLp_.set(5000, sr_);
        }
        if (p[VZ_REVERB] > 0.005f || rvTail_ > 0) {
            rvTail_ = p[VZ_REVERB] > 0.005f ? 6.0f : rvTail_ - n / sr_;
            rv_.t60 = 1.2f + p[VZ_REVERB] * 2.5f; rv_.damp = 6000; rv_.size = 0.5f; rv_.pre = 0.015f;
            float sl[256], srr[256], ol[256], orr[256];
            for (int o = 0; o < n; o += 256) {
                const int k = std::min(256, n - o);
                for (int i = 0; i < k; ++i) { sl[i] = L[o + i] * p[VZ_REVERB]; srr[i] = R[o + i] * p[VZ_REVERB]; }
                rv_.process(sl, srr, ol, orr, k);
                for (int i = 0; i < k; ++i) { L[o + i] += ol[i] * 0.7f; R[o + i] += orr[i] * 0.7f; }
            }
        }
        for (int i = 0; i < n; ++i) { L[i] *= p[VZ_LVL]; R[i] *= p[VZ_LVL]; }
        grDb = maxGr;
    }

private:
    static constexpr int kRN = 16384, kRM = kRN - 1, kDN = 1024, kDM = kDN - 1, kB = 16;
    static constexpr long kDry = 600;   // retardo de la señal limpia para que cuadre con la procesada
    struct Shifter { double next = 0, period = 480, inMark = 0; int out = 0; float gain = 1; };

    // Detección de tono (YIN simplificado a 12 kHz): periodo en muestras a sr
    void detect() {
        constexpr int W = 256, tmin = 15, tmax = 150;   // 80 – 800 Hz
        auto at = [&](long q) { return dec_[static_cast<size_t>(q & kDM)]; };
        float d[tmax + 2]; float run = 0, e = 0;
        for (int j = 0; j < W; ++j) { const float v = at(dw_ - W + j); e += v * v; }
        d[0] = 1;
        for (int t = 1; t <= tmax + 1; ++t) {
            float s = 0; for (int j = 0; j < W; j += 2) { const float a = at(dw_ - W + j) - at(dw_ - W + j - t); s += a * a; }
            run += s; d[t] = run > 0 ? s * t / run : 1;
        }
        int bt = 0;
        for (int t = tmin; t <= tmax; ++t) if (d[t] < 0.15f) { while (t < tmax && d[t + 1] < d[t]) ++t; bt = t; break; }
        voiced_ = bt > 0 && e > 2e-5f * W;
        if (voiced_) {
            float tt = static_cast<float>(bt);
            const float a = d[bt - 1], b = d[bt], c = d[bt + 1], den = a - 2 * b + c; if (std::fabs(den) > 1e-6f) tt += std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f);
            const float per = tt * 4.0f;
            inPer_ = inPer_ <= 0 ? per : inPer_ + (per - inPer_) * 0.5f;
            f0 = sr_ / inPer_;
        } else f0 = 0;
    }
    static int nearestInScale(float m, int key, int scale) {
        static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MIN[7] = {0, 2, 3, 5, 7, 8, 10};
        const int base = static_cast<int>(std::floor(m)); int best = base; float bd = 1e9f;
        for (int c = base - 2; c <= base + 2; ++c) {
            const int pc = ((c - key) % 12 + 12) % 12; bool ok = scale == 0;
            for (int q = 0; q < 7 && !ok; ++q) ok = (scale == 1 ? MAJ[q] : MIN[q]) == pc;
            if (ok && std::fabs(c - m) < bd) { bd = std::fabs(c - m); best = c; }
        }
        return best;
    }
    // prepara periodos de salida de cada desplazador según el modo
    void voiceSetup(int mode) {
        const float per = voiced_ && inPer_ > 0 ? inPer_ : 480.0f;   // sin tono claro: granos de 10 ms
        const float detM = voiced_ ? 69.0f + 12.0f * std::log2(sr_ / per / 440.0f) : 60.0f;
        float semis = p[VZ_PITCH];
        if (mode == VM_AUTO && voiced_) {
            const float tgt = static_cast<float>(nearestInScale(detM, static_cast<int>(p[VZ_KEY]), static_cast<int>(p[VZ_SCALE])));
            const float k = 0.02f + 0.98f * p[VZ_TUNE] * p[VZ_TUNE];
            corr_ += ((tgt - detM) - corr_) * k; semis += corr_;
        } else corr_ *= 0.9f;
        // con notas MIDI (salvo en armonía con «MIDI notes»), la voz canta en la última nota
        const bool keys = !notes_.empty() && mode != VM_VOCODER && !(mode == VM_HARM && static_cast<int>(p[VZ_HARM]) == 6);
        if (keys && voiced_ && mode != VM_ROBOT) semis = static_cast<float>(notes_.back()) - detM + p[VZ_PITCH];
        nv_ = 1;
        sh_[0].out = 0; sh_[0].gain = 1;
        if (mode == VM_ROBOT) {
            const float note = (notes_.empty() ? p[VZ_ROBOT] : static_cast<float>(notes_.back())) + p[VZ_PITCH];
            sh_[0].period = sr_ / mtof(note);
        } else sh_[0].period = per / std::exp2(semis / 12.0f);
        if (mode == VM_HARM) {
            static const float H[6][2] = {{4, 7}, {4, 0}, {7, 0}, {-12, 0}, {12, 0}, {7, 12}};
            const int h = std::clamp(static_cast<int>(p[VZ_HARM]), 0, 6);
            float iv[2] = {0, 0}; int nh = 0;
            if (h == 6) { for (size_t q = 0; q < notes_.size() && nh < 2; ++q) iv[nh++] = voiced_ ? static_cast<float>(notes_[q]) - detM : 0; }
            else { iv[0] = H[h][0]; nh = 1; if (H[h][1] != 0) { iv[1] = H[h][1]; nh = 2; } }
            for (int q = 0; q < nh; ++q) { Shifter& s = sh_[1 + q]; s.out = 1 + q; s.gain = 0.8f; s.period = per / std::exp2((semis + iv[q]) / 12.0f); if (s.next < static_cast<double>(w_)) s.next = static_cast<double>(w_); }
            nv_ = 1 + nh;
        }
        inPerNow_ = per;
    }
    // un grano: dos periodos de entrada alrededor de la última marca, con ventana de Hann
    void grain(Shifter& s, float fShift) {
        const double T = inPerNow_;
        const int half = static_cast<int>(T);                         // medio grano en la entrada
        const int ho = std::max(8, static_cast<int>(half / fShift));    // medio grano en la salida
        const double readEnd = static_cast<double>(w_) - half * fShift - 2;   // el grano entero ya está grabado
        if (s.inMark < readEnd - 4 * T || s.inMark > readEnd) s.inMark = readEnd - T;
        while (s.inMark + T <= readEnd) s.inMark += T;
        const long center = w_ + ho;                                   // en la salida: justo por delante de lo que suena
        const float norm = static_cast<float>(std::min(2.0, s.period / ho)) * s.gain;
        auto& acc = acc_[s.out];
        for (int k = -ho; k < ho; ++k) {
            const double ip = s.inMark + k * static_cast<double>(fShift);
            const long i0 = static_cast<long>(std::floor(ip)); const float fr = static_cast<float>(ip - i0);
            const float a = ring_[static_cast<size_t>(i0 & kRM)], b = ring_[static_cast<size_t>((i0 + 1) & kRM)];
            const float win = 0.5f + 0.5f * fsin(static_cast<float>(k) / (2.0f * ho) + 0.25f);
            acc[static_cast<size_t>((center + k) & kRM)] += (a + (b - a) * fr) * win * norm;
        }
    }
    // vocoder de 16 bandas: la voz da la forma, la portadora el tono
    float vocoder(float mod, const float* const* trk, int i) {
        const int c = std::clamp(static_cast<int>(p[VZ_CARRIER]), 0, 4);
        float car = 0;
        if (c <= 1) {
            if (notes_.empty()) { cph_[0] += mtof(p[VZ_ROBOT]) / sr_; if (cph_[0] >= 1) cph_[0] -= 1; car = c == 0 ? 2 * cph_[0] - 1 : (cph_[0] < 0.3f ? 1.0f : -0.4f); }
            else for (size_t q = 0; q < notes_.size() && q < 4; ++q) { float& ph = cph_[q]; ph += mtof(static_cast<float>(notes_[q])) / sr_; if (ph >= 1) ph -= 1; car += (c == 0 ? 2 * ph - 1 : (ph < 0.3f ? 1.0f : -0.4f)) * 0.5f; }
            car += nz_.next() * 0.05f;
        } else car = trk[c - 2] ? trk[c - 2][i] * 3.0f : 0.0f;
        float y = 0;
        const float tone = p[VZ_VTONE];
        for (int b = 0; b < kB; ++b) {
            const float m = std::fabs(mod_[b].process(mod));
            ve_[b] += (m - ve_[b]) * (m > ve_[b] ? envA_ : envR_);
            const float tilt = 0.4f + tone * 1.2f * (b / (kB - 1.0f)) + (1.0f - tone) * 0.6f * (1.0f - b / (kB - 1.0f));
            y += car_[b].process(car) * ve_[b] * tilt;
        }
        const float sib = mod - vsib_.process(mod);
        return y * 6.0f + sib * tone * 0.6f;
    }
    int fx1Type_ = -1;
    void colorFx(int fx, float& l, float& r) {
        const float a = p[VZ_FXAMT];
        float m = 0.5f * (l + r), y = m;
        switch (fx) {
            case 1: { if (fx1Type_ != 1) { fx1Type_ = 1; fx1_.set(2, 1600, 1.4f, sr_); } y = std::tanh(fx1_.process(m) * (2 + a * 10)) * 0.6f; break; }   // megáfono
            case 2: { if (fx1Type_ != 2) { fx1Type_ = 2; fx1_.set(2, 1800, 0.8f, sr_); } y = vcrush(fx1_.process(m) * (1 + a * 3), 6.0f - a * 2) * 0.8f + nz_.next() * 0.01f * a; break; }   // radio
            case 3: { sph_ += (4.0f + a * 16.0f) / sr_; if (sph_ >= 1) sph_ -= 1; y = m * (sph_ < 0.5f ? 1.0f : 1.0f - std::min(1.0f, a * 1.5f)); break; }   // estroboscopio
            case 4: { if (++lfc_ >= 1 + static_cast<int>(a * 8)) { lfc_ = 0; lfh_ = vcrush(m, 12.0f - a * 8.0f); } y = lfh_; break; }   // lo-fi
            default: { const float g = 1 + a * 12; y = std::tanh(m * g) / std::tanh(g) * 0.8f; break; }   // drive
        }
        const float d = y - m; l += d; r += d;
    }

    float sr_ = 48000, genv_ = 0, gg_ = 1, cenv_ = 0, denv_ = 0;
    float ring_[kRN] = {}, acc_[3][kRN] = {}, dec_[kDN] = {}, dAcc_ = 0; long w_ = 0, dw_ = 0; int dCnt_ = 0;
    bool voiced_ = false; float inPer_ = 0, inPerNow_ = 480, corr_ = 0;
    Shifter sh_[3]; int nv_ = 1;
    BQ hpf_, dsf_, mod_[kB], car_[kB], vsib_, fx1_; float ve_[kB] = {}, envA_ = 0.01f, envR_ = 0.001f, cph_[4] = {};
    float sph_ = 0, lfh_ = 0; int lfc_ = 0; Noise nz_;
    FDN rv_; DelayLine echo_; OnePoleLP echoLp_; float echoTail_ = 0, rvTail_ = 0;
    std::vector<int> notes_;
};

}  // namespace gb

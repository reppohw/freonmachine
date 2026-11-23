// vis.h — datos para los visuales (la nevera, ui/nevera.html).
//
// El hilo de audio solo copia el máster a un anillo (vring), y solo mientras hay una pantalla de visuales conectada.
// Aquí, en el hilo de control, se analiza: 60 veces por segundo (cada sr/60 muestras, contadas por muestras, no por reloj)
// se calcula el espectro del máster con la misma ventana y escala que el AnalyserNode del navegador (Blackman, FFT de 2048,
// |X|/N en dB, sin suavizado). Así la nevera recibe exactamente lo que medía al escuchar por la tarjeta, y su oído (calibrado
// con lives reales) sirve tal cual, sin micro ni interfaz en el aparato que dibuja.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "recorder.h"
#include "server.h"

namespace vis {

constexpr int kN = 2048, kMask = kN - 1;

class Feed {
public:
    void prepare(float rate) {
        sr_ = rate; hop_ = std::max(1, static_cast<int>(std::lround(rate / 60.0f))); since_ = 0; pos_ = 0;
        std::fill(hist_, hist_ + kN, 0.0f);
        constexpr double a = 0.16, a0 = (1 - a) / 2, a1 = 0.5, a2 = a / 2, PI = 3.14159265358979323846;   // Blackman, como el navegador
        for (int n = 0; n < kN; ++n) win_[n] = static_cast<float>(a0 - a1 * std::cos(2 * PI * n / kN) + a2 * std::cos(4 * PI * n / kN));
        for (int k = 0; k < kN / 2; ++k) { cs_[k] = static_cast<float>(std::cos(2 * PI * k / kN)); sn_[k] = static_cast<float>(-std::sin(2 * PI * k / kN)); }
        for (int i = 0, j = 0; i < kN; ++i) { rev_[i] = j; int b = kN >> 1; while (j & b) { j ^= b; b >>= 1; } j |= b; }
        // hasta 16 kHz: la nevera no mira más arriba (así da igual 44,1 o 48 kHz)
        nb_ = std::min(kN / 2, static_cast<int>(std::ceil(16000.0f / (rate / kN))) + 2);
    }
    // Lee lo que haya en el anillo y llama a emit(frame) por cada análisis. Si se ha acumulado retraso (la red se
    // atascó, el control tardó), se salta lo viejo: la nevera siempre ve lo que suena ahora.
    template <class F> void run(rec::DiskRing& r, F&& emit) {
        float L[512], R[512]; float* d[2] = {L, R};
        size_t skip = 0; const size_t av = r.avail();
        if (av > static_cast<size_t>(kN + 3 * hop_)) skip = av - static_cast<size_t>(kN + hop_);
        for (;;) {
            const size_t m = r.pull(d, 512); if (!m) break;
            for (size_t i = 0; i < m; ++i) {
                hist_[pos_] = 0.5f * (L[i] + R[i]); pos_ = (pos_ + 1) & kMask;
                const bool old = skip > 0; if (skip) --skip;
                if (++since_ >= hop_) { since_ = 0; if (!old) { analyse(); emit(out_); } }
            }
        }
    }
    void reset(rec::DiskRing& r) { r.r.store(r.w.load(std::memory_order_acquire), std::memory_order_release); since_ = 0; }
    float rate() const { return sr_; }

private:
    void analyse() {
        double ms = 0;
        for (int n = 0; n < kN; ++n) { const float x = hist_[(pos_ + n) & kMask]; ms += static_cast<double>(x) * x; re_[rev_[n]] = x * win_[n]; im_[rev_[n]] = 0; }
        ms /= kN;
        for (int len = 2; len <= kN; len <<= 1) {
            const int h = len >> 1, step = kN / len;
            for (int i = 0; i < kN; i += len)
                for (int j = 0; j < h; ++j) {
                    const float wr = cs_[j * step], wi = sn_[j * step];
                    const float xr = re_[i + j + h] * wr - im_[i + j + h] * wi, xi = re_[i + j + h] * wi + im_[i + j + h] * wr;
                    re_[i + j + h] = re_[i + j] - xr; im_[i + j + h] = im_[i + j] - xi; re_[i + j] += xr; im_[i + j] += xi;
                }
        }
        // cada banda en 1 byte: medio dB por paso, de −127,5 a 0 dBFS
        bytes_.resize(static_cast<size_t>(nb_));
        for (int k = 0; k < nb_; ++k) {
            const float mag = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]) / kN;
            const float db = 20.0f * std::log10(mag + 1e-12f);
            bytes_[static_cast<size_t>(k)] = static_cast<char>(static_cast<unsigned char>(std::clamp(static_cast<int>(std::lround((db + 127.5f) * 2)), 0, 255)));
        }
        char b[48]; std::snprintf(b, sizeof b, "%.2f ", 10.0 * std::log10(ms + 1e-12));
        out_ = b; out_ += web::base64(bytes_);
    }
    float sr_ = 48000; int hop_ = 800, since_ = 0, pos_ = 0, nb_ = 700;
    float hist_[kN] = {}, win_[kN] = {}, re_[kN] = {}, im_[kN] = {}, cs_[kN / 2] = {}, sn_[kN / 2] = {}; int rev_[kN] = {};
    std::string bytes_, out_;
};

}  // namespace vis

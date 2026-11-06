// plaits_glue.cpp — se compila aparte (Plaits/stmlib definen macros como CONSTRAIN y ONE_POLE).
#include "plaits_glue.h"
#include <cmath>
#include <mutex>
#include "plaits/resources.h"
#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/drums/synthetic_bass_drum.h"
#include "plaits/dsp/drums/analog_snare_drum.h"
#include "plaits/dsp/drums/synthetic_snare_drum.h"
#include "plaits/dsp/drums/hi_hat.h"

namespace plaits {
float lut_sine[641];
void InitSineLut() { for (int i = 0; i < 641; ++i) lut_sine[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * i / 512.0)); }
}

namespace pd {
struct Drum::Impl {
    plaits::AnalogBassDrum bd8; plaits::SyntheticBassDrum bd9;
    plaits::AnalogSnareDrum sd8; plaits::SyntheticSnareDrum sd9;
    plaits::HiHat<plaits::SquareNoise, plaits::SwingVCA, true, false> hh8;
    plaits::HiHat<plaits::RingModNoise, plaits::LinearVCA, false, true> hh9;
    float t1[512], t2[512];
};
Drum::Drum() : p_(new Impl) {
    static std::once_flag f; std::call_once(f, plaits::InitSineLut);
    p_->bd8.Init(); p_->bd9.Init(); p_->sd8.Init(); p_->sd9.Init(); p_->hh8.Init(); p_->hh9.Init();
}
Drum::~Drum() { delete p_; }
void Drum::setModel(int m) { model_ = m; }
void Drum::render(float* out, int n, float sr, float f0hz, float tone, float decay, float a, float b) {
    while (n > 0) {
        const int k = n > 256 ? 256 : n;
        const float f0 = f0hz / sr;
        const bool t = trig_; trig_ = false;
        switch (model_) {
            case BD808: p_->bd8.Render(false, t, accent_, f0, tone, decay, a, b, out, k); break;
            case BD909: p_->bd9.Render(false, t, accent_, f0, tone, decay, 0.15f + 0.85f * a, b, 0.2f + 0.6f * decay, out, k); break;
            case SD808: p_->sd8.Render(false, t, accent_, f0, tone, decay, a, out, k); break;
            case SD909: p_->sd9.Render(false, t, accent_, f0, tone, decay, a, out, k); break;
            case HH808: p_->hh8.Render(false, t, accent_, f0, tone, decay, a, p_->t1, p_->t2, out, k); break;
            default:    p_->hh9.Render(false, t, accent_, f0, tone, decay, a, p_->t1, p_->t2, out, k); break;
        }
        out += k; n -= k;
    }
}
}

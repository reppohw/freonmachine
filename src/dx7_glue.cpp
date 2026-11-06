// dx7_glue.cpp — puente con MSFA (ver dx7_glue.h).
#include "dx7_glue.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>
#include "../third_party/msfa/synth.h"
#include "../third_party/msfa/controllers.h"
#include "../third_party/msfa/dx7note.h"
#include "../third_party/msfa/freqlut.h"
#include "../third_party/msfa/exp2.h"
#include "../third_party/msfa/sin.h"
#include "../third_party/msfa/lfo.h"
#include "../third_party/msfa/pitchenv.h"
#include "../third_party/msfa/patch.h"

namespace dx7 {
using Patch = std::array<uint8_t, 156>;
namespace {
std::vector<Patch> gLib; std::vector<std::string> gNames; bool gBuiltins = false;

// Operadores en orden natural (OP1..OP6); se guardan al revés, como en el DX7 (OP6 primero)
struct Op { int coarse; int fine; int det; int ol; int r[4]; int l[4]; int kvs; int rs; };
Patch make(const char* name, int algo, int fb, const Op ops[6], int lfoSpd = 35, int pmd = 0, int pms = 3) {
    Patch p{};
    for (int n = 0; n < 6; ++n) {
        const Op& o = ops[n]; const int off = (5 - n) * 21;
        for (int i = 0; i < 4; ++i) { p[off + i] = o.r[i]; p[off + 4 + i] = o.l[i]; }
        p[off + 8] = 39; p[off + 9] = 0; p[off + 10] = 0; p[off + 11] = 0; p[off + 12] = 0;   // sin escalado de teclado
        p[off + 13] = o.rs; p[off + 14] = 0; p[off + 15] = o.kvs; p[off + 16] = o.ol; p[off + 17] = 0;
        p[off + 18] = o.coarse; p[off + 19] = o.fine; p[off + 20] = o.det;
    }
    for (int i = 0; i < 4; ++i) { p[126 + i] = 99; p[130 + i] = 50; }   // envolvente de tono plana
    p[134] = algo - 1; p[135] = fb; p[136] = 1;
    p[137] = lfoSpd; p[138] = 0; p[139] = pmd; p[140] = 0; p[141] = 0; p[142] = 4; p[143] = pms; p[144] = 24;
    for (int i = 0; i < 10; ++i) p[145 + i] = name[i] ? name[i] : ' ';
    return p;
}
#define OFF {1, 0, 7, 0, {99, 99, 99, 99}, {0, 0, 0, 0}, 0, 0}
void addBuiltins() {
    // Sonidos de serie hechos para Freon (recetas clásicas de FM). Los bancos del DX7 se añaden con archivos .syx.
    const Op ep[6] = {{1, 0, 7, 99, {96, 25, 25, 67}, {99, 75, 0, 0}, 2, 3}, {14, 0, 7, 58, {95, 50, 35, 78}, {99, 75, 0, 0}, 7, 3},
                      {1, 0, 6, 99, {95, 20, 20, 50}, {99, 95, 0, 0}, 2, 3}, {1, 0, 8, 89, {95, 29, 20, 50}, {99, 95, 0, 0}, 6, 3},
                      {1, 0, 8, 99, {95, 20, 20, 50}, {99, 95, 0, 0}, 2, 3}, {1, 0, 6, 79, {95, 29, 20, 50}, {99, 95, 0, 0}, 6, 3}};
    const Op bass[6] = {{0, 0, 7, 99, {99, 50, 30, 60}, {99, 80, 0, 0}, 3, 2}, {0, 0, 7, 82, {99, 62, 40, 70}, {99, 50, 0, 0}, 6, 2},
                        {0, 0, 9, 90, {99, 50, 30, 60}, {99, 80, 0, 0}, 3, 2}, {1, 0, 5, 72, {99, 70, 40, 70}, {99, 40, 0, 0}, 6, 2}, OFF, OFF};
    const Op bell[6] = {{1, 0, 7, 99, {99, 22, 20, 30}, {99, 0, 0, 0}, 2, 2}, {3, 50, 7, 76, {99, 28, 20, 35}, {99, 0, 0, 0}, 4, 2},
                        {1, 0, 9, 95, {99, 18, 20, 30}, {99, 0, 0, 0}, 2, 2}, {3, 51, 5, 72, {99, 30, 20, 35}, {99, 0, 0, 0}, 4, 2},
                        {0, 0, 7, 85, {99, 15, 20, 30}, {99, 0, 0, 0}, 2, 2}, {7, 0, 7, 55, {99, 40, 20, 40}, {99, 0, 0, 0}, 3, 2}};
    const Op brass[6] = {{1, 0, 7, 99, {72, 76, 99, 71}, {99, 88, 96, 0}, 2, 2}, {1, 0, 7, 86, {62, 51, 29, 71}, {82, 95, 96, 0}, 4, 2},
                         {1, 0, 9, 97, {72, 76, 99, 71}, {99, 88, 96, 0}, 2, 2}, {1, 0, 5, 82, {60, 50, 29, 71}, {82, 95, 96, 0}, 4, 2}, OFF, OFF};
    const Op mar[6] = {{1, 0, 7, 99, {99, 48, 30, 60}, {99, 0, 0, 0}, 3, 3}, {4, 0, 7, 70, {99, 75, 40, 60}, {99, 0, 0, 0}, 5, 3},
                       {1, 0, 8, 90, {99, 42, 30, 60}, {99, 0, 0, 0}, 3, 3}, {10, 0, 6, 58, {99, 80, 40, 60}, {99, 0, 0, 0}, 5, 3}, OFF, OFF};
    const Op lead[6] = {{1, 0, 7, 99, {90, 40, 30, 60}, {99, 92, 90, 0}, 2, 2}, {1, 0, 7, 84, {90, 30, 30, 60}, {99, 90, 85, 0}, 3, 2},
                        {2, 0, 8, 80, {90, 30, 30, 60}, {99, 90, 80, 0}, 3, 2}, OFF, OFF, OFF};
    const Op pad[6] = {{1, 0, 7, 99, {45, 30, 20, 40}, {99, 92, 90, 0}, 1, 1}, {1, 0, 9, 62, {40, 25, 20, 40}, {99, 85, 80, 0}, 2, 1},
                       {1, 0, 4, 99, {45, 30, 20, 40}, {99, 92, 90, 0}, 1, 1}, {2, 0, 10, 55, {40, 25, 20, 40}, {99, 85, 80, 0}, 2, 1},
                       {1, 0, 7, 95, {45, 30, 20, 40}, {99, 92, 90, 0}, 1, 1}, {1, 1, 3, 60, {40, 25, 20, 40}, {99, 85, 80, 0}, 2, 1}};
    const Op init[6] = {{1, 0, 7, 99, {99, 99, 99, 70}, {99, 99, 99, 0}, 0, 0}, OFF, OFF, OFF, OFF, OFF};
    gLib.push_back(make("E.PIANO   ", 5, 6, ep)); gLib.push_back(make("FM BASS   ", 1, 5, bass, 35, 0, 0)); gLib.push_back(make("BELLS     ", 5, 3, bell));
    gLib.push_back(make("BRASS     ", 1, 7, brass, 34, 6, 3)); gLib.push_back(make("MARIMBA   ", 1, 0, mar)); gLib.push_back(make("LEAD      ", 7, 6, lead, 38, 8, 3));
    gLib.push_back(make("STRING PAD", 5, 2, pad, 30, 4, 2)); gLib.push_back(make("INIT VOICE", 32, 0, init));
}
#undef OFF
std::string nameOf(const Patch& p) {
    std::string s; for (int i = 145; i < 155; ++i) { const char c = static_cast<char>(p[i]); s += (c >= 32 && c < 127) ? c : ' '; }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s.empty() ? "(sin nombre)" : s;
}
void loadSyx(const std::filesystem::path& f) {
    FILE* fp = std::fopen(f.string().c_str(), "rb"); if (!fp) return;
    std::vector<uint8_t> d(8192); const size_t n = std::fread(d.data(), 1, d.size(), fp); std::fclose(fp); d.resize(n);
    const uint8_t* bulk = nullptr;
    if (n >= 4104 && d[0] == 0xF0 && d[1] == 0x43 && d[3] == 0x09) bulk = d.data() + 6;      // banco de 32
    else if (n == 4096) bulk = d.data();                                                     // banco sin cabecera
    else if (n >= 163 && d[0] == 0xF0 && d[1] == 0x43 && d[3] == 0x00) {                     // una sola voz
        Patch p{}; for (int i = 0; i < 155; ++i) p[i] = d[6 + i] & 0x7F; gLib.push_back(p); gNames.push_back(nameOf(p)); return;
    }
    if (!bulk) { std::printf("DX7: %s no es un banco reconocible\n", f.filename().string().c_str()); return; }
    for (int v = 0; v < 32; ++v) {
        char unpacked[156]; UnpackPatch(reinterpret_cast<const char*>(bulk + v * 128), unpacked);
        Patch p{}; for (int i = 0; i < 156; ++i) p[i] = static_cast<uint8_t>(unpacked[i]) & 0x7F;
        gLib.push_back(p); gNames.push_back(nameOf(p));
    }
}
// operadores portadores (salen al audio) de cada algoritmo, en el orden del patch (OP6..OP1)
bool isCarrier(int algo, int i) {
    static const uint8_t A[32][6] = {
        {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}, {0x41, 0x11, 0x94, 0x01, 0x11, 0x14},
        {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0x41, 0x94, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14},
        {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
        {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14},
        {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14},
        {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}, {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
        {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14},
        {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04}, {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}};
    return (A[std::clamp(algo, 0, 31)][i] & 7) == 4;
}
}  // namespace

void libInit(const std::string& folder) {
    if (!gBuiltins) { addBuiltins(); for (auto& p : gLib) gNames.push_back(nameOf(p)); gBuiltins = true; }
    if (folder.empty()) return;
    std::error_code ec; std::vector<std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(folder, ec)) { auto x = e.path().extension().string(); for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); if (x == ".syx") files.push_back(e.path()); }
    std::sort(files.begin(), files.end());
    for (auto& f : files) { const size_t before = gLib.size(); loadSyx(f); if (gLib.size() > before) std::printf("DX7: %s (%zu sonidos)\n", f.filename().string().c_str(), gLib.size() - before); }
}
int libCount() { return static_cast<int>(gLib.size()); }
std::string libName(int i) { return i >= 0 && i < static_cast<int>(gNames.size()) ? gNames[static_cast<size_t>(i)] : std::string(); }

struct Synth::Impl {
    static constexpr int kVoices = 16;
    Dx7Note note[kVoices]; int key[kVoices]; bool live[kVoices] = {}, down[kVoices] = {}; unsigned age[kVoices] = {}; unsigned clock = 0;
    Patch patch{}; Lfo lfo; Controllers ctrls;
    int32_t buf[N]; int bufPos = N;
    Macros lastM; bool lfoDirty = true;
};
Synth::Synth() : p_(new Impl) { for (int& k : p_->key) k = -1; std::memset(p_->ctrls.values_, 0, sizeof p_->ctrls.values_); p_->ctrls.values_[kControllerPitch] = 0x2000; }
Synth::~Synth() { delete p_; }
void Synth::prepare(double sr) {
    static double initSr = 0; static std::mutex mx;
    { std::lock_guard<std::mutex> lk(mx); if (initSr != sr) { Freqlut::init(sr); Exp2::init(); Sin::init(); Lfo::init(sr); PitchEnv::init(sr); initSr = sr; } }
    if (!gBuiltins) libInit("");
    select(0); allOff();
}
void Synth::select(int i) {
    if (gLib.empty()) return;
    p_->patch = gLib[static_cast<size_t>(std::clamp(i, 0, libCount() - 1))];
    p_->lfoDirty = true;
}
void Synth::noteOn(int k, int vel, const Macros& m) {
    auto& s = *p_;
    // patch con los knobs aplicados (cambian las notas nuevas, como al editar un DX7)
    Patch p = s.patch;
    if (m.algo > 0) p[134] = static_cast<uint8_t>(std::clamp(m.algo, 1, 32) - 1);
    const int algo = p[134];
    auto adj = [](uint8_t& v, float d, int lo, int hi) { v = static_cast<uint8_t>(std::clamp(static_cast<int>(v) + static_cast<int>(std::lround(d)), lo, hi)); };
    for (int i = 0; i < 6; ++i) {
        const int off = i * 21; const bool car = isCarrier(algo, i);
        adj(p[off + 0], -(m.attack - 0.5f) * 120.0f, 0, 99);
        adj(p[off + 1], -(m.decay - 0.5f) * 100.0f, 0, 99); adj(p[off + 2], -(m.decay - 0.5f) * 100.0f, 0, 99);
        adj(p[off + 3], -(m.release - 0.5f) * 120.0f, 0, 99);
        if (car) adj(p[off + 6], (m.sustain - 0.5f) * 100.0f, 0, 99);
        else { adj(p[off + 16], (m.bright - 0.5f) * 50.0f, 0, 99); adj(p[off + 1], -(m.modDecay - 0.5f) * 100.0f, 0, 99); adj(p[off + 2], -(m.modDecay - 0.5f) * 100.0f, 0, 99); }
        adj(p[off + 15], (m.velocity - 0.5f) * 14.0f, 0, 7);
        static const float spread[6] = {1.0f, -1.0f, 0.5f, -0.5f, 0.75f, -0.75f};
        adj(p[off + 20], (m.detune - 0.5f) * 14.0f * spread[i], 0, 14);
    }
    adj(p[135], (m.feedback - 0.5f) * 14.0f, 0, 7);
    adj(p[139], (m.vibrato - 0.5f) * 198.0f, 0, 99); adj(p[143], (m.vibrato - 0.5f) * 14.0f, 0, 7);
    adj(p[137], (m.vibRate - 0.5f) * 198.0f, 0, 99);
    if (s.lfoDirty || m.vibRate != s.lastM.vibRate || m.vibrato != s.lastM.vibrato) { s.lfo.reset(reinterpret_cast<const char*>(&p[137])); s.lfoDirty = false; }
    s.lastM = m;
    // voz: una libre, o la más antigua
    int v = -1; for (int i = 0; i < Impl::kVoices; ++i) if (!s.live[i]) { v = i; break; }
    if (v < 0) { unsigned best = ~0u; for (int i = 0; i < Impl::kVoices; ++i) if (!s.down[i] && s.age[i] < best) { best = s.age[i]; v = i; } }
    if (v < 0) { unsigned best = ~0u; for (int i = 0; i < Impl::kVoices; ++i) if (s.age[i] < best) { best = s.age[i]; v = i; } }
    const int midinote = std::clamp(k + m.transpose + (static_cast<int>(p[144]) - 24), 0, 127);
    s.note[v].init(reinterpret_cast<const char*>(p.data()), midinote, std::clamp(vel, 1, 127));
    if (p[141]) s.lfo.keydown();
    s.key[v] = k; s.live[v] = true; s.down[v] = true; s.age[v] = ++s.clock;
}
void Synth::noteOff(int k) { for (int i = 0; i < Impl::kVoices; ++i) if (p_->live[i] && p_->down[i] && p_->key[i] == k) { p_->note[i].keyup(); p_->down[i] = false; } }
void Synth::allOff() { for (int i = 0; i < Impl::kVoices; ++i) if (p_->live[i] && p_->down[i]) { p_->note[i].keyup(); p_->down[i] = false; } }
void Synth::render(float* out, int n) {
    auto& s = *p_;
    for (int i = 0; i < n; ++i) {
        if (s.bufPos >= N) {
            std::memset(s.buf, 0, sizeof s.buf);
            const int32_t lv = s.lfo.getsample(), ld = s.lfo.getdelay();
            for (int v = 0; v < Impl::kVoices; ++v) {
                if (!s.live[v]) continue;
                s.note[v].compute(s.buf, lv, ld, &s.ctrls);
                if (!s.down[v] && !s.note[v].isPlaying()) s.live[v] = false;   // release acabado: voz libre
            }
            s.bufPos = 0;
        }
        out[i] = static_cast<float>(s.buf[s.bufPos++]) * (1.0f / 268435456.0f);   // como el DX7: 16 bits a partir del bus de 32
    }
}
}  // namespace dx7

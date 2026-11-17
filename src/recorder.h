// recorder.h — grabación de lives a disco (fase 1: el estéreo del máster). Ver GRABACION.md.
//
// Regla de oro: el hilo de audio nunca toca el disco ni pide memoria. Solo copia cada bloque a un anillo (DiskRing, ~40 s).
// Un hilo aparte (Recorder) lo vacía: guarda siempre los últimos 15 min en RAM («Guardar lo último») y, mientras se graba,
// escribe un WAV de 24 bits (BWF, con la hora de inicio para colocarlo en Pro Tools). Si el disco se atasca más de lo que
// cabe en el anillo, el audio en directo sigue: se meten ceros del mismo largo (la toma no se desplaza) y se marca el hueco.
// La cabecera se pone al día cada 2 s y los datos se fuerzan al disco cada 5 s: un corte de luz pierde como mucho eso, y
// al arrancar repairAll() deja bien las tomas que quedaron abiertas.
#pragma once
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace rec {

// anillo de un productor (audio) y un consumidor (Recorder), sin bloqueos; tamaño en tramas, potencia de 2
struct DiskRing {
    std::vector<float> buf; size_t mask = 0; std::atomic<uint64_t> w{0}, r{0}; std::atomic<uint64_t> dropped{0};
    void init(size_t framesPow2) { buf.assign(framesPow2 * 2, 0.0f); mask = framesPow2 - 1; }
    // hilo de audio
    void push(const float* L, const float* R, int n) {
        if (buf.empty()) return;
        const uint64_t wr = w.load(std::memory_order_relaxed), rd = r.load(std::memory_order_acquire);
        if (wr - rd + static_cast<uint64_t>(n) > mask + 1) { dropped.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed); return; }
        for (int i = 0; i < n; ++i) { const size_t j = static_cast<size_t>((wr + static_cast<uint64_t>(i)) & mask) * 2; buf[j] = L[i]; buf[j + 1] = R[i]; }
        w.store(wr + static_cast<uint64_t>(n), std::memory_order_release);
    }
};

inline void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }
inline uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

// WAV de 24 bits estéreo con chunk bext (BWF). Cabecera fija: los datos empiezan en el byte kData
constexpr long kBext = 602, kData = 12 + 24 + 8 + kBext + 8;   // = 654
inline void wavHeader(uint8_t* h, uint32_t sr, uint64_t dataBytes, uint64_t timeRef, const std::tm& t) {
    std::memset(h, 0, kData);
    std::memcpy(h, "RIFF", 4); put32(h + 4, static_cast<uint32_t>(std::min<uint64_t>(kData - 8 + dataBytes, 0xFFFFFFFFu))); std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4); put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, 2); put32(h + 24, sr); put32(h + 28, sr * 6); put16(h + 32, 6); put16(h + 34, 24);
    uint8_t* b = h + 36; std::memcpy(b, "bext", 4); put32(b + 4, kBext); b += 8;
    std::snprintf(reinterpret_cast<char*>(b), 256, "Freon Machine live"); std::snprintf(reinterpret_cast<char*>(b + 256), 32, "Freon Machine");
    char d[16]; std::strftime(d, sizeof d, "%Y-%m-%d", &t); std::memcpy(b + 320, d, 10); std::strftime(d, sizeof d, "%H:%M:%S", &t); std::memcpy(b + 330, d, 8);
    put32(b + 338, static_cast<uint32_t>(timeRef & 0xFFFFFFFFu)); put32(b + 342, static_cast<uint32_t>(timeRef >> 32)); put16(b + 346, 1);
    std::memcpy(h + kData - 8, "data", 4); put32(h + kData - 4, static_cast<uint32_t>(std::min<uint64_t>(dataBytes, 0xFFFFFFFFu)));
}
inline void to24(const float* x, uint8_t* o, size_t nSamples) {
    for (size_t i = 0; i < nSamples; ++i) {
        const float v = std::isfinite(x[i]) ? std::fmax(-1.0f, std::fmin(1.0f, x[i])) : 0.0f;
        const int32_t s = static_cast<int32_t>(std::lrint(v * 8388607.0f));
        o[3 * i] = static_cast<uint8_t>(s); o[3 * i + 1] = static_cast<uint8_t>(s >> 8); o[3 * i + 2] = static_cast<uint8_t>(s >> 16);
    }
}

// Una toma que quedó abierta (corte de luz, cuelgue): su cabecera dice menos datos de los que hay. Se ajusta al archivo.
inline bool repairWav(const std::string& f) {
    std::error_code ec; const uint64_t sz = std::filesystem::file_size(f, ec); if (ec || sz < static_cast<uint64_t>(kData)) return false;
    FILE* fp = std::fopen(f.c_str(), "r+b"); if (!fp) return false;
    uint8_t h[kData]; bool fixed = false;
    if (std::fread(h, 1, kData, fp) == static_cast<size_t>(kData) && !std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 36, "bext", 4) && !std::memcmp(h + kData - 8, "data", 4)) {
        const uint64_t riff = get32(h + 4);
        if (riff + 8 < sz) {   // cerrada bien: RIFF + 8 = tamaño del archivo
            const uint64_t data = (sz - kData) / 6 * 6;
            put32(h + 4, static_cast<uint32_t>(std::min<uint64_t>(kData - 8 + data, 0xFFFFFFFFu))); put32(h + kData - 4, static_cast<uint32_t>(std::min<uint64_t>(data, 0xFFFFFFFFu)));
            std::fseek(fp, 0, SEEK_SET); fixed = std::fwrite(h, 1, kData, fp) == static_cast<size_t>(kData);
            std::fflush(fp); if (fixed) { if (::ftruncate(fileno(fp), static_cast<off_t>(kData + data)) != 0) {} }
        }
    }
    std::fclose(fp); return fixed;
}

class Recorder {
public:
    // estado que lee el hilo de control (para la web y las luces)
    std::atomic<bool> recording{false}, savingLast{false};
    std::atomic<uint64_t> frames{0}, gaps{0};   // tramas de la toma en curso, huecos (ceros metidos por un disco lento)
    std::atomic<int> lastMinutes{0};            // minutos que hay ahora en «lo último» (llega a kLastMin)
    std::function<void(const std::string&)> onEvent;   // avisos para la web (el hilo de control los reparte): «saved …», «error …»
    static constexpr int kLastMin = 15;

    void start(DiskRing* ring, uint32_t sr, std::string dir) {
        ring_ = ring; sr_ = sr; dir_ = std::move(dir);
        th_ = std::thread([this] { run(); });
    }
    ~Recorder() { { std::lock_guard<std::mutex> lk(mx_); quit_ = true; } cv_.notify_all(); if (th_.joinable()) th_.join(); }
    const std::string& dir() const { return dir_; }
    std::string takeName() { std::lock_guard<std::mutex> lk(mx_); return take_; }

    // órdenes (hilo de control)
    void cmdStart() { push({C_START, ""}); }
    void cmdStop() { push({C_STOP, ""}); }
    void cmdMark(const std::string& label) { push({C_MARK, label}); }
    void cmdSaveLast() { push({C_LAST, ""}); }

    // tomas que quedaron abiertas al apagarse: se reparan al arrancar
    static int repairAll(const std::string& dir) {
        int n = 0; std::error_code ec;
        for (const auto& t : std::filesystem::directory_iterator(dir, ec)) {
            if (!t.is_directory()) continue;
            for (const auto& f : std::filesystem::directory_iterator(t.path(), ec)) if (f.path().extension() == ".wav" && repairWav(f.path().string())) ++n;
        }
        return n;
    }

private:
    enum CK { C_START, C_STOP, C_MARK, C_LAST };
    struct Cmd { CK k; std::string s; };
    void push(Cmd c) { { std::lock_guard<std::mutex> lk(mx_); q_.push_back(std::move(c)); } cv_.notify_all(); }

    DiskRing* ring_ = nullptr; uint32_t sr_ = 48000; std::string dir_, take_, takeDir_;
    std::thread th_; std::mutex mx_; std::condition_variable cv_; std::deque<Cmd> q_; bool quit_ = false;
    // «lo último»: los últimos 15 min ya en 24 bits (6 bytes por trama), en anillo
    std::vector<uint8_t> last_; uint64_t lastW_ = 0;
    // toma en curso
    FILE* fp_ = nullptr; uint64_t fileBytes_ = 0; int part_ = 1; std::tm t0_{}; uint64_t timeRef_ = 0, partStart_ = 0;
    std::chrono::steady_clock::time_point hdrAt_{}, syncAt_{};
    std::vector<float> tmpF_; std::vector<uint8_t> tmpB_;
    static constexpr uint64_t kSplit = 3500ull * 1024 * 1024;   // por debajo de los 4 GB de un WAV normal

    static std::string stamp(const std::tm& t, const char* fmt) { char b[64]; std::strftime(b, sizeof b, fmt, &t); return b; }
    std::string hms(uint64_t fr) const {
        const double s = static_cast<double>(fr) / sr_; char b[32]; const int h = static_cast<int>(s / 3600), m = static_cast<int>(s / 60) % 60;
        std::snprintf(b, sizeof b, "%d:%02d:%06.3f", h, m, std::fmod(s, 60.0)); return b;
    }
    void note(const std::string& s) { if (onEvent) onEvent(s); std::printf("Grabación: %s\n", s.c_str()); std::fflush(stdout); }

    void run() {
        last_.assign(static_cast<size_t>(sr_) * 60 * kLastMin * 6, 0);   // ~260 MB a 48 kHz; se pide aquí, no en el audio
        tmpF_.resize(8192 * 2); tmpB_.resize(8192 * 6);
        for (;;) {
            std::deque<Cmd> cmds;
            { std::unique_lock<std::mutex> lk(mx_); cv_.wait_for(lk, std::chrono::milliseconds(20), [this] { return quit_ || !q_.empty(); });
              if (quit_) break; cmds.swap(q_); }
            drain();   // primero lo que ya sonó: una orden se aplica en este punto de la toma
            for (auto& c : cmds) handle(c);
            if (fp_) keepSafe(false);
        }
        drain(); if (fp_) closeTake();
    }
    void handle(const Cmd& c) {
        switch (c.k) {
        case C_START: if (!fp_) openTake(); break;
        case C_STOP: if (fp_) closeTake(); break;
        case C_MARK: if (fp_) mark(c.s.empty() ? "Mark" : c.s); break;
        case C_LAST: saveLast(); break;
        }
    }
    // lo que haya en el anillo: a «lo último» y, si se graba, al archivo
    void drain() {
        const uint64_t gap = ring_->dropped.exchange(0);
        if (gap) {   // el disco no llegó: ceros del mismo largo, para que la toma no se desplace
            std::fill(tmpB_.begin(), tmpB_.end(), 0);
            for (uint64_t left = gap; left > 0;) { const size_t n = static_cast<size_t>(std::min<uint64_t>(left, 8192)); write(tmpB_.data(), n); left -= n; }
            if (fp_) { gaps.fetch_add(1); mark("Gap (disk too slow)"); }
        }
        for (;;) {
            const uint64_t rd = ring_->r.load(std::memory_order_relaxed), wr = ring_->w.load(std::memory_order_acquire);
            if (wr == rd) break;
            const size_t n = static_cast<size_t>(std::min<uint64_t>(wr - rd, 8192));
            for (size_t i = 0; i < n; ++i) { const size_t j = static_cast<size_t>((rd + i) & ring_->mask) * 2; tmpF_[2 * i] = ring_->buf[j]; tmpF_[2 * i + 1] = ring_->buf[j + 1]; }
            ring_->r.store(rd + n, std::memory_order_release);
            to24(tmpF_.data(), tmpB_.data(), n * 2);
            write(tmpB_.data(), n);
        }
    }
    void write(const uint8_t* b, size_t n) {
        const size_t cap = last_.size() / 6;   // «lo último»
        for (size_t i = 0; i < n;) { const size_t pos = static_cast<size_t>(lastW_ % cap), m = std::min(n - i, cap - pos); std::memcpy(&last_[pos * 6], b + i * 6, m * 6); i += m; lastW_ += m; }
        lastMinutes = static_cast<int>(std::min<uint64_t>(lastW_, cap) / (60ull * sr_));
        if (!fp_) return;
        if (fileBytes_ + n * 6 > kSplit) { finishPart(); ++part_; partStart_ = frames.load(); openPart(); if (!fp_) return; }
        if (std::fwrite(b, 1, n * 6, fp_) != n * 6) { note("error writing (disk full?)"); closeTake(); return; }
        fileBytes_ += n * 6; frames.fetch_add(n);
    }
    void openTake() {
        const std::time_t now = std::time(nullptr); localtime_r(&now, &t0_);
        timeRef_ = static_cast<uint64_t>(t0_.tm_hour * 3600 + t0_.tm_min * 60 + t0_.tm_sec) * sr_;   // muestras desde medianoche (BWF)
        { std::lock_guard<std::mutex> lk(mx_); take_ = stamp(t0_, "%Y-%m-%d %H-%M-%S"); }
        takeDir_ = dir_ + "/" + take_; std::error_code ec; std::filesystem::create_directories(takeDir_, ec);
        part_ = 1; partStart_ = 0; frames = 0; gaps = 0; openPart();
        if (fp_) { recording = true; note("rec " + take_); }
    }
    std::string partFile() const { return takeDir_ + (part_ == 1 ? "/master.wav" : "/master " + std::to_string(part_) + ".wav"); }
    void openPart() {
        fp_ = std::fopen(partFile().c_str(), "wb"); fileBytes_ = 0;
        if (!fp_) { note("error: can't create " + partFile()); recording = false; return; }
        uint8_t h[kData]; wavHeader(h, sr_, 0, timeRef_ + partStart_, t0_);
        if (std::fwrite(h, 1, kData, fp_) != static_cast<size_t>(kData)) { std::fclose(fp_); fp_ = nullptr; note("error writing the header"); recording = false; return; }
        hdrAt_ = syncAt_ = std::chrono::steady_clock::now();
    }
    // cabecera al día cada 2 s y datos forzados al disco cada 5 s
    void keepSafe(bool force) {
        const auto now = std::chrono::steady_clock::now();
        if (force || now - hdrAt_ > std::chrono::seconds(2)) {
            hdrAt_ = now; uint8_t h[kData]; wavHeader(h, sr_, fileBytes_, timeRef_ + partStart_, t0_);
            std::fflush(fp_); std::fseek(fp_, 0, SEEK_SET); if (std::fwrite(h, 1, kData, fp_) != static_cast<size_t>(kData)) {} std::fseek(fp_, 0, SEEK_END);
        }
        if (force || now - syncAt_ > std::chrono::seconds(5)) { syncAt_ = now; std::fflush(fp_); ::fsync(fileno(fp_)); }
    }
    void finishPart() { if (!fp_) return; keepSafe(true); std::fclose(fp_); fp_ = nullptr; }
    void closeTake() {
        finishPart(); recording = false;
        note("saved " + take_ + " · " + hms(frames.load()) + (gaps.load() ? " · " + std::to_string(gaps.load()) + " gaps" : ""));
    }
    // marcas: en marcas.txt, al momento (si se va la luz, se conservan)
    void mark(const std::string& label) {
        const std::string f = takeDir_ + "/marcas.txt"; FILE* m = std::fopen(f.c_str(), "ab"); if (!m) return;
        std::fprintf(m, "%s\t%s\n", hms(frames.load()).c_str(), label.c_str()); std::fclose(m);
        note("mark " + hms(frames.load()) + " " + label);
    }
    // «Guardar lo último»: copia en memoria (rápido) y lo escribe otro hilo, para que este siga vaciando el anillo
    void saveLast() {
        if (savingLast.exchange(true)) return;
        const size_t cap = last_.size() / 6, have = static_cast<size_t>(std::min<uint64_t>(lastW_, cap));
        if (have < sr_) { savingLast = false; note("error nothing to save yet"); return; }
        auto data = std::make_shared<std::vector<uint8_t>>(have * 6);
        const size_t start = static_cast<size_t>((lastW_ - have) % cap);
        const size_t first = std::min(have, cap - start);
        std::memcpy(data->data(), &last_[start * 6], first * 6); if (have > first) std::memcpy(data->data() + first * 6, &last_[0], (have - first) * 6);
        const std::time_t now = std::time(nullptr) - static_cast<std::time_t>(have / sr_); std::tm t{}; localtime_r(&now, &t);
        const int mins = static_cast<int>((have + sr_ * 30) / (60ull * sr_));
        const std::string name = stamp(t, "%Y-%m-%d %H-%M-%S") + " (last " + std::to_string(std::max(1, mins)) + " min)", d = dir_ + "/" + name;
        const uint64_t tref = static_cast<uint64_t>(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec) * sr_; const uint32_t sr = sr_;
        std::thread([this, data, d, name, tref, t, sr] {
            std::error_code ec; std::filesystem::create_directories(d, ec);
            const std::string f = d + "/master.wav"; FILE* fp = std::fopen(f.c_str(), "wb"); bool ok = fp != nullptr;
            if (fp) {
                uint8_t h[kData]; wavHeader(h, sr, data->size(), tref, t);
                ok = std::fwrite(h, 1, kData, fp) == static_cast<size_t>(kData) && std::fwrite(data->data(), 1, data->size(), fp) == data->size();
                ok = std::fflush(fp) == 0 && ok; ::fsync(fileno(fp)); ok = std::fclose(fp) == 0 && ok;
            }
            savingLast = false; note(ok ? "saved " + name : "error saving " + name);
        }).detach();
    }
};

}  // namespace rec

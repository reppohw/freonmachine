// recorder.h — grabación de lives a disco. Ver GRABACION.md.
//
// Regla de oro: el hilo de audio nunca toca el disco ni pide memoria. Solo copia cada bloque a dos anillos (DiskRing):
//   · máster (2 canales, ~44 s): siempre; de ahí salen master.wav y «Guardar lo último» (los últimos 15 min en RAM);
//   · pistas (entradas, canales después del fader y retornos de efectos, ~11 s): siempre, pero solo se escriben si la toma
//     lleva multipista (las entradas, siempre: hacen falta para regenerar el live).
// y apunta en un tercer anillo (EvRing) cada orden y cada nota que aplica, con su muestra exacta: live.freon.
// Los tres comparten el contador de muestras del motor, así que todo queda alineado a la muestra.
// Un hilo aparte (Recorder) los vacía. Si el disco se atasca más de lo que cabe, el audio en directo sigue: se meten ceros
// del mismo largo (la toma no se desplaza) y se marca el hueco. Cabeceras al día cada 2 s y datos al disco cada 5 s: un
// corte de luz pierde como mucho eso, y al arrancar repairAll() deja bien las tomas que quedaron abiertas.
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

// anillo de un productor (audio) y un consumidor (Recorder), sin bloqueos; tamaño en tramas (potencia de 2) × canales
struct DiskRing {
    std::vector<float> buf; size_t mask = 0; int ch = 2; std::atomic<uint64_t> w{0}, r{0}; std::atomic<uint64_t> dropped{0};
    void init(size_t framesPow2, int channels) { ch = channels; buf.assign(framesPow2 * static_cast<size_t>(ch), 0.0f); mask = framesPow2 - 1; }
    // hilo de audio: un puntero por canal (nullptr = silencio)
    void push(const float* const* chans, int n) {
        if (buf.empty()) return;
        const uint64_t wr = w.load(std::memory_order_relaxed), rd = r.load(std::memory_order_acquire);
        if (wr - rd + static_cast<uint64_t>(n) > mask + 1) { dropped.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed); return; }
        for (int i = 0; i < n; ++i) {
            float* o = &buf[static_cast<size_t>((wr + static_cast<uint64_t>(i)) & mask) * static_cast<size_t>(ch)];
            for (int c = 0; c < ch; ++c) o[c] = chans[c] ? chans[c][i] : 0.0f;
        }
        w.store(wr + static_cast<uint64_t>(n), std::memory_order_release);
    }
    // para la reproducción (al revés: escribe un hilo de lectura, lee el audio)
    size_t space() const { return static_cast<size_t>(mask + 1 - (w.load(std::memory_order_relaxed) - r.load(std::memory_order_acquire))); }
    size_t avail() const { return static_cast<size_t>(w.load(std::memory_order_acquire) - r.load(std::memory_order_relaxed)); }
    size_t pull(float* const* dst, int n) {   // hilo de audio: copia hasta n tramas, un puntero por canal
        if (buf.empty()) return 0;
        const uint64_t rd = r.load(std::memory_order_relaxed), wr = w.load(std::memory_order_acquire);
        const size_t m = static_cast<size_t>(std::min<uint64_t>(wr - rd, static_cast<uint64_t>(n)));
        for (size_t i = 0; i < m; ++i) { const float* f = &buf[static_cast<size_t>((rd + i) & mask) * static_cast<size_t>(ch)]; for (int c = 0; c < ch; ++c) if (dst[c]) dst[c][i] = f[c]; }
        r.store(rd + m, std::memory_order_release); return m;
    }
};

// --- reproducción de live.freon: eventos con su muestra absoluta del motor, de un escritor (control) a un lector (audio)
struct RpEv { uint64_t abs; uint8_t t; uint8_t k; int16_t a, b, c; float v; int64_t ticks; };   // t: 'c' orden · 'm' MIDI (a,b,c) · 't' tempo · 'P' reloj (ticks, a = en marcha, v = tempo)
struct RpRing {
    static constexpr size_t N = 1 << 16; RpEv buf[N]; std::atomic<uint64_t> w{0}, r{0};
    bool push(const RpEv& e) { const uint64_t wr = w.load(std::memory_order_relaxed); if (wr - r.load(std::memory_order_acquire) >= N) return false; buf[wr & (N - 1)] = e; w.store(wr + 1, std::memory_order_release); return true; }
    size_t size() const { return static_cast<size_t>(w.load(std::memory_order_acquire) - r.load(std::memory_order_acquire)); }
    const RpEv* front() const { const uint64_t rd = r.load(std::memory_order_relaxed); return rd == w.load(std::memory_order_acquire) ? nullptr : &buf[rd & (N - 1)]; }   // hilo de audio
    void pop() { r.store(r.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
};

// eventos del motor para live.freon: orden (Cmd), nota/mensaje MIDI o tempo
struct Ev { uint64_t f; uint8_t t; uint8_t k; int16_t a, b, c; float v; };   // t: 'c' orden (k,a,b,c,v) · 'm' MIDI (a,b,c) · 't' tempo (v)
struct EvRing {
    static constexpr size_t N = 1 << 15; Ev buf[N]; std::atomic<uint64_t> w{0}, r{0}; std::atomic<uint64_t> lost{0};
    void push(const Ev& e) {   // hilo de audio
        const uint64_t wr = w.load(std::memory_order_relaxed);
        if (wr - r.load(std::memory_order_acquire) >= N) { lost.fetch_add(1, std::memory_order_relaxed); return; }
        buf[wr & (N - 1)] = e; w.store(wr + 1, std::memory_order_release);
    }
};

inline void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }
inline uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

// WAV de 24 bits con chunk bext (BWF). Cabecera fija: los datos empiezan en el byte kData
constexpr long kBext = 602, kData = 12 + 24 + 8 + kBext + 8;   // = 654
inline void wavHeader(uint8_t* h, uint32_t sr, int nch, uint64_t dataBytes, uint64_t timeRef, const std::tm& t, const char* desc) {
    std::memset(h, 0, kData);
    std::memcpy(h, "RIFF", 4); put32(h + 4, static_cast<uint32_t>(std::min<uint64_t>(kData - 8 + dataBytes, 0xFFFFFFFFu))); std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4); put32(h + 16, 16); put16(h + 20, 1); put16(h + 22, static_cast<uint16_t>(nch)); put32(h + 24, sr);
    put32(h + 28, sr * 3u * static_cast<uint32_t>(nch)); put16(h + 32, static_cast<uint16_t>(3 * nch)); put16(h + 34, 24);
    uint8_t* b = h + 36; std::memcpy(b, "bext", 4); put32(b + 4, kBext); b += 8;
    std::snprintf(reinterpret_cast<char*>(b), 256, "%s", desc); std::snprintf(reinterpret_cast<char*>(b + 256), 32, "Freon Machine");
    char d[16]; std::strftime(d, sizeof d, "%Y-%m-%d", &t); std::memcpy(b + 320, d, 10); std::strftime(d, sizeof d, "%H:%M:%S", &t); std::memcpy(b + 330, d, 8);
    put32(b + 338, static_cast<uint32_t>(timeRef & 0xFFFFFFFFu)); put32(b + 342, static_cast<uint32_t>(timeRef >> 32)); put16(b + 346, 1);
    std::memcpy(h + kData - 8, "data", 4); put32(h + kData - 4, static_cast<uint32_t>(std::min<uint64_t>(dataBytes, 0xFFFFFFFFu)));
}
inline void put24(float x, uint8_t* o) {
    const float v = std::isfinite(x) ? std::fmax(-1.0f, std::fmin(1.0f, x)) : 0.0f;
    const int32_t s = static_cast<int32_t>(std::lrint(v * 8388607.0f));
    o[0] = static_cast<uint8_t>(s); o[1] = static_cast<uint8_t>(s >> 8); o[2] = static_cast<uint8_t>(s >> 16);
}

// Una toma que quedó abierta (corte de luz, cuelgue): su cabecera dice menos datos de los que hay. Se ajusta al archivo.
inline bool repairWav(const std::string& f) {
    std::error_code ec; const uint64_t sz = std::filesystem::file_size(f, ec); if (ec || sz < static_cast<uint64_t>(kData)) return false;
    FILE* fp = std::fopen(f.c_str(), "r+b"); if (!fp) return false;
    uint8_t h[kData]; bool fixed = false;
    if (std::fread(h, 1, kData, fp) == static_cast<size_t>(kData) && !std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 36, "bext", 4) && !std::memcmp(h + kData - 8, "data", 4)) {
        const uint64_t riff = get32(h + 4), align = std::max<uint64_t>(3, h[32] | h[33] << 8);
        if (riff + 8 < sz) {   // cerrada bien: RIFF + 8 = tamaño del archivo
            const uint64_t data = (sz - kData) / align * align;
            put32(h + 4, static_cast<uint32_t>(std::min<uint64_t>(kData - 8 + data, 0xFFFFFFFFu))); put32(h + kData - 4, static_cast<uint32_t>(std::min<uint64_t>(data, 0xFFFFFFFFu)));
            std::fseek(fp, 0, SEEK_SET); fixed = std::fwrite(h, 1, kData, fp) == static_cast<size_t>(kData);
            std::fflush(fp); if (fixed) { if (::ftruncate(fileno(fp), static_cast<off_t>(kData + data)) != 0) {} }
        }
    }
    std::fclose(fp); return fixed;
}

// Lee las entradas grabadas («Input 1-4.wav», 24 bits mono) desde una muestra y las va dejando en un anillo de 4 canales
// que el audio consume en lugar de la interfaz. Hilo propio: el disco nunca toca el audio
class InputPlayer {
public:
    ~InputPlayer() { stop(); }
    void start(DiskRing* ring, const std::string& takeDir, uint64_t fromFrame) {
        stop(); ring_ = ring; quit_ = false;
        for (int q = 0; q < 4; ++q) {
            fp_[q] = std::fopen((takeDir + "/Input " + std::to_string(q + 1) + ".wav").c_str(), "rb");
            if (fp_[q] && std::fseek(fp_[q], kData + static_cast<long>(fromFrame * 3), SEEK_SET) != 0) { std::fclose(fp_[q]); fp_[q] = nullptr; }
        }
        fill(); th_ = std::thread([this] { while (!quit_) { fill(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); } });
    }
    void stop() { quit_ = true; if (th_.joinable()) th_.join(); for (auto*& f : fp_) if (f) { std::fclose(f); f = nullptr; } }
    bool any() const { for (auto* f : fp_) if (f) return true; return false; }
private:
    void fill() {   // hasta dejar el anillo casi lleno; lo que no haya (sin archivo o acabado) son ceros
        if (!ring_) return;
        for (;;) {
            const size_t n = std::min<size_t>(4096, ring_->space()); if (n < 1024) return;
            for (int q = 0; q < 4; ++q) {
                ch_[q].assign(n, 0.0f);
                if (fp_[q]) { raw_.resize(n * 3); const size_t got = std::fread(raw_.data(), 3, n, fp_[q]); for (size_t i = 0; i < got; ++i) { int32_t v = raw_[3 * i] | raw_[3 * i + 1] << 8 | raw_[3 * i + 2] << 16; if (v & 0x800000) v -= 0x1000000; ch_[q][i] = static_cast<float>(v) / 8388608.0f; } }
            }
            const float* p[4] = {ch_[0].data(), ch_[1].data(), ch_[2].data(), ch_[3].data()};
            ring_->push(p, static_cast<int>(n));
        }
    }
    DiskRing* ring_ = nullptr; FILE* fp_[4] = {}; std::thread th_; std::atomic<bool> quit_{true};
    std::vector<float> ch_[4]; std::vector<uint8_t> raw_;
};

// Lo que el hilo de control da al empezar una toma
struct TakeInfo {
    std::string state;                 // estado completo (como una memoria): punto de partida de live.freon
    std::vector<std::string> names;    // nombre de cada pista del anillo de pistas, en su orden (ver stemLayout)
    std::vector<int> width;            // 1 = mono, 2 = estéreo (canales consecutivos del anillo)
    std::vector<bool> always;          // se escribe aunque la toma no lleve multipista (las entradas)
    bool stems = false;                // multipista
    int stemDelay = 0;                 // muestras de retraso del máster respecto a las pistas (el limitador mira por delante)
    uint64_t startAbs = 0;             // si no es 0: la toma empieza justo en esa muestra del motor (render de un live)
    std::string suffix;                // se añade al nombre de la carpeta, p. ej. « (render v2)»
    std::string origin;                // texto para origen.txt (de qué toma y versión sale un render)
    std::string build;                 // versión del programa (para avisar si se regenera con otra)
};

class Recorder {
public:
    std::atomic<bool> recording{false}, savingLast{false}, stemsOn{false};
    std::atomic<uint64_t> frames{0}, gaps{0};
    std::atomic<int> lastMinutes{0};
    std::function<void(const std::string&)> onEvent;
    static constexpr int kLastMin = 15;

    void start(DiskRing* master, DiskRing* stems, EvRing* ev, uint32_t sr, std::string dir) {
        ring_ = master; sring_ = stems; evr_ = ev; sr_ = sr; dir_ = std::move(dir);
        th_ = std::thread([this] { run(); });
    }
    ~Recorder() { { std::lock_guard<std::mutex> lk(mx_); quit_ = true; } cv_.notify_all(); if (th_.joinable()) th_.join(); }
    const std::string& dir() const { return dir_; }
    std::string takeName() { std::lock_guard<std::mutex> lk(mx_); return take_; }

    void cmdStart(TakeInfo info) { Cmd c{C_START, ""}; c.info = std::make_shared<TakeInfo>(std::move(info)); push(std::move(c)); }
    void cmdStop() { push({C_STOP, ""}); }
    void cmdMark(const std::string& label) { push({C_MARK, label}); }
    void cmdSaveLast() { push({C_LAST, ""}); }
    // línea para live.freon desde el hilo de control (cambios que no pasan por el motor: archivos de samples y breaks, memorias)
    void cmdLog(uint64_t frame, const std::string& line) { Cmd c{C_LOG, line}; c.frame = frame; push(std::move(c)); }

    static int repairAll(const std::string& dir) {
        int n = 0; std::error_code ec;
        for (const auto& t : std::filesystem::directory_iterator(dir, ec)) {
            if (!t.is_directory()) continue;
            for (const auto& f : std::filesystem::directory_iterator(t.path(), ec)) if (f.path().extension() == ".wav" && repairWav(f.path().string())) ++n;
        }
        return n;
    }

private:
    enum CK { C_START, C_STOP, C_MARK, C_LAST, C_LOG };
    struct Cmd { CK k; std::string s; std::shared_ptr<TakeInfo> info = {}; uint64_t frame = 0; };
    void push(Cmd c) { { std::lock_guard<std::mutex> lk(mx_); q_.push_back(std::move(c)); } cv_.notify_all(); }

    // un archivo WAV de la toma: «first» = primer canal en su anillo, «nch» canales
    struct Stream {
        std::string name; int first = 0, nch = 2; bool fromStems = false, keepSilent = true;
        FILE* fp = nullptr; uint64_t bytes = 0, partStart = 0; int part = 1; float peak = 0; std::vector<uint8_t> tmp;
    };

    DiskRing* ring_ = nullptr; DiskRing* sring_ = nullptr; EvRing* evr_ = nullptr;
    uint32_t sr_ = 48000; std::string dir_, take_, takeDir_;
    std::thread th_; std::mutex mx_; std::condition_variable cv_; std::deque<Cmd> q_; bool quit_ = false;
    std::vector<uint8_t> last_; uint64_t lastW_ = 0;   // «lo último»: 15 min en 24 bits (6 bytes por trama)
    uint64_t absM_ = 0, absS_ = 0, takeAbs_ = 0;        // muestra absoluta del motor de lo próximo que se lee de cada anillo; inicio de la toma
    std::vector<Stream> streams_; std::shared_ptr<TakeInfo> info_; FILE* log_ = nullptr; bool open_ = false;
    std::tm t0_{}; uint64_t timeRef_ = 0;
    std::chrono::steady_clock::time_point hdrAt_{}, syncAt_{};
    std::vector<float> tmpF_; std::vector<uint8_t> tmpB_;
    static constexpr uint64_t kSplit = 3500ull * 1024 * 1024;   // por debajo de los 4 GB de un WAV normal
    static constexpr size_t kChunk = 4096;

    static std::string stamp(const std::tm& t, const char* fmt) { char b[64]; std::strftime(b, sizeof b, fmt, &t); return b; }
    std::string hms(uint64_t fr) const {
        const double s = static_cast<double>(fr) / sr_; char b[32]; const int h = static_cast<int>(s / 3600), m = static_cast<int>(s / 60) % 60;
        std::snprintf(b, sizeof b, "%d:%02d:%06.3f", h, m, std::fmod(s, 60.0)); return b;
    }
    void note(const std::string& s) { if (onEvent) onEvent(s); std::printf("Grabación: %s\n", s.c_str()); std::fflush(stdout); }

    void run() {
        last_.assign(static_cast<size_t>(sr_) * 60 * kLastMin * 6, 0);
        tmpF_.resize(kChunk * 64); tmpB_.resize(kChunk * 6);
        for (;;) {
            std::deque<Cmd> cmds;
            { std::unique_lock<std::mutex> lk(mx_); cv_.wait_for(lk, std::chrono::milliseconds(20), [this] { return quit_ || !q_.empty(); });
              if (quit_) break; cmds.swap(q_); }
            drainMaster(); drainStems(); drainEvents();   // primero lo que ya sonó: una orden se aplica en este punto de la toma
            for (auto& c : cmds) handle(c);
            if (open_) keepSafe(false);
        }
        drainMaster(); drainStems(); drainEvents(); if (open_) closeTake();
    }
    void handle(const Cmd& c) {
        switch (c.k) {
        case C_START: if (!open_) openTake(c.info); break;
        case C_STOP: if (open_) closeTake(); break;
        case C_MARK: if (open_) mark(c.s.empty() ? "Mark" : c.s); break;
        case C_LAST: saveLast(); break;
        case C_LOG: if (open_ && log_ && c.frame >= takeAbs_) std::fprintf(log_, "%llu %s\n", static_cast<unsigned long long>(c.frame - takeAbs_), c.s.c_str()); break;
        }
    }
    // ---- máster: «lo último» siempre; master.wav si hay toma
    void drainMaster() {
        const uint64_t gap = ring_->dropped.exchange(0);
        if (gap) {
            std::fill(tmpB_.begin(), tmpB_.end(), 0);
            for (uint64_t left = gap; left > 0;) { const size_t n = static_cast<size_t>(std::min<uint64_t>(left, kChunk)); writeMaster(tmpB_.data(), n); left -= n; }
            absM_ += gap; if (open_) { gaps.fetch_add(1); mark("Gap (disk too slow)"); }
        }
        for (;;) {
            const uint64_t rd = ring_->r.load(std::memory_order_relaxed), wr = ring_->w.load(std::memory_order_acquire);
            if (wr == rd) break;
            const size_t n = static_cast<size_t>(std::min<uint64_t>(wr - rd, kChunk));
            for (size_t i = 0; i < n; ++i) { const float* s = &ring_->buf[static_cast<size_t>((rd + i) & ring_->mask) * 2]; put24(s[0], &tmpB_[6 * i]); put24(s[1], &tmpB_[6 * i + 3]); }
            ring_->r.store(rd + n, std::memory_order_release);
            const size_t skip = open_ && absM_ < takeAbs_ ? static_cast<size_t>(std::min<uint64_t>(takeAbs_ - absM_, n)) : 0;   // antes del inicio exacto de la toma
            if (skip) { writeLast(tmpB_.data(), skip); }
            if (n > skip) writeMaster(tmpB_.data() + skip * 6, n - skip);
            absM_ += n;
        }
    }
    void writeLast(const uint8_t* b, size_t n) {   // «lo último»
        const size_t cap = last_.size() / 6;
        for (size_t i = 0; i < n;) { const size_t pos = static_cast<size_t>(lastW_ % cap), m = std::min(n - i, cap - pos); std::memcpy(&last_[pos * 6], b + i * 6, m * 6); i += m; lastW_ += m; }
        lastMinutes = static_cast<int>(std::min<uint64_t>(lastW_, cap) / (60ull * sr_));
    }
    void writeMaster(const uint8_t* b, size_t n) {
        writeLast(b, n);
        if (!open_ || streams_.empty()) return;
        if (!writeStream(streams_[0], b, n)) return;
        frames.fetch_add(n);
    }
    // ---- pistas: entradas siempre en la toma; el resto solo con multipista
    void drainStems() {
        if (!sring_ || sring_->buf.empty()) return;
        const int ch = sring_->ch;
        const uint64_t gap = sring_->dropped.exchange(0);
        if (gap) {
            for (uint64_t left = gap; left > 0;) {
                const size_t n = static_cast<size_t>(std::min<uint64_t>(left, kChunk));
                if (open_) for (size_t s = 1; s < streams_.size(); ++s) { auto& st = streams_[s]; st.tmp.assign(n * 3 * static_cast<size_t>(st.nch), 0); writeStream(st, st.tmp.data(), n); }
                left -= n;
            }
            absS_ += gap;
        }
        for (;;) {
            const uint64_t rd = sring_->r.load(std::memory_order_relaxed), wr = sring_->w.load(std::memory_order_acquire);
            if (wr == rd) break;
            size_t n = static_cast<size_t>(std::min<uint64_t>(wr - rd, kChunk));
            // lo de antes del inicio de la toma (el anillo de pistas puede ir unas muestras por delante o por detrás) no entra
            const uint64_t from = takeAbs_ - std::min<uint64_t>(takeAbs_, static_cast<uint64_t>(info_ ? info_->stemDelay : 0));
            size_t skip = 0; if (open_ && absS_ < from) skip = static_cast<size_t>(std::min<uint64_t>(from - absS_, n));
            if (open_ && n > skip) {
                for (size_t s = 1; s < streams_.size(); ++s) {
                    auto& st = streams_[s]; if (!st.fromStems) continue;
                    st.tmp.resize((n - skip) * 3 * static_cast<size_t>(st.nch));
                    for (size_t i = skip; i < n; ++i) {
                        const float* f = &sring_->buf[static_cast<size_t>((rd + i) & sring_->mask) * static_cast<size_t>(ch)];
                        for (int c = 0; c < st.nch; ++c) { const float x = f[st.first + c]; st.peak = std::max(st.peak, std::fabs(x)); put24(x, &st.tmp[((i - skip) * static_cast<size_t>(st.nch) + static_cast<size_t>(c)) * 3]); }
                    }
                    writeStream(st, st.tmp.data(), n - skip);
                }
            }
            sring_->r.store(rd + n, std::memory_order_release); absS_ += n;
        }
    }
    // ---- live.freon: eventos del motor desde el inicio de la toma (en muestras desde el inicio)
    void drainEvents() {
        if (!evr_) return;
        for (;;) {
            const uint64_t rd = evr_->r.load(std::memory_order_relaxed), wr = evr_->w.load(std::memory_order_acquire);
            if (wr == rd) break;
            const Ev e = evr_->buf[rd & (EvRing::N - 1)]; evr_->r.store(rd + 1, std::memory_order_release);
            if (!open_ || !log_ || e.f < takeAbs_) continue;
            const unsigned long long f = static_cast<unsigned long long>(e.f - takeAbs_);
            if (e.t == 'c') std::fprintf(log_, "%llu c %d %d %d %d %.9g\n", f, e.k, e.a, e.b, e.c, e.v);
            else if (e.t == 'm') std::fprintf(log_, "%llu m %d %d %d\n", f, e.a, e.b, e.c);
            else if (e.t == 't') std::fprintf(log_, "%llu t %.6g\n", f, e.v);
        }
        if (const uint64_t l = evr_->lost.exchange(0); l && open_ && log_) std::fprintf(log_, "# %llu events lost (engine faster than the disk)\n", static_cast<unsigned long long>(l));
    }

    bool writeStream(Stream& s, const uint8_t* b, size_t n) {
        if (!s.fp) return false;
        const size_t bytes = n * 3 * static_cast<size_t>(s.nch);
        if (s.bytes + bytes > kSplit) { closeStream(s); ++s.part; s.partStart = frames.load(); openStream(s); if (!s.fp) return false; }
        if (std::fwrite(b, 1, bytes, s.fp) != bytes) { note("error writing " + s.name + " (disk full?)"); closeTake(); return false; }
        s.bytes += bytes; return true;
    }
    std::string streamFile(const Stream& s) const { return takeDir_ + "/" + s.name + (s.part == 1 ? "" : " " + std::to_string(s.part)) + ".wav"; }
    void openStream(Stream& s) {
        s.fp = std::fopen(streamFile(s).c_str(), "wb"); s.bytes = 0;
        if (!s.fp) { note("error: can't create " + streamFile(s)); return; }
        uint8_t h[kData]; wavHeader(h, sr_, s.nch, 0, timeRef_ + s.partStart, t0_, ("Freon Machine live · " + s.name).c_str());
        if (std::fwrite(h, 1, kData, s.fp) != static_cast<size_t>(kData)) { std::fclose(s.fp); s.fp = nullptr; note("error writing the header of " + s.name); }
    }
    void headerNow(Stream& s) {
        if (!s.fp) return;
        uint8_t h[kData]; wavHeader(h, sr_, s.nch, s.bytes, timeRef_ + s.partStart, t0_, ("Freon Machine live · " + s.name).c_str());
        std::fflush(s.fp); std::fseek(s.fp, 0, SEEK_SET); if (std::fwrite(h, 1, kData, s.fp) != static_cast<size_t>(kData)) {} std::fseek(s.fp, 0, SEEK_END);
    }
    void closeStream(Stream& s) { if (!s.fp) return; headerNow(s); std::fflush(s.fp); ::fsync(fileno(s.fp)); std::fclose(s.fp); s.fp = nullptr; }

    void openTake(const std::shared_ptr<TakeInfo>& info) {
        const std::time_t now = std::time(nullptr); localtime_r(&now, &t0_);
        timeRef_ = static_cast<uint64_t>(t0_.tm_hour * 3600 + t0_.tm_min * 60 + t0_.tm_sec) * sr_;   // muestras desde medianoche (BWF)
        info_ = info ? info : std::make_shared<TakeInfo>();
        { std::lock_guard<std::mutex> lk(mx_); take_ = stamp(t0_, "%Y-%m-%d %H-%M-%S") + info_->suffix; }
        takeDir_ = dir_ + "/" + take_; std::error_code ec; std::filesystem::create_directories(takeDir_, ec);
        takeAbs_ = std::max(absM_, info_->startAbs); frames = 0; gaps = 0;
        if (!info_->origin.empty()) { FILE* o = std::fopen((takeDir_ + "/origen.txt").c_str(), "wb"); if (o) { std::fprintf(o, "%s\n", info_->origin.c_str()); std::fclose(o); } }
        streams_.clear();
        Stream m; m.name = "master"; m.nch = 2; streams_.push_back(m);
        int pos = 0;
        for (size_t i = 0; i < info_->names.size(); ++i) {
            const int w = i < info_->width.size() ? info_->width[i] : 2;
            const bool always = i < info_->always.size() && info_->always[i];
            if (info_->stems || always) { Stream s; s.name = info_->names[i]; s.first = pos; s.nch = w; s.fromStems = true; s.keepSilent = false; streams_.push_back(s); }
            pos += w;
        }
        for (auto& s : streams_) openStream(s);
        // las pistas tienen que empezar en la misma muestra que el máster: si su anillo ya iba por delante, silencio delante
        // y el máster sale info_->stemDelay muestras tarde (limitador con anticipación): las pistas se retrasan lo mismo
        if (absS_ + static_cast<uint64_t>(info_->stemDelay) > takeAbs_) for (size_t i = 1; i < streams_.size(); ++i) {
            auto& st = streams_[i]; const size_t pad = static_cast<size_t>(absS_ + static_cast<uint64_t>(info_->stemDelay) - takeAbs_);
            st.tmp.assign(pad * 3 * static_cast<size_t>(st.nch), 0); if (st.fp) { std::fwrite(st.tmp.data(), 1, st.tmp.size(), st.fp); st.bytes += st.tmp.size(); }
        }
        if (!streams_[0].fp) { for (auto& s : streams_) if (s.fp) { std::fclose(s.fp); s.fp = nullptr; } streams_.clear(); note("error: can't record"); return; }
        // live.freon: cabecera, estado de partida y, después, una línea por evento
        log_ = std::fopen((takeDir_ + "/live.freon").c_str(), "wb");
        if (log_) {
            std::fprintf(log_, "# Freon Machine live v1: estado inicial y eventos del motor; la primera cifra de cada línea es la muestra desde el inicio\n");
            std::fprintf(log_, "{\"v\":1,\"sr\":%u,\"start\":\"%s\",\"build\":\"%s\",\"stems\":%s}\n", sr_, stamp(t0_, "%Y-%m-%d %H:%M:%S").c_str(), info_->build.c_str(), info_->stems ? "true" : "false");
            std::fprintf(log_, "0 S %s\n", info_->state.c_str());
        }
        hdrAt_ = syncAt_ = std::chrono::steady_clock::now();
        open_ = true; recording = true; stemsOn = info_->stems;
        note("rec " + take_ + (info_->stems ? " (multitrack)" : ""));
    }
    void keepSafe(bool force) {
        const auto now = std::chrono::steady_clock::now();
        if (force || now - hdrAt_ > std::chrono::seconds(2)) { hdrAt_ = now; for (auto& s : streams_) headerNow(s); if (log_) std::fflush(log_); }
        if (force || now - syncAt_ > std::chrono::seconds(5)) { syncAt_ = now; for (auto& s : streams_) if (s.fp) { std::fflush(s.fp); ::fsync(fileno(s.fp)); } if (log_) ::fsync(fileno(log_)); }
    }
    void closeTake() {
        if (!open_) return;
        open_ = false;
        int removed = 0;
        const uint64_t mFrames = streams_.empty() ? 0 : streams_[0].bytes / 6;
        for (auto& s : streams_) {
            // todas las pistas, del mismo largo que el máster (la última vuelta puede haber leído unas muestras de más)
            if (s.fromStems && s.fp && s.part == 1 && s.bytes > mFrames * 3 * static_cast<uint64_t>(s.nch)) {
                s.bytes = mFrames * 3 * static_cast<uint64_t>(s.nch); std::fflush(s.fp); if (::ftruncate(fileno(s.fp), static_cast<off_t>(kData + s.bytes)) != 0) {}
                std::fseek(s.fp, 0, SEEK_END);
            }
            closeStream(s);
            if (!s.keepSilent && s.peak < 1e-4f) {   // una pista que no ha sonado (por debajo de −80 dB) no se guarda
                std::error_code ec; for (int p = 1; p <= s.part; ++p) { Stream t = s; t.part = p; std::filesystem::remove(streamFile(t), ec); } ++removed;
            }
        }
        if (log_) { std::fflush(log_); ::fsync(fileno(log_)); std::fclose(log_); log_ = nullptr; }
        recording = false; stemsOn = false;
        const int kept = static_cast<int>(streams_.size()) - 1 - removed;
        note("saved " + take_ + " · " + hms(frames.load()) + (kept > 0 ? " · " + std::to_string(kept) + " tracks" : "") + (gaps.load() ? " · " + std::to_string(gaps.load()) + " gaps" : ""));
        streams_.clear();
    }
    void mark(const std::string& label) {
        const std::string f = takeDir_ + "/marcas.txt"; FILE* m = std::fopen(f.c_str(), "ab"); if (!m) return;
        std::fprintf(m, "%s\t%s\n", hms(frames.load()).c_str(), label.c_str()); std::fclose(m);
        if (log_) std::fprintf(log_, "%llu mark %s\n", static_cast<unsigned long long>(frames.load()), label.c_str());
        note("mark " + hms(frames.load()) + " " + label);
    }
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
                uint8_t h[kData]; wavHeader(h, sr, 2, data->size(), tref, t, "Freon Machine live · last minutes");
                ok = std::fwrite(h, 1, kData, fp) == static_cast<size_t>(kData) && std::fwrite(data->data(), 1, data->size(), fp) == data->size();
                ok = std::fflush(fp) == 0 && ok; ::fsync(fileno(fp)); ok = std::fclose(fp) == 0 && ok;
            }
            savingLast = false; note(ok ? "saved " + name : "error saving " + name);
        }).detach();
    }
};

}  // namespace rec

// par.h — reparto del trabajo de audio entre núcleos, pensado para no poner en riesgo el directo.
//
// Reglas que hacen que sea seguro:
//  - El hilo de audio trabaja como uno más: coge tareas de la misma lista. Si un ayudante llega tarde o no
//    llega, el hilo de audio hace ese trabajo él mismo. Solo espera a una tarea que otro ya haya empezado.
//  - Sin bloqueos ni reservas de memoria: una sola palabra atómica dice qué tarea es la siguiente.
//  - Los ayudantes duermen fuera de la parte repartida del bloque, y dentro esperan en bajo consumo.
//  - En Linux cada ayudante va fijo a su núcleo y con prioridad de tiempo real. Si no la consigue, no entra
//    en la lista: mejor un núcleo menos que uno al que el sistema pueda quitar la CPU a mitad de una tarea.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

namespace gb {

// Subnormales a cero en el hilo que llama (en ARM no basta con la opción de x86)
inline void flushDenormals() {
#if defined(__aarch64__)
    uint64_t fpcr; asm volatile("mrs %0, fpcr" : "=r"(fpcr)); asm volatile("msr fpcr, %0" :: "r"(fpcr | (1ull << 24)));
#elif defined(__SSE__) || defined(__x86_64__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}
// Espera de bajo consumo: en ARM el núcleo se duerme hasta el siguiente aviso (cpuWake) y, como mucho, unos 100 µs.
// Girar a tope calentaba la Pi hasta frenarla.
inline void cpuRelax() {
#if defined(__aarch64__)
    asm volatile("wfe");
#elif defined(__SSE__) || defined(__x86_64__)
    _mm_pause();
#endif
}
inline void cpuWake() {
#if defined(__aarch64__)
    asm volatile("sev");
#endif
}
// Fija el hilo que llama a un núcleo (solo en Linux; en el Mac no hace nada)
inline bool pinToCore(int core) {
#if defined(__linux__)
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0;
#else
    (void)core; return true;
#endif
}

class Parallel {
public:
    using Fn = void (*)(void* ctx, int i);
    Parallel() {
#if defined(__APPLE__)
        sem_ = dispatch_semaphore_create(0);
#else
        sem_init(&sem_, 0, 0);
#endif
    }
    ~Parallel() {
        stop();
#if defined(__APPLE__)
        dispatch_release(sem_);
#else
        sem_destroy(&sem_);
#endif
    }
    Parallel(const Parallel&) = delete; Parallel& operator=(const Parallel&) = delete;

    // Arranca hasta «helpers» ayudantes (fuera del audio). Con rtPrio > 0, en Linux, los que no consigan
    // tiempo real se quedan fuera. Devuelve cuántos han entrado.
    int start(int helpers, int rtPrio) {
        stop(); quit_ = false; int ok = 0;
        for (int h = 0; h < helpers; ++h) {
            std::atomic<int> st{0};   // 0 arrancando, 1 listo, -1 no vale
            std::thread t([this, h, rtPrio, &st]() { helper(h, rtPrio, st); });
            const auto t0 = std::chrono::steady_clock::now();
            while (st.load() == 0 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) std::this_thread::yield();
            if (st.load() == 1) { threads_.push_back(std::move(t)); ++ok; }
            else { t.join(); break; }
        }
        count_.store(ok, std::memory_order_release);
        return ok;
    }
    void stop() {
        count_.store(0, std::memory_order_release); quit_ = true; awake_ = false;
        for (size_t i = 0; i < threads_.size(); ++i) post();
        for (auto& t : threads_) if (t.joinable()) t.join();
        threads_.clear();
    }
    int helpers() const { return count_.load(std::memory_order_acquire); }
    // Al empezar y al acabar cada bloque de audio
    void enter() { const int n = helpers(); if (n == 0) return; awake_.store(true, std::memory_order_release); for (int i = 0; i < n; ++i) post(); }
    void leave() { awake_.store(false, std::memory_order_release); cpuWake(); }

    // Ejecuta f(ctx, 0..count-1) y vuelve cuando todas han terminado
    void run(Fn f, void* ctx, int count) {
        if (count <= 0) return;
        if (count == 1 || helpers() == 0 || !awake_.load(std::memory_order_relaxed)) { for (int i = 0; i < count; ++i) f(ctx, i); return; }
        fn_ = f; ctx_ = ctx; done_.store(0, std::memory_order_relaxed);
        gen_ = (gen_ + 1) & 0xFFFF;
        ticket_.store(gen_ << 16 | static_cast<uint32_t>(count) << 8, std::memory_order_release);
        cpuWake();
        while (claim()) {}
        while (done_.load(std::memory_order_acquire) < count) cpuRelax();
    }
private:
    // Boleto: generación (16 bits) | nº de tareas (8) | siguiente tarea (8). Coger una tarea es un solo CAS:
    // si sale bien, el trabajo publicado es válido hasta que esa tarea se marque como hecha.
    bool claim() {
        uint32_t t = ticket_.load(std::memory_order_acquire);
        for (;;) {
            const uint32_t count = t >> 8 & 0xFF, next = t & 0xFF;
            if (next >= count) return false;
            if (ticket_.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                fn_(ctx_, static_cast<int>(next));
                done_.fetch_add(1, std::memory_order_release);
                cpuWake();
                return true;
            }
        }
    }
    void helper(int h, int rtPrio, std::atomic<int>& st) {
        flushDenormals();
#if defined(__linux__)
        if (rtPrio > 0) {
            sched_param sp{}; sp.sched_priority = rtPrio;
            if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0 || !pinToCore(h + 1)) { st = -1; return; }
        }
#else
        (void)h; (void)rtPrio;
#endif
        st = 1;   // a partir de aquí no se vuelve a tocar «st»: vive en la pila de start()
        for (;;) {
            wait();
            if (quit_.load(std::memory_order_acquire)) return;
            while (awake_.load(std::memory_order_acquire)) { if (!claim()) cpuRelax(); }
        }
    }
    void post() {
#if defined(__APPLE__)
        dispatch_semaphore_signal(sem_);
#else
        sem_post(&sem_);
#endif
    }
    void wait() {
#if defined(__APPLE__)
        dispatch_semaphore_wait(sem_, DISPATCH_TIME_FOREVER);
#else
        while (sem_wait(&sem_) != 0) {}
#endif
    }
    std::vector<std::thread> threads_;
    std::atomic<int> count_{0}; std::atomic<bool> awake_{false}, quit_{false};
    alignas(64) std::atomic<uint32_t> ticket_{0};
    alignas(64) std::atomic<int> done_{0};
    alignas(64) Fn fn_ = nullptr; void* ctx_ = nullptr; uint32_t gen_ = 0;
#if defined(__APPLE__)
    dispatch_semaphore_t sem_;
#else
    sem_t sem_;
#endif
};

}  // namespace gb

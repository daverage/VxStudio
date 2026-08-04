#pragma once

#include <atomic>
#include <utility>

namespace vxsuite {

// Defers one-shot heavy analysis work off the audio thread.
// The audio thread calls signal() when the work becomes due; a message-thread
// tick (timer) calls runIfPending() to execute it. The release/acquire pair
// publishes everything the audio thread wrote before signalling, so the
// analysis code may freely read buffers the audio thread filled.
class DeferredAnalysisRunner {
public:
    void signal() noexcept { pending.store(true, std::memory_order_release); }
    bool isPending() const noexcept { return pending.load(std::memory_order_acquire); }

    template <typename Fn>
    void runIfPending(Fn&& fn) {
        if (pending.exchange(false, std::memory_order_acquire))
            fn();
    }

private:
    std::atomic<bool> pending { false };
};

// Hands a heap-built result (e.g. a struct holding vectors) from the message
// thread to the audio thread without the audio thread ever allocating,
// freeing, or blocking. The audio thread consumes by swapping, and the
// displaced old value is reclaimed on the message thread via collect().
// Single producer (message thread) / single consumer (audio thread).
template <typename T>
class RealtimeResultMailbox {
public:
    ~RealtimeResultMailbox() {
        delete incoming.exchange(nullptr);
        delete retired.exchange(nullptr);
    }

    // Message thread: publish a new value. Reclaims any value the audio thread
    // retired and any previously published value it never consumed.
    void publish(T value) {
        collect();
        delete incoming.exchange(new T(std::move(value)), std::memory_order_acq_rel);
    }

    // Message thread: delete whatever the audio thread has retired. Call this
    // from a periodic tick so consume() never stays deferred for long.
    void collect() {
        delete retired.exchange(nullptr, std::memory_order_acquire);
    }

    // Audio thread: swap a pending value into target. Returns true if target
    // was updated. While a previously displaced value awaits collect(), the
    // hand-off is deferred (returns false) — never freed on this thread.
    bool consume(T& target) noexcept {
        if (retired.load(std::memory_order_relaxed) != nullptr)
            return false;
        T* p = incoming.exchange(nullptr, std::memory_order_acquire);
        if (p == nullptr)
            return false;
        using std::swap;
        swap(target, *p);
        retired.store(p, std::memory_order_release);
        return true;
    }

private:
    std::atomic<T*> incoming { nullptr };
    std::atomic<T*> retired { nullptr };
};

} // namespace vxsuite

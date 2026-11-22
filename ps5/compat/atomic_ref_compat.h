#pragma once
// Stand-in for std::atomic_ref (C++20) on libc++ 18, which the PS5 payload
// SDK ships and which does not implement it. Force-included only by the PS5
// build (-include); does nothing where the standard library has it.
//
// Covers the operations the runtime uses: load, store, exchange,
// compare_exchange_strong/weak, fetch_add/sub/and/or/xor, on integral types.
#include <atomic>
#include <type_traits>
#include <version>

#ifndef __cpp_lib_atomic_ref
namespace std {
template <class T>
class atomic_ref {
    static_assert(std::is_trivially_copyable_v<T>, "atomic_ref needs a trivially copyable type");
    T* ptr_;

    static constexpr int order(memory_order o) {
        switch (o) {
        case memory_order_relaxed: return __ATOMIC_RELAXED;
        case memory_order_consume: return __ATOMIC_CONSUME;
        case memory_order_acquire: return __ATOMIC_ACQUIRE;
        case memory_order_release: return __ATOMIC_RELEASE;
        case memory_order_acq_rel: return __ATOMIC_ACQ_REL;
        default: return __ATOMIC_SEQ_CST;
        }
    }
    // A failure order may not be release or acq_rel.
    static constexpr int failure_order(memory_order o) {
        const int v = order(o);
        return v == __ATOMIC_RELEASE ? __ATOMIC_RELAXED : v == __ATOMIC_ACQ_REL ? __ATOMIC_ACQUIRE : v;
    }

public:
    using value_type = T;
    static constexpr size_t required_alignment = alignof(T) < sizeof(T) ? sizeof(T) : alignof(T);
    static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), 0);

    explicit atomic_ref(T& object) noexcept : ptr_(&object) {}
    atomic_ref(const atomic_ref&) noexcept = default;
    atomic_ref& operator=(const atomic_ref&) = delete;

    bool is_lock_free() const noexcept { return __atomic_is_lock_free(sizeof(T), ptr_); }

    T load(memory_order o = memory_order_seq_cst) const noexcept { return __atomic_load_n(ptr_, order(o)); }
    void store(T v, memory_order o = memory_order_seq_cst) const noexcept { __atomic_store_n(ptr_, v, order(o)); }
    operator T() const noexcept { return load(); }
    T operator=(T v) const noexcept { store(v); return v; }
    T exchange(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_exchange_n(ptr_, v, order(o)); }

    bool compare_exchange_strong(T& expected, T desired, memory_order s, memory_order f) const noexcept {
        return __atomic_compare_exchange_n(ptr_, &expected, desired, false, order(s), order(f));
    }
    bool compare_exchange_strong(T& expected, T desired, memory_order o = memory_order_seq_cst) const noexcept {
        return __atomic_compare_exchange_n(ptr_, &expected, desired, false, order(o), failure_order(o));
    }
    bool compare_exchange_weak(T& expected, T desired, memory_order s, memory_order f) const noexcept {
        return __atomic_compare_exchange_n(ptr_, &expected, desired, true, order(s), order(f));
    }
    bool compare_exchange_weak(T& expected, T desired, memory_order o = memory_order_seq_cst) const noexcept {
        return __atomic_compare_exchange_n(ptr_, &expected, desired, true, order(o), failure_order(o));
    }

    T fetch_add(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_add(ptr_, v, order(o)); }
    T fetch_sub(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_sub(ptr_, v, order(o)); }
    T fetch_and(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_and(ptr_, v, order(o)); }
    T fetch_or(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_or(ptr_, v, order(o)); }
    T fetch_xor(T v, memory_order o = memory_order_seq_cst) const noexcept { return __atomic_fetch_xor(ptr_, v, order(o)); }
};
}  // namespace std
#endif

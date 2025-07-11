#pragma once

#include <new>
#include <vector>
#include <memory>
#include <functional>

extern "C" {
#include "postgres.h"
#include "utils/memutils.h"
}

template <typename T> struct PallocZeroAllocator {
  using value_type = T;
  MemoryContext context;

  explicit PallocZeroAllocator(MemoryContext ctx) noexcept : context(ctx) {}

  template <class U>
  PallocZeroAllocator(const PallocZeroAllocator<U> &other) noexcept
      : context(other.context) {}

  template <typename U> struct rebind { using other = PallocZeroAllocator<U>; };

  T *allocate(size_t n) {
    return static_cast<T *>(MemoryContextAllocZero(context, n * sizeof(T)));
  }

  void deallocate(T *p, size_t) { pfree(p); }
};

template <class T, class U>
bool operator==(const PallocZeroAllocator<T> &a,
                const PallocZeroAllocator<U> &b) noexcept {
  return a.context == b.context;
}

template <class T, class U>
bool operator!=(const PallocZeroAllocator<T> &a,
                const PallocZeroAllocator<U> &b) noexcept {
  return !(a == b);
}

class ManagedMemContext;

struct PallocDeleter {
  void operator()(ManagedMemContext *p) const;
};

using ContextUniquePtr = std::unique_ptr<ManagedMemContext, PallocDeleter>;

// Manages the lifetime of a PG MemoryContext and handles the
// destruction of C++ objects allocated within it.
class ManagedMemContext {
  friend struct PallocDeleter;

public:
  using DtorThunk = std::function<void()>;

  static ContextUniquePtr
  create_empty(MemoryContext ctx_alloc,
               MemoryContext ctx_manager = TopMemoryContext);

  ManagedMemContext(const ManagedMemContext &) = delete;
  ManagedMemContext &operator=(const ManagedMemContext &) = delete;

  // Constructs an object within the context.
  // Its destructor will be called when the context is reset or destroyed.
  template <typename T, typename... Args> T *pnew(Args &&...args) {
    // Double-check that the destructor does not throw because it is called in
    // PG code.
    static_assert(std::is_nothrow_destructible_v<T>,
                  "Types allocated in a ManagedMemContext must have a noexcept "
                  "destructor");
    // We do not own the ctx_alloc, it can be reset or deleted any time,
    // reinit if the context was reset.
    if (reset) {
      init();
    }
    void *mem = allocate0(sizeof(T));
    T *obj = construct_at<T>(mem, std::forward<Args>(args)...);
    // Register the destructor to be called on cleanup.
    DtorThunk dtor = [obj]() { obj->~T(); };
    dtor_thunks->push_back(std::move(dtor));
    return obj;
  }

  template <typename T> PallocZeroAllocator<T> get_allocator() const {
    return PallocZeroAllocator<T>(ctx_alloc);
  }

  MemoryContext get_context() const { return ctx_alloc; }

  // Allocate raw, zero initialized memory within the context.
  template <typename T = void> T *allocate0(size_t size) {
    return static_cast<T *>(MemoryContextAllocZero(ctx_alloc, size));
  }

private:
  template <typename T, typename... Args>
  static T *construct_at(void *mem, Args &&...args) {
    try {
      return new (mem) T(std::forward<Args>(args)...);
    } catch (...) {
      pfree(mem);
      throw;
    }
  }

  ManagedMemContext(MemoryContext ctx);
  ~ManagedMemContext();

  void init();
  void run_destructors();
  static void reset_callback(void *arg);

  MemoryContext ctx_alloc = nullptr;
  bool reset = true;
  MemoryContextCallback context_callback = {0};
  using DtorThunks = std::vector<DtorThunk, PallocZeroAllocator<DtorThunk>>;
  // Holds the destructors. It is itself allocated within the context.
  DtorThunks *dtor_thunks = nullptr;
};

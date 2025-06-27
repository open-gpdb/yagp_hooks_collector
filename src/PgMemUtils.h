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

  // Deallocation is a no-op because MemoryContext owns the memory.
  void deallocate(T *, size_t) noexcept {}
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

  static ContextUniquePtr create(MemoryContext parent, const char *name);

  ManagedMemContext(const ManagedMemContext &) = delete;
  ManagedMemContext &operator=(const ManagedMemContext &) = delete;

  // Constructs an object within the context.
  // Its destructor will be called when the context is reset or destroyed.
  template <typename T, typename... Args> T *pnew(Args &&...args) {
    T *obj = new (allocate0<T>(sizeof(T))) T(std::forward<Args>(args)...);
    // Register the destructor to be called on cleanup.
    dtor_thunks->push_back([obj]() { obj->~T(); });
    return obj;
  }

  template <typename T> PallocZeroAllocator<T> get_allocator() const {
    return PallocZeroAllocator<T>(context);
  }

  MemoryContext get_context() const { return context; }

  // Allocate raw, zero initialized memory within the context.
  template <typename T = void> T *allocate0(size_t size) {
    return static_cast<T *>(MemoryContextAllocZero(context, size));
  }

private:
  ManagedMemContext(MemoryContext parent, const char *name);

  // Calls MemoryContextDelete(), which first calls its callback - destructors
  // in our case and then frees associated raw memory.
  ~ManagedMemContext();

  void run_destructors();

  static void reset_callback(void *arg);

  MemoryContext context;
  MemoryContextCallback context_callback;
  // Holds the destructors. It is itself allocated within the context.
  std::vector<DtorThunk, PallocZeroAllocator<DtorThunk>> *dtor_thunks = nullptr;
};

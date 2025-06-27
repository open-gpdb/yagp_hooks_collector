#include "PgMemUtils.h"

ManagedMemContext::ManagedMemContext(MemoryContext parent, const char *name) {
  context = AllocSetContextCreate(parent, name, ALLOCSET_DEFAULT_SIZES);
  // Allocate the vector itself inside the context.
  PallocZeroAllocator<DtorThunk> allocator(context);
  void *vec_mem = MemoryContextAlloc(
      context, sizeof(std::vector<DtorThunk, PallocZeroAllocator<DtorThunk>>));
  dtor_thunks = new (vec_mem)
      std::vector<DtorThunk, PallocZeroAllocator<DtorThunk>>(allocator);
  // Register a callback so PG's MemoryContext Reset/Delete will call our
  // destructors.
  context_callback.func = ManagedMemContext::reset_callback;
  context_callback.arg = this;
  MemoryContextRegisterResetCallback(context, &context_callback);
}

ManagedMemContext::~ManagedMemContext() {
  if (context) {
    // This will trigger the reset_callback, which calls run_destructors.
    // Note that callback is called before raw memory is freed.
    MemoryContextDelete(context);
  }
}

ContextUniquePtr ManagedMemContext::create(MemoryContext parent,
                                           const char *name) {
  void *mem = MemoryContextAllocZero(parent, sizeof(ManagedMemContext));
  ManagedMemContext *obj = new (mem) ManagedMemContext(parent, name);
  return ContextUniquePtr(obj);
}

void ManagedMemContext::run_destructors() {
  // Call destructors in reverse order of construction.
  for (auto it = dtor_thunks->rbegin(); it != dtor_thunks->rend(); ++it) {
    (*it)();
  }
  // The vector's memory will be freed by the subsequent MemoryContext
  // Reset/Delete, so we don't need to clear it here. Just call its own
  // destructor.
  dtor_thunks->~vector();
}

void ManagedMemContext::reset_callback(void *arg) {
  static_cast<ManagedMemContext *>(arg)->run_destructors();
}

void PallocDeleter::operator()(ManagedMemContext *p) const {
  if (p) {
    p->~ManagedMemContext();
    pfree(p);
  }
}

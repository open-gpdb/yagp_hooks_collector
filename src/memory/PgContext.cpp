#include "PgContext.h"

#include <stdexcept>
#include <type_traits>

ManagedMemContext::ManagedMemContext(MemoryContext ctx) : ctx_alloc(ctx) {}

ManagedMemContext::~ManagedMemContext() {
  ctx_alloc = nullptr;
  dtor_thunks = nullptr;
}

ContextUniquePtr ManagedMemContext::create_empty(MemoryContext ctx_alloc,
                                                 MemoryContext ctx_manager) {
  void *mem = MemoryContextAllocZero(ctx_manager, sizeof(ManagedMemContext));
  auto obj = construct_at<ManagedMemContext>(mem, ctx_alloc);
  return ContextUniquePtr(obj);
}

void ManagedMemContext::init() {
  if (!MemoryContextIsValid(ctx_alloc)) {
    throw std::runtime_error(
        "Can not init ManagedMemContext with invalid PG context.");
  }
  // Allocate the vector itself inside the ctx_alloc.
  PallocZeroAllocator<DtorThunk> allocator(ctx_alloc);
  void *vec_mem = MemoryContextAlloc(ctx_alloc, sizeof(DtorThunks));
  dtor_thunks = construct_at<DtorThunks>(vec_mem, allocator);
  // Register a callback so PG's MemoryContext Reset/Delete will call our dtors.
  context_callback.func = ManagedMemContext::reset_callback;
  context_callback.arg = this;
  MemoryContextRegisterResetCallback(ctx_alloc, &context_callback);
  reset = false;
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
  auto self = static_cast<ManagedMemContext *>(arg);
  self->run_destructors();
  self->dtor_thunks = nullptr;
  self->reset = true;
}

void PallocDeleter::operator()(ManagedMemContext *p) const {
  if (p) {
    p->~ManagedMemContext();
    pfree(p);
  }
}

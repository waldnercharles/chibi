// SPDX-License-Identifier: GPL-3.0-only
#ifndef CHIBI_VNEXT_FOREIGN_H
#define CHIBI_VNEXT_FOREIGN_H

#include <stddef.h>
#include <stdint.h>

// Process-local metadata, borrowed only until compile_both returns. Parameter
// and result types use the generated typed Model's scalar IDs (bool=1, i64=2).
// address is a real function address with this exact System V scalar signature;
// its code remains caller-owned and loaded until every JIT owner is destroyed.
typedef struct ChibiVnextForeignBinding {
    const uint8_t *library;
    uint64_t library_length;
    const uint8_t *symbol;
    uint64_t symbol_length;
    const uint32_t *parameter_types;
    uint64_t parameter_count;
    uint32_t result_type;
    const void *address;
} ChibiVnextForeignBinding;

#if defined(__linux__) && defined(__x86_64__)
_Static_assert(sizeof(ChibiVnextForeignBinding) == 64, "foreign descriptor byte budget");
_Static_assert(_Alignof(ChibiVnextForeignBinding) == 8, "foreign descriptor alignment");
_Static_assert(offsetof(ChibiVnextForeignBinding, library) == 0, "library offset");
_Static_assert(offsetof(ChibiVnextForeignBinding, library_length) == 8, "library length offset");
_Static_assert(offsetof(ChibiVnextForeignBinding, symbol) == 16, "symbol offset");
_Static_assert(offsetof(ChibiVnextForeignBinding, symbol_length) == 24, "symbol length offset");
_Static_assert(offsetof(ChibiVnextForeignBinding, parameter_types) == 32, "parameter types offset");
_Static_assert(offsetof(ChibiVnextForeignBinding, parameter_count) == 40, "parameter count offset");
_Static_assert(offsetof(ChibiVnextForeignBinding, result_type) == 48, "result type offset");
_Static_assert(offsetof(ChibiVnextForeignBinding, address) == 56, "code address offset");
#endif

// Every nonempty input buffer must be readable, aligned, live and unchanged
// throughout this call. At most 32 bindings; NULL with count zero is allowed.
// Returns one owned ready/failure result; no metadata borrow is retained.
void *chibi_vnext_compile_both(const uint32_t *words, uint64_t word_count,
    const int64_t *literals, uint64_t literal_count,
    const uint8_t *names, uint64_t name_byte_count,
    const ChibiVnextForeignBinding *bindings, uint64_t binding_count);
uint32_t chibi_vnext_status(const void *owner);
uint64_t chibi_vnext_diagnostic_length(const void *owner);
int32_t chibi_vnext_diagnostic_copy(const void *owner, uint8_t *destination, uint64_t capacity);
const void *chibi_vnext_entry(const void *owner, uint32_t function_id);
uint64_t chibi_vnext_object_length(const void *owner);
int32_t chibi_vnext_object_copy(const void *owner, uint8_t *destination, uint64_t capacity);
// Destroy exactly once, after every generated call finishes. This invalidates
// all entries; copying an object does not retain a JIT/code dependency.
int32_t chibi_vnext_destroy(void *owner);

#endif

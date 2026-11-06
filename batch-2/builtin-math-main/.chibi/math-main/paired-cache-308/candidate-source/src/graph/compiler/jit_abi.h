#ifndef CHIBI_JIT_ABI_H
#define CHIBI_JIT_ABI_H

#include <stddef.h>
#include <stdint.h>

#define GRAPH_JIT_ABI_VERSION 1u

typedef enum
{
    GRAPH_JIT_OK = 0,
    GRAPH_JIT_SUSPENDED = 1,
    GRAPH_JIT_ERROR = -1,
} graph_jit_status;

enum
{
    GRAPH_JIT_FRAME_SUSPENDED = 1u << 0,
};

typedef struct
{
    uint64_t resource_id;
    void *data;
    size_t size;
} graph_jit_column;

typedef struct
{
    uint32_t resume_block;
    uint32_t flags;
    uint32_t slot_count;
    void *slots;
    double wake_time;
} graph_jit_frame;

typedef struct graph_jit_host graph_jit_host;

typedef struct
{
    uint32_t abi_version;
    uint32_t entity_slot;
    uint32_t entity_generation;
    float delta_seconds;
    double monotonic_time;
    const graph_jit_column *columns;
    int column_count;
    const graph_jit_host *host;
    graph_jit_frame *frame;
} graph_jit_context;

typedef int (*graph_jit_entry_fn)(const graph_jit_context *context);
typedef void *(*graph_jit_component_fn)(void *user, uint64_t resource_id);
typedef int (*graph_jit_call_fn)(
    void *user,
    uint64_t operation_id,
    void *output,
    const void *const *inputs,
    int input_count
);
typedef void (*graph_jit_error_fn)(void *user, int code, uint64_t operation_id);

struct graph_jit_host
{
    void *user;
    graph_jit_component_fn component;
    graph_jit_call_fn call;
    graph_jit_error_fn error;
};

#endif

#ifndef CHIBI_SHADER_SOURCE_DEBUG_H
#define CHIBI_SHADER_SOURCE_DEBUG_H
#include "runtime/shaders/program.h"

#define SHADER_DEBUG_DOCUMENT_MAX 256
#define SHADER_DEBUG_SOURCE_BYTES_MAX 524288
#define SHADER_DEBUG_TOTAL_BYTES_MAX 8388608
#define SHADER_DEBUG_IDENTITY_BYTES_MAX 4096

typedef struct
{
    const char *identity, *text;
} shader_debug_document;
typedef struct
{
    uint64_t source_id;
    uint64_t occurrence,
        call_site; /* Original occurrence and enclosing call's source_id, or 0. */
    int document, start, end; /* Half-open byte range in documents[document]. */
} shader_debug_origin;
typedef struct
{
    uint64_t source_id;
    int node; /* Principal result, including aliases that emit no new node. */
} shader_debug_result;
typedef struct
{
    const shader_debug_document *documents;
    int document_count;
    const shader_debug_origin *origins;
    int origin_count;
    const shader_debug_result *results;
    int result_count;
} shader_debug_source_map;
typedef struct
{
    shader_debug_origin origin;
    shader_op op;
    int width;
    float value[4];
    bool finite;
} shader_debug_value;
typedef struct shader_debug_snapshot shader_debug_snapshot;
enum
{
    SHADER_DEBUG_FLOAT_APPROXIMATION = 1 << 0,
    SHADER_DEBUG_BASE_LEVEL_IMAGES = 1 << 1,
    SHADER_DEBUG_COORDINATE_GRADIENTS = 1 << 2,
    SHADER_DEBUG_REFERENCE_FOOTPRINT = 1 << 3,
    SHADER_DEBUG_SCENE_INFLUENCE = 1 << 4,
    SHADER_DEBUG_NONFINITE_VALUE = 1 << 5,
};
/* Copies source documents, origins and all intermediate values for one sample
 * of the existing evaluator. Every nonzero IR source_id needs a unique origin;
 * several auxiliary nodes may share that origin. Source results bind separately
 * to their principal node. Each helper expansion uses distinct source_ids and
 * retains its original occurrence plus enclosing call-site chain. Zero IR IDs
 * are synthetic, unmapped nodes. Nonfinite results are retained and flagged.
 * The caller pairs this source map with its exact compiled program generation.
 * Replaces/frees *result only on success. Failure leaves it unchanged and reports
 * the originating source_id in error->node. No GPU equivalence is implied.
 * Source text is bounded by SOURCE_BYTES_MAX per document and TOTAL_BYTES_MAX
 * for the closure; identities have IDENTITY_BYTES_MAX bytes. All are C strings.
 * Documents/origins are borrowed for the call; snapshots retain no graph,
 * sample, parameters, resolver or caller-owned source pointers. */
bool shader_debug_capture(
    const shader_graph *,
    const shader_sample_context *,
    const float (*parameters)[4],
    const shader_debug_source_map *,
    shader_debug_snapshot **result,
    shader_error *
);
void shader_debug_snapshot_free(shader_debug_snapshot *);
int shader_debug_value_count(const shader_debug_snapshot *);
unsigned shader_debug_limitations(const shader_debug_snapshot *);
/* Borrowed provenance for the captured generation, including enclosing calls
 * that emit no node. Value access separately checks the current editor source. */
const shader_debug_origin *shader_debug_origin_find(const shader_debug_snapshot *, uint64_t source_id);
const shader_debug_document *shader_debug_document_at(const shader_debug_snapshot *, int document);
/* Node order is IR dependency order, not GPU lane or fragment execution order.
 * Values are returned only when the whole current source closure matches the
 * captured identities/text in document order. This includes callers and helper
 * documents. For a source-free program pass NULL/0. Borrowed result. */
const shader_debug_value *shader_debug_value_at(
    const shader_debug_snapshot *,
    int node,
    const shader_debug_document *current,
    int document_count
);
/* Find the next node carrying source_id after `after`, starting with -1.
 * Returns -1 when absent. Multiple expansions of one source remain inspectable. */
int shader_debug_find(const shader_debug_snapshot *, uint64_t source_id, int after);
/* The principal source result, not an arbitrary auxiliary node. Exact source
 * matching follows value_at. A local alias can refer to an earlier IR value. */
const shader_debug_value *shader_debug_source_value(
    const shader_debug_snapshot *,
    uint64_t source_id,
    const shader_debug_document *current,
    int document_count
);
#endif

#ifndef CHIBI_SUPPORT_FILE_PAIR_INTERNAL_H
#define CHIBI_SUPPORT_FILE_PAIR_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

/* Private two-file transport. Callers own path storage through dispose.
 * Native callers stage their own bytes; the host admits arbitrary paths.
 * No operation changes cwd or interprets project/compiler metadata. */
typedef enum
{
    FILE_PAIR_NATIVE,
    FILE_PAIR_EXCLUSIVE
} file_pair_mode;
typedef enum
{
    FILE_PAIR_RESERVE,
    FILE_PAIR_BACKUP,
    FILE_PAIR_PUBLISH,
    FILE_PAIR_REMOVE_NEW,
    FILE_PAIR_RESTORE,
    FILE_PAIR_REMOVE_SOURCE,
    FILE_PAIR_REMOVE_BACKUP,
    FILE_PAIR_OPERATION_COUNT
} file_pair_operation;

/* The private test dispatcher can fail a particular operation/entry without
 * process-global callbacks or environment controls. Production passes zero. */
#define FILE_PAIR_FAIL(OP, ENTRY) (1u << ((OP) * 2 + (ENTRY)))
typedef struct
{
    const char *source[2], *destination[2];
    file_pair_mode mode;
    unsigned failures;
} file_pair_plan;

typedef struct
{
    file_pair_plan plan;
    char *backup[2];
    bool existed[2], reserved[2], old_moved[2], published[2];
    bool consumed[2], retained_source[2], retained_backup[2];
    bool admitted, committed, prepared;
    int status, system_error, failed_operation, failed_entry;
    char *diagnostic;
    size_t diagnostic_capacity, diagnostic_size;
} file_pair;

/* Exact conservative diagnostic bound; includes escaped full path names. */
size_t file_pair_diagnostic_capacity(const file_pair_plan *);
/* Error-only pre-admission evidence. Returns allocated escaped source names
 * if their complete block fits limit; otherwise NULL without allocation.
 * Size always receives the required byte count, excluding NUL. */
char *file_pair_source_diagnostic(const char *const sources[2], size_t limit, size_t *size);
/* Reserves BOTH empty backup slots and all result/diagnostic storage before
 * any old destination moves. Failure cleans only owned empty reservations;
 * sources remain caller-owned. Full retained paths stay in this result. */
bool file_pair_prepare(file_pair *, const file_pair_plan *);
/* Reject a prepared but unadmitted transaction, cleaning empty reservations.
 * Execute transfers disposable source ownership and defers cancellation.
 * 0 committed/clean; 1 prior state restored; 2 restoration incomplete;
 * 3 committed with housekeeping retained. Neither function allocates. */
int file_pair_abort(file_pair *);
int file_pair_execute(file_pair *);
/* Frees storage only. Never removes retained files. */
void file_pair_dispose(file_pair *);
#endif

#define _GNU_SOURCE
#include "native_adapter.h"
#include "host-contract.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if !defined(__linux__) || !defined(__x86_64__)
#error "Census native tests require Linux x86-64 System V"
#endif
_Static_assert(sizeof(void *) == sizeof(void (*)(void)), "System V pointer widths");
/* A process-wide synchronous callback hook, not retained generated storage.
 * Reentrancy is forbidden. No callback touches the borrowed input. */
static struct { int active; unsigned calls; int64_t fields[16]; } capture;
int64_t chibi_census_report(int64_t a, int64_t b, int64_t c, int64_t d,
    int64_t e, int64_t f, int64_t g, int64_t h, int64_t i, int64_t j,
    int64_t k, int64_t l, int64_t m, int64_t n, int64_t o, int64_t p) {
    assert(capture.active);
    if (capture.calls < 2) ++capture.calls; /* Saturation preserves multiple-call failure. */
    int64_t fields[16] = {a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p};
    memcpy(capture.fields, fields, sizeof(fields));
    return a;
}
static const uint32_t parameters[16] = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
static const uint8_t library[] = "libchibi_census_host.a";
static const uint8_t symbol[] = "chibi_census_report";
/* Only the report is foreign; BorrowedBytes is never a fake scalar binding. */
static const ChibiVnextForeignBinding binding = {
    library, sizeof(library)-1, symbol, sizeof(symbol)-1,
    parameters, 16, 2, (const void *)chibi_census_report
};
const ChibiVnextForeignBinding *chibi_census_test_bindings(void) { return &binding; }
uint64_t chibi_census_test_binding_count(void) { return 1; }
static void begin_report(void) {
    assert(!capture.active);
    memset(&capture, 0, sizeof(capture));
    capture.active = 1;
}
static int finish_report(int64_t returned, const int64_t expected[16]) {
    capture.active = 0;
    int failed = capture.calls != 1 || returned != expected[0];
    for (unsigned field = 0; field < 16; ++field) {
        if (capture.fields[field] != expected[field]) {
            fprintf(stderr, "report field %u: got %" PRId64 ", expected %" PRId64 "\n",
                field, capture.fields[field], expected[field]);
            failed = 1;
        }
    }
    if (capture.calls != 1 || returned != expected[0])
        fprintf(stderr, "report calls %u, return %" PRId64 ", expected %" PRId64 "\n",
            capture.calls, returned, expected[0]);
    return failed;
}
/* Each mapping owns real storage with inaccessible neighbors. Read-only before
 * any generated call; input stays live until all calls on that mapping finish. */
typedef struct { uint8_t *mapping, *bytes; size_t mapped, usable; } Guard;
static int guard_create(Guard *g, size_t count, int right) {
    memset(g, 0, sizeof(*g));
    if (!count) return 0; /* NULL, zero is a valid empty borrow. */
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || count > SIZE_MAX - 3 * (size_t)page) return 3;
    size_t usable = ((count + (size_t)page - 1) / (size_t)page) * (size_t)page;
    size_t mapped = usable + 2 * (size_t)page;
    uint8_t *base = mmap(NULL, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { perror("mmap"); return 3; }
    if (mprotect(base + page, usable, PROT_READ | PROT_WRITE)) {
        perror("mprotect writable"); munmap(base, mapped); return 3;
    }
    *g = (Guard){base, base + page + (right ? usable-count : 0), mapped, usable};
    return 0;
}
static int guard_seal(Guard *g) {
    if (!g->mapping) return 0;
    size_t page = (g->mapped - g->usable) / 2;
    if (mprotect(g->mapping + page, g->usable, PROT_READ)) { perror("mprotect readonly"); return 3; }
    return 0;
}
static int guard_close(Guard *g) {
    if (g->mapping && munmap(g->mapping, g->mapped)) { perror("munmap"); return 3; }
    return 0;
}
static int read_exact(FILE *stream, void *out, size_t count) {
    if (count && fread(out, 1, count, stream) != count) {
        fprintf(stderr, "suite is truncated or unreadable\n"); return 3;
    }
    return 0;
}
static int read_u64(FILE *stream, uint64_t *out) {
    uint8_t bytes[8];
    if (read_exact(stream, bytes, sizeof(bytes))) return 3;
    *out = 0;
    for (unsigned i = 0; i < 8; ++i) *out |= (uint64_t)bytes[i] << (8*i);
    return 0;
}
typedef int64_t (*CensusEntry)(const uint8_t *, int64_t);
/* Suite transport only: C never reads CCN1 headers or computes a census.
 * Input tuples, including every zero/error field, come from reference.py. */
int32_t chibi_census_test_corpus(const void *census_address) {
    assert(census_address);
    CensusEntry census = (CensusEntry)census_address;
    const char *path = getenv("CENSUS_NATIVE_CASES");
    if (!path || !*path) { fprintf(stderr, "CENSUS_NATIVE_CASES is required\n"); return 3; }
    FILE *stream = fopen(path, "rb");
    if (!stream) { perror(path); return 3; }
    int result = 0;
    uint8_t magic[8]; uint64_t cases = 0;
    if (read_exact(stream, magic, 8) || memcmp(magic, "CCTEST1\n", 8) ||
        read_u64(stream, &cases) || cases == 0 || cases > 100000) { fprintf(stderr, "%s: invalid suite magic/count/header\n", path); result = 3; goto done; }
    for (uint64_t index = 0; index < cases && !result; ++index) {
        uint64_t size = 0; int64_t expected[16];
        if (read_u64(stream, &size) || size > 536870912) { fprintf(stderr, "%s: invalid extent at case %" PRIu64 "\n", path, index); result = 3; break; }
        for (unsigned field = 0; field < 16; ++field) {
            uint64_t bits;
            if (read_u64(stream, &bits)) { result = 3; break; }
            memcpy(&expected[field], &bits, 8);
        }
        if (result) break;
        Guard left, right;
        result = guard_create(&left, (size_t)size, 0);
        if (result) break;
        result = guard_create(&right, (size_t)size, 1);
        if (result) { guard_close(&left); break; }
        result = read_exact(stream, left.bytes, (size_t)size);
        if (!result && size) memcpy(right.bytes, left.bytes, (size_t)size);
        if (!result) result = guard_seal(&left);
        if (!result) result = guard_seal(&right);
        if (!result) {
            begin_report();
            int64_t returned = census(left.bytes, (int64_t)size);
            result = finish_report(returned, expected);
            begin_report();
            returned = census(right.bytes, (int64_t)size);
            result |= finish_report(returned, expected);
            if (result) fprintf(stderr, "suite case %" PRIu64 " failed\n", index);
        }
        int a = guard_close(&left), b = guard_close(&right);
        if (a || b) result = 3;
    }
    if (!result && (fgetc(stream) != EOF || ferror(stream))) { fprintf(stderr, "%s: trailing bytes or final read error\n", path); result = 3; }
done:
    if (fclose(stream)) { perror("suite close"); result = 3; }
    return result;
}
int32_t chibi_census_test_lifetime(const void *census_address, const void *read_address,
    const void *length_address, const void *merge_address, const void *sub_address,
    const void *loops_address, const void *report_address) {
    assert(census_address && read_address && length_address && merge_address &&
        sub_address && loops_address && report_address);
    CensusEntry census = (CensusEntry)census_address;
    int64_t (*read_byte)(const uint8_t *, int64_t, int64_t) = (void *)read_address;
    CensusEntry length = (CensusEntry)length_address;
    int64_t (*merge)(const uint8_t *, int64_t, const uint8_t *, int64_t, bool, int64_t) = (void *)merge_address;
    int64_t (*sub)(int64_t, int64_t) = (void *)sub_address;
    int64_t (*loops)(int64_t, int64_t) = (void *)loops_address;
    int64_t (*report)(void) = (void *)report_address;
    int result = 0;
    const int64_t indices[] = {INT64_MIN, -1, 0, 1, 2, 3, 4, INT64_MAX};
    const uint8_t data[] = {0,127,128,255};
    const int64_t answers[] = {-1,-1,0,127,128,255,-1,-1};
    for (int side = 0; side < 2; ++side) {
        Guard g; result = guard_create(&g, sizeof(data), side);
        if (result) return result;
        memcpy(g.bytes, data, sizeof(data));
        result = guard_seal(&g);
        if (!result) {
            result |= length(NULL, 0) != 0 || length(g.bytes, 4) != 4;
            for (unsigned i = 0; i < sizeof(indices)/sizeof(indices[0]); ++i) {
                result |= read_byte(NULL, 0, indices[i]) != -1;
                result |= read_byte(g.bytes, 4, indices[i]) != answers[i];
                result |= merge(NULL, 0, g.bytes, 4, true, indices[i]) != answers[i];
                result |= merge(g.bytes, 4, NULL, 0, false, indices[i]) != answers[i];
                result |= merge(g.bytes, 4, NULL, 0, true, indices[i]) != -1;
                result |= merge(NULL, 0, g.bytes, 4, false, indices[i]) != -1;
            }
        }
        if (guard_close(&g)) result = 3;
        if (result) return result;
    }
    result |= sub(INT64_MIN, 1) != INT64_MAX;
    result |= sub(INT64_MAX, -1) != INT64_MIN;
    result |= sub(0, INT64_MIN) != INT64_MIN;
    result |= sub(9, 4) != 5;
    result |= loops(0, 100) != 0 || loops(100, 0) != 0;
    result |= loops(1, 1) != 0 || loops(3, 4) != 30;
    result |= loops(100, 2) != 10000;
    const int64_t expected[16] = {101,-202,303,-404,505,-606,707,-808,
        909,-1010,1111,-1212,1313,-1414,1515,-1616};
    begin_report();
    int64_t returned = report();
    result |= finish_report(returned, expected);
    if (result) { fprintf(stderr, "capability checks failed\n"); return 1; }
    /* Independently authored CCN1 literal, not an oracle-generated suite case:
     * one package, one replaced original "a\n " and its generated "b\n\nc".
     * Both original/replaced categories have 3 bytes, 2 lines, 1 nonblank;
     * generated has 4 bytes, 3 lines, 2 nonblank. No C recounting occurs.
     * This bounded ownership witness must exercise actual Census, not merely
     * capabilities, after input metadata poisoning and peer destruction. */
    static const uint8_t frame[] = {
        'C','C','N','1', 1,0,0,0, 1,0,0,0, 1,0,0,0,
        2,0,0,0, 1,0,0,0, 3,0,0,0, 'a','\n',' ',
        3,0,0,0, 1,0,0,0, 4,0,0,0, 'b','\n','\n','c'
    };
    static const int64_t frame_expected[16] = {
        0,0,1, 1,3,2,1, 1,3,2,1, 1,4,3,2,47
    };
    _Static_assert(sizeof(frame) == 47, "bounded lifetime Census fixture");
    for (int side = 0; side < 2; ++side) {
        Guard g; result = guard_create(&g, sizeof(frame), side);
        if (result) return result;
        memcpy(g.bytes, frame, sizeof(frame));
        result = guard_seal(&g);
        if (!result) {
            begin_report();
            returned = census(g.bytes, (int64_t)sizeof(frame));
            result = finish_report(returned, frame_expected);
            if (result) fprintf(stderr, "lifetime Census fixture failed\n");
        }
        if (guard_close(&g)) result = 3;
        if (result) return result;
    }
    return 0;
}

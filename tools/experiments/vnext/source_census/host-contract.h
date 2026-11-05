/* PROPOSED ABI only. No host or compiler implementation is supplied. */
#ifndef CHIBI_SOURCE_CENSUS_HOST_CONTRACT_H
#define CHIBI_SOURCE_CENSUS_HOST_CONTRACT_H
#include <stdint.h>

/* Expanded BorrowedBytes entry on the first target, Linux x86-64 System V.
 * length >= 0; NULL is permitted only for zero length. Nonempty bytes must
 * describe actual readable immutable storage, alive through Census return.
 * The host reads one opaque frame file; it does not count or parse its bytes.
 */
int64_t Census(const uint8_t *bytes, int64_t length);

/* One call per Census invocation. Print one report or filename/offset error.
 * Return status unchanged; a printing failure is separately a host I/O error.
 * Census passes only scalars. No payload pointer escapes through this call.
 */
int64_t chibi_census_report(int64_t status, int64_t error_offset, int64_t packages,
    int64_t original_files, int64_t original_bytes,
    int64_t original_lines, int64_t original_nonblank,
    int64_t replaced_files, int64_t replaced_bytes,
    int64_t replaced_lines, int64_t replaced_nonblank,
    int64_t generated_files, int64_t generated_bytes,
    int64_t generated_lines, int64_t generated_nonblank,
    int64_t frame_bytes);
#endif

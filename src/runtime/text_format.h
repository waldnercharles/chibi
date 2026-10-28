#ifndef CHIBI_NATIVE_TEXT_FORMAT_H
#define CHIBI_NATIVE_TEXT_FORMAT_H
#include "execution/host.h"
#include "runtime/text_definition.h"
/* Borrowed inputs, arena-owned text. Format has one fixed input and at most
 * twelve rendered arguments. Invalid modifiers use ordinary value display;
 * valid scalar flags/width/precision are bounded to 255, with physical integer
 * length chosen internally. Caller-authored length/pointer/write modifiers
 * never reach printf. */
pin_value native_format_text(const pin_value *, int, arena *);
pin_value native_text_equal(const pin_value *, int, arena *);
extern const node_host_call NATIVE_FORMAT_TEXT_HOST, NATIVE_FORMAT_TEXT_CALL;
extern const node_host_call NATIVE_TEXT_EQUAL_HOST, NATIVE_TEXT_EQUAL_CALL;
/* Byte operations: length rejects text beyond INT_MAX; byte access returns -1
 * outside the string. Slice clamps start/count and owns its NUL-terminated
 * result in the supplied arena. Byte ranges may split UTF-8 sequences. Each
 * call reads the stored byte length; length and indexing are constant time.
 * text_bytes returns every byte, from 0 to 255, as an int[]
 * in the arena, in one pass. */
extern const node_host_call NATIVE_TEXT_BYTE_LENGTH_CALL,
    NATIVE_TEXT_BYTE_AT_CALL, NATIVE_TEXT_SLICE_CALL, NATIVE_TEXT_BYTES_CALL;
extern const node_access NATIVE_TEXT_ACCESS;
/* Copy every u8 into immutable length-carrying text, without Unicode or NUL
 * conversion. Packed arrays need one copy; boxed arrays use linear scratch. */
extern const node_host_call NATIVE_TEXT_FROM_BYTES_CALL;
#endif

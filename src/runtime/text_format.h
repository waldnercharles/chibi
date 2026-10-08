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
extern const node_access NATIVE_TEXT_ACCESS;
#endif

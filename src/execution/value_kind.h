#ifndef CHIBI_EXECUTION_VALUE_KIND_H
#define CHIBI_EXECUTION_VALUE_KIND_H

/* Executable provenance is independent of physical scalar width. A U64 value
 * is numeric unless its producer explicitly declares an address or reference. */
typedef enum
{
    BEHAVIOR_VALUE,
    BEHAVIOR_ADDRESS,
    BEHAVIOR_REFERENCE,
    /* Immutable invocation-owned UTF-8 snapshot, consumed only by intrinsics. */
    BEHAVIOR_STRING_CURSOR,
    BEHAVIOR_QUERY_CURSOR
} behavior_value_kind;
typedef enum
{
    BEHAVIOR_BANK_SLOT,
    BEHAVIOR_BANK_LOCAL,
    BEHAVIOR_BANK_ARGUMENT,
    BEHAVIOR_BANK_RESULT,
    BEHAVIOR_BANK_DATA,
    BEHAVIOR_BANK_COMMAND_ARGUMENT,
    BEHAVIOR_BANK_COMMAND_RESULT
} behavior_value_bank;
#endif

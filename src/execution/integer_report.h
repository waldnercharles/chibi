#ifndef CHIBI_EXECUTION_INTEGER_REPORT_H
#define CHIBI_EXECUTION_INTEGER_REPORT_H
#include "execution/module.h"

#include <stdbool.h>
#include <stdint.h>

/* A development build reports each integer overflow, zero divisor and clamping
 * conversion once per source site. The program keeps the values a shipped
 * build computes; shipped builds and native code carry no checks. */
typedef enum
{
    EXECUTION_INTEGER_OVERFLOW,
    EXECUTION_INTEGER_ZERO_DIVISOR,
    EXECUTION_INTEGER_CLAMP,
    EXECUTION_INTEGER_REPORT_KINDS
} execution_integer_report_kind;
typedef struct
{
    execution_integer_report_kind kind;
    uint64_t module_id, node_id;
    int start, end, line, column;
    const char *asset_name; /* Borrowed until the module is released; may be NULL. */
} execution_integer_report;

/* Modules published afterwards check their integer operations. A development
 * host enables this once at startup; published modules keep their code. */
void execution_integer_reports_enable(bool enabled);
bool execution_integer_reports_enabled();
/* Appends each site of the module and its imports that reported since its last
 * take. Safe while the module's programs run on other threads. */
void execution_module_take_integer_reports(const execution_module *, execution_integer_report **reports);
/* Lets every site of the module and its imports report again. */
void execution_module_reset_integer_reports(const execution_module *);

#endif

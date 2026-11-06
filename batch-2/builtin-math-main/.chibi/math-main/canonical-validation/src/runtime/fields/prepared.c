#include "runtime/fields/prepared.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert(
    sizeof(int) == 4 && sizeof(float) == 4 && FLT_RADIX == 2 &&
        FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
    "Prepared fields require 32-bit integers and IEEE binary32"
);

static const unsigned char MAGIC[] = { 'C', 'H', 'B', 'I',
                                       'F', 'L', 'D', FIELD_PREPARED_VERSION };
typedef struct
{
    unsigned char *bytes;
    size_t count, capacity;
    char *error;
    size_t size;
} writer;
typedef struct
{
    const unsigned char *bytes;
    size_t length, at;
    char *error;
    size_t size;
    size_t budget, allocated;
} reader;
static bool fail(char *error, size_t size, const char *message)
{
    if (size && !error[0]) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool write_bytes(writer *w, const void *bytes, size_t length)
{
    if (length > FIELD_PREPARED_MAX_BYTES - w->count) {
        return fail(w->error, w->size, "Prepared field exceeds byte limit");
    }
    size_t required = w->count + length;
    if (required > w->capacity) {
        size_t capacity = w->capacity ? w->capacity * 2 : 256;
        if (capacity < required) {
            capacity = required;
        }
        if (capacity > FIELD_PREPARED_MAX_BYTES) {
            capacity = FIELD_PREPARED_MAX_BYTES;
        }
        w->bytes = xrealloc(w->bytes, capacity);
        w->capacity = capacity;
    }
    memcpy(w->bytes + w->count, bytes, length);
    w->count += length;
    return true;
}
static bool write_u32(writer *w, uint32_t value)
{
    unsigned char bytes[4] = { value, value >> 8, value >> 16, value >> 24 };
    return write_bytes(w, bytes, sizeof bytes);
}
static bool write_text(writer *w, const char *text)
{
    if (!text) {
        return write_u32(w, UINT32_MAX);
    }
    size_t length = strlen(text);
    if (length > FIELD_PREPARED_MAX_NAME) {
        return fail(w->error, w->size, "Prepared field name exceeds limit");
    }
    return write_u32(w, (uint32_t)length) && write_bytes(w, text, length);
}
bool field_prepared_write(const field_program *p, unsigned char **bytes, size_t *length, char *error, size_t size)
{
    assert(p && bytes && length && (!size || error));
    *bytes = NULL;
    *length = 0;
    if (size) {
        error[0] = 0;
    }
    writer w = { .error = error, .size = size };
    int count = field_program_instruction_count(p),
        parameters = field_program_parameter_count(p);
    bool valid = write_bytes(&w, MAGIC, sizeof MAGIC) &&
                 write_u32(&w, (uint32_t)count) &&
                 write_u32(&w, (uint32_t)field_program_output(p)) &&
                 write_u32(&w, (uint32_t)parameters);
    for (int i = 0; valid && i < count; i++) {
        field_ir ir = field_program_instruction_at(p, i);
        uint32_t value;
        memcpy(&value, &ir.value, sizeof value);
        valid = write_u32(&w, ir.op) && write_u32(&w, (uint32_t)ir.count) &&
                write_u32(&w, (uint32_t)ir.parameter) && write_u32(&w, value);
        for (int j = 0; valid && j < FIELD_INPUT_MAX; j++) {
            valid = write_u32(&w, (uint32_t)ir.inputs[j]);
        }
    }
    for (int i = 0; valid && i < parameters; i++) {
        field_parameter parameter = field_program_parameter_at(p, i);
        uint32_t value;
        switch (parameter.value.type) {
            case PIN_F32:
                memcpy(&value, &parameter.value.f, sizeof value);
                break;
            case PIN_I32:
                memcpy(&value, &parameter.value.i, sizeof value);
                break;
            case PIN_U32: value = parameter.value.u; break;
            default: assert(false);
        }
        valid = write_text(&w, parameter.name) &&
                write_text(&w, parameter.value.type_name) &&
                write_u32(&w, (uint32_t)parameter.value.type) &&
                write_u32(&w, value);
    }
    if (!valid) {
        free(w.bytes);
        return false;
    }
    *bytes = w.bytes;
    *length = w.count;
    return true;
}
static bool read_u32(reader *r, uint32_t *value)
{
    if (r->length - r->at < 4) {
        return fail(r->error, r->size, "Prepared field record is truncated");
    }
    const unsigned char *p = r->bytes + r->at;
    *value = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
             (uint32_t)p[3] << 24;
    r->at += 4;
    return true;
}
static bool read_text(reader *r, const char **text)
{
    uint32_t length;
    if (!read_u32(r, &length)) {
        return false;
    }
    if (length == UINT32_MAX) {
        *text = NULL;
        return true;
    }
    if (length > FIELD_PREPARED_MAX_NAME || length > r->length - r->at ||
        memchr(r->bytes + r->at, 0, length)) {
        return fail(r->error, r->size, "Prepared field name is invalid");
    }
    if ((size_t)length + 1 > r->budget - r->allocated) {
        return fail(r->error, r->size, "Prepared field decoded storage exceeds budget");
    }
    r->allocated += (size_t)length + 1;
    char *copy = xmalloc((size_t)length + 1);
    memcpy(copy, r->bytes + r->at, length);
    copy[length] = 0;
    r->at += length;
    *text = copy;
    return true;
}
field_program *field_prepared_read_budget(
    const void *bytes,
    size_t length,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(remaining && (!size || error));
    if (size) {
        error[0] = 0;
    }
    if (!bytes || length < sizeof MAGIC || length > FIELD_PREPARED_MAX_BYTES ||
        memcmp(bytes, MAGIC, sizeof MAGIC)) {
        fail(error, size, "Invalid prepared field header");
        return NULL;
    }
    reader r = { .bytes = bytes,
                 .length = length,
                 .at = sizeof MAGIC,
                 .error = error,
                 .size = size,
                 .budget = *remaining };
    uint32_t count, output, parameters;
    if (!read_u32(&r, &count) || !read_u32(&r, &output) ||
        !read_u32(&r, &parameters) || !count || count > FIELD_INSTRUCTION_MAX ||
        output >= count || parameters > FIELD_INSTRUCTION_MAX) {
        fail(error, size, "Invalid prepared field bounds");
        return NULL;
    }
    size_t temporary = (size_t)count * sizeof(field_ir) +
                       (size_t)parameters * sizeof(field_parameter);
    if (temporary > r.budget) {
        fail(error, size, "Prepared field decoded storage exceeds budget");
        return NULL;
    }
    r.allocated = temporary;
    field_ir *code = xcalloc(count, sizeof *code);
    field_parameter *defaults = xcalloc(parameters, sizeof *defaults);
    field_program *program = NULL;
    for (unsigned i = 0; i < count; i++) {
        uint32_t op, inputs, parameter, value;
        if (!read_u32(&r, &op) || !read_u32(&r, &inputs) ||
            !read_u32(&r, &parameter) || !read_u32(&r, &value) ||
            op >= FIELD_OP_COUNT || inputs > FIELD_INPUT_MAX) {
            fail(error, size, "Invalid prepared field instruction");
            goto done;
        }
        code[i].op = (field_op)op;
        code[i].count = (int)inputs;
        memcpy(&code[i].parameter, &parameter, sizeof parameter);
        memcpy(&code[i].value, &value, sizeof value);
        for (int j = 0; j < FIELD_INPUT_MAX; j++) {
            uint32_t operand;
            if (!read_u32(&r, &operand) || operand > INT32_MAX) {
                fail(error, size, "Invalid prepared field operand");
                goto done;
            }
            code[i].inputs[j] = (int)operand;
        }
    }
    for (unsigned i = 0; i < parameters; i++) {
        uint32_t type, value;
        if (!read_text(&r, &defaults[i].name) ||
            !read_text(&r, &defaults[i].value.type_name) ||
            !read_u32(&r, &type) || !read_u32(&r, &value)) {
            goto done;
        }
        defaults[i].value.type = (int)type;
        switch (type) {
            case PIN_F32:
                memcpy(&defaults[i].value.f, &value, sizeof value);
                break;
            case PIN_I32:
                memcpy(&defaults[i].value.i, &value, sizeof value);
                break;
            case PIN_U32: defaults[i].value.u = value; break;
            default:
                fail(error, size, "Invalid prepared field parameter type");
                goto done;
        }
    }
    if (r.at != r.length) {
        fail(error, size, "Prepared field record has trailing bytes");
        goto done;
    }
    program = field_program_new_budget(
        code,
        (int)count,
        (int)output,
        defaults,
        (int)parameters,
        remaining,
        error,
        size
    );
done:
    for (unsigned i = 0; i < parameters; i++) {
        free((char *)defaults[i].name);
        free((char *)defaults[i].value.type_name);
    }
    free(defaults);
    free(code);
    return program;
}
field_program *field_prepared_read(const void *bytes, size_t length, char *error, size_t size)
{
    size_t remaining = FIELD_PREPARED_MAX_BYTES;
    return field_prepared_read_budget(bytes, length, &remaining, error, size);
}

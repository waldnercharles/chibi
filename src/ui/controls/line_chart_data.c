#include "ui/controls/line_chart_data.h"

#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
void line_chart_data_free(line_chart_series *series, int count)
{
    for (int i = 0; i < count; i++) {
        free((void *)series[i].times);
        free((void *)series[i].values);
    }
    free(series);
}
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
bool line_chart_data_parse(const char *json, line_chart_series **out, int *count, char *error, size_t size)
{
    assert(out && count && (!size || error));
    if (size) {
        error[0] = 0;
    }
    if (!json || strnlen(json, 262145) > 262144) {
        return fail(error, size, "Chart data exceeds 256 KiB");
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    if (!yyjson_is_arr(root) || yyjson_arr_size(root) > 8) {
        yyjson_doc_free(doc);
        return fail(error, size, "Expected at most eight chart series");
    }
    int n = (int)yyjson_arr_size(root);
    line_chart_series *series = xcalloc(n ? n : 1, sizeof *series);
    const char *message = NULL;
    for (int i = 0; i < n; i++) {
        yyjson_val *item = yyjson_arr_get(root, (size_t)i);
        yyjson_val *times = yyjson_obj_get(item, "times"),
                   *values = yyjson_obj_get(item, "values");
        if (!yyjson_is_obj(item) || !yyjson_is_arr(times) || !yyjson_is_arr(values) ||
            yyjson_arr_size(times) != yyjson_arr_size(values) ||
            yyjson_arr_size(times) > 2048) {
            message = "Chart times/values must be matching arrays of <=2048 "
                      "samples";
            break;
        }
        int samples = (int)yyjson_arr_size(times);
        float *t = xmalloc((size_t)(samples ? samples : 1) * sizeof *t),
              *v = xmalloc((size_t)(samples ? samples : 1) * sizeof *v);
        series[i] = (line_chart_series){ .times = t, .values = v, .count = samples };
        for (int j = 0; j < samples; j++) {
            yyjson_val *time = yyjson_arr_get(times, (size_t)j),
                       *value = yyjson_arr_get(values, (size_t)j);
            double seconds = yyjson_get_num(time), number = yyjson_get_num(value);
            if (!yyjson_is_num(time) || !yyjson_is_num(value) ||
                !isfinite(seconds) || !isfinite(number) ||
                fabs(seconds) > FLT_MAX || fabs(number) > FLT_MAX) {
                message = "Chart samples must be finite floats";
                break;
            }
            t[j] = (float)seconds;
            v[j] = (float)number;
            if (j && t[j] <= t[j - 1]) {
                message = "Chart times must remain strictly ascending at float "
                          "precision";
                break;
            }
        }
        if (message) {
            break;
        }
        yyjson_val *now = yyjson_obj_get(item, "now"),
                   *color = yyjson_obj_get(item, "color");
        series[i].now = now     ? yyjson_get_num(now)
                      : samples ? t[samples - 1]
                                : 0;
        if ((now && !yyjson_is_num(now)) || !isfinite(series[i].now) ||
            (samples && series[i].now < t[samples - 1])) {
            message = "Chart now must be finite and no earlier than its last "
                      "sample";
            break;
        }
        if (color) {
            double c = yyjson_get_num(color);
            if (!yyjson_is_int(color) || c < 0 || c > UINT32_MAX) {
                message = "Chart color must be an unsigned RGBA32 integer";
                break;
            }
            series[i].color = (uint32_t)c;
        }
    }
    yyjson_doc_free(doc);
    if (message) {
        line_chart_data_free(series, n);
        return fail(error, size, message);
    }
    *out = series;
    *count = n;
    return true;
}

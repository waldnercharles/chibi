#ifndef CHIBI_LINE_CHART_DATA_H
#define CHIBI_LINE_CHART_DATA_H
#include "ui/controls/controls.h"

#include <stddef.h>
/* JSON boundary for authored chart samples: [{times:[seconds],values:[number],
 * now:seconds,color:0xRRGGBBAA-as-decimal}]. <=256KiB,8series,2048samples/series;
 * matching finite float arrays, strictly ascending times. now defaults to last
 * sample, color to zero. Returned array owns malloc samples; failure leaves the
 * caller's old array/count unchanged. Empty [] succeeds and clears a chart. */
bool line_chart_data_parse(const char *, line_chart_series **out, int *count, char *error, size_t);
void line_chart_data_free(line_chart_series *, int count);
#endif

#include "ui/controls/line_chart_data.h"

#include "third_party/pico_headers/pico_unit.h"

#include <string.h>
TEST_CASE(chart_authored_series_copy_timing_values_and_color)
{
    char json[] = "[{\"times\":[0,0.25,0.5],\"values\":[16,8,12],\"color\":"
                  "1872759039}]";
    line_chart_series *series = NULL;
    int count = 0;
    char error[192];
    REQUIRE(line_chart_data_parse(json, &series, &count, error, sizeof error));
    memset(json, 'x', sizeof json - 1);
    REQUIRE(
        count == 1 && series[0].count == 3 && series[0].times[1] == 0.25f &&
        series[0].values[1] == 8 && series[0].now == 0.5 && series[0].color == 1872759039u
    );
    line_chart_data_free(series, count);
    return true;
}
TEST_CASE(chart_invalid_updates_preserve_the_callers_last_valid_series)
{
    line_chart_series *series = NULL;
    int count = 0;
    char error[192];
    REQUIRE(
        line_chart_data_parse("[{\"times\":[1],\"values\":[2]}]", &series, &count, error, sizeof error)
    );
    line_chart_series *previous = series;
    REQUIRE(
        !line_chart_data_parse("[{\"times\":[1,2],\"values\":[2]}]", &series, &count, error, sizeof error)
    );
    REQUIRE(series == previous && count == 1 && series[0].values[0] == 2);
    REQUIRE(
        !line_chart_data_parse("[{\"times\":[2,1],\"values\":[2,3]}]", &series, &count, error, sizeof error)
    );
    REQUIRE(
        !line_chart_data_parse("[{\"times\":[1],\"values\":[2],\"now\":0}]", &series, &count, error, sizeof error)
    );
    REQUIRE(!line_chart_data_parse(
        "[{\"times\":[1],\"values\":[2],\"color\":4294967296}]",
        &series,
        &count,
        error,
        sizeof error
    ));
    REQUIRE(series == previous && count == 1);
    line_chart_data_free(series, count);
    return true;
}
TEST_CASE(chart_empty_and_float_precision_boundaries_are_explicit)
{
    line_chart_series *series = NULL;
    int count = 0;
    char error[192];
    REQUIRE(line_chart_data_parse("[]", &series, &count, error, sizeof error));
    REQUIRE(count == 0);
    line_chart_data_free(series, count);
    series = NULL;
    REQUIRE(!line_chart_data_parse(
        "[{\"times\":[100000000,100000000.25],\"values\":[1,2]}]",
        &series,
        &count,
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "precision"));
    REQUIRE(
        !line_chart_data_parse("[{\"times\":[1],\"values\":[1e100]}]", &series, &count, error, sizeof error)
    );
    return true;
}
void test_tool_line_chart_data()
{
    RUN_TEST_CASE(chart_authored_series_copy_timing_values_and_color);
    RUN_TEST_CASE(chart_invalid_updates_preserve_the_callers_last_valid_series);
    RUN_TEST_CASE(chart_empty_and_float_precision_boundaries_are_explicit);
}

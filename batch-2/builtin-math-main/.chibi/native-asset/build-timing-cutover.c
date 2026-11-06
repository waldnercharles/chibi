#include "host/build_timing.h"
#include <math.h>
#define TEST_CASE(name) static bool name(void)
#define REQUIRE(condition) do { if (!(condition)) { fprintf(stderr, "timing regression at line %d\n", __LINE__); return false; } } while (0)
static int timing_rows(FILE *file, const char *wanted, int wanted_status, int wanted_error)
{
    rewind(file);
    char line[256], category[32];
    int count = 0;
    while (fgets(line, sizeof line, file)) {
        if (!strncmp(line, "category\t", 9)) {
            continue;
        }
        int step, status, error;
        double started, observed;
        if (sscanf(line, "%31s%d%lf%lf%d%d", category, &step, &started, &observed, &status, &error) !=
                6 ||
            !isfinite(started) || !isfinite(observed) || started <= 0 ||
            observed < started || step < 0) {
            return -1;
        }
        count += !strcmp(category, wanted) && status == wanted_status &&
                 error == wanted_error;
    }
    return ferror(file) ? -1 : count;
}
TEST_CASE(build_timings_classify_without_recording_arguments)
{
    char *compile[] = { "/usr/bin/ccache",  "/usr/bin/clang", "-c",
                        "private-source.c", "-DSECRET=value", NULL };
    char *link[] = { "/usr/bin/clang++", "-o", "private-output", NULL };
    char *archive[] = { "/usr/bin/llvm-ar", "rcs", "private-output", NULL };
    char *reflect[] = { "/build/reflect", NULL };
    char *build[] = { "/build/chibi", "--source", "tools/graph/asset_wasm/Build.chibi", NULL };
    REQUIRE(!strcmp(build_timing_category(compile, true), "compile"));
    REQUIRE(!strcmp(build_timing_category(link, true), "link"));
    REQUIRE(!strcmp(build_timing_category(archive, true), "archive"));
    REQUIRE(!strcmp(build_timing_category(reflect, true), "reflection"));
    REQUIRE(!strcmp(build_timing_category(build, true), "build-child"));
    REQUIRE(!strcmp(build_timing_category(compile, false), "probe"));
    FILE *file = tmpfile();
    REQUIRE(file);
    build_timing_write(file, build_timing_category(compile, true), 1, 1, 2, 7, 0);
    REQUIRE(timing_rows(file, "compile", 7, 0) == 1);
    rewind(file);
    char text[256] = { 0 };
    REQUIRE(fread(text, 1, sizeof text - 1, file) > 0);
    REQUIRE(!strstr(text, "private") && !strstr(text, "SECRET") && !strstr(text, "clang"));
    REQUIRE(!fclose(file));
    return true;
}

int main(void) { return !build_timings_classify_without_recording_arguments(); }

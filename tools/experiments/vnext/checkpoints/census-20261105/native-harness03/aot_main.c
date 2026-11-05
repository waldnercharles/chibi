#include "native_adapter.h"
#include "host-contract.h"
extern int64_t ProbeRead(const uint8_t *, int64_t, int64_t);
extern int64_t ProbeLength(const uint8_t *, int64_t);
extern int64_t ProbeMerge(const uint8_t *, int64_t, const uint8_t *, int64_t, bool, int64_t);
extern int64_t ProbeSub(int64_t, int64_t);
extern int64_t ProbeLoops(int64_t, int64_t);
extern int64_t ProbeReport(void);
int main(void) {
    int32_t result = chibi_census_test_lifetime((const void *)Census, (const void *)ProbeRead,
        (const void *)ProbeLength, (const void *)ProbeMerge, (const void *)ProbeSub,
        (const void *)ProbeLoops, (const void *)ProbeReport);
    if (result) return result;
    return chibi_census_test_corpus((const void *)Census);
}

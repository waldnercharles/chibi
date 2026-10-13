#ifndef CHIBI_CTL_EVIDENCE_H
#define CHIBI_CTL_EVIDENCE_H
#include <stdbool.h>
#include <stddef.h>
typedef struct chibi_ctl_evidence chibi_ctl_evidence;
/* NULL on failure. Existing directories are never overwritten. */
chibi_ctl_evidence *chibi_ctl_evidence_begin(
    const char *directory,
    const char *project,
    bool render,
    int argc,
    char **argv
);
bool chibi_ctl_evidence_request(chibi_ctl_evidence *evidence, const char *request, size_t size);
bool chibi_ctl_evidence_response(
    chibi_ctl_evidence *evidence,
    const char *request,
    const char *response,
    int status
);
/* Writes final identity/coverage and releases evidence. */
bool chibi_ctl_evidence_finish(chibi_ctl_evidence *evidence, int status);
#endif

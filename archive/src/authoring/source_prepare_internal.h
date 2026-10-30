#ifndef CHIBI_AUTHORING_SOURCE_PREPARE_INTERNAL_H
#define CHIBI_AUTHORING_SOURCE_PREPARE_INTERNAL_H
#include "authoring/source_prepare.h"

authoring_source *authoring_source_analyze(
    const char *path,
    const char *key,
    const script_syntax *,
    const script_catalog *
);
authoring_source *authoring_source_analyze_requested(
    const char *path,
    const char *key,
    const script_syntax *,
    const script_catalog *,
    const script_specialization_request *,
    int request_count
);
void authoring_source_lower(
    authoring_source *,
    const script_catalog *,
    const script_lower_bindings *,
    behavior_backend
);
authoring_source *authoring_source_retain(authoring_source *);
#endif

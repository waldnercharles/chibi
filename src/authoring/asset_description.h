#ifndef CHIBI_AUTHORING_ASSET_DESCRIPTION_H
#define CHIBI_AUTHORING_ASSET_DESCRIPTION_H

#include "support/asset_value.h"
#include "types/type.h"

/* Portable reflected authoring metadata allocated into doc. Ranges are editing
 * hints, never storage constraints. Enum choices come from the active catalog. */
av_value *authoring_field_describe(av_doc *doc, const type_field *field);
av_value *authoring_type_describe(av_doc *doc, const type_desc *type);

#endif

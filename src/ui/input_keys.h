#pragma once
#include "ui/rml.h"
/* Canonical data-keys name; scratch holds up to F12 plus its terminator.
 * Returns NULL for an unnamed key. Returned storage is borrowed or scratch. */
const char *ui_key_name(rml_key, char scratch[4]);

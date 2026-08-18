/* Reflection entry point. The reflect tool parses this file; it is not
 * compiled. It pulls in the interface (annotated forward decls) and the
 * definitions, so libclang merges them in a single translation unit. */
#include "interface.h"
#include "transform.h"
#include "vec3.h"

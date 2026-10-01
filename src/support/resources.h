#ifndef CHIBI_RESOURCES_H
#define CHIBI_RESOURCES_H

/* Process-lifetime paths. Native packages resolve beside the executable;
 * CHIBI_RESOURCE_DIR overrides their share/chibi directory. */
const char *resource_root();
const char *resource_shaders();
const char *resource_ui();
const char *resource_studio();
const char *resource_studio_content();
/* Development checkout only; NULL in a native package. */
const char *resource_workspace();
/* Owned JSON for --engine-info; NULL with a diagnostic for an invalid package. */
char *resource_info();

#endif

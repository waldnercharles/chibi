#ifndef CHIBI_HOST_APPLICATION_H
#define CHIBI_HOST_APPLICATION_H

/* Runs an ordinary application package's Main with the SDK's application,
 * filesystem, process, project and program services. Every application,
 * including the public CLI, uses the same compiler/session and argument path.
 * All paths are absolute; argv includes the application's program name. */
int chibi_application_run(
    const char *path,
    const char *bin,
    const char *core,
    int argc,
    const char *const *argv
);

#endif

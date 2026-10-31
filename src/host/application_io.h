#ifndef CHIBI_HOST_APPLICATION_IO_H
#define CHIBI_HOST_APPLICATION_IO_H

#include "runtime/native_capabilities.h"

#include <signal.h>

typedef struct execution_program execution_program;
typedef struct pin_array pin_array;
typedef struct application_io application_io;

#define APPLICATION_IO_SERVICE "application.io"

typedef const pin_array *(*application_arguments_fn)(void *, const execution_program *);

/* One process-level application environment. Argument lookup is per program;
 * the environment never assumes which program is the public command. Signal
 * dispositions are restored when it is freed. */
application_io *application_io_new(const char *bin, application_arguments_fn, void *);
void application_io_free(application_io *);
bool application_io_stopped(const application_io *);
/* Controlling-thread host operations may borrow the signal flag while they
 * supervise child processes. Sync afterwards so a child's control signal is
 * also visible to worker-safe application cancellation polling. */
const native_capability_pack *application_io_natives();

#endif

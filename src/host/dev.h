#ifndef CHIBI_HOST_DEV_H
#define CHIBI_HOST_DEV_H

#include <stdbool.h>

typedef struct chibi_dev chibi_dev;

/* Bootstrap replaces this short-lived invocation with the immutable host. */
int chibi_dev_bootstrap(int argc, char **argv, const char *generation);
chibi_dev *chibi_dev_create(const char *config, const char *generation, int jobs);
const char *chibi_dev_initial_application(const chibi_dev *dev);
const char *chibi_dev_checkpoint_path(const chibi_dev *dev);
/* Returns a borrowed candidate path once, after a successful, current build. */
const char *chibi_dev_poll(chibi_dev *dev);
/* Acknowledge the initial application and every returned candidate after load.
 * Only successful loads publish application.current and advance accepted state. */
void chibi_dev_accept(chibi_dev *dev, bool loaded);
void chibi_dev_stop(chibi_dev *dev);
bool chibi_dev_stopped(chibi_dev *dev);
void chibi_dev_free(chibi_dev *dev);

#endif

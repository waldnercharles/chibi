#ifndef BLUEPRINTS_HOST_DEV_H
#define BLUEPRINTS_HOST_DEV_H

#include <stdbool.h>

typedef struct bp_dev bp_dev;

/* Bootstrap replaces this short-lived invocation with the immutable host. */
int bp_dev_bootstrap(int argc, char **argv, const char *generation);
bp_dev *bp_dev_create(const char *config, const char *generation, int jobs);
const char *bp_dev_initial_application(const bp_dev *dev);
const char *bp_dev_checkpoint_path(const bp_dev *dev);
/* Returns a borrowed candidate path once, after a successful, current build. */
const char *bp_dev_poll(bp_dev *dev);
/* Acknowledge the initial application and every returned candidate after load.
 * Only successful loads publish application.current and advance accepted state. */
void bp_dev_accept(bp_dev *dev, bool loaded);
void bp_dev_stop(bp_dev *dev);
bool bp_dev_stopped(bp_dev *dev);
void bp_dev_free(bp_dev *dev);

#endif

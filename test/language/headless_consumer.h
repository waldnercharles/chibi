#ifndef CHIBI_TEST_HEADLESS_CONSUMER_H
#define CHIBI_TEST_HEADLESS_CONSUMER_H
#include "ecs/ecs.h"
#include "execution/module.h"

const ecs_component_desc *headless_consumer_component();
/* Consumes one published module reference, including failure cleanup. */
bool headless_consumer_run(execution_module *module);
execution_module *headless_consumer_read(const void *, size_t length, behavior_backend, uint64_t print_abi);
void headless_consumer_cleanup();
#endif

#pragma once
#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "runtime/fields/program.h"

field_program *field_program_compile(const behavior_asset *asset, char *error, size_t size);
extern const registry_pack FIELD_NODE_PACK;

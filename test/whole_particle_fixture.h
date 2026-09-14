#ifndef TEST_WHOLE_PARTICLE_FIXTURE_H
#define TEST_WHOLE_PARTICLE_FIXTURE_H
#include "graph/model/behavior_asset.h"
#include "render/particles.h"
behavior_asset *whole_particle_fixture(bool directed, bool waits, bool random);
void whole_particle_smoke();
void test_compiled_particles(const char *path);
void emit_particle_c(const char *path);
int bench_particles(const char *path, int count, int rotation);
#endif

#ifndef CHIBI_TEST_SUITE_INTERNAL_H
#define CHIBI_TEST_SUITE_INTERNAL_H

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "ecs/ecs.h"
#include "editor/graph/flow_visual.h"
#include "editor/graph/preview_gate.h"
#include "editor/workspace/editor.h"
#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/model/graph.h"
#include "graph/model/pin_conv.h"
#include "graph/model/pin_fmt.h"
#include "graph/model/registry.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/fmt_nodes.h"
#include "graph/nodes/operator.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "host/stacktrace.h"
#include "platform/cute.h"
#include "runtime/behaviors/behavior.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "runtime/scenes/scene.h"
#include "support/arena.h"
#include "third_party/fmt/fmt.h"
#include "third_party/pico_headers/pico_unit.h"

#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void test_encounter_backend_parity();
extern node_desc g_test_add_desc;
void test_compiled_behaviors();
void test_behavior_batches();
void test_tick_sequences();
void emit_sequence_c(const char *path);
int bench_sequences(const char *path, int count, int frames, int rotation);
void emit_batch_c(const char *path);
int bench_batch(const char *path, int count, int frames, int rotation);
void test_compiled_components();
void test_compiled_loops();
void test_compiled_conversions();
void test_compiled_aliases();
void test_compiled_stores();
void test_managed_reuse();
void test_compiled_arrays();
void test_compiled_queries();
void test_compiled_commands();
void test_compiled_suspensions();
void test_compiled_delays();
void test_compiled_recursion(const char *library);
void emit_recursion_c(const char *path);
int whole_recursion_audit();
void test_compiled_async(const char *path);
void emit_async_c(const char *path);
int bench_async(const char *path, int count, int rotation);
void test_delay_entries(const char *path);
void emit_delay_c(const char *path);
int bench_delay(const char *path, int count, int rotation);
void test_suspended_commands(void *library);
void emit_suspended_commands(FILE *out);
void emit_suspend_c(const char *path);
void test_generated_suspensions(const char *path);
int bench_suspend(const char *path, int count, int rotation);
void test_compiled_entity_commands();
void emit_command_c(const char *path);
int bench_command(const char *path, int count, int rotation);
void emit_query_c(const char *path);
int bench_query(const char *path, int count, int rotation);
void emit_array_c(const char *path);
int bench_array(const char *path, int count, int rotation);
void emit_store_c(const char *path);
int bench_store(const char *path, int count, int rotation);
void emit_alias_c(const char *path);
int bench_alias(const char *path, int count, int rotation);
void emit_loop_c(const char *path);
int bench_loop(const char *path, int count, int rotation, int length);
void emit_component_c(const char *path);
int bench_component(const char *path, int count, int rotation);
behavior_asset *whole_component_fixture(int mode);
void test_compiled_math();
void test_compiled_motion(const char *path);
void emit_motion_c(const char *path);
void emit_data_c(const char *path);
void emit_nearest_c(const char *path);
int bench_nearest(const char *path, int count, int rotation);
void test_compiled_data(const char *path);
void test_compiled_material(const char *path);
void emit_material_c(const char *path);
void emit_material_bench_c(FILE *out);
int bench_material(const char *path, int count, int rotation);
int bench_motion(const char *path, int count, int rotation);
void test_compiled_aggregates();
void test_compiled_integers();
void test_behavior_unavailable();
void test_editor_checkpoint();
void test_check(int ok, const char *message);
extern void test_host_wait();
extern void test_combat();
extern void test_targeting();
extern void test_pursuit();
extern void test_vector();
extern void test_arena();
extern void test_number_serialization();
extern void test_auto_weapon();
extern void test_graph_navigation();
extern void test_extract();
extern void test_shader_graph();
extern void test_typed_events();
extern void test_component_conversion();
extern void test_field_outputs();
extern void test_function_wait();
extern void test_function_fields();
extern void test_flags();
extern void test_particles();
extern void test_labels();
extern bool test_ui_keyboard();
extern bool test_ui_scroll_smoothing();
extern bool test_gameplay_keyboard();
extern void test_math_expression();
extern void test_behavior_math();
extern void test_appearance();
extern void test_retained_components();
extern void test_editor_flow();
extern void test_component_fields();
extern void test_scene_navigation();
extern void test_scene_debug();
extern void test_appearance_geometry();
extern void test_orientation();
extern void test_movement_input();
extern void test_movement_trail();
extern void test_sdf_motion();
extern void test_material();
extern void test_wave();
extern void test_scene_history();
extern void test_enemy_attack();
extern void test_entities();
void test_compiled_spawns();
extern void test_projectile();
extern void test_system_host_wait();
void run_editor_command_checks(void);
void fmt_check_stream(fmt_stream *ss, const char *expected, const char *msg);
void flow_log_reset(void);
void print_sink(const node *origin, const char *message, void *user);
node *make_string(graph *g, const char *value);
node *make_const(graph *g, float v);
extern node_desc g_const_desc;
extern int g_count;
extern node_desc g_counter_desc;
extern node_desc g_all_types_desc;
extern char g_flow_log[128];
extern int g_flow_log_count;
extern int g_flow_indices[128];
extern int g_flow_index_count;
extern node_desc g_sink_a_desc;
extern node_desc g_sink_b_desc;
extern node_desc g_sink_c_desc;
extern node_desc g_sink_done_desc;
extern node_desc g_sink_body_desc;
extern node_desc g_sink_error_desc;
extern int g_parameter_integer;
extern int g_parameter_flag;
extern char g_parameter_text[32];
extern node_desc g_parameter_sink_desc;
extern char g_print_messages[8][64];
extern const node *g_print_origins[8];
extern int g_print_count;
extern flow_run *g_pause_target;
extern CF_V3 g_watch_struct;
extern node_desc g_watch_desc;
extern int g_captured_int;
extern char g_captured_str[64];
extern uint64_t g_captured_handle;
extern node_desc g_capture_desc;
extern node_desc g_owned_watch_desc;
extern int g_foreach_values[8];
extern int g_foreach_count;
extern node_desc g_foreach_capture_desc;
extern node_desc g_str_capture_desc;
extern node_desc g_handle_capture_desc;
void run_ecs_checks(void);
void run_ecs_growth_checks();
void run_component_read_checks();
void run_prototype_checks(void);
behavior_asset *make_behavior_behavior_asset(const char *name, int latent);
void run_scene_document_checks(void);
void run_behavior_checks(void);
behavior_asset *make_live_behavior_asset(void);
void run_live_preview_checks(void);
void run_prototype_live_checks();
void run_live_checks(void);
void run_engine_checks(void);
void run_parallel_checks(void);
void run_ecs_migration_checks(void);
void run_scene_checks(void);
void run_graph_model_checks();
void run_graph_document_checks();
void run_runtime_execution_checks();
void run_runtime_instance_checks();
void run_graph_compile_checks();
void run_graph_conversion_checks();
void run_graph_functions_checks();
void run_runtime_arrays_checks();
void run_runtime_catalog_checks();
void run_support_format_checks();
void run_content_catalog_checks();

#endif

void test_parallel_ticks();

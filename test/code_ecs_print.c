#include "assets/code_print.h"
#include "assets/code_program_internal.h"
#include "assets/code_source_internal.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int, const char *);

static void diagnostics(const code_source *source, const char *text)
{
    if (source->asset) {
        return;
    }
    fprintf(stderr, "Canonical ECS source:\n%s\n", text);
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const code_source_diagnostic *d = &source->diagnostics[i];
        int length = (int)strlen(source->text);
        int start = d->start >= 0 && d->start <= length ? d->start : 0;
        int end = d->end >= start && d->end <= length ? d->end : start;
        fprintf(
            stderr,
            "%s:%d:%d [%d..%d] %s; source: %.*s\n",
            "Canonical ECS",
            d->line,
            d->column,
            d->start,
            d->end,
            d->message,
            end - start,
            source->text + start
        );
    }
}

static void behavior_roundtrip(const char *text, const char *expected)
{
    code_source *source = code_source_parse(text);
    diagnostics(source, text);
    char error[256];
    char *printed = source->asset
                      ? code_print_behavior(source->asset, error, sizeof error)
                      : NULL;
    test_check(printed && strstr(printed, expected), "Native canonical printing reconstructs concise authored statements");
    if (!printed || !strstr(printed, expected)) {
        fprintf(stderr, "Expected ECS statement: %s\nPrinted:\n%s\n", expected, printed ? printed : error);
        for (int i = 0; i < acount(source->asset->graph->nodes); i++) {
            const node *n = source->asset->graph->nodes[i];
            fprintf(
                stderr,
                "Node %llu %s presentation=%d\n",
                (unsigned long long)n->id,
                n->desc->name,
                n->presentation
            );
            for (int j = 0; j < n->input_count; j++) {
                fprintf(
                    stderr,
                    " input %s kind=%d\n",
                    n->input_pins[j].name,
                    n->input_pins[j].kind
                );
            }
            for (int j = 0; j < n->output_count; j++) {
                fprintf(
                    stderr,
                    " output %s kind=%d field=%llu\n",
                    n->output_pins[j].name,
                    n->output_pins[j].kind,
                    (unsigned long long)n->output_pins[j].field_source
                );
            }
        }
        for (int i = 0; i < acount(source->asset->graph->edges); i++) {
            const edge *e = &source->asset->graph->edges[i];
            fprintf(
                stderr,
                " edge %llu -> %llu kind=%d\n",
                (unsigned long long)e->from_id,
                (unsigned long long)e->to_id,
                e->kind
            );
        }
    }
    test_check(
        printed && !strstr(printed, "owner ") && !strstr(printed, "@node"),
        "Canonical behaviors infer entity ownership without source annotations"
    );
    code_source *copy = printed ? code_source_parse(printed) : NULL;
    if (copy) {
        diagnostics(copy, printed);
    }
    behavior_asset *compiled = copy ? code_source_compile_behavior(copy) : NULL;
    test_check(compiled != NULL, "Canonical ECS statements reopen as executable source");
    behavior_asset_free(compiled);
    code_source_free(copy);
    free(printed);
    code_source_free(source);
}

static void system_roundtrip(const char *text, const char *expected)
{
    code_source *source = code_source_parse(text);
    diagnostics(source, text);
    char error[256], *metadata = NULL;
    const code_program *program = source->program;
    char *printed = source->asset ? code_print_system_with_metadata(
                                        source->asset,
                                        program->query,
                                        &metadata,
                                        error,
                                        sizeof error
                                    )
                                  : NULL;
    test_check(printed && strstr(printed, expected) && !strstr(printed, "on Tick"), "System canonical printing preserves its component header and implicit Tick body");
    code_source *copy = printed ? code_source_parse(printed) : NULL;
    if (copy) {
        diagnostics(copy, printed);
    }
    behavior_asset *compiled = copy ? code_source_compile_behavior(copy) : NULL;
    test_check(
        compiled && acount(compiled->component_parameters) ==
                        acount(source->asset->component_parameters),
        "System parameters and optionality survive native graph printing"
    );
    if (compiled) {
        for (int i = 0; i < acount(compiled->component_parameters); i++) {
            const behavior_component_parameter *a = &source->asset->component_parameters[i];
            const behavior_component_parameter *b = &compiled->component_parameters[i];
            test_check(!strcmp(a->name, b->name) && !strcmp(a->key, b->key) && a->optional == b->optional, "System aliases retain their reflected type and optional contract");
        }
    }
    test_check(metadata != NULL, "System formatting exports anchors at emitted source positions");
    free(metadata);
    behavior_asset_free(compiled);
    code_source_free(copy);
    free(printed);
    code_source_free(source);
}

void run_code_ecs_print_checks()
{
    behavior_roundtrip(
        "behavior Effects { on Begin { self.ParticleEmitter.Active = true; } }",
        "self.ParticleEmitter.Active = true;"
    );
    behavior_roundtrip(
        "behavior Linked { state pressed := false; on Begin { pressed = "
        "self.ParticleEmitter.Parent.ActionBinding.Pressed; } }",
        "self.ParticleEmitter.Parent.ActionBinding.Pressed"
    );
    behavior_roundtrip(
        "behavior Capture { state original := 1; state captured := 0; on Begin "
        "{ saved := original; original = 2; captured = saved; } }",
        "saved := original;"
    );
    behavior_roundtrip("behavior Time { state elapsed := 0; on Tick(dt) { elapsed += dt; } }", "on Tick(dt)");
    behavior_roundtrip("behavior Clock { state elapsed := 0; on Tick { elapsed += &delta; } }", "&delta");
    system_roundtrip("system Empty {}", "system Empty {");
    system_roundtrip("system Empty { shared count := 0; count += dt; }", "system Empty {");
    system_roundtrip("system Motion(position: Transform, velocity: Velocity, damping: Igniter?) without ActionBinding { position += velocity * dt; if damping { damping.heat = 0; } }", "system Motion(position: Transform, velocity: Velocity, damping: Igniter?) without ActionBinding");
}

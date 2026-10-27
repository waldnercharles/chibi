#include "authoring/package_tests.h"

#include "execution/function.h"
#include "execution/module.h"
#include "execution/status.h"
#include "runtime/tasks.h"
#include "support/arena.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char PROJECT[] = "project:";

static const behavior_function *body(const execution_module *module, uint64_t id)
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.id == id) {
            return info.body;
        }
    }
    return NULL;
}

static int by_position(const void *a, const void *b)
{
    const package_test *left = a, *right = b;
    int order = strcmp(left->path, right->path);
    if (order) {
        return order;
    }
    return left->line != right->line ? left->line - right->line
                                     : left->column - right->column;
}

static void add_tests(package_test **tests, const authoring_package *package)
{
    const script_analysis *analysis = package->analysis;
    for (int i = 0; i < analysis->function_count; i++) {
        if (!analysis->functions[i].test) {
            continue;
        }
        const script_symbol *symbol = &analysis->symbols[analysis->functions[i].symbol];
        const script_range *at = &symbol->declaration;
        package_test test = { analysis->name,
                              symbol->name,
                              analysis->documents[at->document].path,
                              at->line,
                              at->column,
                              body(package->module, symbol->id),
                              analysis->functions[i].pure &&
                                  !analysis->functions[i].effects };
        assert(test.body);
        test.parallel = test.parallel && behavior_function_hosts_pure(test.body);
        apush(*tests, test);
    }
}

package_test *package_tests_find(const authoring_packages *packages)
{
    assert(authoring_packages_valid(packages));
    package_test *tests = NULL;
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (!strncmp(package->identity, PROJECT, strlen(PROJECT))) {
            add_tests(&tests, package);
        }
    }
    if (tests) {
        qsort(tests, (size_t)acount(tests), sizeof *tests, by_position);
    }
    return tests;
}

/* The innermost failing call reports first; its callers report after it. */
static void observe(const behavior_function *function, const behavior_function_context *context, int)
{
    package_test_failure *failure = context->inspection_user;
    if (failure->message[0]) {
        return;
    }
    if (context->error_source >= 0 &&
        context->error_source < behavior_function_source_count(function)) {
        const behavior_function_source
            *source = behavior_function_source_at(function, context->error_source);
        failure->path = source->asset_name;
        failure->line = source->line;
        failure->column = source->column;
    }
    snprintf(
        failure->message,
        sizeof failure->message,
        "%s",
        context->error_message ? context->error_message
                               : behavior_function_failure_text(context->failure)
    );
}

/* A test runs alone: nothing resumes a wait it begins or runs a task it
 * starts, so either fails it instead. */
static int refuse_wait(behavior_function_context *context, int)
{
    context->error_message = "a test must finish without waiting";
    return FLOW_ERR_INVALID_GRAPH;
}
static int refuse_task(behavior_function_context *context, int)
{
    package_test_failure *failure = context->inspection_user;
    snprintf(failure->message, sizeof failure->message, "a test cannot start a task");
    return FLOW_ERR_INVALID_GRAPH;
}

bool package_test_run(
    const package_test *test,
    behavior_backend backend,
    uintptr_t budget,
    package_test_failure *failure
)
{
    *failure = (package_test_failure){ 0 };
    behavior_function_activation activation = { 0 };
    bool resumable = behavior_function_resumable(test->body);
    if (resumable) {
        behavior_function_activation_init(test->body, &activation);
    }
    /* Each run owns its heap, so parallel tests never share one. */
    behavior_managed_heap managed;
    behavior_managed_heap_init(&managed);
    behavior_function_context context = { .managed_heap = &managed,
                                          .activation = resumable ? &activation : NULL,
                                          .budget = budget,
                                          .inspection_user = failure,
                                          .failure_observer = observe,
                                          .begin_command = refuse_wait,
                                          .start_task = refuse_task };
    arena storage = { 0 };
    behavior_function_bind_storage(&context, &storage);
    /* Without services nothing can be missing from them. */
    bool bound = behavior_function_bind(test->body, &context, NULL, 0);
    assert(bound);
    (void)bound;
    int status = behavior_function_run(test->body, &context, backend != BEHAVIOR_BYTECODE);
    /* Refused waits and tasks leave nothing to suspend. */
    assert(status <= 0);
    bool passed = status == FLOW_OK && context.returned;
    if (!passed && !failure->message[0]) {
        snprintf(
            failure->message,
            sizeof failure->message,
            "%s",
            behavior_function_failure_text(context.failure)
        );
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
    return passed;
}

typedef struct
{
    const package_test *tests;
    package_test_result *results;
    behavior_backend backend;
    uintptr_t budget;
} test_span;

static void run_span(int first, int end, uint32_t, void *user)
{
    test_span *span = user;
    for (int i = first; i < end; i++) {
        package_test_result *result = &span->results[i];
        result->passed = package_test_run(
            &span->tests[i],
            span->backend,
            span->budget,
            &result->failure
        );
    }
}

void package_tests_run(
    const package_test *tests,
    int count,
    behavior_backend backend,
    uintptr_t budget,
    const task_system *tasks,
    package_test_result *results
)
{
    assert(count >= 0 && (!count || (tests && results)));
    if (behavior_execution_scope_active()) {
        tasks = NULL;
    }
    for (int first = 0; first < count;) {
        int end = first + 1;
        while (tests[first].parallel && end < count && tests[end].parallel) {
            end++;
        }
        test_span span = { tests + first, results + first, backend, budget };
        if (tasks && tasks->workers > 1 && end - first > 1) {
            void *task = tasks->enqueue(run_span, end - first, 1, &span, tasks->user);
            tasks->finish(task, tasks->user);
        } else {
            run_span(0, end - first, 0, &span);
        }
        first = end;
    }
}

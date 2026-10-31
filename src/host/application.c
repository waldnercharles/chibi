#include "host/application.h"

#include "authoring/program_service.h"
#include "execution/log.h"
#include "execution/status.h"
#include "host/application_io.h"
#include "host/application_tools.h"
#include "host/native_default.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    task_system tasks;
    int workers;
} application_tests;

typedef struct
{
    task_pool *pool;
    void *task;
} application_test_job;

static void *enqueue(task_range *run, int count, int minimum, void *context, void *user)
{
    application_tests *tests = user;
    task_pool *pool = task_pool_acquire_workers(tests->workers);
    task_pool_begin(pool, true);
    const task_system *tasks = task_pool_tasks(pool);
    void *task = tasks->enqueue(run, count, minimum, context, tasks->user);
    if (!task) {
        task_pool_end(pool);
        task_pool_release(pool);
        return NULL;
    }
    application_test_job *job = xmalloc(sizeof *job);
    *job = (application_test_job){ pool, task };
    return job;
}

static void finish(void *task, void *)
{
    if (!task) {
        return;
    }
    application_test_job *job = task;
    const task_system *tasks = task_pool_tasks(job->pool);
    tasks->finish(job->task, tasks->user);
    task_pool_end(job->pool);
    task_pool_release(job->pool);
    free(job);
}

static void test_executor(application_tests *tests)
{
    const char *setting = getenv("CHIBI_TEST_WORKERS");
    /* The test operation reports invalid settings. Other commands still run. */
    if (setting && *setting && strspn(setting, "0123456789") == strlen(setting)) {
        char *end;
        long workers = strtol(setting, &end, 10);
        if (!*end && workers >= 1 && workers <= TASK_WORKER_MAX) {
            tests->workers = (int)workers;
        }
    }
    task_pool *pool = task_pool_acquire_workers(tests->workers);
    tests->tasks = (task_system){ .workers = task_pool_tasks(pool)->workers,
                                  .user = tests,
                                  .enqueue = enqueue,
                                  .finish = finish };
    task_pool_release(pool);
}

static const pin_array *arguments(void *service, const execution_program *program)
{
    return program_service_arguments(service, program);
}

static bool cancelled(void *service)
{
    return application_io_stopped(service);
}

static void print(void *, const execution_log_message *message)
{
    fwrite(message->text, 1, message->text_length, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

int chibi_application_run(
    const char *path,
    const char *bin,
    const char *core,
    int argc,
    const char *const *argv
)
{
    program_service *programs = program_service_new(core);
    application_io *io = application_io_new(bin, arguments, programs);
    size_t size = strlen(bin) + sizeof "/../share/chibi";
    char *share = xmalloc(size);
    snprintf(share, size, "%s/../share/chibi", bin);
    application_tools tools = { .bin = bin, .core = core, .share = share, .io = io };
    const native_capability_pack packs[] = { *application_io_natives(),
                                             *application_tools_natives(),
                                             *program_service_pack() };
    char error[512];
    native_capabilities *native = host_native_standalone_catalog_new(packs, 3, NULL, 0, error, sizeof error);
    execution_log_service log = { .emit = print };
    application_tests tests = { 0 };
    test_executor(&tests);
    int status = 1;
    if (!native) {
        fprintf(stderr, "Cannot prepare application capabilities: %s\n", error);
    } else {
        service_binding *services = NULL;
        apush(services, ((service_binding){ APPLICATION_IO_SERVICE, io }));
        apush(services, ((service_binding){ APPLICATION_TOOLS_SERVICE, &tools }));
        apush(services, ((service_binding){ PROGRAM_SERVICE, programs }));
        apush(services, ((service_binding){ EXECUTION_LOG_SERVICE, &log }));
        authoring_program_options options = {
            .native = native,
            .backend = behavior_default_backend(),
            .services = services,
            .cancelled = cancelled,
            .cancel_user = io,
            .test_tasks = &tests.tasks,
        };
        program_service_configure(programs, &options);
        afree(services);
        uint64_t app = program_service_open(programs, path, "Main", argv, argc);
        if (program_service_update(programs, app) != 1) {
            fprintf(stderr, "%s\n", program_service_diagnostic(programs, app));
        } else {
            int result = 0;
            const authoring_program_call_options call = { .cooperative = true };
            int flow = program_service_call(programs, app, &call, &result);
            if (flow != FLOW_OK) {
                fprintf(stderr, "%s\n", program_service_diagnostic(programs, app));
            } else if (result < 0 || result > 255) {
                fprintf(stderr, "Main returned an exit status outside 0..255\n");
            } else {
                status = result;
            }
        }
    }
    program_service_free(programs);
    application_io_free(io);
    application_tools_free(&tools);
    native_capabilities_free(native);
    free(share);
    return status;
}

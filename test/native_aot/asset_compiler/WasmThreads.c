#include "authoring/asset_compiler_host.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *request;
    size_t count;
    pthread_barrier_t compiling;
} requests;

typedef struct
{
    pthread_barrier_t *compiling;
    unsigned calls;
} cancellation;

static bool cancel_together(void *user)
{
    cancellation *cancel = user;
    if (++cancel->calls != 16) {
        return false;
    }
    /* Both threads hold a live compiler context when cancellation unwinds. */
    int result = pthread_barrier_wait(cancel->compiling);
    assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    return true;
}

static void balanced(void)
{
#ifdef CHIBI_ASSET_COMPILER_WASM_TEST
    extern size_t asset_compiler_wasm_test_allocations(void);
    assert(asset_compiler_wasm_test_allocations() == 0);
#endif
}

static void *worker(void *user)
{
    requests *input = user;
    asset_compiler_diagnostic diagnostic;
    size_t length = 0;
    unsigned char *reference = asset_compiler_compile_closure(
        input->request,
        input->count,
        &length,
        NULL,
        &diagnostic
    );
    assert(reference && length == 329);
    balanced();
    for (unsigned round = 0; round < 4; round++) {
        asset_compiler_options options = { .compile_budget = 1 };
        unsigned char *bytes = asset_compiler_compile_closure(
            input->request,
            input->count,
            &length,
            &options,
            &diagnostic
        );
        assert(
            !bytes && !length &&
            !strcmp(diagnostic.message, "The package compile exceeded its step budget")
        );
        balanced();
        cancellation cancel = { .compiling = &input->compiling };
        options = (asset_compiler_options){ .cancelled = cancel_together,
                                            .user = &cancel };
        bytes = asset_compiler_compile_closure(input->request, input->count, &length, &options, &diagnostic);
        assert(!bytes && !length && !strcmp(diagnostic.message, "The package compile was cancelled"));
        balanced();
        bytes = asset_compiler_compile_closure(input->request, input->count, &length, NULL, &diagnostic);
        assert(bytes && length == 329 && !memcmp(bytes, reference, length));
        balanced();
        free(bytes);
    }
    free(reference);
    return NULL;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *input = fopen(argv[1], "rb");
    assert(input);
    int status = fseek(input, 0, SEEK_END);
    assert(!status);
    long count = ftell(input);
    assert(count >= 0 && (unsigned long)count <= ASSET_COMPILER_MAX_REQUEST_BYTES);
    status = fseek(input, 0, SEEK_SET);
    assert(!status);
    char *request = malloc((size_t)count ? (size_t)count : 1);
    assert(request);
    size_t read = fread(request, 1, (size_t)count, input);
    assert(read == (size_t)count);
    status = fclose(input);
    assert(!status);
    requests calls = { .request = request, .count = (size_t)count };
    status = pthread_barrier_init(&calls.compiling, NULL, 2);
    assert(!status);
    pthread_t threads[2];
    for (unsigned i = 0; i < 2; i++) {
        status = pthread_create(&threads[i], NULL, worker, &calls);
        assert(!status);
    }
    for (unsigned i = 0; i < 2; i++) {
        status = pthread_join(threads[i], NULL);
        assert(!status);
    }
    status = pthread_barrier_destroy(&calls.compiling);
    assert(!status);
    free(request);
    puts("two Wasm threads recover from simultaneous cancellation with balanced allocations");
    return 0;
}

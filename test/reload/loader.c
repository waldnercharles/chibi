#include "src/host/module.h"
#include "src/host/state_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void expect(chibi_module *module, const char *value)
{
    char *checkpoint = module->api->checkpoint(module->state);
    assert(checkpoint && !strcmp(checkpoint, value));
    free(checkpoint);
}
int main(int argc, char **argv)
{
    assert(argc == 7);
    chibi_module module = { 0 };
    chibi_host host = { argc, argv, malloc, free };
    char error[1024];
    assert(chibi_module_replace(&module, argv[1], &host, NULL, error, sizeof error));
    module.api->frame(module.state);
    expect(&module, "1");
    void *state = module.state;
    assert(!chibi_module_replace(&module, "/does/not/exist", &host, NULL, error, sizeof error));
    assert(module.state == state);
    assert(!chibi_module_replace(&module, argv[3], &host, NULL, error, sizeof error));
    assert(!chibi_module_replace(&module, argv[4], &host, NULL, error, sizeof error));
    assert(module.state == state);
    const chibi_application_api *api = module.api;
    void *library = module.library;
    assert(!chibi_module_replace(&module, argv[5], &host, NULL, error, sizeof error));
    assert(module.state == state && module.api == api && module.library == library);
    char corrupt[] = "/tmp/chibi-corrupt-XXXXXX";
    int corrupt_fd = mkstemp(corrupt);
    assert(corrupt_fd >= 0);
    assert(write(corrupt_fd, "not a library", 13) == 13);
    close(corrupt_fd);
    assert(!chibi_module_replace(&module, corrupt, &host, NULL, error, sizeof error));
    assert(module.state == state && module.api == api && module.library == library);
    unlink(corrupt);
    module.api->frame(module.state);
    expect(&module, "2");
    assert(chibi_module_replace(&module, argv[2], &host, NULL, error, sizeof error));
    module.api->frame(module.state);
    expect(&module, "4");
    for (int i = 0; i < 20; i++) {
        assert(chibi_module_replace(&module, argv[1 + i % 2], &host, NULL, error, sizeof error));
        module.api->frame(module.state);
    }
    expect(&module, "34");
    char path[] = "/tmp/chibi-checkpoint-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    char *checkpoint = module.api->checkpoint(module.state);
    assert(chibi_write_file(path, checkpoint));
    free(checkpoint);
    chibi_module_close(&module);
    checkpoint = chibi_read_file(path);
    assert(checkpoint && !strcmp(checkpoint, "34"));
    assert(chibi_module_replace(&module, argv[2], &host, checkpoint, error, sizeof error));
    free(checkpoint);
    module.api->frame(module.state);
    expect(&module, "36");
    chibi_module_close(&module);
    unlink(path);
    assert(chibi_module_replace(&module, argv[6], &host, NULL, error, sizeof error));
    module.api->frame(module.state);
    state = module.state;
    api = module.api;
    library = module.library;
    assert(!chibi_module_replace(&module, argv[2], &host, NULL, error, sizeof error));
    assert(!strcmp(error, "could not checkpoint active workspace"));
    assert(module.state == state && module.api == api && module.library == library);
    module.api->frame(module.state);
    expect(&module, "2");
    chibi_module_close(&module);
    puts("PASS: real library replacement, rejection, repeated generations and checkpoint restart");
}

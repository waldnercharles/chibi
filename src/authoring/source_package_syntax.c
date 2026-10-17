#include "authoring/source_package_syntax.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool import_node(TSNode node)
{
    return !strcmp(ts_node_type(node), "import_declaration");
}
static char *copy_name(const script_syntax *syntax, TSNode node)
{
    script_range range = script_node_range(node);
    size_t size = (size_t)(range.end - range.start);
    char *name = xmalloc(size + 1);
    memcpy(name, syntax->text + range.start, size);
    name[size] = 0;
    return name;
}
void source_package_syntax_free(source_package_syntax *source)
{
    if (!source) {
        return;
    }
    for (int i = 0; i < source->import_count; i++) {
        free((char *)source->imports[i].alias);
    }
    free((char *)source->file.name);
    free(source->imports);
    free(source->locations);
    free(source);
}
source_package_syntax *source_package_syntax_new(
    const script_syntax *syntax,
    const char *path,
    const char *directory,
    int file,
    script_diagnostic *error
)
{
    assert(syntax && path && directory && file >= 0 && error);
    *error = (script_diagnostic){ 0 };
    if (syntax->diagnostic_count) {
        *error = syntax->diagnostics[0];
        return NULL;
    }
    TSNode root = ts_tree_root_node(syntax->tree);
    TSNode header = field(root, "package");
    if (ts_node_is_null(header)) {
        error->range = script_node_range(root);
        snprintf(error->message, sizeof error->message, "Expected package header");
        return NULL;
    }
    TSNode name = field(header, "name");
    assert(!ts_node_is_null(name));
    source_package_syntax *source = xcalloc(1, sizeof *source);
    source->file = (source_package_file){ path,
                                          directory,
                                          copy_name(syntax, name),
                                          script_node_range(name) };
    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        source->import_count += import_node(ts_node_named_child(root, i));
    }
    if (!source->import_count) {
        return source;
    }
    source->imports = xcalloc((size_t)source->import_count, sizeof *source->imports);
    source->locations = xcalloc((size_t)source->import_count, sizeof *source->locations);
    int at = 0;
    for (uint32_t i = 0; i < count; i++) {
        TSNode node = ts_node_named_child(root, i);
        if (!import_node(node)) {
            continue;
        }
        TSNode path_node = field(node, "path"), alias = field(node, "alias");
        assert(!ts_node_is_null(path_node));
        script_range path_range = script_node_range(path_node);
        script_range range = script_node_range(node);
        range.end = path_range.end;
        source->imports[at] = (source_package_import){
            .file = file,
            .alias = ts_node_is_null(alias) ? NULL : copy_name(syntax, alias),
            .range = range
        };
        source->locations[at] = (source_package_import_syntax){
            .path = path_range,
            .alias = ts_node_is_null(alias) ? (script_range){ 0 }
                                            : script_node_range(alias)
        };
        at++;
    }
    assert(at == source->import_count);
    return source;
}

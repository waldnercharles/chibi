#include "authoring/native_bindings.h"
#include "authoring/source_package_files.h"
#include "authoring/source_package_prepare.h"
#include "runtime/arrays_definition.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int type_catalog_pack_count(void) { return 0; }
type_catalog_pack type_catalog_pack_at(int i) { (void)i; abort(); }
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    source_package_input *files = NULL;
    source_package_load_error load = {0};
    source_package_input *more = NULL;
    if (!source_package_files_read(argv[1], &files, &load) ||
        !source_package_files_read(argv[2], &more, &load)) {
        fprintf(stderr, "%s\n", load.message); return 1;
    }
    for (int i=0; i<acount(more); i++) apush(files, more[i]);
    afree(more);
    source_package_collection core = {"core", argv[2]};
    source_package_snapshot_options options = {.project_root=argv[1], .files=files,
        .file_count=acount(files), .collections=&core, .collection_count=1};
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    const source_package_snapshot_error *problem = source_package_snapshot_error_at(snapshot);
    if (problem) { int d=problem->diagnostic.range.document;
        fprintf(stderr, "%s:%d:%d: %s\n", d>=0 && d<acount(files) ? files[d].path : "source snapshot", problem->diagnostic.range.line, problem->diagnostic.range.column, problem->diagnostic.message); return 1; }
    native_definition_pack packs[] = {NATIVE_TEXT_DEFINITION_PACK,
        NATIVE_ARRAY_DEFINITION_PACK, COMPILER_JSON_DEFINITION_PACK, COMPILER_SYNTAX_DEFINITION_PACK};
    char message[256];
    native_capabilities *native = native_capabilities_from_definition_packs(NULL, 0, packs, 4, message, sizeof message);
    if (!native) { fprintf(stderr, "%s\n", message); return 1; }
    script_lower_bindings bindings = authoring_native_bindings(native);
    authoring_packages *packages = authoring_packages_prepare(snapshot,
        native_capabilities_catalog(native), &bindings, BEHAVIOR_BYTECODE, NULL);
    const char *owner_error = authoring_packages_error(packages);
    if (owner_error) fprintf(stderr, "%s\n", owner_error);
    for (int p=0; p<authoring_packages_count(packages); p++) {
        const authoring_package *package = authoring_packages_at(packages,p);
        for (int d=0; d<package->diagnostic_count; d++) {
            const script_diagnostic *diagnostic=&package->diagnostics[d];
            const script_source_document *document=package->analysis ?
                script_analysis_document(package->analysis,diagnostic->range.document) : NULL;
            fprintf(stderr,"%s:%d:%d: %s\n",document ? document->path : package->identity,
                diagnostic->range.line,diagnostic->range.column,diagnostic->message);
        }
    }
    bool valid=authoring_packages_valid(packages);
    if (valid) printf("Prepared %d source files in %d authored packages\n", acount(files), authoring_packages_count(packages));
    authoring_packages_free(packages);
    source_package_files_free(files);
    native_capabilities_free(native);
    return valid ? 0 : 1;
}

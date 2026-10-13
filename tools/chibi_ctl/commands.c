#include "commands.h"

#include "automation/controller.h"
#include "support/asset_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool number(av_doc *doc, av_value *root, const char *key, const char *text)
{
    char *end;
    double value = strtod(text, &end);
    if (!*text || *end || !isfinite(value) || value < 0 || value > 1) {
        return false;
    }
    av_obj_set(doc, root, key, av_real(doc, value));
    return true;
}

char *chibi_ctl_command(int argc, char **argv, char *error, size_t size)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "contract", CHIBI_AUTOMATION_CONTRACT);
    const char *op = NULL;
    if (argc >= 1 && !strcmp(argv[0], "schema") && argc <= 2) {
        op = "describe_asset_schema";
        if (argc == 2) {
            av_obj_add_str(doc, root, "kind", argv[1]);
        }
    } else if (argc == 1 && !strcmp(argv[0], "assets")) {
        op = "catalog";
    } else if (argc == 1 && !strcmp(argv[0], "resources")) {
        op = "resource_catalog";
    } else if (argc >= 1 && !strcmp(argv[0], "validate") && argc <= 2) {
        op = argc == 1 ? "validate_project" : "validate_asset";
        if (argc == 2) {
            av_obj_add_str(doc, root, "path", argv[1]);
        }
    } else if (argc >= 2 && !strcmp(argv[0], "inspect") && (argc == 2 || (argc == 3 && !strcmp(argv[2], "--sample")))) {
        op = "inspect_graph";
        av_obj_add_str(doc, root, "path", argv[1]);
        if (argc == 3) {
            av_obj_add_bool(doc, root, "cpu_sample", true);
        }
    } else if (argc >= 5 && !strcmp(argv[0], "material") && !strcmp(argv[1], "create")) {
        op = "create_material";
        av_obj_add_str(doc, root, "name", argv[2]);
        bool preset = false;
        for (int i = 3; i < argc; i += 2) {
            if (i + 1 == argc) {
                op = NULL;
                break;
            }
            if (!strcmp(argv[i], "--preset") && !preset &&
                (!strcmp(argv[i + 1], "matte") || !strcmp(argv[i + 1], "metal") ||
                 !strcmp(argv[i + 1], "emissive") || !strcmp(argv[i + 1], "unlit"))) {
                av_obj_add_str(doc, root, "preset", argv[i + 1]);
                preset = true;
                continue;
            }
            const char *key = !strcmp(argv[i], "--roughness") ? "roughness"
                            : !strcmp(argv[i], "--metallic")  ? "metallic"
                                                              : NULL;
            if (!key || av_obj_get(root, key) || !number(doc, root, key, argv[i + 1])) {
                op = NULL;
                break;
            }
        }
        if (!preset) {
            op = NULL;
        }
    } else if (argc >= 3 && !strcmp(argv[0], "model")) {
        if (!strcmp(argv[1], "reimport") &&
            (argc == 3 || (argc == 4 && !strcmp(argv[3], "--check")))) {
            op = "reimport_model";
            av_obj_add_str(doc, root, "name", argv[2]);
            if (argc == 4) {
                av_obj_add_bool(doc, root, "check", true);
            }
        }
        if ((!strcmp(argv[1], "plan") && argc == 5) ||
            (!strcmp(argv[1], "import") && (argc == 5 || argc == 6))) {
            if (!strcmp(argv[3], "--name") &&
                (argc == 5 || !strcmp(argv[5], "--recipe"))) {
                op = !strcmp(argv[1], "plan") ? "preflight_model" : "import_model";
                av_obj_add_str(doc, root, "source", argv[2]);
                av_obj_add_str(doc, root, "name", argv[4]);
                if (argc == 6) {
                    av_obj_add_bool(doc, root, "recipe", true);
                }
            }
        }
    }
    char *request = NULL;
    if (op) {
        av_obj_add_str(doc, root, "operation", op);
        request = av_json_write(root);
    } else {
        snprintf(error, size, "Invalid direct command or arguments. Use --help. Material values must be finite numbers in [0,1].");
    }
    av_doc_free(doc);
    return request;
}

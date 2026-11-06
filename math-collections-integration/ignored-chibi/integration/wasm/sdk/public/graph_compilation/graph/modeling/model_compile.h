#ifndef CHIBI_MODEL_COMPILE_H
#define CHIBI_MODEL_COMPILE_H

#include "graph/model/behavior_asset.h"
#include "graph/model/registry.h"
#include "runtime/models/model_program.h"

/* Result must be initialized. Failure preserves the previous generation. */
bool model_compile(const behavior_asset *asset, model_program *result, shader_error *error);
bool model_compile_with_catalog(
    const behavior_asset *asset,
    const behavior_asset *const *catalog,
    int count,
    model_program *result,
    shader_error *error
);
/* Names are borrowed from the node; numeric values are copied. */
bool model_parameter_get(const node *n, model_parameter_decl *declaration, shader_error *error);
bool model_graph_validate(const graph *source, shader_error *error);
bool model_material_get(const node *n, model_material_decl *declaration, shader_error *error);
/* Re-expands and validates bindings without lowering distance/hit graphs.
 * READY requires identical retained geometry, numeric contracts and part IDs.
 * Result must be initialized; failures preserve it. */
model_bindings_result model_program_compile_bindings(
    const model_program *geometry,
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    model_material_bindings *result,
    shader_error *error
);
bool model_anchor_get(const node *, model_anchor_decl *, shader_error *);
node *model_graph_add_output(graph *g);
extern const registry_pack MODEL_PACK;
extern node_desc NODE_ModelProfileOffset, NODE_ModelProfileUnion,
    NODE_ModelProfileIntersection, NODE_ModelProfileDifference,
    NODE_ModelRoundedExtrude, NODE_ModelBeveledExtrude, NODE_ModelOffset,
    NODE_ModelShell, NODE_ModelTubeSweep, NODE_ModelBezierTube,
    NODE_ModelProfileBezier, NODE_ModelRotateAround, NODE_ModelScaleAround,
    NODE_ModelAnchor, NODE_ModelAttach;
extern node_desc NODE_ModelSampledSdf, NODE_ModelSphere, NODE_ModelOutput,
    NODE_ModelBox, NODE_ModelRoundedBox, NODE_ModelCylinder, NODE_ModelCapsule,
    NODE_ModelTorus, NODE_ModelTranslate, NODE_ModelRotate, NODE_ModelScale,
    NODE_ModelReflect, NODE_ModelUnion, NODE_ModelIntersection,
    NODE_ModelDifference, NODE_ModelSmoothUnion, NODE_ModelPart, NODE_ModelInstance,
    NODE_ModelParameter, NODE_ModelLinearArray, NODE_ModelRadialArray,
    NODE_ModelProfileCircle, NODE_ModelProfileRectangle, NODE_ModelProfilePolygon,
    NODE_ModelExtrude, NODE_ModelRevolve, NODE_ModelMaterialSlot;

#endif

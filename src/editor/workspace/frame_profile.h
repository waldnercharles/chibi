#ifndef CHIBI_FRAME_PROFILE_H
#define CHIBI_FRAME_PROFILE_H

#include "runtime/engine.h"

#define EDITOR_FRAME_HISTORY 240

typedef enum
{
    EDITOR_FRAME_INPUT,
    EDITOR_FRAME_DOCUMENTS,
    EDITOR_FRAME_SIMULATION,
    EDITOR_FRAME_SCENE,
    EDITOR_FRAME_UI,
    EDITOR_FRAME_PRESENT,
    EDITOR_FRAME_STAGE_COUNT
} editor_frame_stage;

typedef struct
{
    double stages[EDITOR_FRAME_STAGE_COUNT];
    double total_ms;
    engine_frame_timing runtime;
} editor_frame_sample;

typedef struct
{
    int key;
    uint64_t starts, resumes;
    double cpu_ms;
} editor_behavior_measure;

typedef struct
{
    char *behavior;
    char *event;
} editor_behavior_key;

typedef struct
{
    editor_behavior_measure *rows;
    editor_behavior_measure overflow;
} editor_behavior_frame;

typedef struct
{
    editor_frame_sample samples[EDITOR_FRAME_HISTORY];
    editor_frame_sample current;
    uint64_t start, mark;
    int count, next;
    bool frozen;
    bool simulation_expanded;
    editor_behavior_key *behavior_keys;
    editor_behavior_frame behaviors[EDITOR_FRAME_HISTORY];
} editor_frame_profile;

void editor_frame_begin(editor_frame_profile *profile);
void editor_frame_mark(editor_frame_profile *profile, editor_frame_stage stage);
void editor_frame_end(
    editor_frame_profile *profile,
    const engine_frame_timing *runtime,
    const game_behavior_profile_frame *behaviors
);
void editor_frame_profile_free(editor_frame_profile *profile);
const editor_frame_sample *editor_frame_latest(const editor_frame_profile *profile);
void editor_frame_profile_draw(editor_frame_profile *profile);

#endif

#ifndef CHIBI_SAVE_NATIVE_H
#define CHIBI_SAVE_NATIVE_H
#include "execution/host.h"
extern const node_host_call GAME_SAVE_READ_CALL;
extern const node_host_call GAME_SAVE_SET_CALL;
extern const node_host_call GAME_SAVE_REMOVE_CALL;
extern const node_host_call GAME_SAVE_COMMIT_CALL;
extern const node_host_call GAME_SAVE_DISCARD_CALL;
extern const node_access GAME_SAVE_READ_ACCESS;
extern const node_access GAME_SAVE_WRITE_ACCESS;
#endif

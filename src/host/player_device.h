#ifndef CHIBI_HOST_PLAYER_DEVICE_H
#define CHIBI_HOST_PLAYER_DEVICE_H

#include "runtime/input_timeline.h"

/* Interactive host bridge. No world pointers survive into browser callbacks.
 * CSS viewport dimensions and capabilities are independent of sensor access. */
void player_device_start();
void player_device_stop();
input_device_state player_device_sample(int width, int height, bool active);
void player_device_submit(input_device_requests requests, bool active);

#endif

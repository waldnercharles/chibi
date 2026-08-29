#ifndef CBLUEPRINT_APP_WEB_LOOP_H
#define CBLUEPRINT_APP_WEB_LOOP_H

void app_web_set_main_loop(void (*frame)(void));
void app_web_cancel_main_loop(void);

#endif

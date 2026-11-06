#ifndef CHIBI_HOST_PLAYER_URL_H
#define CHIBI_HOST_PLAYER_URL_H

/* Interactive player only. Browser requests own their JS strings and listeners;
 * stop removes them without retaining pointers into the runtime world. */
void player_url_start();
void player_url_stop();
/* NULL cancels the pending browser gesture. */
void player_url_submit(const char *url);

#endif

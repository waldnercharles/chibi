#ifndef CHIBI_HOST_SERVE_H
#define CHIBI_HOST_SERVE_H

#include <signal.h>
#include <stdbool.h>

/* A socket listening on 127.0.0.1 at port, or a free port for 0, which
 * *bound receives; -1 when it cannot listen. */
int chibi_serve_listen(int port, int *bound);
/* Answers GET and HEAD requests with the regular files below root until
 * *stop is set, then closes server. Each connection has ten seconds from
 * accept to its last byte. Nothing outside root is served: paths with ..
 * segments are refused, and so is a request whose Host is not this computer. */
void chibi_serve(int server, const char *root, const volatile sig_atomic_t *stop);

#endif

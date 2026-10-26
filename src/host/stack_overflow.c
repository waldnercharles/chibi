/* host/stack_overflow.c -- name a stack overflow instead of a bare fault.
 *
 * The fault handler runs on an alternate stack, since the faulting one is
 * full. It writes its message with write(2) and ends with abort(), both
 * async-signal-safe.
 */
#define _GNU_SOURCE /* pthread_getattr_np */

#include "host/stack_overflow.h"

#if defined(_WIN32) || defined(__EMSCRIPTEN__)

/* Windows reports EXCEPTION_STACK_OVERFLOW itself; the web has no guard page. */
void stack_overflow_init() {}

#else

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

enum
{
    ALTERNATE_STACK_BYTES = 64 * 1024
};
static const char message[] = "stack overflow\n";
static char alternate[ALTERNATE_STACK_BYTES];
/* The guard page below the stack, and the page above it in case the system's
 * stack bounds round differently than its growth limit. */
static uintptr_t guard_low, guard_high;
static struct sigaction previous[2]; /* SIGSEGV, then SIGBUS. */

/* The lowest address of the calling thread's usable stack. */
static uintptr_t stack_low()
{
#ifdef __APPLE__
    pthread_t self = pthread_self();
    return (uintptr_t)pthread_get_stackaddr_np(self) - pthread_get_stacksize_np(self);
#else
    pthread_attr_t attributes;
    void *low;
    size_t bytes;
    bool known = !pthread_getattr_np(pthread_self(), &attributes);
    assert(known);
    known = !pthread_attr_getstack(&attributes, &low, &bytes);
    assert(known);
    (void)known;
    pthread_attr_destroy(&attributes);
    return (uintptr_t)low;
#endif
}

static void fault(int number, siginfo_t *info, void *)
{
    /* kill, raise and sigqueue send a fault signal with no faulting address. */
    bool sent = info->si_code <= 0;
    uintptr_t address = (uintptr_t)info->si_addr;
    if (!sent && address >= guard_low && address < guard_high) {
        ssize_t written = write(STDERR_FILENO, message, sizeof message - 1);
        (void)written;
        abort();
    }
    /* Hand the signal back to the action installed before, with its own
     * mask and flags: a fault repeats when this returns, and a sent signal,
     * blocked while this runs, is sent again. That action stays installed. */
    sigaction(number, &previous[number == SIGBUS], NULL);
    if (sent) {
        raise(number);
    }
}

void stack_overflow_init()
{
    assert(!guard_high);
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE), low = stack_low();
    guard_low = low - page;
    guard_high = low + page;
    stack_t stack = { .ss_sp = alternate, .ss_size = sizeof alternate };
    struct sigaction action = { .sa_sigaction = fault,
                                .sa_flags = SA_SIGINFO | SA_ONSTACK };
    sigemptyset(&action.sa_mask);
    bool installed = !sigaltstack(&stack, NULL) &&
                     !sigaction(SIGSEGV, &action, &previous[0]) &&
                     !sigaction(SIGBUS, &action, &previous[1]);
    assert(installed);
    (void)installed;
}

#endif

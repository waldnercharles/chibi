#ifndef CHIBI_STACK_OVERFLOW_H
#define CHIBI_STACK_OVERFLOW_H

/* Native code recurses unchecked and probes its stack, so running out of
 * stack faults in the guard page below it; emitted code installs no handler
 * of its own. Once a host has run this on a thread, such a fault there
 * prints "stack overflow" to stderr and aborts, as Rust's runtime does.
 * Any other SIGSEGV or SIGBUS, a fault or a sent signal, goes back to the
 * action installed before, which then handles every later one: from then on
 * an overflow is not reported by name. Call it once, on the thread that runs
 * native code. */
void stack_overflow_init();

#endif

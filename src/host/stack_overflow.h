#ifndef CHIBI_STACK_OVERFLOW_H
#define CHIBI_STACK_OVERFLOW_H

/* Native code recurses unchecked and probes its stack, so running out of
 * stack faults in the guard page below it. Once this has run on a thread,
 * such a fault there prints "stack overflow" to stderr and aborts, as Rust's
 * runtime does. Other faults keep the action installed before. Call it once,
 * on the thread that runs native code. */
void stack_overflow_init();

#endif

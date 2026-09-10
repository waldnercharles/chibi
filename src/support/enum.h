#ifndef CHIBI_ENUM_H
#define CHIBI_ENUM_H

/* X-macro enum -> name table (from Neon-Swarm, trimmed to what we use).
 *
 *   #define COLOR_LIST(X) X(RED, "red") X(GREEN, "green") X(BLUE, "blue")
 *   DEFINE_ENUM(color, COLOR_LIST)
 *
 * Declares `enum color` with values in list order and a static inline
 * `color_name(int v)` returning the string for each value. A NULL name is
 * allowed (the enum value still occupies its list position). */
#define XVAL_(id, str) id,
#define XNAME_(id, str) str,

#define DEFINE_ENUM(T, LIST)                                                   \
    typedef enum                                                               \
    {                                                                          \
        LIST(XVAL_)                                                            \
    } T;                                                                       \
    static inline const char *T##_name(int v)                                  \
    {                                                                          \
        static const char *n[] = { LIST(XNAME_) };                             \
        return n[v];                                                           \
    }

#endif

#ifndef CHIBI_TOOLCHAIN_COMPILER_BOOTSTRAP_H
#define CHIBI_TOOLCHAIN_COMPILER_BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>

/* A bootstrap artifact is a folder of two files. compiler.chibip is a closure
 * of the stage-one package compiler that compiles the compiler sources to
 * itself. bootstrap.json records its format, its bootstrap version, the
 * revision and checksums of the sources it compiles, its own checksum and the
 * target it was compiled for. package_compiler/Bootstrap.chibi supplies these
 * versions through the compiled BootstrapContract function. Recovery checks the
 * artifact metadata before loading, then the current contract after compilation. */
typedef struct
{
    const char *folder;   /* The artifact. */
    const char *compiler; /* The compiler sources, including Bootstrap.chibi. */
    const char *core;     /* The core collection the sources import. */
    const char *seed;     /* write: the seed's closure and arguments. */
    const char *revision; /* write: the sources' revision, recorded as given. */
    const char *output;   /* check: where to write the recovered compiler. */
} compiler_bootstrap;

/* Writes the artifact from a seed folder: compiler.chibip and compile.json,
 * Compile's arguments for the compiler sources on this host. The seed must
 * compile the sources, as they are on disk, to itself. The closure is written
 * before the manifest, so an interrupted write leaves a closure its old
 * manifest's checksum rejects. False with why. */
bool compiler_bootstrap_write(const compiler_bootstrap *, char *error, size_t size);
/* Recovers the compiler from the artifact and the sources on disk. When the
 * sources are those it recorded, the artifact must compile them to its own
 * bytes; otherwise the compiler it compiles must compile them to itself. An
 * unknown format, invalid metadata, a damaged closure or another target is
 * rejected before compilation. The recovered contract's minimum is checked
 * before accepting or writing the result. False with why; on success, report
 * says what was recovered, and the recovered compiler is written to `output`
 * when there is one. */
bool compiler_bootstrap_check(const compiler_bootstrap *, char *report, size_t size);

#endif

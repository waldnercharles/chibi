#ifndef CHIBI_TOOLCHAIN_COMPILER_BOOTSTRAP_H
#define CHIBI_TOOLCHAIN_COMPILER_BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>

/* A bootstrap artifact is a folder of two files, a seed. compiler.chibip is a
 * closure of the stage-one package compiler that compiles the compiler sources
 * it was written from to itself. bootstrap.json records its format, its
 * bootstrap version, the revision and checksums of those sources, its own
 * checksum and the target it was compiled for. The sources' bootstrap.kdl
 * names the version a compiler built from them has and the oldest version that
 * can compile them, and asset_compiler/CompilerSources.txt lists the sources
 * in Compile's order. The seed need not match the sources on disk. */
typedef struct
{
    const char *folder;   /* The artifact. */
    const char *compiler; /* The compiler sources, holding bootstrap.kdl. */
    const char *core;     /* The core collection the sources import. */
    const char *seed;     /* write: the seed's closure and arguments. */
    const char *revision; /* write: the sources' revision, recorded as given. */
    const char *output;   /* check: where to write the recovered compiler. */
    const char *arguments; /* check: where to write Compile's arguments. */
    const char *cache; /* load: directory keeping recovered compilers, or NULL. */
    bool validate;     /* check: validate the artifact without recovering. */
} compiler_bootstrap;

/* Writes the artifact from a seed folder: compiler.chibip and compile.json,
 * Compile's arguments for the compiler sources on this host. The seed must
 * compile the sources, as they are on disk, to itself. The closure is written
 * before the manifest, so an interrupted write leaves a closure its old
 * manifest's checksum rejects. False with why. */
bool compiler_bootstrap_write(const compiler_bootstrap *, char *error, size_t size);
/* Recovers the compiler from the artifact and the sources on disk. When the
 * sources are those it recorded, the artifact must compile them to its own
 * bytes. Otherwise it only has to compile them: that compiler (stage one)
 * compiles them to stage two, and when the two differ stage two compiles them
 * to a third that must be stage two's bytes, which is the fixed point. An
 * unknown format, a damaged closure, another target, a package with a source
 * the list omits or a version older than the sources' minimum is rejected
 * first. False with why; on success, report says what was recovered, and the
 * recovered compiler is written to `output` when there is one, and the
 * arguments it compiled the sources with to `arguments`. Those two files make a
 * seed folder for compiler_bootstrap_write. */
bool compiler_bootstrap_check(const compiler_bootstrap *, char *report, size_t size);
/* The checks compiler_bootstrap_check makes before it recovers anything: the
 * artifact's format, version, closure checksum and target, and the sources'
 * completeness. Compiles nothing, so it takes seconds where recovery takes
 * minutes. False with why; on success, report says how many sources changed. */
bool compiler_bootstrap_validate(const compiler_bootstrap *, char *report, size_t size);

struct compiler_host;

/* Loads one owned, validated current compiler generation. Matching source
 * hashes admit the recorded closure directly, after checksum, version, ABI and
 * source-completeness validation. Changed sources go through the
 * recovery/fixed-point path, which takes minutes. With a cache directory the
 * recovered compiler is kept there under a digest of the seed, the sources and
 * target, the prepared format and this host binary, and read back, framed and
 * checksummed, by a later load of the same inputs; a closure that fails any
 * check is dropped and recovered again. The check path never reads or writes
 * the cache, so recovery proofs always recover. Writes no other file.
 * Release the result with compiler_host_free. */
struct compiler_host *compiler_bootstrap_load(const compiler_bootstrap *, char *error, size_t size);

#endif

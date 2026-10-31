#pragma once

/* The compiler host's bootstrap mode refusing bad options, contracts and
 * artifacts before it compiles anything. */
void test_tool_compiler_bootstrap(const char *host);
/* A Release compiler host recovering the compiler from the artifact folder
 * that tools/build.sh build bootstrap-artifact writes, with sources from a clean checkout. */
void test_tool_bootstrap_recovery(const char *host, const char *artifact);
/* Loading the compiler in process from that folder when the sources changed:
 * one recovery, then the cache entry it keeps. */
void test_tool_bootstrap_load(const char *artifact);

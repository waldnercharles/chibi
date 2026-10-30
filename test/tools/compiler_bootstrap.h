#pragma once

/* The compiler host's bootstrap mode refusing bad options, contracts and
 * artifacts before it compiles anything. */
void test_tool_compiler_bootstrap(const char *producer, const char *host);
/* Cache ownership on failed loads, without a compiler-source recovery. */
void test_tool_compiler_cache();
/* A Release compiler host recovering the compiler from the artifact folder
 * that tools/build.sh build bootstrap-artifact writes, with sources from a clean checkout. */
void test_tool_bootstrap_recovery(const char *host, const char *artifact);

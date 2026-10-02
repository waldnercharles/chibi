#!/usr/bin/env python3
"""Compare Assets row preparation at the audit baseline and current checkout.

Extracts the measured C functions, compiles a small synthetic harness in a
TemporaryDirectory, and prints raw results. It does not build the editor.
"""

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import textwrap


def between(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="29a0e24f", help="Git source revision")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"),
                        help="Compiler command; also accepts the CC environment variable")
    parser.add_argument("--runs", type=int, default=3, help="Independent harness executions")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    root = Path(__file__).resolve().parents[4]
    path = "src/editor/studio/studio_assets.c"
    baseline = subprocess.check_output(
        ["git", "show", f"{args.baseline}:{path}"], cwd=root, text=True
    )
    src = (root / path).read_text()
    before = between(baseline, "    int *uses = xcalloc", "    s->assets_hover = -1;")
    contains = between(src, "static bool contains(", "void studio_assets_header(")
    models = between(src, "typedef struct\n{\n    int asset;", "static bool menu_item(")
    pre = '''#include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <strings.h>
    #include <ctype.h>
    #include <time.h>
    #include <assert.h>
    #include "support/array.h"
    static unsigned long allocs, visits;
    void *xmalloc(size_t n) { allocs++; void *p=malloc(n); assert(p); return p; }
    void *xcalloc(size_t n,size_t z) { allocs++; void *p=calloc(n,z); assert(p); return p; }
    void *xrealloc(void *p,size_t n) { allocs++; p=realloc(p,n); assert(p); return p; }
    #define REFERENCE_ASSET_KIND_COUNT 10
    typedef struct { const char *name; int kind; } reference_asset;
    typedef struct { int asset; } reference;
    typedef struct { reference_asset *assets; reference *refs; int count, refcount; } reference_index;
    typedef struct { uint64_t asset_index_revision; } scene_type;
    typedef struct { scene_type *scene; int assets_kind; unsigned assets_collapsed; char assets_filter[128]; struct studio_assets_cache *assets_cache; } studio;
    int reference_index_asset_count(const reference_index *i) { return i->count; }
    int reference_index_count(const reference_index *i) { return i->refcount; }
    const reference_asset *reference_index_asset_at(const reference_index *i,int n) { visits++; return &i->assets[n]; }
    const reference *reference_index_at(const reference_index *i,int n) { visits++; return &i->refs[n]; }
    int reference_dependency(const reference *r) { return r->asset; }
    '''
    post = '''
    static volatile int sink;
    static void baseline(studio *s,const reference_index *index) {
     int count=reference_index_asset_count(index);
    '''+before+'''
     sink+=acount(rows);
     afree(rows); afree(matches); free(uses);
    }
    static double now() { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
    int main() {
     scene_type scene={1}; studio s={.scene=&scene,.assets_kind=-1};
     for(int scale=0;scale<2;scale++) {
      int n=scale?10000:1000; reference_index index={.count=n,.refcount=n*10};
      index.assets=calloc(n,sizeof *index.assets); index.refs=calloc(index.refcount,sizeof *index.refs);
      for(int i=0;i<n;i++) { char name[64]; snprintf(name,sizeof name,"Asset_%05d",n-i); index.assets[i]=(reference_asset){strdup(name),i%10}; }
      for(int i=0;i<index.refcount;i++) index.refs[i].asset=i%n;
      allocs=visits=0; double start=now();
      for(int i=0;i<200;i++) baseline(&s,&index);
      double old=(now()-start)*1e6/200; unsigned long oa=allocs/200, ov=visits/200;
      allocs=visits=0; start=now();
      for(int i=0;i<200;i++) { scene.asset_index_revision++; sink+=acount(asset_rows(&s,&index)->rows); }
      double fresh=(now()-start)*1e6/200; unsigned long fa=allocs, fv=visits/200;
      allocs=visits=0; start=now();
      for(int i=0;i<100000;i++) sink+=acount(asset_rows(&s,&index)->rows);
      double cached=(now()-start)*1e6/100000;
      printf("assets=%d refs=%d baseline_us=%.3f baseline_allocs=%lu baseline_visits=%lu fresh_us=%.3f fresh_total_allocs=%lu fresh_visits=%lu cached_us=%.6f cached_total_allocs=%lu cached_total_visits=%lu\\n",n,index.refcount,old,oa,ov,fresh,fa,fv,cached,allocs,visits);
      allocs=visits=0; start=now();
      for(int i=0;i<200;i++) { snprintf(s.assets_filter,sizeof s.assets_filter,"Asset_%s",i%2?"0":""); baseline(&s,&index); }
      double filter_old=(now()-start)*1e6/200; unsigned long filter_old_visits=visits/200;
      allocs=visits=0; start=now();
      for(int i=0;i<200;i++) { snprintf(s.assets_filter,sizeof s.assets_filter,"Asset_%s",i%2?"0":""); sink+=acount(asset_rows(&s,&index)->rows); }
      printf("assets=%d changing_filter_before_us=%.3f after_us=%.3f index_visits_before=%lu after=%lu allocations_after=%lu\\n",n,filter_old,(now()-start)*1e6/200,filter_old_visits,visits/200,allocs);
      snprintf(s.assets_filter,sizeof s.assets_filter,"Missing"); assert(!acount(asset_rows(&s,&index)->rows));
      free((void *)index.assets[0].name); index.assets[0].name=strdup("Missing"); scene.asset_index_revision++; assert(acount(asset_rows(&s,&index)->rows)==2);
      s.assets_filter[0]=0; s.assets_collapsed=~0u; assert(acount(asset_rows(&s,&index)->rows)==10);
      s.assets_kind=0; assert(acount(asset_rows(&s,&index)->rows)==n/10);
      studio_assets_free(&s); s.assets_kind=-1; s.assets_collapsed=0;
      for(int i=0;i<n;i++) free((void *)index.assets[i].name);
      free(index.assets); free(index.refs);
     }
     puts("PASS filter, collapse, kind, explicit same-size index refresh, teardown");
    }
    '''
    with tempfile.TemporaryDirectory(prefix="chibi-assets-benchmark-") as temp:
        generated = Path(temp) / "assets_benchmark.c"
        binary = Path(temp) / "assets_benchmark"
        generated.write_text(textwrap.dedent(pre) + contains + models + textwrap.dedent(post))
        command = shlex.split(args.cc) + [
            "-O2", "-UNDEBUG", "-I", str(root / "src"), str(generated), "-o", str(binary)
        ]
        print(f"baseline={args.baseline} current_source={path}", flush=True)
        print("current_source_sha256=" + hashlib.sha256(src.encode()).hexdigest(), flush=True)
        print("compile=" + shlex.join(command), flush=True)
        subprocess.run(command, cwd=root, check=True)
        for run in range(1, args.runs + 1):
            print(f"run={run}", flush=True)
            subprocess.run([str(binary)], cwd=root, check=True)


if __name__ == "__main__":
    main()

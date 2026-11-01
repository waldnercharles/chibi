#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static void *al(void *c, size_t n){ return calloc(1,n);} static void rel(void*c,void*p){free(p);} 
static void idx(void*c,int a,int b){abort();}
void *ctx[5]={0,0,(void*)al,(void*)idx,(void*)rel};
int64_t run_ints(void*,int32_t); int64_t run_text(void*,int32_t);
int main(int argc,char**argv){ int n=atoi(argv[1]); printf("%lld %lld\n",(long long)run_ints(ctx,n),(long long)run_text(ctx,n%1000)); }

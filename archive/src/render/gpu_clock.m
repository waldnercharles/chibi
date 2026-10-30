#include "render/gpu_clock.h"

#include "render/gpu_timing.h"

#include <cute.h>
#include <stdatomic.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <objc/runtime.h>

static IMP g_commit;
static Method g_method;
static int g_users;
/* Completion handlers not yet run; they call into this module's code. */
static _Atomic int g_outstanding;

/* Replaces -commit on the device's command buffer class, so every buffer the
 * renderer commits reports its GPU interval without changing submission. */
static void s_timed_commit(id self, SEL selector)
{
    unsigned timing_id = render_gpu_timing_committed();
    if (timing_id) {
        atomic_fetch_add(&g_outstanding, 1);
        [(id<MTLCommandBuffer>)self addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            render_gpu_timing_completed(
                timing_id,
                buffer.GPUStartTime * 1e3,
                buffer.GPUEndTime * 1e3
            );
            atomic_fetch_sub(&g_outstanding, 1);
        }];
    }
    ((void (*)(id, SEL))g_commit)(self, selector);
}

bool render_gpu_clock_install(void)
{
    if (g_commit) {
        g_users++;
        return true;
    }
    @autoreleasepool {
        /* The pinned SDL backend commits buffers from its default device's
         * queue; a probe buffer from the same device has their class. */
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> probe = [queue commandBuffer];
        Class buffer_class = object_getClass(probe);
        Method commit = class_getInstanceMethod(buffer_class, @selector(commit));
        if (commit) {
            g_commit = method_getImplementation(commit);
            class_replaceMethod(
                buffer_class,
                @selector(commit),
                (IMP)s_timed_commit,
                method_getTypeEncoding(commit)
            );
            g_method = class_getInstanceMethod(buffer_class, @selector(commit));
        }
        [queue release];
        [device release];
    }
    if (!g_commit) {
        return false;
    }
    g_users = 1;
    render_gpu_timing_track_commits(true);
    return true;
}

void render_gpu_clock_release(void)
{
    if (!g_commit || --g_users > 0) {
        return;
    }
    /* Restore -commit and finish outstanding timing reports before teardown. */
    method_setImplementation(g_method, g_commit);
    g_commit = NULL;
    render_gpu_timing_track_commits(false);
    cf_gpu_sync();
    while (atomic_load(&g_outstanding)) {
        cf_sleep(1);
    }
}

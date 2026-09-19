#include "automation/gpu_capture.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <unistd.h>

bool chibi_gpu_capture_begin(const char *path, char *error, size_t size)
{
    @autoreleasepool {
        MTLCaptureManager *manager = [MTLCaptureManager sharedCaptureManager];
        if (![manager supportsDestination:MTLCaptureDestinationGPUTraceDocument]) {
            snprintf(error, size, "Metal trace capture is unavailable; launch with MTL_CAPTURE_ENABLED=1 and install Xcode's Metal tools");
            return false;
        }
        if ([manager isCapturing] || access(path, F_OK) == 0) {
            snprintf(error, size, "A capture is already active or the output path exists");
            return false;
        }
        /* The pinned SDL backend uses MTLCreateSystemDefaultDevice without a
         * low-power override. Restrict this tooling seam to a single GPU until
         * the renderer exposes explicit native-device selection. */
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        bool single = devices.count == 1;
        [devices release];
        if (!single) {
            snprintf(error, size, "Programmatic capture currently requires one Metal GPU; use Xcode device selection on multi-GPU hosts");
            return false;
        }
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        MTLCaptureDescriptor *descriptor = [[MTLCaptureDescriptor alloc] init];
        descriptor.captureObject = device;
        descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
        descriptor.outputURL = [NSURL
            fileURLWithPath:[NSString stringWithUTF8String:path]];
        NSError *failure = nil;
        bool ok = [manager startCaptureWithDescriptor:descriptor
                                                error:&failure];
        if (!ok) {
            snprintf(
                error,
                size,
                "Metal capture failed: %s",
                failure.localizedDescription.UTF8String
            );
        }
        [descriptor release];
        [device release];
        return ok;
    }
}

bool chibi_gpu_capture_end(const char *path, char *error, size_t size)
{
    @autoreleasepool {
        [[MTLCaptureManager sharedCaptureManager] stopCapture];
        if (access(path, F_OK)) {
            snprintf(error, size, "Metal did not write the requested GPU trace");
            return false;
        }
        return true;
    }
}

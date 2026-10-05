#!/usr/bin/env python3
"""Read-only Vulkan enumeration; no device creation, rendering or driver changes."""
import ctypes as c
import glob
import json
import os


class InstanceInfo(c.Structure):
    _fields_ = [('type', c.c_int), ('next', c.c_void_p), ('flags', c.c_uint32),
                ('app', c.c_void_p), ('layers', c.c_uint32), ('layer_names', c.c_void_p),
                ('extensions', c.c_uint32), ('extension_names', c.c_void_p)]


def probe():
    result = dict(driver=os.getenv('VK_DRIVER_FILES', 'loader-default'),
                  device_nodes=glob.glob('/dev/dri/*') + glob.glob('/dev/nvidia*'),
                  devices=[], hardware_available=False)
    try:
        vulkan = c.CDLL('libvulkan.so.1')
    except OSError as error:
        return dict(result, status='unavailable', reason=str(error))
    vulkan.vkCreateInstance.argtypes = [c.POINTER(InstanceInfo), c.c_void_p, c.POINTER(c.c_void_p)]
    vulkan.vkEnumeratePhysicalDevices.argtypes = [c.c_void_p, c.POINTER(c.c_uint32), c.c_void_p]
    vulkan.vkGetPhysicalDeviceProperties.argtypes = [c.c_void_p, c.c_void_p]
    vulkan.vkDestroyInstance.argtypes = [c.c_void_p, c.c_void_p]
    instance = c.c_void_p()
    code = vulkan.vkCreateInstance(c.byref(InstanceInfo(type=1)), None, c.byref(instance))
    result['instance_result'] = code
    if code:
        return dict(result, status='unavailable')
    try:
        count = c.c_uint32()
        code = vulkan.vkEnumeratePhysicalDevices(instance, c.byref(count), None)
        if code:
            return dict(result, status='unavailable', enumeration_result=code)
        devices = (c.c_void_p * count.value)()
        code = vulkan.vkEnumeratePhysicalDevices(instance, c.byref(count), devices)
        if code:
            return dict(result, status='unavailable', enumeration_result=code)
        for device in devices:
            # VkPhysicalDeviceProperties begins with five uint32 values and a
            # 256-byte name; reserve ample space for its trailing limits struct.
            buffer = c.create_string_buffer(4096)
            vulkan.vkGetPhysicalDeviceProperties(device, buffer)
            device_type = int.from_bytes(buffer.raw[16:20], 'little')
            result['devices'].append(dict(type=device_type,
                                          name=buffer.raw[20:276].split(b'\0')[0].decode()))
        result['hardware_available'] = any(d['type'] in (1, 2, 3) for d in result['devices'])
        return dict(result, status='enumerated', scope='discovery_only_not_render_validation')
    finally:
        vulkan.vkDestroyInstance(instance, None)


if __name__ == '__main__':
    print(json.dumps(probe()))

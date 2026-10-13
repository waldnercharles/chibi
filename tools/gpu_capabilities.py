#!/usr/bin/env python3
"""Read-only GPU discovery; no rendering or driver changes."""
import ctypes as c
import glob
import json
import os
import subprocess
import sys


def probe_metal():
    """Read OS-reported Metal support without creating a rendering device."""
    result = dict(devices=[], hardware_available=False, acceleration='unknown',
                  backend='Metal', scope='discovery_only_not_render_validation',
                  source='system_profiler', discovery_timeout_seconds=4)
    try:
        process = subprocess.run(
            ['/usr/sbin/system_profiler', 'SPDisplaysDataType', '-json', '-timeout', '3'],
            capture_output=True, text=True, timeout=4, check=True,
            env=dict(os.environ, LC_ALL='C'))
        profile = json.loads(process.stdout)
        if not isinstance(profile, dict) or not isinstance(profile.get('SPDisplaysDataType'), list):
            return dict(result, status='unavailable', reason='invalid_display_profile')
        for gpu in profile['SPDisplaysDataType']:
            if not isinstance(gpu, dict):
                return dict(result, status='unavailable', reason='invalid_gpu_profile')
            support = gpu.get('spdisplays_mtlgpufamilysupport', gpu.get('spdisplays_metal', ''))
            if not isinstance(support, str):
                support = ''
            family = support.removeprefix('spdisplays_metal')
            supported = ((support.startswith('spdisplays_metal') and family.isdecimal()) or
                         support == 'spdisplays_supported' or
                         support == 'Supported' or support.startswith('Supported, feature set ') or
                         (support.startswith('Metal ') and support[6:].isdecimal()))
            name = gpu.get('sppci_model', gpu.get('_name', 'Unknown GPU'))
            if not isinstance(name, str):
                name = 'Unknown GPU'
            # Do not expose display profiles, serials or other machine metadata.
            result['devices'].append(dict(name=name, metal_supported=supported,
                                          metal_support=support))
        result['hardware_available'] = any(device['metal_supported'] for device in result['devices'])
        if result['hardware_available']:
            result['acceleration'] = 'hardware'
        return dict(result, status='enumerated')
    except subprocess.TimeoutExpired:
        return dict(result, status='unavailable', reason='discovery_timeout')
    except (OSError, ValueError, subprocess.SubprocessError):
        return dict(result, status='unavailable', reason='display_profile_unavailable')


class InstanceInfo(c.Structure):
    _fields_ = [('type', c.c_int), ('next', c.c_void_p), ('flags', c.c_uint32),
                ('app', c.c_void_p), ('layers', c.c_uint32), ('layer_names', c.c_void_p),
                ('extensions', c.c_uint32), ('extension_names', c.c_void_p)]


def classify_devices(devices):
    """Classify Vulkan's reported type, never the hostname or driver filename."""
    if any(device['type'] in (1, 2) for device in devices):
        return 'hardware'
    if devices and all(device['type'] == 4 for device in devices):
        return 'software'
    return 'unknown'  # Virtual/other devices do not prove physical acceleration.


def probe():
    if sys.platform == 'darwin':
        return probe_metal()
    result = dict(driver=os.getenv('VK_DRIVER_FILES', 'loader-default'),
                  device_nodes=glob.glob('/dev/dri/*') + glob.glob('/dev/nvidia*'),
                  devices=[], hardware_available=False, acceleration='unknown', backend='Vulkan')
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
        result['acceleration'] = classify_devices(result['devices'])
        result['hardware_available'] = result['acceleration'] == 'hardware'
        return dict(result, status='enumerated', scope='discovery_only_not_render_validation')
    finally:
        vulkan.vkDestroyInstance(instance, None)


if __name__ == '__main__':
    print(json.dumps(probe()))

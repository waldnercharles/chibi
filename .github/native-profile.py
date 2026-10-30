#!/usr/bin/env python3
"""Select CI-only Debug profiles; the build driver defaults are unchanged."""
import os


def settings(sanitizers):
    if sanitizers not in ('ON', 'OFF'):
        raise ValueError('CHIBI_CI_SANITIZERS must be ON or OFF')
    debug = '-g -O1' + (' -fno-omit-frame-pointer' if sanitizers == 'OFF' else '')
    flags = (f'-DCMAKE_BUILD_TYPE=Debug -DCHIBI_ENABLE_SANITIZERS={sanitizers} '
             '-DCHIBI_SANITIZERS=address,undefined -DCHIBI_ENABLE_WARNINGS=ON '
             '-DCHIBI_ENABLE_CCACHE=ON ' + ' '.join(
                 f'-DCMAKE_{language}_FLAGS_DEBUG="{debug}"' for language in ('C', 'CXX', 'OBJC')))
    return {'BUILD_PROFILE': 'sanitizer' if sanitizers == 'ON' else 'debug',
            'BUILD_DEBUG_OPTIMIZATION': '1',
            'BUILD_DEBUG_FRAME_POINTERS': 'on' if sanitizers == 'OFF' else '',
            'SANITIZER_CMAKE_FLAGS': flags}


def main():
    values = settings(os.environ['CHIBI_CI_SANITIZERS'])
    with open(os.environ['GITHUB_ENV'], 'a') as stream:
        for key, value in values.items():
            stream.write(f'{key}={value}\n')
    print('CI native profile:', values)


if __name__ == '__main__':
    main()

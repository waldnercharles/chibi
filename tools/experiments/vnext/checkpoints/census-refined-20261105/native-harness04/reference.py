#!/usr/bin/env python3
"""Independent CCN1 byte oracle, not a source-closure/preparation tool.

The result is the contract's sixteen integers. Input is bytes or a read-only
mmap kept unchanged through the call. No decoding, Unicode line/space rules,
Chibi byte loop, or compiler/backend module participates in the answer.
"""

import mmap
import re
import struct
import sys


MAGIC = 827212611
PACKAGE_LIMIT = 4096
ORIGINAL_FILE_COUNT_LIMIT = 16384
ORIGINAL_FILE_LIMIT = 16 * 1024 * 1024
ORIGINAL_TOTAL_LIMIT = 256 * 1024 * 1024
GENERATED_COUNT_LIMIT = 1
GENERATED_TOTAL_LIMIT = 256
FRAME_LIMIT = 16 + 12 * (ORIGINAL_FILE_COUNT_LIMIT + GENERATED_COUNT_LIMIT) + ORIGINAL_TOTAL_LIMIT + GENERATED_TOTAL_LIMIT

HEADER = struct.Struct("<4I")
RECORD = struct.Struct("<3I")
NONBLANK = re.compile(rb"[^\x09-\x0d\x20]")
LINE_CHUNK = 64 * 1024


def count_lines(frame: bytes | mmap.mmap, start: int, stop: int) -> tuple[int, int]:
    """Split on LF only, bounded to 64 KiB plus its line-segment list.

    Split copies at most one chunk's bytes, not a complete large payload. At
    most 65,537 segment references exist per chunk, independent of frame size.
    A boolean carries a line spanning chunks. The regex searches bytes for the
    exact non-whitespace set without another stripped/translated payload copy.
    """
    physical = nonblank = 0
    pending_text = False
    for at in range(start, stop, LINE_CHUNK):
        parts = frame[at:min(at + LINE_CHUNK, stop)].split(b"\n")
        tail = parts.pop()
        physical += len(parts)
        for line in parts:
            nonblank += pending_text or NONBLANK.search(line) is not None
            pending_text = False
        pending_text = pending_text or NONBLANK.search(tail) is not None
        del parts  # Release this bounded list before constructing the next one.
    if stop > start and frame[stop - 1] != 10:
        physical += 1
        nonblank += pending_text
    return physical, nonblank


def census(frame: bytes | mmap.mmap) -> tuple[int, ...]:
    """Validate in specified first-failure order; errors zero all 13 counters."""
    size = len(frame)

    def failure(status: int, offset: int) -> tuple[int, ...]:
        return (status, offset, *(0,) * 13, size)

    if size < HEADER.size:
        return failure(1, 0)
    if size > FRAME_LIMIT:
        return failure(2, 0)
    magic, declared_packages, declared_originals, declared_generated = HEADER.unpack_from(frame)
    if magic != MAGIC:
        return failure(3, 0)
    if not (1 <= declared_packages <= PACKAGE_LIMIT and
            1 <= declared_originals <= ORIGINAL_FILE_COUNT_LIMIT and
            declared_generated <= GENERATED_COUNT_LIMIT):
        return failure(4, 0)

    packages = 0
    # Four counters per category: files, bytes, physical lines, nonblank lines.
    original = [0, 0, 0, 0]
    replaced = [0, 0, 0, 0]
    generated = [0, 0, 0, 0]
    pending_package = 0
    at = HEADER.size
    while at < size:
        if size - at < RECORD.size:
            return failure(5, at)
        tag, package, length = RECORD.unpack_from(frame, at)
        start = at + RECORD.size
        if tag not in (1, 2, 3):
            return failure(6, at)
        if length > size - start:
            return failure(7, at)
        if ((pending_package and (tag != 3 or package != pending_package)) or
                (not pending_package and tag == 3)):
            return failure(8, at)
        if tag == 3:
            if generated[0] >= declared_generated:
                return failure(9, at)
            if length > GENERATED_TOTAL_LIMIT - generated[1]:
                return failure(10, at)
        else:
            if not (1 <= package <= declared_packages and
                    (package == packages or package == packages + 1)):
                return failure(11, at)
            if original[0] >= declared_originals:
                return failure(12, at)
            if length > ORIGINAL_FILE_LIMIT:
                return failure(13, at)
            if length > ORIGINAL_TOTAL_LIMIT - original[1]:
                return failure(14, at)
            if tag == 2 and replaced[0]:
                return failure(15, at)

        stop = start + length
        physical, nonblank = count_lines(frame, start, stop)
        if tag == 3:
            generated[0] += 1
            generated[1] += length
            generated[2] += physical
            generated[3] += nonblank
            pending_package = 0
        else:
            packages = package
            original[0] += 1
            original[1] += length
            original[2] += physical
            original[3] += nonblank
            if tag == 2:
                replaced[:] = (1, length, physical, nonblank)
                pending_package = package
        at = stop

    if pending_package:
        return failure(16, size)
    if packages != declared_packages:
        return failure(17, size)
    if original[0] != declared_originals:
        return failure(18, size)
    if generated[0] != declared_generated:
        return failure(19, size)
    return (0, 0, packages, *original, *replaced, *generated, size)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: reference.py FRAME", file=sys.stderr)
        return 2
    path = sys.argv[1]
    try:
        with open(path, "rb") as source:
            # Empty files cannot be mapped; their exact bytes still reach census.
            if source.seek(0, 2) == 0:
                report = census(b"")
            else:
                with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as frame:
                    report = census(frame)
    except (OSError, ValueError) as error:
        print(f"{path}: input I/O: {error}", file=sys.stderr)
        return 3
    try:
        print(*report)
        sys.stdout.flush()
    except OSError as error:
        print(f"{path}: output I/O: {error}", file=sys.stderr)
        return 5
    return 0 if report[0] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

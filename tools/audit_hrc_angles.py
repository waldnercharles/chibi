#!/usr/bin/env python3
"""Show HRC ray angular widths and whether an equal-angle layout chains exactly."""
import argparse
import math


HALF_QUADRANT = math.pi / 4


def slope_angle(level, k):
    step = 1 << level
    return math.atan2(2 * k - step, step)


def equal_angle(level, k):
    step = 1 << level
    return -HALF_QUADRANT + k * (math.pi / 2) / step


def equal_offset(level, k):
    step = 1 << level
    return step * math.tan(equal_angle(level, k))


def widths(angle, level):
    step = 1 << level
    values = [angle(level, k) for k in range(step + 1)]
    return [values[i + 1] - values[i] for i in range(step)]


def max_chain_error(level):
    step = 1 << level
    errors = []
    for k in range(step + 1):
        lo = k // 2
        hi = (k + 1) // 2
        errors.append(abs(equal_offset(level, k) -
                          equal_offset(level - 1, lo) -
                          equal_offset(level - 1, hi)))
    return max(errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--levels', type=int, default=5)
    args = parser.parse_args()
    if not 0 <= args.levels <= 8:
        parser.error('levels must be between 0 and 8')
    print('level step slope_min slope_max slope_ratio equal_width chain_error_px')
    for level in range(args.levels + 1):
        current = widths(slope_angle, level)
        equal = widths(equal_angle, level)
        ratio = max(current) / min(current)
        error = 0.0 if level == 0 else max_chain_error(level)
        print(f'{level:5d} {1 << level:4d} {min(current) * 180 / math.pi:9.4f} '
              f'{max(current) * 180 / math.pi:9.4f} {ratio:10.4f} '
              f'{equal[0] * 180 / math.pi:11.4f} {error:15.6f}')


if __name__ == '__main__':
    main()

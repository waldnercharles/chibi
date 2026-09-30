#!/usr/bin/env python3
"""CPU operation/storage models for docs/archive/research/hrc-sparse-directional-probes.md.

This is not a renderer or a GPU benchmark. Only the interval-tree experiment
compares numerical transport; the HRC audit enumerates shader dependencies.
"""

import json
import math
import random


IDENTITY = (0.0, 1.0)


def compose(near, far):
    light, transmission = near
    return light + transmission * far[0], transmission * far[1]


def direct(values, begin, end):
    result = IDENTITY
    for value in values[begin:end]:
        result = compose(result, value)
    return result


class IntervalTree:
    def __init__(self, values):
        self.size = 1 << (len(values) - 1).bit_length()
        self.nodes = [IDENTITY] * (2 * self.size)
        self.nodes[self.size:self.size + len(values)] = values
        for index in range(self.size - 1, 0, -1):
            self.nodes[index] = compose(self.nodes[2 * index], self.nodes[2 * index + 1])

    def query(self, begin, end):
        left, right = IDENTITY, IDENTITY
        begin += self.size
        end += self.size
        reads = 0
        while begin < end:
            if begin & 1:
                left = compose(left, self.nodes[begin])
                reads += 1
                begin += 1
            if end & 1:
                end -= 1
                right = compose(self.nodes[end], right)
                reads += 1
            begin //= 2
            end //= 2
        return compose(left, right), reads

    def update(self, index, value):
        index += self.size
        self.nodes[index] = value
        merges = 0
        while index > 1:
            index //= 2
            self.nodes[index] = compose(self.nodes[2 * index], self.nodes[2 * index + 1])
            merges += 1
        return merges


def tree_experiment():
    rng = random.Random(20260926)
    results = []
    for size in [16, 64, 256, 1024]:
        values = [(rng.random(), rng.choice([0.0, 1.0, rng.random()])) for _ in range(size)]
        tree = IntervalTree(values)
        max_error, max_reads, checks = 0.0, 0, 0
        for _ in range(2000):
            begin, end = sorted([rng.randrange(size + 1), rng.randrange(size + 1)])
            actual, reads = tree.query(begin, end)
            expected = direct(values, begin, end)
            error = max(abs(a - b) for a, b in zip(actual, expected))
            max_error, max_reads = max(error, max_error), max(reads, max_reads)
            assert error < 1e-12
            checks += 1
        max_updates = 0
        for _ in range(100):
            index = rng.randrange(size)
            values[index] = (rng.random(), rng.random())
            max_updates = max(max_updates, tree.update(index, values[index]))
            actual, _ = tree.query(0, size)
            expected = direct(values, 0, size)
            error = max(abs(a - b) for a, b in zip(actual, expected))
            max_error = max(error, max_error)
            assert error < 1e-12
            checks += 1
        results.append(dict(cells=size, checks=checks, max_error=max_error,
                            max_sampled_node_reads=max_reads, update_merges=max_updates,
                            stored_nodes=2 * size - 1))
    return results


def ceil_div(value, divisor):
    return (value + divisor - 1) // divisor


def dense_storage(width, height):
    """Current fresh allocation sizes, excluding images, weights and driver padding."""
    levels = max(width, height).bit_length()
    if max(width, height) == 1 << (levels - 1):
        levels -= 1
    levels = min(levels, 15)
    intervals, max_r = 0, 0
    for level in range(levels):
        step = 1 << level
        intervals += max(ceil_div(w, step) * (step + 1) * h
                         for w, h in [(width, height), (height, width)])
        max_r = max(max_r, *(ceil_div(w, step) * step * h
                            for w, h in [(width, height), (height, width)]))
    return dict(width=width, height=height, levels=levels,
                interval_mib=intervals * 8 / 2**20,
                hierarchy_mib=(intervals * 8 + max_r * 8 + width * height * 16) / 2**20)


def compact_storage(width, height, trace_levels=3):
    """T0/T1 experiment: actual buffer slots and unpadded logical trace work."""
    dense = dense_storage(width, height)
    cutoff = min(trace_levels, dense['levels'])
    saved, traced_dense, traced_compact = 0, 0, 0
    for level in range(cutoff):
        counts = []
        for w, h in [(width, height), (height, width)]:
            probes = ceil_div(w, 1 << level)
            row = probes * ((1 << level) + 1)
            packed = row
            if level == 0 and cutoff >= 2:
                packed = probes // 2 * 2
            if level == 1 and cutoff >= 3:
                packed = probes // 2 * 5 + probes % 2 * 2
            counts.append((row * h, packed * h))
        saved += max(a for a, _ in counts) - max(b for _, b in counts)
        traced_dense += 2 * sum(a for a, _ in counts)
        traced_compact += 2 * sum(b for _, b in counts)
    return dict(width=width, height=height, trace_levels=trace_levels,
                rgb_saved_mib=saved * 8 / 2**20,
                scalar_saved_mib=saved * 4 / 2**20,
                logical_traces_dense=traced_dense,
                logical_traces_compact=traced_compact)


def dependency_audit(width, height, trace_levels):
    """Union of in-bounds reads for every R0 output in one quadrant.

    Mirrors index maps only from merge.shd and extend.shd. It does not prune
    based on values or material occupancy, and does not remove known-zero R.
    """
    levels = (max(width, height) - 1).bit_length()
    radiance = [set() for _ in range(levels)]
    intervals = [set() for _ in range(levels)]
    radiance[0] = {(x, 0, y) for x in range(width) for y in range(height)}

    def mark(target, level, x, direction, y, extra_direction):
        if level >= levels:
            return
        step = 1 << level
        if 0 <= x < ceil_div(width, step) and 0 <= y < height and 0 <= direction < step + extra_direction:
            target[level].add((x, direction, y))

    for level in range(levels):
        step = 1 << level
        for x, direction, y in radiance[level]:
            if x & 1:
                for edge in [direction, direction + 1]:
                    mark(intervals, level, x, edge, y, 1)
                for side in [0, 1]:
                    mark(radiance, level + 1, (x + 1) // 2, 2 * direction + side,
                         y + 2 * (direction + side) - step, 0)
            else:
                for side in [0, 1]:
                    mark(intervals, level + 1, x // 2, 2 * (direction + side), y, 1)
                    mark(radiance, level + 1, x // 2, 2 * direction + side, y, 0)
                    mark(radiance, level + 1, x // 2 + 1, 2 * direction + side,
                         y + 4 * (direction + side) - 2 * step, 0)

    for level in range(levels - 1, trace_levels - 1, -1):
        step = 1 << (level - 1)
        for x, direction, y in intervals[level]:
            low, high = direction // 2, (direction + 1) // 2
            for near, far in {(low, high), (high, low)}:
                mark(intervals, level - 1, 2 * x, near, y, 1)
                mark(intervals, level - 1, 2 * x + 1, far, y + 2 * near - step, 1)

    rows = []
    for level in range(levels):
        step = 1 << level
        dense_t = ceil_div(width, step) * (step + 1) * height
        dense_r = ceil_div(width, step) * step * height
        rows.append(dict(level=level, interval_live=len(intervals[level]), interval_dense=dense_t,
                         radiance_live=len(radiance[level]), radiance_dense=dense_r))
    return dict(width=width, height=height, trace_levels=trace_levels, levels=rows)


def sparse_tree_counts():
    """Coordinate-preserving binary OR tree; unary ancestors are retained."""
    rng = random.Random(3)
    size = 1024
    patterns = {
        'one_cell': {size // 2},
        'contiguous_64': set(range(480, 544)),
        'scattered_64': set(rng.sample(range(size), 64)),
        'all_cells': set(range(size)),
    }
    results = []
    for name, leaves in patterns.items():
        current, counts = leaves, [len(leaves)]
        for _ in range((size - 1).bit_length()):
            current = {index // 2 for index in current}
            counts.append(len(current))
        results.append(dict(pattern=name, active_leaves=len(leaves), stored_nodes=sum(counts),
                            nodes_by_level=counts))
    return results


def resampling_counterexample():
    # Two neighboring lanes. The near blocker hides the far light in lane 0;
    # lane 1 is open near the camera but has no far light.
    near = [(0.0, 0.0), (0.0, 1.0)]
    far = [(1.0, 1.0), (0.0, 0.0)]

    def average(a, b):
        return tuple((x + y) / 2 for x, y in zip(a, b))

    correct = average(compose(near[0], far[0]), compose(near[1], far[1]))
    resampled = compose(average(*near), average(*far))
    assert correct == (0.0, 0.0)
    assert resampled == (0.25, 0.25)
    return dict(compose_then_average=correct, average_then_compose=resampled)


def support_counterexample():
    fine_positions = [0.0, 1.0, 2.0, 3.0]
    coarse_positions = [0.5, 2.5]
    active_fine = 2.0
    nearest = sorted(fine_positions, key=lambda p: abs(p - coarse_positions[0]))[:2]
    low_weight = (coarse_positions[1] - active_fine) / (coarse_positions[1] - coarse_positions[0])
    assert active_fine not in nearest and low_weight > 0
    return dict(active_fine=active_fine, coarse=coarse_positions,
                nearest_fine_to_first_coarse=nearest, required_first_coarse_weight=low_weight)


def main():
    report = {
        'meaning': 'Analytical counts and CPU algebra checks. No GPU time or GI image validation.',
        'interval_tree': tree_experiment(),
        'resampling_counterexample': resampling_counterexample(),
        'nearest_two_support_counterexample': support_counterexample(),
        'coordinate_preserving_sparse_tree': sparse_tree_counts(),
        'current_hrc_allocations': [dense_storage(w, h) for w, h in
                                    [(966, 546), (1030, 610), (1094, 674), (1024, 631), (1007, 621)]],
        'compact_interval_allocations': [compact_storage(w, h) for w, h in
                                         [(1094, 674), (1024, 631)]],
        'current_hrc_dependencies': [dependency_audit(w, h, cutoff) for w, h, cutoff in
                                    [(64, 64, 1), (64, 64, 3), (70, 43, 3), (128, 80, 3)]],
    }
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

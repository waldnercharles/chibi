#!/usr/bin/env python3
"""Headless 2D specular light tracing reference, independent of HRC.

Measures pixel-area-averaged fluence, not a camera image. Paths carry RGB power.
Dielectrics split by exact unpolarized Fresnel; no radiance-mode eta^2 factor
belongs on these forward power paths. Fog removes ballistic power and records
its first-scattering source separately. Diffuse/volume re-scattering is not solved.
"""

import argparse
from array import array
from dataclasses import dataclass
import json
import math
from pathlib import Path
import struct
import sys
import time
import zlib

EPSILON = 1e-9
WHITE = (1.0, 1.0, 1.0)
BLACK = (0.0, 0.0, 0.0)


def add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def scale(a, s):
    return (a[0] * s, a[1] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def cross(a, b):
    return a[0] * b[1] - a[1] * b[0]


def unit(v):
    return scale(v, 1 / math.hypot(*v))


def dielectric(direction, normal, incident_ior, transmitted_ior):
    """Normal faces the incident medium; directions point along light travel."""
    cosine = max(0.0, min(1.0, -dot(direction, normal)))
    reflected = add(direction, scale(normal, 2 * cosine))
    if incident_ior == transmitted_ior:
        return reflected, direction, 0.0
    ratio = incident_ior / transmitted_ior
    sine_squared = ratio * ratio * max(0.0, 1 - cosine * cosine)
    if sine_squared >= 1:
        return reflected, None, 1.0
    transmitted_cosine = math.sqrt(1 - sine_squared)
    transmitted = add(scale(direction, ratio),
                      scale(normal, ratio * cosine - transmitted_cosine))
    s = ((incident_ior * cosine - transmitted_ior * transmitted_cosine) /
         (incident_ior * cosine + transmitted_ior * transmitted_cosine))
    p = ((transmitted_ior * cosine - incident_ior * transmitted_cosine) /
         (transmitted_ior * cosine + incident_ior * transmitted_cosine))
    return reflected, transmitted, 0.5 * (s * s + p * p)


@dataclass(frozen=True)
class Material:
    kind: str = "glass"
    ior: float = 1.5
    absorption: tuple = BLACK
    reflectance: tuple = WHITE


@dataclass(frozen=True)
class Shape:
    material: Material
    center: tuple = (0.0, 0.0)
    radius: float = 0.0
    points: tuple = ()  # CCW convex polygon, or two endpoints of a mirror.

    def contains(self, position):
        if self.radius:
            return math.dist(position, self.center) < self.radius
        if len(self.points) < 3:
            return False
        return all(cross(add(b, scale(a, -1)), add(position, scale(a, -1))) > 0
                   for a, b in zip(self.points, self.points[1:] + self.points[:1]))

    def area(self):
        if self.radius:
            return math.pi * self.radius**2
        return 0.5 * sum(cross(a, b) for a, b in
                         zip(self.points, self.points[1:] + self.points[:1]))

    def hit(self, origin, direction):
        if self.radius:
            relative = add(origin, scale(self.center, -1))
            b = dot(relative, direction)
            discriminant = b * b - dot(relative, relative) + self.radius**2
            if discriminant <= 0:
                return None
            root = math.sqrt(discriminant)
            for distance in (-b - root, -b + root):
                if distance > EPSILON:
                    return distance, unit(add(relative, scale(direction, distance)))
            return None
        nearest = None
        ends = self.points[1:] if len(self.points) == 2 else self.points[1:] + self.points[:1]
        for a, b in zip(self.points, ends):
            edge = add(b, scale(a, -1))
            determinant = cross(direction, edge)
            if abs(determinant) < EPSILON:
                continue
            relative = add(a, scale(origin, -1))
            distance = cross(relative, edge) / determinant
            fraction = cross(relative, direction) / determinant
            if distance > EPSILON and 0 <= fraction <= 1:
                if nearest is None or distance < nearest[0]:
                    nearest = distance, unit((edge[1], -edge[0]))
        return nearest


def grid_segments(origin, direction, distance, columns, rows, extent):
    """Exact path lengths through a piecewise constant, rectangular field."""
    cells = (columns, rows)
    cell_size = (extent[0] / columns, extent[1] / rows)
    index, next_time, delta = [], [], []
    for axis in range(2):
        coordinate = origin[axis] / cell_size[axis]
        cell = math.floor(coordinate)
        if direction[axis] < 0 and abs(coordinate - round(coordinate)) < EPSILON:
            cell = round(coordinate) - 1
        cell = max(0, min(cells[axis] - 1, cell))
        index.append(cell)
        if direction[axis] == 0:
            next_time.append(math.inf)
            delta.append(math.inf)
            continue
        boundary = (cell + (direction[axis] > 0)) * cell_size[axis]
        next_time.append(max(0.0, (boundary - origin[axis]) / direction[axis]))
        delta.append(cell_size[axis] / abs(direction[axis]))
    start = 0.0
    while start < distance:
        end = min(distance, *next_time)
        if end > start:
            yield index[1] * columns + index[0], start, end
        start = end
        if start >= distance:
            break
        for axis in range(2):
            if next_time[axis] <= end + 1e-14:
                index[axis] += 1 if direction[axis] > 0 else -1
                if index[axis] < 0 or index[axis] >= cells[axis]:
                    return
                next_time[axis] += delta[axis]


def attenuate(power, extinction, distance):
    transmission = tuple(math.exp(-s * distance) for s in extinction)
    outgoing = tuple(p * t for p, t in zip(power, transmission))
    integral = tuple(p * (-math.expm1(-s * distance) / s if s else distance)
                     for p, s in zip(power, extinction))
    return outgoing, integral


@dataclass(frozen=True)
class Scene:
    shapes: tuple = ()
    extent: tuple = (2.0, 1.125)
    ambient_ior: float = 1.0
    density: tuple = (0.0,)
    density_size: tuple = (1, 1)
    extinction: float = 0.0
    scattering_albedo: tuple = (0.8, 0.8, 0.8)


class Reference:
    def __init__(self, scene, width, height, max_events=16):
        self.scene, self.width, self.height = scene, width, height
        self.max_events = max_events
        self.fluence = array("d", [0.0]) * (width * height * 3)
        self.scattering_source = array("d", [0.0]) * (width * height * 3)
        self.ledger = {name: [0.0] * 3 for name in
                       ("emitted", "escaped", "absorbed", "scattered", "unresolved")}
        assert len(scene.density) == math.prod(scene.density_size)
        assert all(math.isfinite(rho) and rho >= 0 for rho in scene.density)
        assert scene.extinction >= 0 and scene.ambient_ior > 0
        assert all(0 <= a <= 1 for a in scene.scattering_albedo)
        assert width > 0 and height > 0 and max_events >= 0
        for shape in scene.shapes:
            m = shape.material
            assert m.kind in ("glass", "mirror", "absorber")
            assert m.ior > 0 and all(s >= 0 for s in m.absorption)
            assert all(0 <= r <= 1 for r in m.reflectance)
            assert shape.radius > 0 or len(shape.points) >= 2
            if len(shape.points) > 2:
                assert shape.area() > 0
                assert all(cross(add(b, scale(a, -1)), add(c, scale(b, -1))) > 0
                           for a, b, c in zip(shape.points, shape.points[1:] + shape.points[:1],
                                              shape.points[2:] + shape.points[:2]))
            assert m.kind != "glass" or shape.radius > 0 or len(shape.points) >= 3

    def account(self, name, power):
        for c in range(3):
            self.ledger[name][c] += power[c]

    def segment(self, origin, direction, distance, power, inside):
        scene = self.scene
        area = math.prod(scene.extent) / (self.width * self.height)
        if inside:
            pieces = [(0, 0.0, distance)]
        else:
            pieces = grid_segments(origin, direction, distance, *scene.density_size, scene.extent)
        for cell, start, end in pieces:
            sigma = (scene.shapes[inside[-1]].material.absorption if inside else
                     (scene.extinction * scene.density[cell],) * 3)
            position = add(origin, scale(direction, start))
            for pixel, a, b in grid_segments(position, direction, end - start,
                                             self.width, self.height, scene.extent):
                outgoing, integral = attenuate(power, sigma, b - a)
                for c in range(3):
                    self.fluence[pixel * 3 + c] += integral[c] / area
                    removed = power[c] - outgoing[c]
                    scattered = 0.0 if inside else removed * scene.scattering_albedo[c]
                    self.ledger["absorbed"][c] += removed - scattered
                    self.ledger["scattered"][c] += scattered
                    # Total first-scattering source per area, integrated over direction.
                    self.scattering_source[pixel * 3 + c] += scattered / area
                power = outgoing
        return power

    def trace(self, origin, direction, power=WHITE):
        scene = self.scene
        direction = unit(direction)
        assert all(0 <= x < extent for x, extent in zip(origin, scene.extent))
        assert all(p >= 0 for p in power)
        inside = tuple(sorted((i for i, shape in enumerate(scene.shapes)
                               if shape.material.kind == "glass" and shape.contains(origin)),
                              key=lambda i: -scene.shapes[i].area()))
        self.account("emitted", power)
        pending = [(origin, direction, power, inside, 0)]
        while pending:
            position, direction, power, inside, events = pending.pop()
            distance = min((scene.extent[a] - position[a]) / direction[a]
                           if direction[a] > 0 else -position[a] / direction[a]
                           for a in range(2) if direction[a])
            hit = None
            for i, shape in enumerate(scene.shapes):
                candidate = shape.hit(position, direction)
                if candidate and candidate[0] < distance:
                    distance, normal = candidate
                    hit = (i, normal)
            power = self.segment(position, direction, distance, power, inside)
            if hit is None:
                self.account("escaped", power)
                continue
            position = add(position, scale(direction, distance))
            i, normal = hit
            material = scene.shapes[i].material
            entering = dot(direction, normal) < 0
            normal = normal if entering else scale(normal, -1)
            if material.kind == "absorber":
                self.account("absorbed", power)
                continue
            if events >= self.max_events:
                self.account("unresolved", power)
                continue
            reflected = add(direction, scale(normal, -2 * dot(direction, normal)))
            if material.kind == "mirror":
                reflected_power = tuple(p * r for p, r in zip(power, material.reflectance))
                self.account("absorbed", tuple(p - r for p, r in zip(power, reflected_power)))
                pending.append((position, reflected, reflected_power, inside, events + 1))
                continue
            if entering:
                assert i not in inside, "Non-nested overlapping dielectrics are unsupported"
                transmitted_inside = inside + (i,)
            else:
                assert inside and inside[-1] == i, "Non-nested overlapping dielectrics are unsupported"
                transmitted_inside = inside[:-1]
            eta_i = scene.shapes[inside[-1]].material.ior if inside else scene.ambient_ior
            eta_t = (scene.shapes[transmitted_inside[-1]].material.ior
                     if transmitted_inside else scene.ambient_ior)
            reflected, transmitted, fresnel = dielectric(direction, normal, eta_i, eta_t)
            if fresnel:
                pending.append((position, reflected, tuple(p * fresnel for p in power),
                                inside, events + 1))
            if transmitted is not None and fresnel < 1:
                pending.append((position, transmitted, tuple(p * (1 - fresnel) for p in power),
                                transmitted_inside, events + 1))


def fixture(name, fog):
    glass = Material(absorption=(0.15, 0.05, 0.01))
    shapes = {
        "lens": (Shape(glass, (0.8, 0.5625), 0.23),),
        "slab": (Shape(glass, points=((0.65, 0.15), (0.9, 0.15),
                                     (0.9, 0.975), (0.65, 0.975))),),
        "prism": (Shape(glass, points=((0.55, 0.25), (1.0, 0.25), (0.775, 0.95))),),
        "mirror": (Shape(Material("mirror", reflectance=(0.95, 0.85, 0.7)),
                         points=((0.55, 0.2), (1.15, 0.8))),),
    }
    density = tuple(1 + 0.25 * math.cos(x * math.tau / 32) * math.sin(y * math.tau / 18)
                    for y in range(18) for x in range(32))
    return Scene(shapes[name], density=density, density_size=(32, 18), extinction=fog)


def write_images(directory, name, values, width, height, exposure):
    # PFM is linear HDR and bottom-to-top. PNG is a fixed-exposure visualization.
    floats = array("f", values)
    if sys.byteorder != "little":
        floats.byteswap()
    (directory / f"{name}.pfm").write_bytes(f"PF\n{width} {height}\n-1.0\n".encode() + floats.tobytes())
    rows = bytearray()
    for y in reversed(range(height)):
        rows.append(0)
        for value in values[y * width * 3:(y + 1) * width * 3]:
            linear = -math.expm1(-exposure * max(0, value))
            srgb = 12.92 * linear if linear <= .0031308 else 1.055 * linear**(1 / 2.4) - .055
            rows.append(round(255 * srgb))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    (directory / f"{name}.png").write_bytes(png + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", choices=("lens", "slab", "prism", "mirror"), default="lens")
    parser.add_argument("--rays", type=int, default=2048)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=360)
    parser.add_argument("--events", type=int, default=12)
    parser.add_argument("--fog", type=float, default=0.2)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.rays < 1 or min(args.width, args.height) < 1 or args.events < 1 or args.fog < 0:
        parser.error("rays, dimensions and events must be positive; fog must be nonnegative")
    reference = Reference(fixture(args.scene, args.fog), args.width, args.height, args.events)
    start = time.monotonic()
    for i in range(args.rays):
        # Midpoint quadrature of a finite collimated emitter, total power = 1 RGB.
        reference.trace((0.06, 0.3625 + 0.4 * (i + 0.5) / args.rays), (1.0, 0.0),
                        (1 / args.rays,) * 3)
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("fluence", "scattering_source"):
        write_images(args.output, name, getattr(reference, name), args.width, args.height, 0.25)
    ledger = reference.ledger
    residual = [ledger["emitted"][c] - sum(ledger[k][c] for k in ledger if k != "emitted")
                for c in range(3)]
    report = dict(scene=args.scene, rays=args.rays, width=args.width, height=args.height,
                  max_events=args.events, fog_extinction=args.fog, seconds=time.monotonic() - start,
                  measure="pixel-area-averaged 2D ballistic fluence", ledger=ledger,
                  power_residual=residual, display_exposure=0.25,
                  limitations=["No diffuse or volume rescattering", "No view-dependent camera image",
                               "RGB absorption with one IOR; no spectral dispersion",
                               "Non-nested overlapping dielectric interiors unsupported",
                               "Finite event budget; remaining power reported as unresolved",
                               "Finite source quadrature; convergence must be checked separately"])
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

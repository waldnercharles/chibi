"""Analytic transport checks for the independent, windowless optics reference."""

import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from optics_reference import (Material, Reference, Scene, Shape, add, attenuate,
                              dielectric, dot, grid_segments, scale)


class OpticsReferenceTest(unittest.TestCase):
    def assertPowerAccounted(self, reference):
        for channel in range(3):
            ledger = reference.ledger
            self.assertAlmostEqual(ledger["emitted"][channel],
                                   sum(value[channel] for key, value in ledger.items()
                                       if key != "emitted"), places=11)

    def test_normal_incidence_fresnel(self):
        reflected, transmitted, fresnel = dielectric((1, 0), (-1, 0), 1, 1.5)
        self.assertEqual(reflected, (-1, 0))
        self.assertEqual(transmitted, (1, 0))
        self.assertAlmostEqual(fresnel, 0.04, places=14)

    def test_snell_and_reciprocity(self):
        incident = (math.cos(math.pi / 6), math.sin(math.pi / 6))
        reflected, transmitted, fresnel = dielectric(incident, (-1, 0), 1, 1.5)
        self.assertAlmostEqual(transmitted[1], 1 / 3, places=14)
        self.assertAlmostEqual(math.hypot(*reflected), 1)
        self.assertAlmostEqual(math.hypot(*transmitted), 1)
        _, reverse, reverse_fresnel = dielectric(scale(transmitted, -1), (1, 0), 1.5, 1)
        self.assertAlmostEqual(dot(reverse, incident), -1, places=14)
        self.assertAlmostEqual(reverse_fresnel, fresnel, places=14)

    def test_total_internal_reflection_and_equal_index_grazing(self):
        ray = (math.cos(math.radians(50)), math.sin(math.radians(50)))
        _, transmitted, fresnel = dielectric(ray, (-1, 0), 1.5, 1)
        self.assertIsNone(transmitted)
        self.assertEqual(fresnel, 1)
        _, transmitted, fresnel = dielectric((0, 1), (-1, 0), 1, 1)
        self.assertEqual(transmitted, (0, 1))
        self.assertEqual(fresnel, 0)

    def test_integral_in_vacuum_and_thin_medium(self):
        for sigma in (0.0, 1e-10, .2, 40):
            outgoing, integral = attenuate((1, .5, .25), (sigma,) * 3, 2)
            self.assertAlmostEqual(outgoing[0], math.exp(-sigma * 2))
            # Independently evaluate the thin limit; exp subtraction would lose precision.
            if sigma < 1e-5:
                self.assertAlmostEqual(integral[0], 2 - 2 * sigma, places=12)

    def test_grid_corner_and_reverse_paths(self):
        root = math.sqrt(2)
        pieces = list(grid_segments((0, 0), (1 / root,) * 2, root, 8, 8, (1, 1)))
        self.assertEqual(len(pieces), 8)
        self.assertEqual([p[0] for p in pieces], [9 * i for i in range(8)])
        reverse = list(grid_segments((1, 1), (-1 / root,) * 2, root, 8, 8, (1, 1)))
        self.assertEqual([p[0] for p in reverse], [9 * i for i in reversed(range(8))])
        self.assertAlmostEqual(sum(b - a for _, a, b in reverse), root)

    def test_piecewise_density_and_output_resolution(self):
        scene = Scene(extent=(2, 1), density=(1, 3), density_size=(2, 1),
                      extinction=.7, scattering_albedo=(.8, .5, 0))
        expected_t = math.exp(-.7 * (.8 + 3))
        expected_integral = -math.expm1(-.56) / .7 + math.exp(-.56) * -math.expm1(-2.1) / 2.1
        for width in (31, 64, 97):
            reference = Reference(scene, width, 17)
            reference.trace((.2, .5), (1, 0))
            self.assertPowerAccounted(reference)
            self.assertAlmostEqual(reference.ledger["escaped"][0], expected_t, places=12)
            self.assertAlmostEqual(reference.ledger["scattered"][0], (1 - expected_t) * .8)
            self.assertAlmostEqual(reference.ledger["absorbed"][1], (1 - expected_t) * .5)
            self.assertEqual(reference.ledger["scattered"][2], 0)
            self.assertAlmostEqual(sum(reference.fluence[::3]) * 2 / (width * 17),
                                   expected_integral, places=12)
            self.assertAlmostEqual(sum(reference.scattering_source[::3]) * 2 / (width * 17),
                                   (1 - expected_t) * .8, places=12)

    def test_colored_glass_slab_including_internal_reflections(self):
        absorption = (.2, .5, 1.2)
        slab = Shape(Material(absorption=absorption),
                     points=((.8, .1), (1.2, .1), (1.2, .9), (.8, .9)))
        reference = Reference(Scene((slab,), extent=(2, 1)), 80, 40, 24)
        reference.trace((.1, .5), (1, 0))
        self.assertPowerAccounted(reference)
        # Integrate fluence across a downstream column to recover transmitted power.
        for channel, sigma in enumerate(absorption):
            actual = sum(reference.fluence[(y * 80 + 65) * 3 + channel] for y in range(40)) / 40
            t = math.exp(-sigma * .4)
            expected = .96**2 * t / (1 - .04**2 * t**2)
            self.assertAlmostEqual(actual, expected, places=11)
        self.assertLess(max(reference.ledger["unresolved"]), 1e-20)

    def test_tinted_mirror_redirects_power(self):
        mirror = Shape(Material("mirror", reflectance=(.9, .8, .7)),
                       points=((.6, .2), (1.2, .8)))
        reference = Reference(Scene((mirror,), extent=(2, 1)), 80, 40)
        reference.trace((.1, .5), (1, 0))
        self.assertPowerAccounted(reference)
        for channel, expected in enumerate((.9, .8, .7)):
            self.assertAlmostEqual(reference.ledger["escaped"][channel], expected)
            # Reflected path crosses a row above the mirror; no right-going beam remains.
            actual = sum(reference.fluence[(35 * 80 + x) * 3 + channel] for x in range(80)) * 2 / 80
            self.assertAlmostEqual(actual, expected, places=11)
            self.assertEqual(sum(reference.fluence[(y * 80 + 70) * 3 + channel] for y in range(40)), 0)

    def test_circle_lens_focus_matches_paraxial_prediction(self):
        lens = Shape(Material(), (.8, .5625), .23)
        position, direction = (.1, .5635), (1, 0)
        distance, normal = lens.hit(position, direction)
        position = add(position, scale(direction, distance))
        _, direction, _ = dielectric(direction, normal, 1, 1.5)
        distance, normal = lens.hit(position, direction)
        position = add(position, scale(direction, distance))
        _, direction, _ = dielectric(direction, scale(normal, -1), 1.5, 1)
        focus_x = position[0] + (.5625 - position[1]) / direction[1] * direction[0]
        expected = .8 + 1.5 * .23 / (2 * (1.5 - 1))
        self.assertAlmostEqual(focus_x, expected, delta=3e-6)

    def test_nested_glass_inside_water(self):
        water = Shape(Material(ior=1.333), (.8, .5625), .3)
        glass = Shape(Material(), (.8, .5625), .15)
        # Identical IOR on both sides of the outer boundary makes it invisible.
        nested = Reference(Scene((water, glass), ambient_ior=1.333), 64, 36, 12)
        direct = Reference(Scene((glass,), ambient_ior=1.333), 64, 36, 12)
        for reference in (nested, direct):
            reference.trace((.1, .62), (1, 0))
            self.assertPowerAccounted(reference)
        self.assertLess(max(abs(a - b) for a, b in zip(nested.fluence, direct.fluence)), 1e-8)

    def test_trapped_ray_is_reported_not_forced_out(self):
        glass = Shape(Material(), (.8, .5625), .23)
        reference = Reference(Scene((glass,)), 64, 36, 12)
        reference.trace((.8, .7625), (1, 0))
        self.assertPowerAccounted(reference)
        self.assertEqual(reference.ledger["escaped"], [0, 0, 0])
        self.assertEqual(reference.ledger["unresolved"], [1, 1, 1])

    def test_two_interface_budget_still_draws_transmitted_beam(self):
        slab = Shape(Material(), points=((.8, .1), (1.2, .1), (1.2, .9), (.8, .9)))
        reference = Reference(Scene((slab,), extent=(2, 1)), 80, 40, 2)
        reference.trace((.1, .5), (1, 0))
        self.assertPowerAccounted(reference)
        transmitted = sum(reference.fluence[(y * 80 + 65) * 3] for y in range(40)) / 40
        self.assertAlmostEqual(transmitted, .96**2, places=11)
        self.assertAlmostEqual(reference.ledger["unresolved"][0], .96 * .04)

    def test_opaque_receiver_stops_light(self):
        wall = Shape(Material("absorber"), points=((.8, .1), (.8, .9)))
        reference = Reference(Scene((wall,)), 64, 36)
        reference.trace((.1, .5), (1, 0))
        self.assertPowerAccounted(reference)
        self.assertEqual(reference.ledger["absorbed"], [1, 1, 1])
        self.assertEqual(reference.ledger["escaped"], [0, 0, 0])


if __name__ == "__main__":
    unittest.main()

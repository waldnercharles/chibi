"""Exercise the real Studio CLI without a display or graphics initialization."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = None
SELECTORS = ('--studio-documents', '--studio-shell')
PROFILES = (('iteration', '0'), ('gpu-check', '0'), ('hardware', '0'), ('hardware', '1'))


class StudioSelectors(unittest.TestCase):
    def invoke(self, arguments, profile='iteration', opt_in='0'):
        environment = dict(
            os.environ, CHIBI_EXECUTION_PROFILE=profile, CHIBI_ALLOW_FULL_GPU=opt_in,
            CHIBI_TEST_TIMINGS='1', DISPLAY='', WAYLAND_DISPLAY='',
            SDL_VIDEO_DRIVER='chibi-no-such-video-driver',
            SDL_VIDEODRIVER='chibi-no-such-video-driver',
        )
        result = subprocess.run([str(BINARY), *arguments], cwd=ROOT, env=environment,
                                capture_output=True, text=True, timeout=10)
        output = result.stdout + result.stderr
        self.assertNotIn('STUDIO_BEGIN:', output)
        self.assertNotIn('PASS:', output)
        self.assertNotIn('ALL CHECKS PASSED', output)
        self.assertNotIn('Studio documents app:', output)
        return result

    def test_listing_is_cpu_only_in_every_profile(self):
        for selector in SELECTORS:
            for profile, opt_in in PROFILES:
                with self.subTest(selector=selector, profile=profile, opt_in=opt_in):
                    result = self.invoke([selector, 'list'], profile, opt_in)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    names = result.stdout.splitlines()
                    self.assertEqual(len(names), 19)
                    self.assertEqual(len(set(names)), 19)
                    self.assertIn('properties' if selector == SELECTORS[0]
                                  else 'readout_follows_camera', names)

    def test_errors_are_validated_before_policy_or_graphics(self):
        for selector in SELECTORS:
            cases = [([''], 'unknown Studio'),
                     (['not-a-case'], 'unknown Studio'),
                     (['readout'], 'unknown Studio'),
                     (['list', 'extra'], 'Studio selectors accept one'),
                     (['list', '--other'], 'multiple test_suite selectors')]
            for profile, opt_in in PROFILES:
                for arguments, message in cases:
                    with self.subTest(selector=selector, arguments=arguments,
                                      profile=profile, opt_in=opt_in):
                        result = self.invoke([selector, *arguments], profile, opt_in)
                        self.assertEqual(result.returncode, 2, result.stderr)
                        self.assertIn(message, result.stderr)
                        self.assertNotIn('requires_hardware_or_classification', result.stderr)
                        self.assertEqual(result.stdout, '')

    def test_valid_cases_and_aggregates_require_full_opt_in(self):
        for selector in SELECTORS:
            names = self.invoke([selector, 'list']).stdout.splitlines()
            for profile, opt_in in PROFILES[:-1]:
                for arguments in [[selector], *[[selector, name] for name in names]]:
                    with self.subTest(arguments=arguments, profile=profile, opt_in=opt_in):
                        result = self.invoke(arguments, profile, opt_in)
                        self.assertEqual(result.returncode, 2, result.stderr)
                        self.assertIn('selector_requires_hardware_or_classification', result.stderr)
                        self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    BINARY = Path(sys.argv.pop(1)).resolve()
    unittest.main()

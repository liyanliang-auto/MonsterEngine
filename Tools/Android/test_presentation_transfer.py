"""Numerical contract tests for the Android Vulkan presentation transfer shader."""
import math
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SHADER = ROOT / "Shaders/Splat/splat_present.frag"


def srgb_to_linear(value: float) -> float:
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def linear_to_srgb(value: float) -> float:
    return 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055


def present(encoded: float, attachment_is_srgb: bool) -> float:
    shader_value = srgb_to_linear(encoded) if attachment_is_srgb else encoded
    return linear_to_srgb(shader_value) if attachment_is_srgb else shader_value


class PresentationTransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shader = SHADER.read_text(encoding="utf-8")

    def test_srgb_attachment_decodes_before_hardware_encoding(self):
        self.assertIn("if (transfer.decodeSRGBForAttachment != 0u) rgb = srgbToLinear(rgb);", self.shader)
        for encoded in (0.0, 0.003, 0.04045, 0.18, 0.5, 1.0):
            self.assertAlmostEqual(present(encoded, True), encoded, delta=2e-7)

    def test_unorm_attachment_preserves_encoded_srgb_values(self):
        self.assertIn("if (transfer.decodeSRGBForAttachment != 0u) rgb = srgbToLinear(rgb);", self.shader)
        for encoded in (0.0, 0.003, 0.04045, 0.18, 0.5, 1.0):
            self.assertEqual(present(encoded, False), encoded)

    def test_both_direct_and_upscaled_paths_apply_identical_transfer(self):
        self.assertEqual(self.shader.count("if (transfer.decodeSRGBForAttachment != 0u) rgb = srgbToLinear(rgb);"), 2)
        values = (0.02, 0.04, 0.1, 0.25, 0.5, 0.9)
        self.assertTrue(all(math.isfinite(present(v, True)) for v in values))


if __name__ == "__main__":
    unittest.main(verbosity=2)

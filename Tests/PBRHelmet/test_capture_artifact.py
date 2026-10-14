"""Validate the README artifact's native/GPU provenance and encoding."""
import hashlib
import io
import json
from pathlib import Path
import unittest
import os
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = Path(os.environ.get('MONSTER_PBR_EVIDENCE', ROOT / 'specs/20261014-183652-43pk'))

class CaptureArtifactTests(unittest.TestCase):
    def test_jpeg_is_exact_encoding_of_accepted_whole_native_capture(self):
        provenance = json.loads((EVIDENCE / 'screenshot-provenance.json').read_text())
        native_path = EVIDENCE / provenance.get('native_source', 'renderPBR-native.png')
        jpeg_path = ROOT / 'docs/images/samples/Vulkan_PBR_helmet.jpg'
        self.assertEqual(hashlib.sha256(native_path.read_bytes()).hexdigest(), provenance['native_source_sha256'])
        self.assertEqual(hashlib.sha256(jpeg_path.read_bytes()).hexdigest(), provenance['jpeg_sha256'])
        encoded = io.BytesIO()
        Image.open(native_path).save(encoded, format='JPEG', quality=96, subsampling=0)
        self.assertEqual(jpeg_path.read_bytes(), encoded.getvalue())
        with Image.open(jpeg_path) as jpeg:
            self.assertEqual(jpeg.format, 'JPEG')
            self.assertEqual(jpeg.size, tuple(provenance['size']))

    def test_stabilized_native_client_is_repeatable(self):
        provenance = json.loads((EVIDENCE / 'screenshot-provenance.json').read_text())
        x, y = provenance['client_offset']; width, height = provenance['client_size']
        native_client = Image.open(EVIDENCE / provenance.get('native_source', 'renderPBR-native.png')).crop((x, y, x + width, y + height)).convert('RGB')
        repeat = Image.open(EVIDENCE / provenance.get('native_repeat', 'renderPBR-repeat.png')).crop((x, y, x + width, y + height)).convert('RGB')
        self.assertEqual(native_client.size, repeat.size)
        self.assertIsNone(ImageChops.difference(native_client, repeat).getbbox())

    def test_readme_reference_resolves_to_delivered_artifact(self):
        self.assertIn('docs/images/samples/Vulkan_PBR_helmet.jpg', (ROOT / 'README.md').read_text(encoding='utf-8'))
        self.assertTrue((ROOT / 'docs/images/samples/Vulkan_PBR_helmet.jpg').is_file())

if __name__ == '__main__': unittest.main()

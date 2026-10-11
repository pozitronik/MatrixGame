# MatrixGame - licensed under GPLv2 or any later version.
"""Keep the built-in sound catalog aligned with the shipped battle configuration."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SoundCatalogTests(unittest.TestCase):
    def test_catalog_covers_battle_configuration(self):
        referenced = set()
        for path in (ROOT / "MatrixGame/CFG").rglob("*.txt"):
            data = path.read_bytes()
            text = data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
            referenced.update(re.findall(r"\bSound\.([A-Za-z0-9_]+)", text))
        catalog = (ROOT / "MatrixGame/src/SoundMapping.cpp").read_text(encoding="utf-8")
        entries = re.findall(r'\{L"Sound\.([A-Za-z0-9_]+)", LR"\(([^\r\n]+)\)"\}', catalog)
        names = [name for name, _ in entries]
        self.assertTrue(referenced)
        self.assertEqual(len(names), len(set(names)), "Duplicate default sound name")
        # The legacy BReady alias has no known clip; do not invent a substitute.
        self.assertEqual(set(names), referenced - {"BReady"})
        for name, path in entries:
            with self.subTest(name=name):
                self.assertTrue(path.startswith("sound\\") and path.endswith(".wav"))
                self.assertNotIn("..", path.split("\\"))


if __name__ == "__main__":
    unittest.main()

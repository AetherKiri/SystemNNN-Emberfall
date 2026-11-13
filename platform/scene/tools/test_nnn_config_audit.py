import tempfile
import unittest
from pathlib import Path

from nnn_config_audit import FXF, audit, discover, format_pattern, setup_fields, setup_files, upstream_keys


class ConfigAuditTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def test_effective_setup_and_first_declared_field(self):
        self.write("game/INIT/Config.XTX", b"plainOnly 1\r\n")
        encrypted = self.write(
            "game/Init2/cOnFiG.FxF",
            b"@ignored 1\r\nSoundSeNumber\t0\r\n soundsenumber 9\rvalue -1\n"
            b"sampleText \x81\x0b\r\n\0afterEnd 5\r\n".translate(FXF),
        )
        self.write("game/INIT/ignored.fxf", b"")
        self.write("game/Init2/ignored.xtx", b"")
        self.assertEqual(setup_files(self.root / "game"), [("config", encrypted)])
        self.assertEqual(
            setup_fields(encrypted),
            {"soundsenumber": ("SoundSeNumber", 2, "zero"),
             "value": ("value", 4, "set"), "sampletext": ("sampleText", 5, "set")},
        )

    def test_upstream_comments_and_format_candidates(self):
        self.write(
            "src/nnnLib/commonConfig.cpp",
            b'// GetInitGameParam(&flag, "comment")\n'
            b'/* GetInitGameParam(&flag, "alsoComment")\n'
            b' */ GetInitGameParam(&flag, "extSeFlag", 0);\n'
            b'wsprintf(name, "%sSeNumber", category);\n'
            b'if (GetInitGameParam2(&number, name)) Update();\n'
            b'wsprintf(path, "noConfig%d", number);\n'
            b"auto text = 'x';\n",
        )
        literal, formats = upstream_keys(self.root / "src")
        self.assertEqual(literal, {"extseflag": {"nnnLib/commonConfig.cpp:3"}})
        self.assertEqual(formats, {"%sSeNumber": {"nnnLib/commonConfig.cpp:5"}})
        self.assertIsNotNone(format_pattern("%sSeNumber").fullmatch("voicesenumber"))
        self.assertIsNone(format_pattern("%d%d"))

    def test_reports_confirmed_and_unreviewed_fields(self):
        self.write("src/nnnLib/commonprintmessage.cpp", b'GetInitGameParam(&rows, "fullMessageNumber");\n')
        self.write("game/init/PrintMessage.xtx", b"fullMessageNumber 6\nlocalExtension 0\n")
        self.write("game/init/other.xtx", b"fullMessageNumber 6\n")
        self.write("game/SPT/sys.spt", b"")
        self.assertEqual(list(discover(self.root)), [self.root / "game"])
        game = audit([self.root / "game"], self.root / "src")["games"][0]
        self.assertEqual(game["status_counts"], {"REVIEW": 1, "UNSUPPORTED_BEHAVIOR": 1, "UNKNOWN": 1})
        self.assertEqual(game["fields"][1]["upstream"], {"literal": ["nnnLib/commonprintmessage.cpp:1"]})
        self.assertEqual(game["fields"][2]["value"], "zero")


if __name__ == "__main__":
    unittest.main()

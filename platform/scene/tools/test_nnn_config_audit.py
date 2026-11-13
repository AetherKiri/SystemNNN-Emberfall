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
        self.assertEqual(game["status_counts"], {"REVIEW": 1, "SUPPORTED": 1, "UNKNOWN": 1})
        self.assertEqual(game["fields"][1]["upstream"], {"literal": ["nnnLib/commonprintmessage.cpp:1"]})
        self.assertEqual(game["fields"][2]["value"], "zero")

    def test_upstream_unused_and_wave1_supported_classifications(self):
        self.write(
            "src/nnnLib/commonprintmessage.cpp",
            b'GetInitGameParam(&m_LBlockX, "LBlockX");\n'
            b'GetInitGameParam(&m_LBlockY, "LBlockY");\n'
            b'GetInitGameParam(&rows, "fullMessageNumber");\n',
        )
        self.write("game/SPT/sys.spt", b"")
        self.write("game/init/printmessage.xtx", b"LBlockX 1\nLBlockY 20\nfullMessageNumber 12\n")
        self.write("game/init/selectobject.xtx", b"optionOffVarFlag 1\noptionOffVarname hideOpt\n")
        self.write("game/init/selectplace.xtx", b"optionOffVarFlag 1\noptionOffVarname hidePlace\n")
        self.write("game/init/config.xtx", b"MustStartPage1Flag 1\n")
        self.write("game/init/listenBGM.xtx", b"cannotColorR 64\ncannotColorG 80\ncannotColorB 96\n")
        self.write(
            "game/init/selectcg.xtx",
            b"filenameDifferenceFont ta_cg_font\ndifferencePrintFlag 1\ndifferencePrintByGetFlag 0\n"
            b"differencePrintX 8\ndifferencePrintY 12\ndifferenceFontSizeX 21\ndifferenceFontSizeY 27\n"
            b"differenceFontNextX 18\ndifferentKeta 2\n",
        )
        self.write(
            "game/init/systemMenu.xtx",
            b"systemMenuSceneBackFileName ta_back\nsystemMenuSceneEndFileName ta_end\n",
        )
        self.write(
            "game/init/dataFile.xtx",
            b"filenameOverwriteCursor ta_ow\n"
            b"saveAnimePattern 4\nsaveAnimePattern2 2\nsaveAnimeSpeed 3\nsaveAnimeSpeed2 1\n"
            b"saveAnimeType 1\nsaveAnimeType2 5\n"
            b"loadAnimePattern 4\nloadAnimePattern2 2\nloadAnimeSpeed 3\nloadAnimeSpeed2 1\n"
            b"loadAnimeType 1\nloadAnimeType2 5\n"
            b"overwriteAnimePattern 6\noverwriteAnimePattern2 2\noverwriteAnimeSpeed 2\noverwriteAnimeSpeed2 1\n"
            b"overwriteAnimeType 3\noverwriteAnimeType2 5\n",
        )
        game = audit([self.root / "game"], self.root / "src")["games"][0]
        self.assertEqual(
            game["status_counts"],
            {"SUPPORTED": 35, "UPSTREAM_UNUSED": 6},
        )
        by_key = {(Path(row["file"]).stem.lower(), row["key"].lower()): row for row in game["fields"]}
        self.assertIn("commonprintmessage.cpp:398-404", by_key[("printmessage", "lblockx")]["behavior"])
        self.assertIn("commonSelectObject.cpp:607", by_key[("selectobject", "optionoffvarflag")]["behavior"])
        self.assertIn("commonSelectPlace.cpp:614", by_key[("selectplace", "optionoffvarname")]["behavior"])

    def test_wave2_long_print_and_datafile_game_date_supported(self):
        self.write("src/nnnLib/commonprintmessage.cpp", b'GetInitGameParam(&rows, "fullMessageNumber");\n')
        self.write(
            "src/nnnUtilLib/dataFileSetup.cpp",
            b'GetInitGameParam(&flag, "gameYearPrintFlag");\n'
            b'GetInitGameParam(&flag, "gameDatePrintFlag");\n'
            b'GetInitGameParam(&flag, "gameWeekPrintFlag");\n'
            b'GetInitGameParam(&x, "gameDatePrintX");\n'
            b'GetInitGameParam(&y, "gameDatePrintY");\n',
        )
        self.write("game/SPT/sys.spt", b"")
        self.write("game/init/printmessage.xtx", b"fullMessageNumber 15\n")
        self.write(
            "game/init/dataFile.xtx",
            b"gameYearPrintFlag 0\ngameDatePrintFlag 1\ngameWeekPrintFlag 1\n"
            b"gameWeekYouPrintFlag 0\ngameWeekBiPrintFlag 0\n"
            b"filenameGameTimeFont ta_sl_font2\nfilenameGameWeekFont ta_sl_week\n"
            b"gameTimePrintX 120\ngameTimePrintY 48\ngameTimeFontSizeX 14\ngameTimeFontSizeY 20\n"
            b"gameTimeFontNextX 14\ngameDatePrintX 10\ngameDatePrintY 20\ngameDateSlashPrintFlag 1\n"
            b"gameMonthUpZeroPrintFlag 1\ngameDayUpZeroPrintFlag 1\n"
            b"gameMonthPrintX 120\ngameMonthPrintY 48\ngameDayPrintX 162\ngameDayPrintY 48\n"
            b"gameWeekPrintX 200\ngameWeekPrintY 48\ngameWeekFontSizeX 20\ngameWeekFontSizeY 20\n",
        )
        game = audit([self.root / "game"], self.root / "src")["games"][0]
        self.assertEqual(game["status_counts"], {"SUPPORTED": 24, "UPSTREAM_UNUSED": 2})

    def test_kazari_inactive_group_folding_when_disabled_or_missing(self):
        self.write(
            "src/nnnLib/commonSelectSceneChara.cpp",
            b'GetInitGameParam(&flag, "kazariPrintFlag");\n'
            b'GetInitGameParam(&x, "kazariPrintX");\n'
            b'GetInitGameParam(&y, "kazariPrintY");\n'
            b'GetInitGameString(&name, "filenameKazari");\n',
        )
        self.write("off/SPT/sys.spt", b"")
        self.write(
            "off/init/printmessage.xtx",
            b"kazariPrintFlag 0\nkazariPrintX 10\nkazariPrintY 20\n",
        )
        self.write(
            "off/init/dataFile.xtx",
            b"filenameKazari ta_kazari\nkazariSizeX 32\n",
        )
        self.write("on/SPT/sys.spt", b"")
        self.write(
            "on/init/printmessage.xtx",
            b"kazariPrintFlag 1\nkazariPrintX 10\nkazariPrintY 20\n",
        )
        self.write(
            "on/init/dataFile.xtx",
            b"kazariPrintFlag 1\nfilenameKazari ta_kazari\nkazariSizeX 32\n",
        )
        games = {g["game"]: g for g in audit([self.root / "off", self.root / "on"], self.root / "src")["games"]}
        self.assertEqual(games["off"]["status_counts"], {"INACTIVE_IN_GAME": 4, "REVIEW": 1})
        off_Status = {row["key"]: row["status"] for row in games["off"]["fields"]}
        self.assertEqual(off_Status["kazariPrintFlag"], "REVIEW")
        self.assertEqual(off_Status["kazariPrintX"], "INACTIVE_IN_GAME")
        self.assertEqual(off_Status["filenameKazari"], "INACTIVE_IN_GAME")

        self.assertNotIn("INACTIVE_IN_GAME", games["on"]["status_counts"])
        self.assertEqual(games["on"]["status_counts"], {"REVIEW": 5, "UNKNOWN": 1})

    def test_wave2_5_adjust_load_bgm_supported(self):
        self.write(
            "src/nnnLib/gamecallback.cpp",
            b'GetInitGameParam(&m_adjustLoadBGMFlag, "adjustLoadBGMFlag");\n'
            b'GetInitGameString(&adjustBGMVarName, "adjustLoadBGMVarName");\n'
            b'sprintf_s(name, sizeof(name), "adjustLoadBGMNumber%d", i + 1);\n'
            b'GetInitGameParam(&target, name);\n',
        )
        self.write("game/SPT/sys.spt", b"")
        self.write(
            "game/init/game.xtx",
            b"adjustLoadBGMFlag 1\nadjustLoadBGMVarName clearFlag\n"
            b"adjustLoadBGMNumber1 3\nadjustLoadBGMNumber2 4\n",
        )
        game = audit([self.root / "game"], self.root / "src")["games"][0]
        self.assertEqual(game["status_counts"], {"SUPPORTED": 4})


if __name__ == "__main__":
    unittest.main()

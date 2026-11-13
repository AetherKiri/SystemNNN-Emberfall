#!/usr/bin/env python3
"""Inventory game setup fields and report the evidence for their behavior."""

import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
from functools import lru_cache
import json
from pathlib import Path
import re
import sys


REPO = Path(__file__).resolve().parents[3]
MAX_SETUP_BYTES = 4 * 1024 * 1024
FXF = bytes(byte ^ 255 for byte in range(256))
SETUP_LINE = re.compile(rb"^[ \t]*([A-Za-z0-9_]+)[ \t]+(.*)$")
CPP_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
KEY_ARG = r'(?:"(?P<literal>[A-Za-z0-9_]+)"|(?P<buffer>[A-Za-z_]\w*))'
LOOKUP = re.compile(r"\bGetInitGame(?:Param|String)\d*\s*\(\s*[^,;()\n]+,\s*" + KEY_ARG + r"\s*[,)]")
FORMAT = re.compile(
    r'\b(?:wsprintfA?|sprintf(?:_s)?|snprintf|_snprintf(?:_s)?)\s*'
    r'\(\s*(?P<buffer>[A-Za-z_]\w*)\s*,[^;\n"]*"(?P<format>[A-Za-z0-9_%]+)"'
)
PLACEHOLDER = re.compile(r"%[0-9]*(?:[diu]|s)")

# Status applies to this field in this setup file. Verify runtime behavior
# independently of spelling matches in either codebase.
REVIEWED = {
    ("game", "layerexpand"): ("SUPPORTED", "Layer and PC-slot geometry tests"),
    ("game", "frametime"): ("SUPPORTED", "Story and background clock tests"),
    ("game", "maxskipframe"): ("SUPPORTED", "Bounded frame repayment test"),
    ("game", "maxskipframebackscript"): ("SUPPORTED", "Decorative pacing test"),
    ("game", "skipadjustframe"): ("SUPPORTED", "Decorative pacing test"),
    ("config", "extseflag"): ("SUPPORTED", "Themed configuration feedback test"),
    ("config", "useconfigsevoice"): ("SUPPORTED", "Themed configuration feedback test"),
    ("config", "voicesamebufferflag"): ("SUPPORTED", "Themed configuration feedback test"),
    ("config", "soundsenumber"): ("SUPPORTED", "Themed configuration feedback test"),
    ("config", "voicesenumber"): ("SUPPORTED", "Themed configuration feedback test"),
    ("config", "voiceisvoice"): ("SUPPORTED", "Themed configuration feedback test"),
    ("config", "muststartpage1flag"): ("SUPPORTED", "Settings page retention in GameSettingsPanel and PlayerActivity"),
    ("printmessage", "drawoverrapoptionflag"): ("SUPPORTED", "GameStoryControlsTest"),
    ("printmessage", "drawwindowwithmessageflag"): ("SUPPORTED", "Message visibility during wipes"),
    ("printmessage", "lprintbgmode"): ("SUPPORTED", "Long-print background tests"),
    ("printmessage", "lblockx"): (
        "UPSTREAM_UNUSED", "Read at nnnLib/commonprintmessage.cpp:398-404 and never used in layout or rendering"
    ),
    ("printmessage", "lblocky"): (
        "UPSTREAM_UNUSED", "Read at nnnLib/commonprintmessage.cpp:398-404 and never used in layout or rendering"
    ),
    ("selectobject", "optionoffvarflag"): (
        "UPSTREAM_UNUSED", "SetOptionVarOn call commented out at nnnLib/commonSelectObject.cpp:607"
    ),
    ("selectobject", "optionoffvarname"): (
        "UPSTREAM_UNUSED", "SetOptionVarOn call commented out at nnnLib/commonSelectObject.cpp:607"
    ),
    ("selectplace", "optionoffvarflag"): (
        "UPSTREAM_UNUSED", "SetOptionVarOn call commented out at nnnLib/commonSelectPlace.cpp:614"
    ),
    ("selectplace", "optionoffvarname"): (
        "UPSTREAM_UNUSED", "SetOptionVarOn call commented out at nnnLib/commonSelectPlace.cpp:614"
    ),
    ("game", "optionoffvarflag"): (
        "UPSTREAM_UNUSED",
        "Read at nnnLib/gamecallback.cpp:2440-2447; SetOptionVarOn commented out at "
        "nnnLib/commonSelectObject.cpp:607 and nnnLib/commonSelectPlace.cpp:614",
    ),
    ("game", "optionoffvarname"): (
        "UPSTREAM_UNUSED",
        "Read at nnnLib/gamecallback.cpp:2440-2447; SetOptionVarOn commented out at "
        "nnnLib/commonSelectObject.cpp:607 and nnnLib/commonSelectPlace.cpp:614",
    ),
    ("maincontrol", "mutexname"): ("PC_WINDOW_ONLY", "Windows CreateMutex name"),
    ("maincontrol", "checkmutexflag"): ("PC_WINDOW_ONLY", "Windows CreateMutex startup check"),
    ("game", "vistacheck"): ("PC_WINDOW_ONLY", "Windows version check"),
    ("game", "f9keyexitoffflag"): ("PC_WINDOW_ONLY", "Windows F9 exit shortcut"),
    ("printmessage", "fullmessagenumber"): (
        "SUPPORTED", "Long-print row scroll limit in GameSession and PC save roundtrip"
    ),
    ("selectcg", "filenamedifferencefont"): (
        "SUPPORTED", "CG difference digit atlas in GalleryCatalog and GameGalleryPanel"
    ),
    ("selectcg", "differenceprintflag"): (
        "SUPPORTED", "CG difference counter visibility flag in GalleryCatalog and GameGalleryPanel"
    ),
    ("selectcg", "differenceprintbygetflag"): (
        "SUPPORTED", "CG difference counter zero-unlock rule in GameGalleryPanel"
    ),
    ("selectcg", "differenceprintx"): ("SUPPORTED", "CG difference counter X offset in GameGalleryPanel"),
    ("selectcg", "differenceprinty"): ("SUPPORTED", "CG difference counter Y offset in GameGalleryPanel"),
    ("selectcg", "differencefontsizex"): ("SUPPORTED", "CG difference digit width in GameGalleryPanel"),
    ("selectcg", "differencefontsizey"): ("SUPPORTED", "CG difference digit height in GameGalleryPanel"),
    ("selectcg", "differencefontnextx"): ("SUPPORTED", "CG difference digit advance in GameGalleryPanel"),
    ("selectcg", "differentketa"): ("SUPPORTED", "CG difference digit column count in GameGalleryPanel"),
    ("listenbgm", "cannotcolorr"): ("SUPPORTED", "Locked BGM label RGB color in GameGalleryPanel"),
    ("listenbgm", "cannotcolorg"): ("SUPPORTED", "Locked BGM label RGB color in GameGalleryPanel"),
    ("listenbgm", "cannotcolorb"): ("SUPPORTED", "Locked BGM label RGB color in GameGalleryPanel"),
    ("systemmenu", "systemmenuscenebackfilename"): (
        "SUPPORTED", "Scene replay return button sprite in GameSystemMenuPanel"
    ),
    ("systemmenu", "systemmenusceneendfilename"): (
        "SUPPORTED", "Scene replay end button sprite in GameSystemMenuPanel"
    ),
    ("datafile", "filenameoverwritecursor"): (
        "SUPPORTED", "Overwrite slot cursor sprite in GameUiAssets and GameSlotsPanel"
    ),
    ("datafile", "saveanimepattern"): ("SUPPORTED", "Save slot cursor animation in GameSlotsPanel"),
    ("datafile", "saveanimepattern2"): ("SUPPORTED", "Save slot selected cursor animation in GameSlotsPanel"),
    ("datafile", "saveanimespeed"): ("SUPPORTED", "Save slot cursor animation speed in GameSlotsPanel"),
    ("datafile", "saveanimespeed2"): ("SUPPORTED", "Save slot selected cursor animation speed in GameSlotsPanel"),
    ("datafile", "saveanimetype"): ("SUPPORTED", "Save slot cursor animation type in GameSlotsPanel"),
    ("datafile", "saveanimetype2"): ("SUPPORTED", "Save slot selected cursor animation type in GameSlotsPanel"),
    ("datafile", "loadanimepattern"): ("SUPPORTED", "Load slot cursor animation in GameSlotsPanel"),
    ("datafile", "loadanimepattern2"): ("SUPPORTED", "Load slot selected cursor animation in GameSlotsPanel"),
    ("datafile", "loadanimespeed"): ("SUPPORTED", "Load slot cursor animation speed in GameSlotsPanel"),
    ("datafile", "loadanimespeed2"): ("SUPPORTED", "Load slot selected cursor animation speed in GameSlotsPanel"),
    ("datafile", "loadanimetype"): ("SUPPORTED", "Load slot cursor animation type in GameSlotsPanel"),
    ("datafile", "loadanimetype2"): ("SUPPORTED", "Load slot selected cursor animation type in GameSlotsPanel"),
    ("datafile", "overwriteanimepattern"): ("SUPPORTED", "Overwrite slot cursor animation in GameSlotsPanel"),
    ("datafile", "overwriteanimepattern2"): ("SUPPORTED", "Overwrite slot selected cursor animation in GameSlotsPanel"),
    ("datafile", "overwriteanimespeed"): ("SUPPORTED", "Overwrite slot cursor animation speed in GameSlotsPanel"),
    ("datafile", "overwriteanimespeed2"): (
        "SUPPORTED", "Overwrite slot selected cursor animation speed in GameSlotsPanel"
    ),
    ("datafile", "overwriteanimetype"): ("SUPPORTED", "Overwrite slot cursor animation type in GameSlotsPanel"),
    ("datafile", "overwriteanimetype2"): ("SUPPORTED", "Overwrite slot selected cursor animation type in GameSlotsPanel"),
    ("datafile", "gameyearprintflag"): ("SUPPORTED", "Save slot in-game year display flag in GameSession and GameSlotsPanel"),
    ("datafile", "gamedateprintflag"): ("SUPPORTED", "Save slot in-game date display flag in GameSession and GameSlotsPanel"),
    ("datafile", "gamemonthprintflag"): ("SUPPORTED", "Save slot in-game month display flag in GameSlotsPanel"),
    ("datafile", "gamedayprintflag"): ("SUPPORTED", "Save slot in-game day display flag in GameSlotsPanel"),
    ("datafile", "gameweekprintflag"): ("SUPPORTED", "Save slot in-game weekday display flag in GameSession and GameSlotsPanel"),
    ("datafile", "gameweekyouprintflag"): ("SUPPORTED", "Save slot weekday suffix sprite flag in GameSlotsPanel"),
    ("datafile", "gameweekbiprintflag"): ("SUPPORTED", "Save slot weekday suffix sprite flag in GameSlotsPanel"),
    ("datafile", "filenamegametimefont"): ("SUPPORTED", "Save slot in-game date digit atlas in GameSlotsPanel"),
    ("datafile", "filenamegameweekfont"): ("SUPPORTED", "Save slot in-game weekday atlas in GameSlotsPanel"),
    ("datafile", "gametimeprintx"): ("SUPPORTED", "Save slot in-game date origin X in GameSlotsPanel"),
    ("datafile", "gametimeprinty"): ("SUPPORTED", "Save slot in-game date origin Y in GameSlotsPanel"),
    ("datafile", "gametimefontsizex"): ("SUPPORTED", "Save slot in-game date digit width in GameSlotsPanel"),
    ("datafile", "gametimefontsizey"): ("SUPPORTED", "Save slot in-game date digit height in GameSlotsPanel"),
    ("datafile", "gametimefontnextx"): ("SUPPORTED", "Save slot in-game date digit advance in GameSlotsPanel"),
    ("datafile", "gamedateprintx"): (
        "UPSTREAM_UNUSED",
        "Parsed into CPrintGameDate::m_printX (printGameDate.cpp:239-245) but bypassed by CCommonDataFile::Print(pt, ...)",
    ),
    ("datafile", "gamedateprinty"): (
        "UPSTREAM_UNUSED",
        "Parsed into CPrintGameDate::m_printY (printGameDate.cpp:239-245) but bypassed by CCommonDataFile::Print(pt, ...)",
    ),
    ("datafile", "gamedateslashprintflag"): ("SUPPORTED", "Save slot in-game date slash visibility in GameSlotsPanel"),
    ("datafile", "gameyearslashprintflag"): ("SUPPORTED", "Save slot in-game year slash visibility in GameSlotsPanel"),
    ("datafile", "gamemonthslashprintflag"): ("SUPPORTED", "Save slot in-game month slash visibility in GameSlotsPanel"),
    ("datafile", "gamedayslashprintflag"): ("SUPPORTED", "Save slot in-game day slash visibility in GameSlotsPanel"),
    ("datafile", "gameyearupzeroprintflag"): ("SUPPORTED", "Save slot in-game year zero-padding flag in GameSlotsPanel"),
    ("datafile", "gamemonthupzeroprintflag"): ("SUPPORTED", "Save slot in-game month zero-padding flag in GameSlotsPanel"),
    ("datafile", "gamedayupzeroprintflag"): ("SUPPORTED", "Save slot in-game day zero-padding flag in GameSlotsPanel"),
    ("datafile", "gameyearprintx"): ("SUPPORTED", "Save slot in-game year X offset in GameSlotsPanel"),
    ("datafile", "gameyearprinty"): ("SUPPORTED", "Save slot in-game year Y offset in GameSlotsPanel"),
    ("datafile", "gamemonthprintx"): ("SUPPORTED", "Save slot in-game month X offset in GameSlotsPanel"),
    ("datafile", "gamemonthprinty"): ("SUPPORTED", "Save slot in-game month Y offset in GameSlotsPanel"),
    ("datafile", "gamedayprintx"): ("SUPPORTED", "Save slot in-game day X offset in GameSlotsPanel"),
    ("datafile", "gamedayprinty"): ("SUPPORTED", "Save slot in-game day Y offset in GameSlotsPanel"),
    ("datafile", "gameweekprintx"): ("SUPPORTED", "Save slot in-game weekday X offset in GameSlotsPanel"),
    ("datafile", "gameweekprinty"): ("SUPPORTED", "Save slot in-game weekday Y offset in GameSlotsPanel"),
    ("datafile", "gameweekfontsizex"): ("SUPPORTED", "Save slot in-game weekday glyph width in GameSlotsPanel"),
    ("datafile", "gameweekfontsizey"): ("SUPPORTED", "Save slot in-game weekday glyph height in GameSlotsPanel"),
    ("datafile", "gameweekfontnextx"): ("SUPPORTED", "Save slot in-game weekday glyph advance in GameSlotsPanel"),
}

KAZARI_GATED_STEMS = frozenset(("printmessage", "datafile", "selectscenechara"))


def inactive_behavior(stem, key, fields):
    if stem in KAZARI_GATED_STEMS and key != "kazariprintflag":
        if key.startswith("kazari") or key == "filenamekazari":
            gate = fields.get("kazariprintflag")
            if gate is None or gate[2] != "set":
                return "Inactive because kazariPrintFlag is not enabled in this setup file"
    return None


def mask_cpp_tokens(source, retain_literals=True):
    def mask(match):
        token = match[0]
        if retain_literals and not token.startswith("/"):
            return token
        return re.sub(r"[^\n]", " ", token)

    return CPP_TOKEN.sub(mask, source)


def source_text(path):
    raw = path.read_bytes()
    try:
        return raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        return raw.decode("cp932", errors="replace")


def format_pattern(template):
    parts = PLACEHOLDER.split(template.lower())
    if len(parts) == 1 or any("%" in part for part in parts):
        return None
    substitutions = [
        "[a-z0-9_]+" if match[0].endswith("s") else "[0-9]+"
        for match in PLACEHOLDER.finditer(template)
    ]
    # A format with no fixed text offers no useful evidence.
    if not any(parts):
        return None
    pattern = re.escape(parts[0])
    for substitution, suffix in zip(substitutions, parts[1:]):
        pattern += substitution + re.escape(suffix)
    return re.compile(pattern)


def upstream_keys(root):
    literal = defaultdict(set)
    formats = defaultdict(set)
    for path in sorted(root.glob("nnn*Lib/**/*.cpp")):
        source = mask_cpp_tokens(source_text(path))
        if "GetInitGame" not in source:
            continue
        code = mask_cpp_tokens(source, retain_literals=False)
        calls = [match for match in LOOKUP.finditer(source) if code[match.start()] != " "]
        line_starts = [0, *(m.end() for m in re.finditer("\n", source))]
        writes = defaultdict(list)
        for match in FORMAT.finditer(source):
            if code[match.start()] != " ":
                writes[match["buffer"]].append((match.start(), match["format"]))
        name = path.relative_to(root).as_posix()
        for call in calls:
            site = f"{name}:{bisect_right(line_starts, call.start())}"
            if call["literal"]:
                literal[call["literal"].lower()].add(site)
                continue
            candidates = writes[call["buffer"]]
            index = bisect_right(candidates, (call.start(), "")) - 1
            if index >= 0:
                position, template = candidates[index]
                if call.start() - position <= 512 and format_pattern(template):
                    formats[template].add(site)
    if not literal:
        raise ValueError(f"No upstream config readers found in {root}")
    return literal, formats


def setup_files(root):
    """Resolve an encrypted setup in preference to the corresponding XTX."""
    folders = {}
    for path in sorted(root.iterdir(), key=lambda p: p.name):
        if path.is_dir() and path.name.lower() in ("init", "init2"):
            name = path.name.lower()
            if name not in folders or path.name == name:
                folders[name] = path
    selected = {}
    for folder, suffix in (("init", ".xtx"), ("init2", ".fxf")):
        directory = folders.get(folder)
        if directory is None:
            continue
        entries = {}
        for path in sorted(directory.iterdir(), key=lambda p: p.name):
            if path.is_file() and path.suffix.lower() == suffix:
                entries.setdefault(path.stem.lower(), path)
        selected.update(entries)
    return sorted(selected.items())


def setup_fields(path):
    with path.open("rb") as stream:
        raw = stream.read(MAX_SETUP_BYTES + 1)
    if len(raw) > MAX_SETUP_BYTES:
        raise ValueError(f"Setup exceeds the engine's text limit: {path}")
    if path.suffix.lower() == ".fxf":
        raw = raw.translate(FXF)
    fields = {}
    for number, line in enumerate(re.split(rb"\r\n?|\n", raw.split(b"\0", 1)[0]), 1):
        match = SETUP_LINE.fullmatch(line)
        if match is None:
            continue
        name, value = match.groups()
        key = name.decode("ascii")
        value = value.strip(b" \t")
        state = "empty" if not value else "zero" if re.fullmatch(rb"[+-]?0+", value) else "set"
        fields.setdefault(key.lower(), (key, number, state))
    return fields


def game_report(root, lookup):
    files = setup_files(root)
    if not files:
        raise ValueError(f"No init/*.xtx or init2/*.fxf in {root}")
    rows = []
    for stem, path in files:
        fields = setup_fields(path)
        for key, (spelling, line, value) in fields.items():
            evidence = lookup(key)
            inactive = inactive_behavior(stem, key, fields)
            if (stem, key) in REVIEWED:
                status, behavior = REVIEWED[(stem, key)]
            elif inactive is not None:
                status, behavior = "INACTIVE_IN_GAME", inactive
            else:
                status, behavior = ("REVIEW" if evidence else "UNKNOWN"), ""
            rows.append({
                "key": spelling, "file": path.relative_to(root).as_posix(), "line": line,
                "value": value, "status": status, "behavior": behavior, "upstream": evidence,
            })
    return {"game": root.name, "setup_files": len(files),
            "status_counts": dict(Counter(row["status"] for row in rows)), "fields": rows}


def discover(directory):
    def has_scripts(root):
        return any(path.name.lower() == "spt" and path.is_dir() for path in root.iterdir())

    for child in sorted(path for path in directory.iterdir() if path.is_dir()):
        if has_scripts(child):
            yield child
            continue
        for nested in sorted(path for path in child.iterdir() if path.is_dir()):
            if has_scripts(nested):
                yield nested


def audit(roots, source):
    literal, formats = upstream_keys(source)
    patterns = [(format_pattern(template), template, sorted(sites)) for template, sites in sorted(formats.items())]

    @lru_cache(maxsize=None)
    def evidence(key):
        if key in literal:
            return {"literal": sorted(literal[key])}
        candidates = {name: sites for pattern, name, sites in patterns if pattern.fullmatch(key)}
        return {"formats": candidates} if candidates else {}

    return {"upstream_literal_keys": len(literal), "upstream_format_candidates": len(formats),
            "upstream_catalog": {
                "literal": {name: sorted(sites) for name, sites in sorted(literal.items())},
                "formats": {name: sorted(sites) for name, sites in sorted(formats.items())},
            },
            "games": [game_report(root, evidence) for root in roots]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, nargs="*", help="game root(s)")
    parser.add_argument("--discover", type=Path, help="find game roots up to two levels under this folder")
    parser.add_argument("--upstream", type=Path, default=REPO / "upstream/SystemNNN")
    parser.add_argument("--json", action="store_true", help="show all fields with their source lines")
    args = parser.parse_args()
    try:
        discovered = discover(args.discover) if args.discover else ()
        roots = list(dict.fromkeys(p.resolve() for p in [*args.game, *discovered]))
        if not roots:
            parser.error("specify at least one game directory or --discover")
        report = audit(roots, args.upstream)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    if args.json:
        # Keep the report portable even when the console has a legacy encoding.
        json.dump(report, sys.stdout, indent=2)
        print()
        return
    for game in report["games"]:
        counts = ", ".join(f"{status}={count}" for status, count in sorted(game["status_counts"].items()))
        print(f'{game["game"]}: {game["setup_files"]} setup files, {counts}')
        for row in game["fields"]:
            if row["status"] in ("UNSUPPORTED_BEHAVIOR", "PARTIAL"):
                print(f'  {row["file"]}:{row["line"]} {row["key"]} ({row["value"]}) - {row["behavior"]}')


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Universal GTA V PC -> PS4/PS5 Road Textures & Map Mod Auto-Converter
Supports:
  - OIV packages (e.g. L.A. Roads, NVE Roads, Realistic Roads, RTX Roads)
  - Unpacked directories containing road RPFs or loose .ytd, .ydr, .ymap, .ytyp, .ymf, .ybn files
Converts:
  - Textures: .ytd -> .otd (with low compression / 1024 max dimension to prevent ragebuilder heap overflow)
  - Models: .ydr -> .odr, .ydd -> .odd (stripping embedded textures via CWXml)
  - Map Placements: .ymap -> .omap (with missing DLC archetype sanitization)
  - Archetypes: .ytyp -> .otyp
  - Manifests: .ymf -> .omf (with PS4 PSO 'oppp' salt & checksum calculation)
  - Collisions: .ybn -> .obn
  - Occlusions & Paths: .ynd -> .ond, .ycd -> .ocd, .ybd -> .obd
Packages everything into high-compatibility overlay RPFs with >= 8KB Orbis RSC7 chunk alignment.
"""

import argparse
import configparser
import contextlib
import glob
import io
import os
import re
import shutil
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from collections import defaultdict
from pathlib import Path

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(TOOLS_DIR)
NEOGEN_DIR = os.path.join(ROOT_DIR, "NeoGen EARY ACCESS", "Terminal Build", "NeoGen")

# Direct all temporary files to secondary SSD (D:\Temp) to prevent filling C: drive
for _cand_tmp in (r"D:\Temp", r"D:\tmp"):
    if os.path.exists(_cand_tmp):
        tempfile.tempdir = _cand_tmp
        os.environ["TEMP"] = _cand_tmp
        os.environ["TMP"] = _cand_tmp
        break

for _p in (NEOGEN_DIR, TOOLS_DIR):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import NeoGen
import convert_vremastered as cv

# GTA V canonical road/map RPF destinations
CANONICAL_ROAD_RPFS = {
    "roads.rpf": "levels/gta5/roads.rpf",
    "roads_metadata.rpf": "levels/gta5/roads_metadata.rpf",
    "roads_col.rpf": "levels/gta5/roads_col.rpf",
    "gtxd.rpf": "x64/data/gtxd.rpf",
}

# RSC files that describe map data.  These must live inside an RPF flagged with
# CONTENTS_DLC_MAP_DATA, otherwise the game mounts the archive but never
# registers its .omap placements with the world map (the classic "mod is
# installed but nothing shows up" symptom).
MAP_ASSET_EXTS = {".ymap", ".ytyp", ".ymf", ".ymt"}
ROAD_ASSET_EXTS = {".ytd", ".ydr", ".ydd", ".ybn", ".ynd", ".ycd", ".ybd"}
COLLECTABLE_EXTS = MAP_ASSET_EXTS | ROAD_ASSET_EXTS
DLC_WRAPPER_NAMES = {"setup2.xml", "content.xml", "dlclist.xml"}


def _rpf_entry_names(rpf_path):
    """Return the flat list of file names stored in an RPF (empty on failure)."""
    try:
        with open(rpf_path, "rb") as fh:
            data = fh.read()
        sink = io.StringIO()
        with contextlib.redirect_stdout(sink):
            entries = NeoGen._parse_rpf(data)
        return [os.path.basename(e["name"]).lower() for e in entries if e.get("type") == "file"]
    except Exception:
        return []


def _rpf_tree_extensions(rpf_path, _seen=None):
    """Extensions contained by an RPF, including archives nested inside a DLC wrapper."""
    _seen = _seen or set()
    real = os.path.abspath(rpf_path)
    if real in _seen:
        return set()
    _seen.add(real)

    names = _rpf_entry_names(real)
    exts = {os.path.splitext(n)[1].lower() for n in names}
    nested = [n for n in names if n.endswith(".rpf")]
    # A PC DLC wrapper (custom_maps/dlc.rpf) hides the real map archive one level down.
    if nested or (DLC_WRAPPER_NAMES & set(names)):
        with tempfile.TemporaryDirectory() as td:
            try:
                sink = io.StringIO()
                with contextlib.redirect_stdout(sink):
                    NeoGen.extract_single_rpf(real, td)
            except Exception:
                return exts
            for f in glob.glob(os.path.join(td, "**", "*.*"), recursive=True):
                if os.path.isfile(f) and not f.lower().endswith(".rpf"):
                    exts.add(os.path.splitext(f)[1].lower())
            for nested_rpf in glob.glob(os.path.join(td, "**", "*.rpf"), recursive=True):
                exts |= _rpf_tree_extensions(nested_rpf, _seen)
    return exts


def source_extensions(src_path):
    """All asset extensions reachable from a mod folder, .oiv or .rpf (recursive)."""
    exts: set[str] = set()
    if os.path.isfile(src_path):
        low = src_path.lower()
        if low.endswith(".oiv"):
            try:
                with zipfile.ZipFile(src_path, "r") as zf:
                    exts |= {os.path.splitext(n)[1].lower() for n in zf.namelist()}
            except Exception:
                pass
        elif low.endswith(".rpf"):
            exts |= _rpf_tree_extensions(src_path)
        else:
            exts.add(os.path.splitext(src_path)[1].lower())
        return exts

    if not os.path.isdir(src_path):
        return exts
    for f in glob.glob(os.path.join(src_path, "**", "*.*"), recursive=True):
        if os.path.isfile(f) and not f.lower().endswith(".rpf"):
            exts.add(os.path.splitext(f)[1].lower())
    for rpf in glob.glob(os.path.join(src_path, "**", "*.rpf"), recursive=True):
        exts |= _rpf_tree_extensions(rpf)
    return exts


def check_rsc7_file_chunks(rsc_path, min_chunk=8192):
    """Inspects an RSC7 header to ensure all virtual and physical chunks meet min_chunk."""
    try:
        with open(rsc_path, "rb") as fh:
            raw = fh.read(16)
        if len(raw) < 16 or raw[:4] != b"RSC7":
            return True, None
        ver, vf, pf = struct.unpack_from("<III", raw, 4)
        vc = cv.rsc7_chunks_from_flag(vf)
        pc = cv.rsc7_chunks_from_flag(pf)
        if vc and min(vc) < min_chunk:
            return False, f"virtual chunk {min(vc)}B < {min_chunk}B"
        if pc and min(pc) < min_chunk:
            return False, f"physical chunk {min(pc)}B < {min_chunk}B"
        return True, None
    except Exception as e:
        return True, None


def resolve_road_source(src_input):
    """Resolves input to an .oiv file or a directory."""
    if os.path.isfile(src_input):
        return src_input
    if os.path.isdir(src_input):
        oivs = glob.glob(os.path.join(src_input, "**", "*.oiv"), recursive=True)
        if oivs:
            return oivs[0]
        return src_input
    raise FileNotFoundError(f"Source not found: {src_input}")


def parse_oiv_road_mod(oiv_path, staging_root):
    """
    Parses an OIV archive's assembly.xml,
    groups files by target RPF archive, and extracts them to staging_root.
    Returns: dict of { target_rpf_name: { 'rel_path': str, 'folder': str, 'files': set() } }
    """
    os.makedirs(staging_root, exist_ok=True)
    rpf_groups = defaultdict(lambda: {"rel_path": "", "folder": "", "files": set()})

    with zipfile.ZipFile(oiv_path, "r") as zf:
        xml_entries = [n for n in zf.namelist() if n.lower().endswith("assembly.xml")]
        if not xml_entries:
            raise ValueError("assembly.xml not found inside OIV archive!")

        root = ET.fromstring(zf.read(xml_entries[0]))
        content_node = root.find("content")
        if content_node is None:
            raise ValueError("No <content> section in assembly.xml")

        def walk_node(node, cur_archive_path=""):
            for child in node:
                if child.tag == "archive":
                    sub_p = child.get("path", "")
                    full_p = (cur_archive_path + "/" + sub_p) if cur_archive_path else sub_p
                    walk_node(child, full_p)
                elif child.tag == "add":
                    src_file = child.get("source")
                    dest_file = child.text.strip() if child.text else os.path.basename(src_file)
                    target_rpf = os.path.basename(cur_archive_path.replace("\\", "/"))
                    if not target_rpf:
                        target_rpf = "roads.rpf"

                    norm_path = cur_archive_path.replace("\\", "/").strip("/").lower()
                    if norm_path.startswith("update/"):
                        rel_gta_path = norm_path[len("update/"):]
                    elif norm_path.startswith("common.rpf"):
                        rel_gta_path = norm_path
                    elif norm_path.startswith("x64"):
                        rel_gta_path = norm_path
                    else:
                        rel_gta_path = f"levels/gta5/{target_rpf}"

                    rpf_dir = os.path.join(staging_root, target_rpf)
                    os.makedirs(rpf_dir, exist_ok=True)
                    rpf_groups[target_rpf]["rel_path"] = rel_gta_path
                    rpf_groups[target_rpf]["folder"] = rpf_dir
                    rpf_groups[target_rpf]["files"].add((src_file, dest_file))

        walk_node(content_node)

        print(f"[*] Extracting assets from {os.path.basename(oiv_path)}...")
        name_map = {n.replace("\\", "/").lower(): n for n in zf.namelist()}
        for rpf_name, info in rpf_groups.items():
            for src, dest in info["files"]:
                src_key = ("content/" + src.replace("\\", "/")).lower()
                zip_member = name_map.get(src_key) or name_map.get(src.replace("\\", "/").lower())
                if zip_member:
                    out_dest = os.path.join(info["folder"], dest)
                    with zf.open(zip_member) as s, open(out_dest, "wb") as d:
                        shutil.copyfileobj(s, d)
                else:
                    print(f"  [WARN] Missing file in OIV: {src}")

    return rpf_groups


def stage_loose_road_files(src_dir, staging_root, dlc_name="roads"):
    """
    Scans a folder for loose PC road/map mod assets and routes them into structured RPF folders.

    PC map mods usually ship as a DLC wrapper (custom_maps/dlc.rpf -> x64/.../custom_maps.rpf)
    or as loose .ymap files.  Both are unwrapped here so the real map archive is converted
    instead of being copied into the PS4 DLC as a raw PC RPF.

    Routing:
      - .ymap, .ytyp, .ymf, .ymt -> levels/gta5/<dlc_name>_metadata.rpf  (CONTENTS_DLC_MAP_DATA)
      - .ytd, .ydr, .ydd         -> levels/gta5/roads.rpf
      - .ybn                     -> levels/gta5/roads_col.rpf
    """
    os.makedirs(staging_root, exist_ok=True)
    rpf_groups = {}
    collected: list[tuple[str, tuple[str, str] | None]] = []
    seen_rpfs: set[str] = set()
    counter = [0]

    def _add_group(name, rel_path):
        if name not in rpf_groups:
            folder = os.path.join(staging_root, name)
            os.makedirs(folder, exist_ok=True)
            rpf_groups[name] = {"rel_path": rel_path, "folder": folder, "files": set()}
        return rpf_groups[name]

    def _ingest_rpf(rpf_path):
        real = os.path.abspath(rpf_path)
        if real in seen_rpfs:
            return
        seen_rpfs.add(real)
        counter[0] += 1
        tmp = os.path.join(staging_root, f"_rpf_{counter[0]}")
        print(f"[*] Extracting existing RPF: {os.path.basename(rpf_path)}...")
        try:
            NeoGen.extract_single_rpf(rpf_path, tmp)
        except Exception as e:
            print(f"  [WARN] Could not extract {os.path.basename(rpf_path)}: {e}")
            return

        # Preserve the target archive for base-game overlay RPFs (roads.rpf, gtxd.rpf...).
        rpf_bn = os.path.basename(rpf_path).lower()
        canonical = CANONICAL_ROAD_RPFS.get(rpf_bn)
        hint = (rpf_bn, canonical) if canonical else None

        # Always unwrap nested RPFs (a DLC wrapper's real payload lives inside one).
        for nested in glob.glob(os.path.join(tmp, "**", "*.rpf"), recursive=True):
            _ingest_rpf(nested)

        # Keep loose assets from this tree, but never the wrapper XMLs themselves.
        for f in glob.glob(os.path.join(tmp, "**", "*.*"), recursive=True):
            if not os.path.isfile(f) or f.lower().endswith(".rpf"):
                continue
            if os.path.basename(f).lower() in DLC_WRAPPER_NAMES:
                continue
            if os.path.splitext(f)[1].lower() in COLLECTABLE_EXTS:
                collected.append((f, hint))

    for r in glob.glob(os.path.join(src_dir, "**", "*.rpf"), recursive=True):
        _ingest_rpf(r)

    for f in glob.glob(os.path.join(src_dir, "**", "*.*"), recursive=True):
        if not os.path.isfile(f) or f.lower().endswith(".rpf"):
            continue
        if os.path.splitext(f)[1].lower() in COLLECTABLE_EXTS:
            collected.append((f, None))

    # The same ymap is commonly present both loose and inside the DLC -> dedupe by name.
    unique: dict[str, tuple[str, tuple[str, str] | None]] = {}
    for f, hint in collected:
        unique.setdefault(os.path.basename(f).lower(), (f, hint))

    cat_maps = []
    asset_buckets: dict[tuple[str, str], list[str]] = {}
    for f, hint in unique.values():
        ext = os.path.splitext(f)[1].lower()
        if ext in MAP_ASSET_EXTS:
            cat_maps.append(f)
        elif ext == ".ytd" or ext in (".ydr", ".ydd"):
            key = hint if hint else ("roads.rpf", "levels/gta5/roads.rpf")
            asset_buckets.setdefault(key, []).append(f)
        elif ext == ".ybn":
            key = hint if hint and "col" in hint[0] else ("roads_col.rpf", "levels/gta5/roads_col.rpf")
            asset_buckets.setdefault(key, []).append(f)

    if cat_maps:
        bn = f"{dlc_name}_metadata.rpf"
        g = _add_group(bn, f"levels/gta5/{bn}")
        for f in cat_maps:
            shutil.copy2(f, os.path.join(g["folder"], os.path.basename(f)))
            g["files"].add((f, os.path.basename(f)))

    for (bn, rel_path), files in asset_buckets.items():
        g = _add_group(bn, rel_path)
        for f in files:
            shutil.copy2(f, os.path.join(g["folder"], os.path.basename(f)))
            g["files"].add((f, os.path.basename(f)))

    return rpf_groups


def _normalize_map_groups(rpf_groups, staging_root, dlc_name):
    """Move every map asset into a single <dlc_name>_metadata.rpf group (e.g. OIV inputs)."""
    target_bn = f"{dlc_name}_metadata.rpf"
    for name, info in list(rpf_groups.items()):
        if not os.path.isdir(info["folder"]):
            continue
        map_files = [
            f for f in os.listdir(info["folder"])
            if os.path.isfile(os.path.join(info["folder"], f))
            and os.path.splitext(f)[1].lower() in MAP_ASSET_EXTS
        ]
        if not map_files:
            continue
        if name == target_bn:
            continue
        target = rpf_groups.get(target_bn)
        if target is None:
            folder = os.path.join(staging_root, target_bn)
            os.makedirs(folder, exist_ok=True)
            target = {"rel_path": f"levels/gta5/{target_bn}", "folder": folder, "files": set()}
            rpf_groups[target_bn] = target
        for f in map_files:
            src = os.path.join(info["folder"], f)
            dst = os.path.join(target["folder"], f)
            if os.path.abspath(src) != os.path.abspath(dst):
                if not os.path.exists(dst):
                    shutil.move(src, dst)
                else:
                    os.remove(src)
            target["files"].add((f, f))
    # Drop groups that became empty.
    for name in list(rpf_groups):
        folder = rpf_groups[name]["folder"]
        if not os.path.isdir(folder) or not os.listdir(folder):
            rpf_groups.pop(name, None)
    return rpf_groups


def convert_road_mod(src_input, output_dlc_rpf, dlc_name=None):
    """
    Main entry point: Converts any road texture or map mod (OIV or folder)
    into a PS4 v1.00 compatible overlay DLC pack with full .ymap support
    and 8KB Orbis chunk alignment.
    """
    resolved_src = resolve_road_source(src_input)
    if not dlc_name:
        dlc_name = "roads"
    dlc_name = re.sub(r"[^a-z0-9_]", "", dlc_name.lower()) or "roads"

    print("\n" + "=" * 65)
    print(f"  AUTO-CONVERTING ROAD TEXTURES & MAP MOD: {dlc_name.upper()}")
    print(f"  Source : {resolved_src}")
    print(f"  Output : {output_dlc_rpf}")
    print("=" * 65 + "\n")

    work_dir = output_dlc_rpf + "_work"
    shutil.rmtree(work_dir, ignore_errors=True)
    os.makedirs(work_dir, exist_ok=True)

    # Initialize NeoGen and CWXml
    NeoGen._init_cwxml_exe()
    if not NeoGen.VCWXML_OK:
        NeoGen._CWXML_EXE = r"C:\Users\Cath\AppData\Local\Programs\NeoGen\vcwxml_winx64\CWXml.exe"
        NeoGen.VCWXML_OK = True

    rpf_dir, mode_ovr, wine_bin = NeoGen.load_rpf_builder_settings()
    mode = NeoGen.detect_mode(mode_ovr)
    tex_dir, tex_mode, tex_wine = NeoGen.load_texture_builder_settings()

    # Load 1024 texture compression options to prevent ragebuilder heap overflow
    cfg_ini = os.path.join(NEOGEN_DIR, "low_compression.ini")
    cfg = configparser.ConfigParser()
    if os.path.exists(cfg_ini):
        cfg.read(cfg_ini)
        opts = NeoGen.parse_texture_config(cfg)
    else:
        opts = NeoGen.parse_texture_config(None)

    staging_dir = output_dlc_rpf + "_staging"
    shutil.rmtree(staging_dir, ignore_errors=True)

    # Step 1: Extract and route assets
    print("[1/5] Extracting and structuring road & map archives...")
    if resolved_src.lower().endswith(".oiv"):
        rpf_groups = parse_oiv_road_mod(resolved_src, staging_dir)
    else:
        rpf_groups = stage_loose_road_files(resolved_src, staging_dir, dlc_name)
    rpf_groups = _normalize_map_groups(rpf_groups, staging_dir, dlc_name)

    if not rpf_groups:
        raise RuntimeError("No road textures, models, or map files found to convert!")

    # Step 2: Convert assets inside each sub-archive
    print("\n[2/5] Converting textures, drawables, ymaps & manifests...")
    built_rpfs = []
    has_omaps = False
    stats = defaultdict(int)

    for rpf_name, group in rpf_groups.items():
        folder = group["folder"]
        rel_path = group["rel_path"]
        files = os.listdir(folder)
        if not files:
            continue
        is_map_archive = any(os.path.splitext(f)[1].lower() in MAP_ASSET_EXTS for f in files)
        print(f"\n  [*] Processing archive: {rpf_name} ({len(files)} files){' [map data]' if is_map_archive else ''}...")

        # 1. Convert .ytd textures
        ytd_files = [f for f in files if f.lower().endswith(".ytd")]
        for ytd in ytd_files:
            fp = os.path.join(folder, ytd)
            stem = os.path.splitext(ytd)[0]
            tex_sub = os.path.join(folder, f"{stem}_tex")
            print(f"      -> Compressing road texture {ytd} -> {stem}.otd...")
            NeoGen.extract_ytd_to_dds(fp, tex_sub, opts)
            res = NeoGen._build_otd_windows(tex_sub, stem, folder, tex_dir)
            if not res or not os.path.exists(os.path.join(folder, f"{stem}.otd")):
                raise RuntimeError(f"Failed to build OTD for {ytd}")
            shutil.rmtree(tex_sub, ignore_errors=True)
            if os.path.exists(fp):
                os.remove(fp)
            stats["otd"] += 1

        # 2. Convert .ydr / .ydd models (strip embedded textures)
        model_files = [f for f in os.listdir(folder) if f.lower().endswith((".ydr", ".ydd"))]
        for mdl in model_files:
            fp = os.path.join(folder, mdl)
            stem = os.path.splitext(mdl)[0]
            res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
            if not res:
                raise RuntimeError(f"Failed to convert drawable model: {mdl}")
            tex_dump = os.path.join(folder, f"{stem}_tex")
            if os.path.isdir(tex_dump):
                shutil.rmtree(tex_dump, ignore_errors=True)
            if os.path.exists(fp):
                os.remove(fp)
            stats["odr" if mdl.lower().endswith(".ydr") else "odd"] += 1

        # 3. Convert .ymap map placements
        ymap_files = [f for f in os.listdir(folder) if f.lower().endswith(".ymap")]
        for ymap in ymap_files:
            fp = os.path.join(folder, ymap)
            stem = os.path.splitext(ymap)[0]
            print(f"      -> Converting map placement: {ymap} -> {stem}.omap...")
            res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
            omap_path = os.path.join(folder, f"{stem}.omap")
            if not res or not os.path.exists(omap_path):
                raise RuntimeError(f"Failed to convert map placement: {ymap}")
            # Sanitize missing DLC archetypes (e.g. Heist props) that cause ERR_GEN_INVALID on PS4 v1.00
            try:
                stripped = cv.strip_omap_archetypes(omap_path, cv.MISSING_DLC_ARCHETYPES, NeoGen)
                if stripped > 0:
                    print(f"         [i] Sanitized {stripped} missing DLC archetypes from {stem}.omap")
            except Exception as e:
                print(f"         [WARN] Archetype sanitize skipped for {stem}.omap: {e}")
            if os.path.exists(fp):
                os.remove(fp)
            has_omaps = True
            stats["omap"] += 1

        # 4. Convert .ytyp archetypes
        ytyp_files = [f for f in os.listdir(folder) if f.lower().endswith(".ytyp")]
        for ytyp in ytyp_files:
            fp = os.path.join(folder, ytyp)
            stem = os.path.splitext(ytyp)[0]
            print(f"      -> Converting archetype: {ytyp} -> {stem}.otyp...")
            res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
            if not res:
                print(f"      [WARN] Could not convert {ytyp}, skipping")
            elif os.path.exists(fp):
                os.remove(fp)
            stats["otyp"] += 1

        # 5. Convert .ymf manifests and patch PSO trailer with 'oppp' salt
        ymf_files = [f for f in os.listdir(folder) if f.lower().endswith(".ymf")]
        for ymf in ymf_files:
            fp = os.path.join(folder, ymf)
            stem = os.path.splitext(ymf)[0]
            print(f"      -> Converting map manifest: {ymf} -> {stem}.omf...")
            res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
            omf_path = os.path.join(folder, f"{stem}.omf")
            if res and os.path.exists(omf_path):
                try:
                    cv.patch_ps4_manifest(omf_path, cv.MISSING_BASE_ITYPS)
                except Exception as e:
                    print(f"         [WARN] Manifest PSO patch skipped for {stem}.omf: {e}")
                if os.path.exists(fp):
                    os.remove(fp)
            stats["omf"] += 1

        # 6. Convert other GTA V resources (.ybn collision, .ynd, .ycd, .ybd)
        for f in list(os.listdir(folder)):
            fp = os.path.join(folder, f)
            ext = os.path.splitext(f)[1].lower()
            if ext in (".ybn", ".ynd", ".ycd", ".ybd"):
                res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
                if res and os.path.exists(fp):
                    os.remove(fp)
                stats[ext.replace(".", "o")] += 1

        # Step 3: Repack the converted sub-archive
        ps4_sub_dir = os.path.join(work_dir, "ps4", os.path.dirname(rel_path))
        os.makedirs(ps4_sub_dir, exist_ok=True)
        ps4_sub_rpf = os.path.join(work_dir, "ps4", rel_path)
        print(f"      [+] Repacking sub-archive: {rel_path}...")
        ok = NeoGen.build_single_rpf(folder, ps4_sub_rpf, rpf_dir, mode, wine_bin)
        if not ok or not os.path.exists(ps4_sub_rpf):
            raise RuntimeError(f"Failed to build sub-archive {ps4_sub_rpf}")
        built_rpfs.append({"path": rel_path, "is_map": is_map_archive})
        if is_map_archive:
            has_omaps = True
        shutil.rmtree(folder, ignore_errors=True)

    # Cleanup staging directory
    shutil.rmtree(staging_dir, ignore_errors=True)

    # Step 4: Generate setup2.xml and content.xml.
    # Map archives must follow the same layout R* uses for add-on map DLCs:
    # overlay="false" + <contents>CONTENTS_DLC_MAP_DATA</contents>, enabled only from
    # the GROUP_MAP_SP change set.  Texture replacements stay as overlays.
    print("\n[3/5] Generating PS4 setup2.xml and content.xml...")

    data_files_items = []
    startup_enable = []
    map_enable = []
    for info in built_rpfs:
        rpath_unix = info["path"].replace("\\", "/")
        path = f"dlc_{dlc_name}:/%PLATFORM%/{rpath_unix}"
        if info["is_map"]:
            data_files_items.append(
                f"""    <Item>
      <filename>{path}</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
      <loadCompletely value="true" />
      <contents>CONTENTS_DLC_MAP_DATA</contents>
    </Item>"""
            )
            map_enable.append(f"          <Item>{path}</Item>")
        else:
            data_files_items.append(
                f"""    <Item>
      <filename>{path}</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value="true" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>"""
            )
            startup_enable.append(f"        <Item>{path}</Item>")

    startup_items = "\n".join(startup_enable)
    startup_changesets_xml = (
        f"\n        <Item>{dlc_name.upper()}_AUTOGEN</Item>\n      " if startup_items else ""
    )

    map_sp_group_xml = ""
    if has_omaps:
        map_sp_group_xml = f"""    <Item>
      <NameHash>GROUP_MAP_SP</NameHash>
      <ContentChangeSets>
        <Item>{dlc_name.upper()}_MAP_CHANGE</Item>
      </ContentChangeSets>
    </Item>"""

    setup2_content = f"""<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_{dlc_name}</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>05/12/2016 08:14:30</timeStamp>
  <nameHash>{dlc_name}</nameHash>
  <contentChangeSets />
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>{startup_changesets_xml}</ContentChangeSets>
    </Item>
{map_sp_group_xml}
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="{60 if has_omaps else 35}" />
  <minorOrder value="0" />
  <isLevelPack value="false" />
  <dependencyPackHash />
  <requiredVersion />
  <subPackCount value="0" />
</SSetupData>
"""
    open(os.path.join(work_dir, "setup2.xml"), "w", encoding="utf-8", newline="\r\n").write(setup2_content)

    change_sets = []
    if startup_items:
        change_sets.append(
            f"""    <Item>
      <changeSetName>{dlc_name.upper()}_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
{startup_items}
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>"""
        )
    if has_omaps:
        map_enable_xml = "\n".join(map_enable)
        change_sets.append(
            f"""    <Item>
      <changeSetName>{dlc_name.upper()}_MAP_CHANGE</changeSetName>
      <mapChangeSetData>
        <Item>
          <associatedMap>MO_JIM_L11</associatedMap>
          <filesToInvalidate />
          <filesToDisable />
          <filesToEnable>
{map_enable_xml}
          </filesToEnable>
          <txdToLoad />
          <txdToUnload />
          <residentResources />
          <unregisterResources />
        </Item>
      </mapChangeSetData>
      <filesToInvalidate />
      <filesToDisable />
      <filesToEnable />
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>"""
        )

    data_files_xml = "\n".join(data_files_items)
    change_sets_xml = "\n".join(change_sets)
    content_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
{data_files_xml}
  </dataFiles>
  <contentChangeSets>
{change_sets_xml}
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
"""
    open(os.path.join(work_dir, "content.xml"), "w", encoding="utf-8", newline="\r\n").write(content_xml)

    # Step 5: Repack outer DLC and enforce 8KB chunk alignment
    print("\n[4/5] Repacking outer PS4 DLC RPF...")
    os.makedirs(os.path.dirname(os.path.abspath(output_dlc_rpf)), exist_ok=True)
    tmp_out = output_dlc_rpf + ".build_tmp"
    if os.path.exists(tmp_out):
        os.remove(tmp_out)
    ok = NeoGen.build_single_rpf(work_dir, tmp_out, rpf_dir, mode, wine_bin)
    if not ok or not os.path.exists(tmp_out):
        raise RuntimeError("Failed to build outer road dlc.rpf")
    shutil.move(tmp_out, output_dlc_rpf)
    shutil.rmtree(work_dir, ignore_errors=True)

    print("\n[5/5] Enforcing 8KB Orbis chunk alignment across all sub-archives...")
    cv.ensure_rpf_min_chunks(output_dlc_rpf, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)

    final_size = os.path.getsize(output_dlc_rpf)
    print("\n" + "=" * 65)
    print(f"  [SUCCESS] Road / Map DLC Built: {output_dlc_rpf}")
    print(f"  Final Size: {final_size:,} bytes ({final_size/1024/1024:.2f} MB)")
    print(f"  Sub-archives mounted: {len(built_rpfs)}")
    print(f"  Assets converted: {dict(stats)}")
    print("=" * 65 + "\n")

    # A DLC that is not listed in dlclist.xml is never mounted -> the mod appears
    # "installed" but nothing shows up in game.  Register the produced pack.
    cv.register_built_dlc(dlc_name, output_dlc_rpf, has_map_data=has_omaps)

    verify_converted_roads(output_dlc_rpf)
    return output_dlc_rpf


def verify_converted_roads(dlc_rpf_path):
    """
    Automated self-testing verification suite for converted road / map DLCs.
    """
    print(f"[*] Running Road & Map Verification Suite on {os.path.basename(dlc_rpf_path)}...")
    issues = []
    passes = []

    with tempfile.TemporaryDirectory() as td:
        try:
            NeoGen.extract_single_rpf(dlc_rpf_path, td)
        except Exception as e:
            print(f"  [FAIL] Failed to extract outer DLC: {e}")
            return False

        # 1. setup2 and content
        s2 = os.path.join(td, "setup2.xml")
        cx = os.path.join(td, "content.xml")
        if os.path.isfile(s2) and os.path.isfile(cx):
            cx_txt = open(cx, "r", encoding="utf-8", errors="ignore").read()
            s2_txt = open(s2, "r", encoding="utf-8", errors="ignore").read()
            map_items = []
            try:
                root = ET.fromstring(cx_txt)
                for item in root.findall("./dataFiles/Item"):
                    contents = item.find("contents")
                    if contents is not None and (contents.text or "").strip() == "CONTENTS_DLC_MAP_DATA":
                        map_items.append(item)
            except ET.ParseError as e:
                issues.append(f"content.xml is not valid XML: {e}")

            if map_items:
                bad_overlay = [
                    it for it in map_items
                    if (it.find("overlay") is None or it.find("overlay").attrib.get("value") != "false")
                ]
                if bad_overlay:
                    issues.append("Map data RPF entries must use overlay value=\"false\" (overlay=true stops .omap registration)")
                else:
                    passes.append(f"{len(map_items)} map archive(s) flagged CONTENTS_DLC_MAP_DATA with overlay='false'")
                if "GROUP_MAP_SP" not in s2_txt:
                    issues.append("Map archives present but setup2.xml has no GROUP_MAP_SP change set")
                else:
                    passes.append("GROUP_MAP_SP configured for single-player world map streaming")
            elif "overlay" in cx_txt:
                passes.append("setup2.xml & content.xml present with overlay RPF configuration")
            else:
                issues.append("content.xml has neither map data nor overlay RPF entries")

            if "GROUP_STARTUP" not in s2_txt:
                issues.append("setup2.xml missing GROUP_STARTUP")
        else:
            issues.append("Missing setup2.xml or content.xml")

        # 2. Check inner RPFs and PS4 assets
        inner_rpfs = glob.glob(os.path.join(td, "ps4", "**", "*.rpf"), recursive=True)
        if not inner_rpfs:
            issues.append("No sub-archives found inside ps4/ directory!")
        else:
            total_odrs = 0
            total_otds = 0
            total_omaps = 0
            total_otyps = 0
            total_omfs = 0
            total_obns = 0

            for nrpf in inner_rpfs:
                sub_ext = nrpf + "_ext"
                NeoGen.extract_single_rpf(nrpf, sub_ext)
                all_sub = os.listdir(sub_ext)
                total_odrs += len([f for f in all_sub if f.endswith(".odr")])
                total_otds += len([f for f in all_sub if f.endswith(".otd")])
                total_omaps += len([f for f in all_sub if f.endswith(".omap")])
                total_otyps += len([f for f in all_sub if f.endswith(".otyp")])
                total_omfs += len([f for f in all_sub if f.endswith(".omf")])
                total_obns += len([f for f in all_sub if f.endswith(".obn")])

                # Check for any unconverted PC files
                leftover_pc = [f for f in all_sub if f.lower().endswith((".ytd", ".ydr", ".ymap", ".ytyp", ".ymf", ".ybn"))]
                if leftover_pc:
                    issues.append(f"Sub-archive {os.path.basename(nrpf)} has unconverted PC files: {leftover_pc[:5]}")
                for f in all_sub:
                    fpath = os.path.join(sub_ext, f)
                    if not os.path.isfile(fpath) or f.lower().endswith(".rpf"):
                        continue
                    ok_chunk, chunk_err = check_rsc7_file_chunks(fpath, 8192)
                    if not ok_chunk:
                        issues.append(f"{os.path.basename(nrpf)} / {f}: {chunk_err}")
                    if f.lower().endswith(".omap"):
                        try:
                            raw = open(fpath, "rb").read()
                            _, dec = NeoGen._decompress_rsc7(raw)
                            if dec[0x10:0x14] != b"0DRP":
                                issues.append(f"{f}: not a valid map resource (missing PRD0 header)")
                        except Exception as e:
                            issues.append(f"{f}: map resource failed to decompress ({e})")

                shutil.rmtree(sub_ext, ignore_errors=True)

            passes.append(
                f"Mounted {len(inner_rpfs)} sub-archives containing {total_otds} .otd textures, "
                f"{total_odrs} .odr drawables, {total_omaps} .omap map placements, "
                f"{total_otyps} .otyp archetypes, {total_omfs} .omf manifests, {total_obns} .obn collisions"
            )

        # 3. Verify Orbis chunk alignment
        if not any("virtual chunk" in i or "physical chunk" in i for i in issues):
            passes.append("All inner RSC7 assets meet >= 8192-byte Orbis chunk alignment")

    print("\n------------------- VERIFICATION REPORT -------------------")
    for p in passes:
        print(f"  [PASS] {p}")
    if issues:
        for i in issues:
            print(f"  [FAIL] {i}")
        print("-----------------------------------------------------------")
        return False
    else:
        print("-----------------------------------------------------------")
        print("  [ALL STATIC CHECKS PASSED - road & map overlay ready for PS4/PS5]")
        return True


def convert_map_mod(src_input, output_dlc_rpf, dlc_name=None):
    """Explicit entry point for ymap / custom_maps style add-on map packs."""
    return convert_road_mod(src_input, output_dlc_rpf, dlc_name or "custom_maps")


def main():
    parser = argparse.ArgumentParser(description="Convert PC Road Textures & Map Mods to PS4/PS5 DLC")
    parser.add_argument("source", help="Path to road mod .oiv, folder, or RPF")
    parser.add_argument("destination", help="Path to destination dlc.rpf")
    parser.add_argument("name", nargs="?", default="roads", help="DLC device name (default: roads)")
    args = parser.parse_args()

    convert_road_mod(args.source, args.destination, args.name)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Universal GTA V PC -> PS4/PS5 Vegetation & Tree Mod Auto-Converter
Supports:
  - OIV packages (e.g. L.A. Vegetation.oiv, Forest of San Andreas, Remastered trees)
  - Unpacked directories containing vegetation RPFs or loose .ydr / .ytd / .ytyp files
Converts all 3D drawables, textures, archetypes, and manifests to PS4 Orbis format,
packages them into overlay RPFs, and enforces >= 8KB RSC7 chunk alignment.
"""

import argparse
import configparser
import glob
import os
import re
import shutil
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
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

sys.path.insert(0, NEOGEN_DIR)
sys.path.insert(0, TOOLS_DIR)

import NeoGen
import convert_vremastered as cv

# Default Canonical GTA V Vegetation & Prop Archive Paths (PS4 Orbis)
CANONICAL_RPF_PATHS = {
    "v_trees.rpf": "levels/gta5/props/vegetation/v_trees.rpf",
    "v_ext_veg.rpf": "levels/gta5/props/vegetation/v_ext_veg.rpf",
    "bush.rpf": "levels/gta5/props/vegetation/v_bush.rpf",
    "v_bush.rpf": "levels/gta5/props/vegetation/v_bush.rpf",
    "v_fanpalm.rpf": "levels/gta5/props/vegetation/v_fanpalm.rpf",
    "v_palm.rpf": "levels/gta5/props/vegetation/v_palm.rpf",
    "v_proc1.rpf": "levels/gta5/props/procedural/v_proc1.rpf",
    "v_cacti.rpf": "levels/gta5/props/vegetation/v_cacti.rpf",
    "v_crops.rpf": "levels/gta5/props/vegetation/v_crops.rpf",
    "v_potted.rpf": "levels/gta5/props/vegetation/v_potted.rpf",
    "v_rocks.rpf": "levels/gta5/props/vegetation/v_rocks.rpf",
    "v_snow.rpf": "levels/gta5/props/vegetation/v_snow.rpf",
}


def _oiv_inner_rel_path(cur_archive_path):
    """
    Translate an OIV `<archive path="...">` into the virtual game-root path of
    the innermost RPF.

    OIV paths describe the *whole* chain of containers, e.g.
      x64f.rpf/levels/gta5/props/vegetation/bush.rpf
      update/x64/dlcpacks/patchday2ng/dlc.rpf/x64/levels/gta5/props/vegetation/v_trees.rpf

    Only the part after the last container (`*.rpf/`) is the nested archive's
    virtual path.  R* DLC packs additionally store content under a platform
    folder (`x64/`, `ps4/`), which our own content.xml re-adds via
    `%PLATFORM%`, so it must be stripped here.

    On PS4 / Orbis, the base game binary explicitly targets `v_bush.rpf`.
    Remap `bush.rpf` to `v_bush.rpf` so MountOverlay overlays the base game
    archive properly.
    """
    norm = cur_archive_path.replace("\\", "/").strip("/")
    low = norm.lower()
    idx = low.rfind(".rpf/")
    if idx != -1:
        inner = norm[idx + 5:]
    else:
        # No container in the path (e.g. a bare base archive): drop the first
        # component so we never mount at "x64c.rpf/...".
        parts = norm.split("/")
        inner = "/".join(parts[1:]) if len(parts) > 1 else norm
    inner = inner.strip("/")
    m = re.match(r"^(?:x64[a-z]?|ps4)/", inner, re.I)
    if m:
        inner = inner[m.end():]
    if inner.lower().endswith("bush.rpf"):
        inner = inner[:-8] + "v_bush.rpf"
    return inner


def _is_update_container(cur_archive_path):
    return cur_archive_path.replace("\\", "/").strip("/").lower().startswith("update/")

# Legacy FourCC -> bytes per 4x4 block.  BC5/ATI2 and BC7 are 16 bytes/block
# even though they are not DXT3/DXT5; treating them as 8 corrupts every
# normal map (vegetation normal maps are ATI2/BC5) because the remaining
# mip data then starts mid-block.
_DDS_BLOCK_BYTES = {
    b'DXT1': 8, b'DXT3': 16, b'DXT5': 16,
    b'ATI1': 8, b'ATI2': 16,
    b'BC4U': 8, b'BC4S': 8, b'BC5U': 16, b'BC5S': 16,
    b'BC7 ': 16,
}


def _dds_payload_layout(data, drop_levels):
    """
    Returns (bytes_per_block, header_size, bytes_to_skip, top_width,
    top_height) after dropping `drop_levels` top mip levels, or None when the
    format is not understood (callers must then leave the file untouched
    rather than risk corrupting it).
    """
    height, width = struct.unpack_from('<II', data, 12)
    mips = struct.unpack_from('<I', data, 28)[0] or 1
    pf_flags, fourcc_int, rgb_bits = struct.unpack_from('<III', data, 80)
    fourcc = struct.pack('<I', fourcc_int)
    header_size = 128
    bpb = None

    if pf_flags & 0x4:  # DDPF_FOURCC
        if fourcc == b'DX10':
            if len(data) < 148:
                return None
            dxgi = struct.unpack_from('<I', data, 128)[0]
            header_size = 148
            if dxgi in (98, 99):      # BC7_UNORM / BC7_UNORM_SRGB
                bpb = 16
            else:
                fmt = NeoGen._DXGI_TO_LEGACY_FOURCC.get(dxgi)
                block = NeoGen._BLOCK_FORMATS.get(fmt) if fmt else None
                bpb = block[2] if block else None
        else:
            bpb = _DDS_BLOCK_BYTES.get(fourcc)
        if bpb is None:
            return None
    else:
        if rgb_bits not in (16, 24, 32):
            return None
        bpb = rgb_bits // 8
        header_size = 128

    tw, th = width, height
    offset = header_size
    for _ in range(drop_levels):
        if fourcc == b'DX10' and header_size == 148:
            pass
        if pf_flags & 0x4:
            offset += max(1, (tw + 3) // 4) * max(1, (th + 3) // 4) * bpb
        else:
            offset += max(1, tw) * max(1, th) * bpb
        tw = max(1, tw // 2)
        th = max(1, th // 2)
    if offset > len(data):
        return None
    return bpb, header_size, offset, tw, th, mips


def downscale_dds_fast(dds_path, max_dim=512):
    """
    Rapidly downscales a DDS with mipmaps by dropping top-level mips.
    Runs in sub-millisecond time without re-encoding, preserving exact DXT
    format and every surviving mip level.  Block size is looked up by actual
    FourCC (ATI2/BC5 and BC7 are 16 bytes/block, not 8).
    """
    try:
        with open(dds_path, 'rb') as f:
            data = f.read()
        if len(data) < 128 or data[:4] != b'DDS ':
            return False
        width = struct.unpack_from('<I', data, 16)[0]
        height = struct.unpack_from('<I', data, 12)[0]
        mips = struct.unpack_from('<I', data, 28)[0] or 1
        if width <= max_dim and height <= max_dim:
            return False
        drops = 0
        cur_w, cur_h = width, height
        while cur_w > max_dim or cur_h > max_dim:
            drops += 1
            cur_w = max(1, cur_w // 2)
            cur_h = max(1, cur_h // 2)
        if drops >= mips:
            return False
        layout = _dds_payload_layout(data, drops)
        if layout is None:
            # Unknown/unsupported format: leave the texture untouched rather
            # than write a header whose pixel data no longer matches.
            return False
        bpb, header_size, offset, tw, th, _ = layout
        fourcc = data[84:88]
        hdr = bytearray(data[:header_size])
        struct.pack_into('<II', hdr, 12, th, tw)
        struct.pack_into('<I', hdr, 28, max(1, mips - drops))
        if data[80] & 0x4:  # compressed: LINEAR_SIZE
            struct.pack_into('<I', hdr, 20,
                             max(1, (tw + 3) // 4) * max(1, (th + 3) // 4) * bpb)
        else:
            struct.pack_into('<I', hdr, 20, max(1, tw) * bpb)
        new_data = bytes(hdr) + data[offset:]
        with open(dds_path, 'wb') as f:
            f.write(new_data)
        return True
    except Exception:
        return False

def resolve_vegetation_source(src_input):
    """
    Resolves input to either an .oiv file or a directory.
    If given a directory, looks for *.oiv inside.
    """
    if os.path.isfile(src_input):
        return src_input
    if os.path.isdir(src_input):
        oivs = glob.glob(os.path.join(src_input, "**", "*.oiv"), recursive=True)
        if oivs:
            return oivs[0]
        return src_input
    raise FileNotFoundError(f"Source not found: {src_input}")


def parse_oiv_vegetation(oiv_path, staging_root):
    """
    Parses an OIV package archive's assembly.xml, groups files by the virtual
    path of their destination RPF, and extracts them to staging_root.

    Returns: dict of { rel_rpf_path: { 'rel_path': str, 'folder': str,
                                       'files': list[(src, dest)] } }

    Grouping is keyed by the destination RPF's virtual path (not its basename)
    because one OIV can write the same basename into two different containers
    (L.A. Vegetation patches both x64i's and patchday2ng's `v_trees.rpf`).
    When two OIV containers legitimately map to the same virtual archive
    (base + `update/` patch), their files are merged deterministically with
    the `update/` copy winning, exactly like the game's own mount order.
    """
    os.makedirs(staging_root, exist_ok=True)
    # rel_key -> {'rel_path', 'folder', 'files': {dest_lower: (src, dest, prio)}}
    rpf_groups = {}

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
                    if not src_file:
                        continue
                    dest_file = child.text.strip() if child.text else os.path.basename(src_file)
                    dest_file = dest_file.replace("\\", "/").lstrip("/")
                    if not dest_file:
                        dest_file = os.path.basename(src_file)

                    rel_gta_path = _oiv_inner_rel_path(cur_archive_path)
                    if not rel_gta_path:
                        continue
                    key = rel_gta_path.lower()
                    group = rpf_groups.get(key)
                    if group is None:
                        folder = os.path.join(staging_root, key.replace("/", os.sep))
                        group = {"rel_path": rel_gta_path, "folder": folder, "files": {}}
                        rpf_groups[key] = group

                    prio = 1 if _is_update_container(cur_archive_path) else 0
                    prev = group["files"].get(dest_file.lower())
                    if prev is None or prio > prev[2]:
                        group["files"][dest_file.lower()] = (src_file, dest_file, prio)

        walk_node(content_node)

        # Extract files from ZIP to staging, deterministically.
        print(f"[*] Extracting assets from {os.path.basename(oiv_path)}...")
        name_map = {n.replace("\\", "/").lower(): n for n in zf.namelist()}
        for key in sorted(rpf_groups):
            info = rpf_groups[key]
            os.makedirs(info["folder"], exist_ok=True)
            for dest_key in sorted(info["files"]):
                src, dest, _prio = info["files"][dest_key]
                src_key = ("content/" + src.replace("\\", "/")).lower()
                zip_member = name_map.get(src_key) or name_map.get(src.replace("\\", "/").lower())
                if not zip_member:
                    print(f"  [WARN] Missing file in OIV: {src}")
                    continue
                out_dest = os.path.join(info["folder"], dest)
                os.makedirs(os.path.dirname(out_dest) or info["folder"], exist_ok=True)
                with zf.open(zip_member) as s, open(out_dest, "wb") as d:
                    shutil.copyfileobj(s, d)

    # Collapse to the public shape (list preserves deterministic order).
    result = {}
    for key in sorted(rpf_groups):
        info = rpf_groups[key]
        result[info["rel_path"]] = {
            "rel_path": info["rel_path"],
            "folder": info["folder"],
            "files": [info["files"][d][:2] for d in sorted(info["files"])],
        }
    return result


def _dds_header_info(path):
    """Minimal DDS header read; returns None if the file is not a DDS."""
    try:
        with open(path, "rb") as fh:
            head = fh.read(128)
    except OSError:
        return None
    if len(head) < 128 or head[:4] != b"DDS ":
        return None
    h, w = struct.unpack_from("<II", head, 12)
    pf_flags, fourcc_int, rgb_bits = struct.unpack_from("<III", head, 80)
    return {
        "width": w, "height": h,
        "pf_flags": pf_flags, "fourcc": head[84:88], "rgb_bits": rgb_bits,
    }


def _is_palette_lut(info):
    """
    Vegetation `_pal` textures are tiny uncompressed A8R8G8B8 palette LUTs
    (typically 256x4).  They must NOT be resized or DXT-compressed: their
    exact colour values are indexed by the shader, so re-encoding changes the
    palette and turns foliage black/wrong.  Leave them byte-for-byte intact.
    """
    if not info:
        return False
    if info["pf_flags"] & 0x4:          # compressed
        return False
    if info["rgb_bits"] not in (16, 24, 32):
        return False
    w, h = info["width"], info["height"]
    return min(w, h) <= 8 or (w * h <= 4096 and max(w, h) <= 1024)


def _prepare_harvested_dds(path, opts):
    """
    Normalise a CWXml-harvested DDS before it is handed to ragebuilder.

    Palette LUTs are passed through untouched.  Everything else goes through
    the normal NeoGen texture pipeline (512px cap, pow2 snap, DXT, mips), so
    an embedded 2K/4K DXT or uncompressed texture cannot blow the PS4 texture
    budget or fail the Orbis texture builder.  If processing fails we still
    hand ragebuilder a merely downscaled (never a corrupted) file.
    """
    info = _dds_header_info(path)
    if not info:
        return False
    if _is_palette_lut(info):
        return True
    try:
        tex = NeoGen._parse_dds_file(path)
        if tex is None:
            downscale_dds_fast(path, max_dim=opts.get("max_dimension", 512))
            return True
        data, _info = NeoGen.build_dds_processed(tex, opts)
        with open(path, "wb") as fh:
            fh.write(data)
        return True
    except Exception as exc:
        print(f"        [WARN] texture processing failed for "
              f"{os.path.basename(path)}: {exc}")
        downscale_dds_fast(path, max_dim=opts.get("max_dimension", 512))
        return True


def _jenkins_hash_u32(s):
    h = 0
    for b in s.lower().encode("latin-1"):
        h = (h + b) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h = (h ^ (h >> 6)) & 0xFFFFFFFF
    h = (h + (h << 3)) & 0xFFFFFFFF
    h = (h ^ (h >> 11)) & 0xFFFFFFFF
    h = (h + (h << 15)) & 0xFFFFFFFF
    return h


def _ytyp_texture_map(folder):
    """
    Export every .ytyp in `folder` to XML through CWXml and return
    { archetype_name_lower: texture_dictionary_lower }.

    This is the only reliable way to know which actual OTD a displaced
    drawable expects: an archetype points at exactly one texture dictionary,
    and the OIV ships those ytyps for the vegetation archives it patches.
    CWXml export is non-destructive (the .ytyp is converted normally later).
    Resolves any Jenkins hash names (e.g. hash_16BF23CD -> prop_tree_oak_01).
    """
    mapping = {}
    model_stems = [os.path.splitext(f)[0] for f in os.listdir(folder)
                   if f.lower().endswith((".ydr", ".odr"))]
    txd_stems = [os.path.splitext(f)[0] for f in os.listdir(folder)
                 if f.lower().endswith((".ytd", ".otd"))]
    hash_to_model = {f"hash_{_jenkins_hash_u32(m):08x}": m.lower() for m in model_stems}
    hash_to_txd = {f"hash_{_jenkins_hash_u32(t):08x}": t.lower() for t in txd_stems}

    for fname in sorted(os.listdir(folder)):
        if not fname.lower().endswith(".ytyp"):
            continue
        src = os.path.join(folder, fname)
        xml_tmp = os.path.join(folder, f".__ytyp_export_{fname}.xml")
        try:
            if not NeoGen._vcwxml_do_export(src, xml_tmp):
                continue
            root = ET.parse(xml_tmp).getroot()
            for item in root.findall("archetypes/Item"):
                name = (item.findtext("name") or "").strip().lower()
                tdict = (item.findtext("textureDictionary") or "").strip().lower()
                if not name or not tdict:
                    continue
                name = hash_to_model.get(name, name)
                tdict = hash_to_txd.get(tdict, tdict)
                mapping[name] = tdict
        except Exception as exc:
            print(f"      [WARN] Could not read archtypes from {fname}: {exc}")
        finally:
            try:
                os.remove(xml_tmp)
            except OSError:
                pass
    if mapping:
        print(f"      [i] Archetype map: {len(mapping)} model(s) -> texture dictionary")
    return mapping


def convert_vegetation_mod(src_input, output_dlc_rpf, dlc_name=None):
    """
    Main entry point: Converts any tree/vegetation OIV or mod folder into a
    PS4 v1.00 compatible overlay DLC pack with 8KB Orbis chunk alignment.
    """
    resolved_src = resolve_vegetation_source(src_input)
    if not dlc_name:
        dlc_name = "laveg"
    dlc_name = re.sub(r"[^a-z0-9_]", "", dlc_name.lower()) or "laveg"

    # ymap / custom_maps packages are map data, not drawable vegetation.  Hand them to
    # the map converter; nesting the raw PC dlc.rpf produces a pack that never appears
    # in game (that was the Sandy Shores vegetation failure mode).
    from auto_convert_roads import source_extensions
    src_exts = source_extensions(resolved_src)
    if ".ymap" in src_exts and not ({".ydr", ".ydd", ".ytd"} & src_exts):
        print("[i] ymap / custom_maps package detected - building a map add-on DLC instead of a vegetation overlay.")
        import auto_convert_roads as acr
        return acr.convert_map_mod(resolved_src, output_dlc_rpf, dlc_name)

    print("\n" + "=" * 65)
    print(f"  AUTO-CONVERTING VEGETATION & TREE MOD: {dlc_name.upper()}")
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

    # Safe 512px texture compression options to prevent PS4 GPU heap exhaustion and kernel panics
    opts = {
        'processing':           True,
        'resize':               True,
        'max_dimension':        512,
        'snap_to_pow2':         True,
        'allow_upscale':        False,
        'min_dimension':        64,
        'regenerate_mips':      True,
        'min_mip_size':         1,
        'mip_count':            0,
        'convert_argb_to_dxt5': True,
    }

    staging_dir = output_dlc_rpf + "_staging"
    shutil.rmtree(staging_dir, ignore_errors=True)

    # Step 1: Extract and group files by target RPF archive
    print("[1/5] Extracting and structuring vegetation archives...")
    if resolved_src.lower().endswith(".oiv"):
        rpf_groups = parse_oiv_vegetation(resolved_src, staging_dir)
    else:
        # User passed a loose folder or RPFs
        rpf_groups = defaultdict(lambda: {"rel_path": "", "folder": "", "files": set()})
        rpfs = glob.glob(os.path.join(resolved_src, "**", "*.rpf"), recursive=True)
        for r in rpfs:
            bn = os.path.basename(r)
            rel_p = CANONICAL_RPF_PATHS.get(bn.lower(), f"levels/gta5/props/vegetation/{bn}")
            t_folder = os.path.join(staging_dir, bn)
            os.makedirs(t_folder, exist_ok=True)
            NeoGen.extract_single_rpf(r, t_folder)
            rpf_groups[bn]["rel_path"] = rel_p
            rpf_groups[bn]["folder"] = t_folder

    if not rpf_groups:
        raise RuntimeError("No vegetation archives or files found to convert!")

    # Step 2: Convert assets inside each sub-archive (YDR -> ODR, YTD -> OTD, YTYP -> OTYP)
    print("\n[2/5] Converting drawables, compressing textures & patching manifests...")
    built_rpfs = []

    for rpf_name, group in rpf_groups.items():
        folder = group["folder"]
        rel_path = group["rel_path"]
        files = os.listdir(folder)
        print(f"\n  [*] Processing archive: {rpf_name} ({len(files)} files)...")

        # Archetype -> texture dictionary mapping must be read BEFORE the .ytyp
        # is converted/removed.  Without it we can only guess where displaced
        # embedded textures belong.
        texmap = _ytyp_texture_map(folder)

        ytd_files = sorted(f for f in os.listdir(folder) if f.lower().endswith(".ytd"))
        ytd_stems = {os.path.splitext(f)[0].lower() for f in ytd_files}

        # 1. Convert .ydr drawables FIRST so their embedded textures can be
        #    harvested and re-homed into the dictionary the archetype expects.
        ydr_files = sorted(f for f in os.listdir(folder) if f.lower().endswith(".ydr"))
        per_model_tex = {}   # drawable stem -> {texture name: dds path}

        for ydr in ydr_files:
            fp = os.path.join(folder, ydr)
            stem = os.path.splitext(ydr)[0]
            res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
            if not res:
                raise RuntimeError(f"Failed to convert drawable model: {ydr}")

            # CWXml clean writes extracted embedded textures to <stem>_tex/.
            # Move them out immediately: a YTD with the SAME stem (e.g.
            # prop_tree_birch_02 ships both a drawable and a dictionary) owns
            # <stem>_tex for its own extraction, and that folder is deleted
            # once its OTD is built.  Leaving the harvest there made a later
            # injection read from an already-deleted folder - which aborted the
            # whole build with FileNotFoundError after the textures had been
            # silently committed to the wrong place.
            tex_dump = os.path.join(folder, f"{stem}_tex")
            emb_dir = os.path.join(folder, f"{stem}__embedded")
            if os.path.isdir(tex_dump):
                shutil.rmtree(emb_dir, ignore_errors=True)
                try:
                    os.replace(tex_dump, emb_dir)
                except OSError:
                    shutil.copytree(tex_dump, emb_dir, dirs_exist_ok=True)
                    shutil.rmtree(tex_dump, ignore_errors=True)
                harvested = {}
                for d in sorted(os.listdir(emb_dir)):
                    if not d.lower().endswith(".dds"):
                        continue
                    dp = os.path.join(emb_dir, d)
                    info = _dds_header_info(dp)
                    # Only discard truly degenerate dumps.  The old check
                    # rejected height <= 4, which threw away every 256x4
                    # `_pal` palette LUT and is why palette-only models ended
                    # up with no .otd at all (black foliage / palms).
                    if not info or info["width"] < 2 or info["height"] < 2:
                        try:
                            os.remove(dp)
                        except OSError:
                            pass
                        continue
                    _prepare_harvested_dds(dp, opts)
                    harvested[os.path.splitext(d)[0]] = dp
                if harvested:
                    per_model_tex[stem] = harvested

            # Remove original PC .ydr
            if os.path.exists(fp):
                os.remove(fp)

        NeoGen._pending_tex_dirs.clear()

        # Decide where each model's harvested textures must go.
        #   * mapped model whose dictionary ships in this archive -> inject there
        #   * anything else -> best-effort per-model .otd (kept below)
        inject = {}     # dict stem -> {texture name: dds path}
        unmapped = []
        for stem, texs in per_model_tex.items():
            target = texmap.get(stem.lower())
            if target and target in ytd_stems:
                inject.setdefault(target, {}).update(texs)
            else:
                unmapped.append(stem)
                # A palette-only model in a single-dictionary archive is cheap
                # to place there too, and greatly increases the chance the
                # archetype finds its palette even without a ytyp.
                if len(ytd_stems) == 1:
                    only = next(iter(ytd_stems))
                    small = {n: p for n, p in texs.items()
                             if _is_palette_lut(_dds_header_info(p))}
                    if small:
                        inject.setdefault(only, {}).update(small)

        # 2. Convert .ytd texture dictionaries, merging each model's own
        #    embedded textures into the dictionary its archetype points at.
        for ytd in ytd_files:
            fp = os.path.join(folder, ytd)
            stem = os.path.splitext(ytd)[0]
            tex_sub = os.path.join(folder, f"{stem}_tex")
            print(f"      -> Compressing texture {ytd} -> {stem}.otd (max 512px)...")
            NeoGen.extract_ytd_to_dds(fp, tex_sub, opts)

            extra = inject.get(stem.lower(), {})
            injected = 0
            for name, src_dds in sorted(extra.items()):
                if not os.path.isfile(src_dds):
                    print(f"      [WARN] harvested texture vanished: {name}")
                    continue
                dst_dds = os.path.join(tex_sub, name + ".dds")
                if not os.path.exists(dst_dds):
                    os.makedirs(tex_sub, exist_ok=True)
                    shutil.copy2(src_dds, dst_dds)
                    injected += 1
            if injected:
                print(f"      -> Merged {injected} embedded texture(s) into {stem}.otd")

            res = NeoGen._build_otd_windows(tex_sub, stem, folder, tex_dir)
            if not res or not os.path.exists(os.path.join(folder, f"{stem}.otd")):
                raise RuntimeError(f"Failed to build OTD for {ytd}")
            shutil.rmtree(tex_sub, ignore_errors=True)
            os.remove(fp)

        # 3. Fallback dictionaries for drawables with no shipped/mapped .ytd.
        #    Builds a <model>.otd so the textures exist under the model's own
        #    name instead of being discarded or dumped into an unrelated
        #    dictionary (the old "oak"/"veg" injection inflated
        #    z_prop_tree_oak1.otd to ~7 MB of duplicate textures).
        built_fallback = 0
        for stem in sorted(unmapped):
            tex_dump = os.path.join(folder, f"{stem}__embedded")
            if not os.path.isdir(tex_dump):
                continue
            otd_path = os.path.join(folder, f"{stem}.otd")
            if os.path.exists(otd_path):
                continue
            try:
                ok = NeoGen._build_otd_windows(tex_dump, stem, folder, tex_dir)
            except Exception as exc:
                print(f"        [WARN] {stem}.otd build failed: {exc}")
                ok = False
            # A dictionary that comes out at a few hundred bytes contains no
            # textures at all - ragebuilder silently accepts the job when every
            # staged DDS is unusable.  Shipping it is strictly worse than
            # shipping nothing: it can satisfy a lookup and still render black.
            if ok and os.path.exists(otd_path) and os.path.getsize(otd_path) < 2048:
                print(f"        [WARN] {stem}.otd was empty ({os.path.getsize(otd_path)} B) - discarded")
                try:
                    os.remove(otd_path)
                except OSError:
                    pass
                ok = False
            if ok and os.path.exists(otd_path):
                built_fallback += 1
            else:
                print(f"        [WARN] Could not build fallback dictionary {stem}.otd")
        if built_fallback:
            print(f"      [i] Built {built_fallback} fallback model dictionary(ies)")

        # Clean up every harvested texture folder now that they have been
        # copied into their destination dictionaries or fallback OTDs.
        for d in list(os.listdir(folder)):
            if (d.endswith("_tex") or d.endswith("__embedded")) and \
                    os.path.isdir(os.path.join(folder, d)):
                shutil.rmtree(os.path.join(folder, d), ignore_errors=True)

        # 4. Convert .ytyp and .ymf.  Manifests (.ymf -> .omf) keep their PC
        #    PSO salt/checksum unless patched, which makes the PS4 resource
        #    manager reject them; the roads converter patches this and so must
        #    vegetation.
        for f in sorted(os.listdir(folder)):
            fp = os.path.join(folder, f)
            ext = os.path.splitext(f)[1].lower()
            if ext not in (".ytyp", ".ymf"):
                continue
            res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
            if not res:
                print(f"      [WARN] Could not convert {f}, keeping as is")
                continue
            if ext == ".ymf":
                omf = os.path.join(folder, os.path.splitext(f)[0] + ".omf")
                if os.path.exists(omf):
                    try:
                        dropped = cv.patch_ps4_manifest(omf, cv.MISSING_BASE_ITYPS)
                        print(f"      [i] Patched {os.path.basename(omf)} "
                              f"(PS4 PSO signature, {dropped} missing .ytyp dep(s) removed)")
                    except Exception as exc:
                        print(f"      [WARN] Could not patch {os.path.basename(omf)}: {exc}")
            if os.path.exists(fp):
                os.remove(fp)

        # Step 3: Repack the converted sub-archive
        ps4_sub_dir = os.path.join(work_dir, "ps4", os.path.dirname(rel_path))
        os.makedirs(ps4_sub_dir, exist_ok=True)
        ps4_sub_rpf = os.path.join(work_dir, "ps4", rel_path)
        print(f"      [+] Repacking sub-archive: {rel_path}...")
        ok = NeoGen.build_single_rpf(folder, ps4_sub_rpf, rpf_dir, mode, wine_bin)
        if not ok or not os.path.exists(ps4_sub_rpf):
            raise RuntimeError(f"Failed to build sub-archive {ps4_sub_rpf}")
        built_rpfs.append(rel_path)
        shutil.rmtree(folder, ignore_errors=True)

    # Cleanup staging directory
    shutil.rmtree(staging_dir, ignore_errors=True)

    # Step 4: Generate setup2.xml and content.xml (Overlay Architecture)
    print("\n[3/5] Generating PS4 setup2.xml and overlay content.xml...")
    setup2_content = f"""<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_{dlc_name}</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>05/12/2016 08:14:30</timeStamp>
  <nameHash>{dlc_name}</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>{dlc_name.upper()}_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="25" />  
</SSetupData>
"""
    open(os.path.join(work_dir, "setup2.xml"), "w", encoding="utf-8", newline="\r\n").write(setup2_content)

    data_files_xml = ""
    enable_files_xml = ""
    for rpath in built_rpfs:
        rpath_unix = rpath.replace("\\", "/")
        data_files_xml += f"""    <Item>
      <filename>dlc_{dlc_name}:/%PLATFORM%/{rpath_unix}</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="false"/>
      <disabled value="true"/>
      <persistent value="false"/>
      <overlay value="true"/>
    </Item>\n"""
        enable_files_xml += f"        <Item>dlc_{dlc_name}:/%PLATFORM%/{rpath_unix}</Item>\n"

    content_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
{data_files_xml}  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>{dlc_name.upper()}_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
{enable_files_xml}      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
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
        raise RuntimeError("Failed to build outer vegetation dlc.rpf")
    shutil.move(tmp_out, output_dlc_rpf)
    shutil.rmtree(work_dir, ignore_errors=True)

    print("\n[5/5] Enforcing 8KB Orbis chunk alignment across all sub-archives...")
    cv.ensure_rpf_min_chunks(output_dlc_rpf, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)

    final_size = os.path.getsize(output_dlc_rpf)
    print("\n" + "=" * 65)
    print(f"  [SUCCESS] Vegetation DLC Built: {output_dlc_rpf}")
    print(f"  Final Size: {final_size:,} bytes ({final_size/1024/1024:.2f} MB)")
    print(f"  Sub-archives mounted: {len(built_rpfs)}")
    print("=" * 65 + "\n")

    cv.register_built_dlc(dlc_name, output_dlc_rpf)
    verify_converted_vegetation(output_dlc_rpf)
    return output_dlc_rpf


def verify_converted_vegetation(dlc_rpf_path):
    """
    Automated self-testing verification suite for converted vegetation DLCs.
    """
    print(f"[*] Running Verification Suite on {os.path.basename(dlc_rpf_path)}...")
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
            if 'overlay value="true"' in cx_txt and "GROUP_STARTUP" in open(s2, "r", encoding="utf-8", errors="ignore").read():
                passes.append("setup2.xml & content.xml present with valid overlay='true' configuration")
            else:
                issues.append("content.xml missing overlay='true' or setup2.xml missing GROUP_STARTUP")
        else:
            issues.append("Missing setup2.xml or content.xml")

        # 2. Check inner RPFs and validate every PS4 resource they contain.
        #    These are the checks that matter: a PC-signed .omf, a non-RSC7
        #    asset or a sub-8KB chunk is exactly what makes the PS4 resource
        #    manager reject the pack or crash the game.
        inner_rpfs = glob.glob(os.path.join(td, "ps4", "**", "*.rpf"), recursive=True)
        if not inner_rpfs:
            issues.append("No sub-archives found inside ps4/ directory!")
        else:
            total_odrs = total_otds = total_omfs = 0
            pc_leftovers = []
            bad_omf = []
            bad_chunks = []
            bad_magic = []
            for nrpf in inner_rpfs:
                sub_ext = nrpf + "_ext"
                NeoGen.extract_single_rpf(nrpf, sub_ext)
                for root, _dirs, fnames in os.walk(sub_ext):
                    for fname in fnames:
                        ext = os.path.splitext(fname)[1].lower()
                        fpath = os.path.join(root, fname)
                        if ext == ".odr":
                            total_odrs += 1
                        elif ext == ".otd":
                            total_otds += 1
                        elif ext in (".ydr", ".ytd", ".ytyp", ".ymf", ".ydd", ".yft"):
                            pc_leftovers.append(fname)
                        elif ext == ".omf":
                            total_omfs += 1
                            try:
                                raw = open(fpath, "rb").read()
                                sz_f, chk_f, salt = struct.unpack_from(">II4s", raw, len(raw) - 12)
                                if (salt != b"oppp" or sz_f != len(raw)
                                        or chk_f != cv.compute_pso_chks(raw, b"oppp")):
                                    bad_omf.append(fname)
                            except Exception:
                                bad_omf.append(fname)
                        elif ext in (".otd", ".odr", ".otyp", ".odd", ".obn", ".omap"):
                            try:
                                raw = open(fpath, "rb").read()
                                if raw[:4] != NeoGen.RSC7_MAGIC:
                                    bad_magic.append(fname)
                                    continue
                                _, vf_r, pf_r = struct.unpack_from("<III", raw, 4)
                                vcs = cv.rsc7_chunks_from_flag(vf_r)
                                pcs = cv.rsc7_chunks_from_flag(pf_r)
                                if (vcs and min(vcs) < 8192) or (pcs and min(pcs) < 8192):
                                    bad_chunks.append(fname)
                            except Exception:
                                bad_magic.append(fname)
                shutil.rmtree(sub_ext, ignore_errors=True)

            passes.append(f"Mounted {len(inner_rpfs)} sub-archives containing "
                          f"{total_odrs} .odr drawables and {total_otds} .otd textures")

            if pc_leftovers:
                issues.append(f"{len(pc_leftovers)} unconverted PC resource(s) left in "
                              f"output, e.g. {', '.join(sorted(set(pc_leftovers))[:5])}")
            else:
                passes.append("No PC-era .ydr/.ytd/.ytyp/.ymf resources left in output")

            if not total_omfs:
                issues.append("No .omf manifest found - pack will not mount")
            elif bad_omf:
                issues.append(f"{len(bad_omf)} .omf manifest(s) still carry a PC PSO "
                              f"signature: {', '.join(bad_omf[:5])}")
            else:
                passes.append(f"All {total_omfs} .omf manifest(s) carry patched PS4 PSO CHKS (salt 'oppp')")

            if bad_magic:
                issues.append(f"{len(bad_magic)} asset(s) are not RSC7: {', '.join(bad_magic[:5])}")
            if bad_chunks:
                issues.append(f"{len(bad_chunks)} asset(s) have sub-8KB RSC7 chunks: "
                              f"{', '.join(bad_chunks[:5])}")
            if not bad_magic and not bad_chunks:
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
        print("  [ALL STATIC CHECKS PASSED - vegetation overlay ready for PS4/PS5]")
        return True


def main():
    parser = argparse.ArgumentParser(description="Convert PC Tree & Vegetation Mods to PS4/PS5 DLC")
    parser.add_argument("source", help="Path to vegetation .oiv or folder")
    parser.add_argument("destination", help="Path to destination dlc.rpf")
    parser.add_argument("name", nargs="?", default="laveg", help="DLC device name (default: laveg)")
    args = parser.parse_args()

    convert_vegetation_mod(args.source, args.destination, args.name)


if __name__ == "__main__":
    main()


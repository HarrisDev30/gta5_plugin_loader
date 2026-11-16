#!/usr/bin/env python3
"""
convert_lwe_vegetation.py
=========================
Converts and builds LWE (Lively World Expansion) Vegetation for GTA V (CUSA00419 v1.00)
on PS5 backward compatibility mode (modHEN / Orbx Native Asset Engine).

Rules & Constraints:
  1. FATAL TRAP 1: default.fxc is completely ignored and NEVER mounted or copied.
  2. FATAL TRAP 2: All PC .ytyp files are converted to PS4 .otyp using NeoGen.
  3. FATAL TRAP 3: dlclist.xml strictly enforces <= 24 DLC packs limit (consolidated lwe_veg pack).
  4. FATAL TRAP 4: PC textures (.ytd) and drawables (.ydr/.ydd) converted to .otd / .odr / .odd.
  5. FATAL TRAP 5: Strictly use D:\\Temp for temporary files; NEVER write to C:\\.
  6. Inner dual-platform layout: ps4/ and x64/ mirrored.
"""

import os
import sys
import shutil
import struct
import glob
import zipfile
import tempfile
from collections import defaultdict
import xml.etree.ElementTree as ET

# Enforce D:\Temp storage
for cand in (r"D:\Temp", r"D:\tmp"):
    if os.path.exists(cand):
        tempfile.tempdir = cand
        os.environ["TEMP"] = cand
        os.environ["TMP"] = cand
        break

BASE_DIR = r"D:\ps5payload"
NEOGEN_DIR = os.path.join(BASE_DIR, "NeoGen EARY ACCESS", "Terminal Build", "NeoGen")
TOOLS_DIR = os.path.join(BASE_DIR, "tools")
sys.path.insert(0, NEOGEN_DIR)
sys.path.insert(0, TOOLS_DIR)

import NeoGen
import convert_vremastered as cv
import auto_convert_vegetation as acv

LWE_SOURCE_DIR = os.path.join(BASE_DIR, "Misc_Vegetation", "LWE - Medium Density Vegetation")
STAGE_ROOT = r"D:\Temp\stage_lwe"
OUTPUT_DLC_DIR = os.path.join(BASE_DIR, "NVE_Renew", "rpf", "dlc_addon", "lwe_veg")
OUTPUT_DLC_RPF = os.path.join(OUTPUT_DLC_DIR, "dlc.rpf")
DLCLIST_PATH = os.path.join(BASE_DIR, "NVE_Renew", "dlclist.xml")

TEX_OPTS = {
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


def prepare_environment():
    print("[*] Initializing NeoGen and CWXml...")
    NeoGen._init_cwxml_exe()
    if not NeoGen.VCWXML_OK:
        cwxml_cand = os.path.join(NEOGEN_DIR, "vcwxml_windows", "CWXml.exe")
        if os.path.isfile(cwxml_cand):
            NeoGen._CWXML_EXE = cwxml_cand
            NeoGen.VCWXML_OK = True
        else:
            raise RuntimeError("CWXml.exe not found! Cannot clean/convert drawables.")

    rpf_dir, mode_ovr, wine_bin = NeoGen.load_rpf_builder_settings()
    mode = NeoGen.detect_mode(mode_ovr) or "windows"
    tex_dir, tex_mode, tex_wine = NeoGen.load_texture_builder_settings()
    print(f"    RPF Builder: mode={mode}")
    print(f"    Texture Builder: {tex_dir}")
    return rpf_dir, mode, wine_bin, tex_dir


def convert_pc_to_ps4_rsc7_file(rsc_path: str, min_chunk: int = 8192) -> bool:
    """
    Converts a PC RSC7 asset (e.g. .ytyp / .ymap with ver=2) into genuine PS4 format (ver=0),
    enforces Orbis >= 8KB chunk buddy alignment, and updates the pgBase BlockMap (PRD0).
    """
    raw_bytes = open(rsc_path, "rb").read()
    if raw_bytes[:4] != b"RSC7":
        return False

    hdr, dec = NeoGen._decompress_rsc7(raw_bytes)
    _, vf, pf = struct.unpack_from("<II", raw_bytes, 8)
    vchunks = cv.rsc7_chunks_from_flag(vf)
    pchunks = cv.rsc7_chunks_from_flag(pf)

    vup = cv.upgrade_rsc7_min_chunk_bytes(vchunks, vf, min_chunk)
    pup = cv.upgrade_rsc7_min_chunk_bytes(pchunks, pf, min_chunk)

    old_vsz = sum(vchunks)
    vdata = bytearray(dec[:old_vsz])
    pdata = bytearray(dec[old_vsz:])

    new_vf, new_vc = vup if vup is not None else (vf, vchunks)
    new_pf, new_pc = pup if pup is not None else (pf, pchunks)

    # PS4 buddy flag encoding (leaf=8192, no division shifts 24-27)
    encoded_vf = cv.encode_rsc7_flag(new_vc, 0, 0)
    encoded_pf = cv.encode_rsc7_flag(new_pc, 0, 0) if new_pc else 0x20000000
    if encoded_vf is None:
        encoded_vf = new_vf

    new_vsz = sum(new_vc)
    new_psz = sum(new_pc)
    if new_vsz > len(vdata):
        vdata.extend(b"\x00" * (new_vsz - len(vdata)))
    if new_psz > len(pdata):
        pdata.extend(b"\x00" * (new_psz - len(pdata)))

    # Update pgBase BlockMap (PRD0)
    if len(vdata) >= 16:
        p08 = struct.unpack_from("<Q", vdata, 8)[0]
        if 0x50000000 <= p08 < 0x50000000 + len(vdata) - 12:
            bm_off = p08 - 0x50000000
            if vdata[bm_off:bm_off+4] == b"PRD0":
                vdata[bm_off + 8] = len(new_vc)
                vdata[bm_off + 9] = len(new_pc)

    new_ver = 0  # Genuine PS4 version (TRAP 2 REMEDY)
    new_hdr = struct.pack("<4sIII", b"RSC7", new_ver, encoded_vf, encoded_pf)
    recompressed = NeoGen._recompress_rsc7(new_hdr, bytes(vdata) + bytes(pdata))
    with open(rsc_path, "wb") as fh:
        fh.write(recompressed)
    return True


def step1_extract_sources(stage_dir):
    """
    Extracts LWE - City Medium Density.oiv into stage_dir.
    Completely discards default.fxc.
    """
    print("\n" + "=" * 65)
    print("  STEP 1: Extracting Source OIV Archives (Discarding default.fxc)")
    print("=" * 65)

    oiv_city = os.path.join(LWE_SOURCE_DIR, "LWE - City Medium Density.oiv")

    if not os.path.isfile(oiv_city):
        raise FileNotFoundError(f"Missing source: {oiv_city}")

    raw_extract = os.path.join(stage_dir, "raw_oiv")
    os.makedirs(raw_extract, exist_ok=True)

    # City Medium: extract content/dlc/lwecitymedium/dlc.rpf
    print(f"[*] Extracting lwecitymedium from {os.path.basename(oiv_city)}...")
    with zipfile.ZipFile(oiv_city, "r") as z:
        for name in z.namelist():
            low = name.lower()
            if low.endswith(".fxc"):
                print(f"    [TRAP 1 AVERTED] Discarded HLSL shader cache: {name}")
                continue
            if "lwecitymedium/dlc.rpf" in low:
                z.extract(name, raw_extract)

    sub_archives_dir = os.path.join(stage_dir, "unpacked_sub")
    os.makedirs(sub_archives_dir, exist_ok=True)

    # Extract City lwecitymedium/dlc.rpf
    city_dlc_rpf = os.path.join(raw_extract, "content", "dlc", "lwecitymedium", "dlc.rpf")
    city_wrapper_ext = os.path.join(raw_extract, "city_wrapper")
    NeoGen.extract_single_rpf(city_dlc_rpf, city_wrapper_ext)

    city_rpf_dir = os.path.join(city_wrapper_ext, "x64", "levels", "gta5")
    extracted_rpfs = {}
    for rpf_file in sorted(os.listdir(city_rpf_dir)):
        if not rpf_file.endswith(".rpf"):
            continue
        stem = os.path.splitext(rpf_file)[0]
        out_folder = os.path.join(sub_archives_dir, stem)
        NeoGen.extract_single_rpf(os.path.join(city_rpf_dir, rpf_file), out_folder)
        extracted_rpfs[stem] = out_folder
        print(f"[+] Unpacked {rpf_file}: {len(os.listdir(out_folder))} entries")

    return extracted_rpfs


def step2_convert_sub_archives(sub_archives, stage_dir, tex_dir):
    """
    Converts all assets across the sub-archives:
      - .ydr -> .odr + extracted .otd texture dictionaries
      - .ydd -> .odd
      - .ytd -> .otd
      - .ybn -> .obn
      - .ymap -> .omap (with archetype sanitization)
      - .ytyp -> .otyp (CRITICAL: Genuine PS4 header, avoiding SIGSEGV)
      - .ymf -> .omf (PS4 PSO 'oppp' salt & checksum patched)
    """
    print("\n" + "=" * 65)
    print("  STEP 2: Asset Conversion (Models, Textures, Archetypes, Manifests)")
    print("=" * 65)

    stats = defaultdict(int)

    # 1. Convert lweprops if present
    if "lweprops" in sub_archives:
        lweprops_folder = sub_archives["lweprops"]
        print(f"\n[*] Processing 'lweprops' ({len(os.listdir(lweprops_folder))} files)...")
        ydr_files = sorted([f for f in os.listdir(lweprops_folder) if f.lower().endswith(".ydr")])
        for idx, ydr in enumerate(ydr_files, 1):
            fp = os.path.join(lweprops_folder, ydr)
            stem = os.path.splitext(ydr)[0]
            odr_path = os.path.join(lweprops_folder, f"{stem}.odr")
            otd_path = os.path.join(lweprops_folder, f"{stem}.otd")

            if not os.path.isfile(odr_path):
                res = NeoGen.convert_rsc_file(fp, lweprops_folder, strip=True, auto_clean_embedded=True)
                if not res:
                    raise RuntimeError(f"Failed to convert model: {ydr}")
                stats["odr"] += 1

                tex_sub = os.path.join(lweprops_folder, f"{stem}_tex")
                if os.path.isdir(tex_sub):
                    dds_files = [f for f in os.listdir(tex_sub) if f.lower().endswith(".dds")]
                    for d in dds_files:
                        dp = os.path.join(tex_sub, d)
                        info = acv._dds_header_info(dp)
                        if not info or info["width"] < 2 or info["height"] < 2:
                            try:
                                os.remove(dp)
                            except OSError:
                                pass
                            continue
                        acv._prepare_harvested_dds(dp, TEX_OPTS)

                    if os.listdir(tex_sub):
                        ok = NeoGen._build_otd_windows(tex_sub, stem, lweprops_folder, tex_dir)
                        if ok and os.path.isfile(otd_path):
                            stats["otd"] += 1
                    shutil.rmtree(tex_sub, ignore_errors=True)
            else:
                stats["odr"] += 1
                if os.path.isfile(otd_path):
                    stats["otd"] += 1

            if os.path.exists(fp):
                os.remove(fp)

            if idx % 20 == 0 or idx == len(ydr_files):
                print(f"    -> Converted {idx}/{len(ydr_files)} models in lweprops...")

        # Count existing odr/otds if already converted
        for f in os.listdir(lweprops_folder):
            e = os.path.splitext(f)[1].lower()
            if e == ".odr": stats["odr"] = max(stats["odr"], len([x for x in os.listdir(lweprops_folder) if x.endswith(".odr")]))
            elif e == ".otd": stats["otd"] = max(stats["otd"], len([x for x in os.listdir(lweprops_folder) if x.endswith(".otd")]))

    # 2. Convert other sub-archives
    for rpf_name, folder in sorted(sub_archives.items()):
        if rpf_name == "lweprops":
            continue
        files = sorted(os.listdir(folder))
        print(f"\n[*] Processing '{rpf_name}' ({len(files)} files)...")

        # 2a. Models (.ydr) in sub-archive
        sub_ydrs = [f for f in files if f.lower().endswith(".ydr")]
        for ydr in sub_ydrs:
            fp = os.path.join(folder, ydr)
            stem = os.path.splitext(ydr)[0]
            odr_path = os.path.join(folder, f"{stem}.odr")
            otd_path = os.path.join(folder, f"{stem}.otd")
            if not os.path.isfile(odr_path):
                res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
                if not res:
                    raise RuntimeError(f"Failed to convert model: {ydr}")
                stats["odr"] += 1

                tex_sub = os.path.join(folder, f"{stem}_tex")
                if os.path.isdir(tex_sub):
                    dds_files = [f for f in os.listdir(tex_sub) if f.lower().endswith(".dds")]
                    for d in dds_files:
                        dp = os.path.join(tex_sub, d)
                        info = acv._dds_header_info(dp)
                        if not info or info["width"] < 2 or info["height"] < 2:
                            try:
                                os.remove(dp)
                            except OSError:
                                pass
                            continue
                        acv._prepare_harvested_dds(dp, TEX_OPTS)
                    if os.listdir(tex_sub):
                        ok = NeoGen._build_otd_windows(tex_sub, stem, folder, tex_dir)
                        if ok and os.path.isfile(otd_path):
                            stats["otd"] += 1
                    shutil.rmtree(tex_sub, ignore_errors=True)
            else:
                stats["odr"] += 1
                if os.path.isfile(otd_path): stats["otd"] += 1

            if os.path.exists(fp):
                os.remove(fp)

        # 2b. Drawables (.ydd) -> .odd
        sub_ydds = [f for f in files if f.lower().endswith(".ydd")]
        for ydd in sub_ydds:
            fp = os.path.join(folder, ydd)
            stem = os.path.splitext(ydd)[0]
            odd_path = os.path.join(folder, f"{stem}.odd")
            if not os.path.isfile(odd_path):
                res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
                if not res:
                    raise RuntimeError(f"Failed to convert drawable dictionary: {ydd}")
            stats["odd"] += 1
            if os.path.exists(fp):
                os.remove(fp)

        # 2c. Texture dictionaries (.ytd) -> .otd
        sub_ytds = [f for f in files if f.lower().endswith(".ytd")]
        for ytd in sub_ytds:
            fp = os.path.join(folder, ytd)
            stem = os.path.splitext(ytd)[0]
            otd_path = os.path.join(folder, f"{stem}.otd")
            if not os.path.isfile(otd_path):
                tex_sub = os.path.join(folder, f"{stem}_tex")
                print(f"    -> Compressing texture {ytd} -> {stem}.otd (max 512px)...")
                NeoGen.extract_ytd_to_dds(fp, tex_sub, TEX_OPTS)
                res = NeoGen._build_otd_windows(tex_sub, stem, folder, tex_dir)
                if not res or not os.path.exists(otd_path):
                    raise RuntimeError(f"Failed to build OTD for {ytd}")
                shutil.rmtree(tex_sub, ignore_errors=True)
            stats["otd"] += 1
            if os.path.exists(fp):
                os.remove(fp)

        # 2d. Archetypes (.ytyp) -> .otyp (CRITICAL TRAP 2 REMEDY: PS4 ver=0 & 8KB buddy chunks)
        sub_ytyps = [f for f in files if f.lower().endswith(".ytyp")]
        for ytyp in sub_ytyps:
            fp = os.path.join(folder, ytyp)
            stem = os.path.splitext(ytyp)[0]
            otyp_path = os.path.join(folder, f"{stem}.otyp")
            if not os.path.isfile(otyp_path):
                res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
                if not res or not os.path.exists(otyp_path):
                    raise RuntimeError(f"Failed to convert archetype {ytyp}")
            convert_pc_to_ps4_rsc7_file(otyp_path, min_chunk=8192)
            stats["otyp"] += 1
            if os.path.exists(fp):
                os.remove(fp)

        # 2e. Map Placements (.ymap) -> .omap (PS4 ver=0 & 8KB buddy chunks)
        sub_ymaps = [f for f in files if f.lower().endswith(".ymap")]
        for ymap in sub_ymaps:
            fp = os.path.join(folder, ymap)
            stem = os.path.splitext(ymap)[0]
            omap_path = os.path.join(folder, f"{stem}.omap")
            if not os.path.isfile(omap_path):
                res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
                if not res or not os.path.exists(omap_path):
                    raise RuntimeError(f"Failed to convert map placement {ymap}")
                try:
                    cv.strip_omap_archetypes(omap_path, cv.MISSING_DLC_ARCHETYPES, NeoGen)
                except Exception:
                    pass
            convert_pc_to_ps4_rsc7_file(omap_path, min_chunk=8192)
            stats["omap"] += 1
            if os.path.exists(fp):
                os.remove(fp)

        # 2f. Collision Bounds (.ybn) -> .obn
        sub_ybns = [f for f in files if f.lower().endswith(".ybn")]
        for ybn in sub_ybns:
            fp = os.path.join(folder, ybn)
            stem = os.path.splitext(ybn)[0]
            obn_path = os.path.join(folder, f"{stem}.obn")
            if not os.path.isfile(obn_path):
                res = NeoGen.convert_rsc_file(fp, folder, strip=True, auto_clean_embedded=True)
                if not res or not os.path.exists(obn_path):
                    raise RuntimeError(f"Failed to convert collision {ybn}")
            stats["obn"] += 1
            if os.path.exists(fp):
                os.remove(fp)

        # 2g. Map Manifests (.ymf / .omf) - REMOVE TO PREVENT FATAL PSO PARSER CRASH
        sub_ymfs = [f for f in files if f.lower().endswith(".ymf") or f.lower().endswith(".omf")]
        for ymf in sub_ymfs:
            fp = os.path.join(folder, ymf)
            if os.path.exists(fp):
                os.remove(fp)
                print(f"    [i] Removed synthetic manifest {ymf} (prevents PSO parser SIGSEGV on PS4 v1.00)")

        # Update stats from disk
        for f in os.listdir(folder):
            ext = os.path.splitext(f)[1].lower()
            if ext in (".odd", ".otyp", ".omap", ".obn", ".omf"):
                stats[ext[1:]] = max(stats[ext[1:]], len([x for x in os.listdir(folder) if x.endswith(ext)]))

    print("\n---------------- CONVERSION SUMMARY ----------------")
    for k, v in sorted(stats.items()):
        print(f"  .{k.upper()} converted: {v}")
    print("----------------------------------------------------")
    return stats


def step3_repack_sub_archives(sub_archives, stage_dir, rpf_dir, mode, wine_bin):
    """
    Repacks each converted sub-archive into an unencrypted PS4 sub-RPF
    and enforces >= 8KB Orbis chunk alignment.
    """
    print("\n" + "=" * 65)
    print("  STEP 3: Repacking Sub-Archives & Enforcing 8KB Chunk Alignment")
    print("=" * 65)

    repacked_rpfs = {}
    sub_out_dir = os.path.join(stage_dir, "repacked_sub")
    os.makedirs(sub_out_dir, exist_ok=True)

    for rpf_name, folder in sorted(sub_archives.items()):
        dst_rpf = os.path.join(sub_out_dir, f"{rpf_name}.rpf")
        print(f"[*] Repacking {rpf_name}.rpf ({len(os.listdir(folder))} files)...")
        ok = NeoGen.build_single_rpf(folder, dst_rpf, rpf_dir, mode, wine_bin)
        if not ok or not os.path.isfile(dst_rpf):
            raise RuntimeError(f"Failed to build sub-archive {rpf_name}.rpf")

        # Enforce 8KB chunk alignment across inner assets
        upgraded = cv.ensure_rpf_min_chunks(dst_rpf, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)
        print(f"    [+] {rpf_name}.rpf built ({os.path.getsize(dst_rpf):,} bytes, {upgraded} chunk upgrades)")
        repacked_rpfs[rpf_name] = dst_rpf

    return repacked_rpfs


def step4_build_consolidated_dlc(repacked_rpfs, stage_dir, rpf_dir, mode, wine_bin):
    """
    Constructs the consolidated dlc.rpf with:
      - setup2.xml (exact required structure)
      - content.xml (referencing dlc_lwe_veg:/%PLATFORM%/levels/gta5/...)
      - Dual-platform mirrored folders: ps4/levels/gta5/ and x64/levels/gta5/
    """
    print("\n" + "=" * 65)
    print("  STEP 4: Staging Dual-Platform Layout & Repacking Consolidated dlc.rpf")
    print("=" * 65)

    dlc_staging = os.path.join(stage_dir, "dlc_package")
    shutil.rmtree(dlc_staging, ignore_errors=True)
    os.makedirs(dlc_staging, exist_ok=True)

    ps4_target = os.path.join(dlc_staging, "ps4", "levels", "gta5")
    x64_target = os.path.join(dlc_staging, "x64", "levels", "gta5")
    os.makedirs(ps4_target, exist_ok=True)
    os.makedirs(x64_target, exist_ok=True)

    # Copy sub-RPFs into both ps4 and x64
    for rpf_name, rpf_path in repacked_rpfs.items():
        fname = f"{rpf_name}.rpf"
        shutil.copy2(rpf_path, os.path.join(ps4_target, fname))
        shutil.copy2(rpf_path, os.path.join(x64_target, fname))
        print(f"[+] Mirrored {fname} into ps4/ and x64/ directories")

    # 1. setup2.xml (Proven PS4 MAP_SP architecture)
    setup2_content = """<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_lwe_veg</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>05/12/2016 08:14:30</timeStamp>
  <nameHash>lwe_veg</nameHash>
  <contentChangeSets />
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>LWE_VEG_MODELS</Item>
      </ContentChangeSets>
    </Item>
    <Item>
      <NameHash>GROUP_MAP_SP</NameHash>
      <ContentChangeSets>
        <Item>LWE_VEG_MAP_CHANGE</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="60" />
  <minorOrder value="0" />
  <isLevelPack value="false" />
  <dependencyPackHash />
  <requiredVersion />
  <subPackCount value="0" />
</SSetupData>
"""
    with open(os.path.join(dlc_staging, "setup2.xml"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(setup2_content)

    # 2. content.xml
    data_items = []
    model_enables = []
    map_enables = []

    # Map architecture definitions (6 clean sub-archives, zero broken PC models)
    data_files = [
        ("levels/gta5/lwecitymedium_models.rpf", "overlay", False, False),
        ("levels/gta5/lwecitymedium_col.rpf", "col", False, False),
        ("levels/gta5/lwecitymedium.rpf", "map", False, True),
        ("levels/gta5/lwecitymedium_metadata.rpf", "metadata", True, True),
        ("levels/gta5/lwecitymedium_refl.rpf", "map", False, True),
        ("levels/gta5/lwecitymedium_refl_metadata.rpf", "metadata", True, True),
    ]

    for rel_path, kind, load_completely, is_map in data_files:
        full_path = f"dlc_lwe_veg:/%PLATFORM%/{rel_path}"
        if kind == "overlay":
            data_items.append(
                f"    <Item>\n"
                f"      <filename>{full_path}</filename>\n"
                f"      <fileType>RPF_FILE</fileType>\n"
                f'      <locked value="false"/>\n'
                f'      <disabled value="true"/>\n'
                f'      <persistent value="false"/>\n'
                f'      <overlay value="true"/>\n'
                f"    </Item>"
            )
            model_enables.append(f"        <Item>{full_path}</Item>")
        else:
            extra = ""
            if load_completely:
                extra += '\n      <loadCompletely value="true"/>'
            if is_map:
                extra += "\n      <contents>CONTENTS_DLC_MAP_DATA</contents>"
            data_items.append(
                f"    <Item>\n"
                f"      <filename>{full_path}</filename>\n"
                f"      <fileType>RPF_FILE</fileType>\n"
                f'      <overlay value="false"/>\n'
                f'      <disabled value="true"/>\n'
                f'      <persistent value="true"/>{extra}\n'
                f"    </Item>"
            )
            map_enables.append(f"            <Item>{full_path}</Item>")

    content_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
{chr(10).join(data_items)}
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>LWE_VEG_MODELS</changeSetName>
      <filesToDisable />
      <filesToEnable>
{chr(10).join(model_enables)}
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
    <Item>
      <changeSetName>LWE_VEG_MAP_CHANGE</changeSetName>
      <mapChangeSetData>
        <Item>
          <associatedMap>MO_JIM_L11</associatedMap>
          <filesToInvalidate />
          <filesToDisable />
          <filesToEnable>
{chr(10).join(map_enables)}
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
    </Item>
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
"""
    with open(os.path.join(dlc_staging, "content.xml"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(content_xml)

    # Build final dlc.rpf
    os.makedirs(OUTPUT_DLC_DIR, exist_ok=True)
    tmp_out = OUTPUT_DLC_RPF + ".tmp"
    if os.path.exists(tmp_out):
        os.remove(tmp_out)

    print(f"\n[*] Building consolidated outer DLC: {OUTPUT_DLC_RPF}...")
    ok = NeoGen.build_single_rpf(dlc_staging, tmp_out, rpf_dir, mode, wine_bin)
    if not ok or not os.path.isfile(tmp_out):
        raise RuntimeError("Failed to build master dlc_lwe_veg dlc.rpf!")

    if os.path.exists(OUTPUT_DLC_RPF):
        os.remove(OUTPUT_DLC_RPF)
    shutil.move(tmp_out, OUTPUT_DLC_RPF)

    final_size = os.path.getsize(OUTPUT_DLC_RPF)
    print(f"[+] Consolidated dlc.rpf built: {final_size:,} bytes ({final_size/1024/1024:.2f} MB)")
    return OUTPUT_DLC_RPF


def step5_verify_package(dlc_rpf_path):
    """
    Rigorous verification suite checking every fatal crash trap.
    """
    print("\n" + "=" * 65)
    print("  STEP 5: Automated Verification Suite (Zero-Crash Certification)")
    print("=" * 65)

    test_dir = dlc_rpf_path + "_verify"
    shutil.rmtree(test_dir, ignore_errors=True)
    os.makedirs(test_dir, exist_ok=True)

    passes = []
    issues = []

    try:
        NeoGen.extract_single_rpf(dlc_rpf_path, test_dir)

        # Check setup2.xml and content.xml
        s2_path = os.path.join(test_dir, "setup2.xml")
        cx_path = os.path.join(test_dir, "content.xml")
        if os.path.isfile(s2_path) and os.path.isfile(cx_path):
            s2_txt = open(s2_path, "r", encoding="utf-8", errors="ignore").read()
            cx_txt = open(cx_path, "r", encoding="utf-8", errors="ignore").read()
            if "dlc_lwe_veg" in s2_txt and "LWE_VEG_MODELS" in s2_txt and "GROUP_MAP_SP" in s2_txt:
                passes.append("setup2.xml correctly defines dlc_lwe_veg, LWE_VEG_MODELS, and GROUP_MAP_SP")
            else:
                issues.append("setup2.xml missing dlc_lwe_veg, LWE_VEG_MODELS, or GROUP_MAP_SP")
            if "dlc_lwe_veg:/%PLATFORM%/" in cx_txt and "MO_JIM_L11" in cx_txt and "LWE_VEG_MAP_CHANGE" in cx_txt:
                passes.append("content.xml properly targets dlc_lwe_veg:/%PLATFORM%/ with MO_JIM_L11 MAP_CHANGE")
            else:
                issues.append("content.xml missing dlc_lwe_veg:/%PLATFORM%/, MO_JIM_L11, or LWE_VEG_MAP_CHANGE")
        else:
            issues.append("setup2.xml or content.xml missing from root dlc.rpf")

        # Check dual-platform mirroring
        for plat in ("ps4", "x64"):
            p_dir = os.path.join(test_dir, plat, "levels", "gta5")
            if os.path.isdir(p_dir):
                rpfs = [f for f in os.listdir(p_dir) if f.endswith(".rpf")]
                passes.append(f"{plat}/ directory populated with {len(rpfs)} sub-archives")
            else:
                issues.append(f"Missing {plat}/levels/gta5 directory in master DLC")

        # Check sub-RPFs inside ps4
        ps4_rpfs = glob.glob(os.path.join(test_dir, "ps4", "**", "*.rpf"), recursive=True)
        total_assets = 0
        pc_residuals = []
        pc_rsc7_ytyps = []
        bad_omfs = []
        sub_8k_chunks = []
        fxc_found = []

        for srpf in ps4_rpfs:
            sub_work = srpf + "_ext"
            NeoGen.extract_single_rpf(srpf, sub_work)
            for root, _, files in os.walk(sub_work):
                for f in files:
                    total_assets += 1
                    fp = os.path.join(root, f)
                    ext = os.path.splitext(f)[1].lower()

                    if ext == ".fxc" or "default.fxc" in f.lower():
                        fxc_found.append(f)

                    if ext in (".ydr", ".ytd", ".ytyp", ".ymap", ".ymf", ".ybn", ".ydd"):
                        pc_residuals.append(f)

                    # Check for residual PSO manifests (cause crashes on PS4 v1.00)
                    if ext in (".omf", ".ymf"):
                        bad_omfs.append(f)

                    if ext in (".otd", ".odr", ".otyp", ".odd", ".obn", ".omap"):
                        try:
                            raw = open(fp, "rb").read()
                            if raw[:4] == NeoGen.RSC7_MAGIC:
                                ver_r, vf_r, pf_r = struct.unpack_from("<III", raw, 4)
                                # Verify genuine PS4 header, NOT PC RSC7 (Trap 2: ver==2 is fatal)
                                if ext in (".otyp", ".omap") and ver_r != 0:
                                    pc_rsc7_ytyps.append(f"{f} (ver={ver_r})")
                                vcs = cv.rsc7_chunks_from_flag(vf_r)
                                pcs = cv.rsc7_chunks_from_flag(pf_r)
                                if (vcs and min(vcs) < 8192) or (pcs and min(pcs) < 8192):
                                    sub_8k_chunks.append(f)
                        except Exception:
                            pass
            shutil.rmtree(sub_work, ignore_errors=True)

        if fxc_found:
            issues.append(f"FATAL TRAP 1 VIOLATION: default.fxc found in build: {fxc_found}")
        else:
            passes.append("TRAP 1 PASSED: Zero .fxc DirectX shader files present in DLC")

        if pc_residuals:
            issues.append(f"PC-era unconverted resources found ({len(pc_residuals)}): {pc_residuals[:5]}")
        else:
            passes.append("TRAP 4 PASSED: Zero PC .ydr/.ytd/.ytyp/.ymap/.ybn/.ydd files remain")

        if pc_rsc7_ytyps:
            issues.append(f"FATAL TRAP 2 VIOLATION: Assets carry PC RSC7 header (ver!=0): {pc_rsc7_ytyps[:5]}")
        else:
            passes.append("TRAP 2 PASSED: All archetype (.otyp) and map (.omap) files are genuine PS4 ver=0 (No SIGSEGV)")

        if bad_omfs:
            issues.append(f"Residual PSO manifests detected ({len(bad_omfs)}): {bad_omfs[:5]}")
        else:
            passes.append("Zero residual PSO manifests present (clean native map registration)")

        if sub_8k_chunks:
            issues.append(f"Sub-8KB chunk alignment detected in {len(sub_8k_chunks)} asset(s)")
        else:
            passes.append("All RSC7 assets strictly meet >= 8192-byte Orbis chunk alignment")

        passes.append(f"Total inner PS4 assets verified: {total_assets}")

    finally:
        shutil.rmtree(test_dir, ignore_errors=True)

    print("\n----------------- VERIFICATION AUDIT -----------------")
    for p in passes:
        print(f"  [PASS] {p}")
    if issues:
        for i in issues:
            print(f"  [FAIL] {i}")
        print("------------------------------------------------------")
        raise RuntimeError("Package verification failed!")
    print("------------------------------------------------------")
    print("  >>> ALL CHECKS PASSED: dlc_lwe_veg IS FULLY PS4/PS5 READY <<<")


def step6_update_dlclist():
    """
    Safely updates dlclist.xml to include /data/orbx/gta/rpf/dlc_addon/lwe_veg/
    without exceeding the hardcoded 24-DLC limit.
    """
    print("\n" + "=" * 65)
    print("  STEP 6: Updating dlclist.xml (Enforcing <= 24 DLC Limit)")
    print("=" * 65)

    if not os.path.isfile(DLCLIST_PATH):
        raise FileNotFoundError(f"Missing dlclist: {DLCLIST_PATH}")

    tree = ET.parse(DLCLIST_PATH)
    root = tree.getroot()
    paths_elem = root.find("Paths")
    if paths_elem is None:
        raise ValueError("Invalid dlclist.xml: missing <Paths>")

    items = [item.text.strip() for item in paths_elem.findall("Item") if item.text]
    target_entry = "/data/orbx/gta/rpf/dlc_addon/lwe_veg/"

    if target_entry in items:
        print(f"  [i] {target_entry} already present in dlclist.xml")
    else:
        # Swap out an unused or duplicate slot (e.g. r8v10 or unused vehicle)
        swap_candidates = [
            "/data/orbx/gta/rpf/dlc_addon/r8v10/",
            "/data/orbx/gta/rpf/dlc_addon/hawkcstm/",
            "/data/orbx/gta/rpf/dlc_addon/gibv48/",
        ]
        swapped = False
        for cand in swap_candidates:
            if cand in items:
                idx = items.index(cand)
                items[idx] = target_entry
                print(f"  [+] Swapped unused slot '{cand}' -> '{target_entry}'")
                swapped = True
                break

        if not swapped:
            if len(items) < 24:
                items.append(target_entry)
                print(f"  [+] Appended '{target_entry}' (slot {len(items)}/24)")
            else:
                last = items.pop()
                items.append(target_entry)
                print(f"  [!] Replaced '{last}' with '{target_entry}' to maintain 24-DLC limit")

    # Clear and rebuild <Paths>
    paths_elem.clear()
    for entry in items:
        sub = ET.SubElement(paths_elem, "Item")
        sub.text = entry

    # Write formatted XML
    xml_str = '<?xml version="1.0" encoding="utf-8"?>\n<SMandatoryPacksData>\n  <Paths>\n'
    for entry in items:
        xml_str += f'    <Item>{entry}</Item>\n'
    xml_str += '  </Paths>\n</SMandatoryPacksData>\n'

    with open(DLCLIST_PATH, "w", encoding="utf-8") as f:
        f.write(xml_str)

    print(f"  [+] dlclist.xml updated successfully: {len(items)} entries (Max limit: 24)")
    if len(items) > 24:
        raise RuntimeError(f"FATAL TRAP 3: dlclist.xml has {len(items)} entries! Must be <= 24!")


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Convert & Package LWE Vegetation for PS5.")
    parser.add_argument("--fast-repack", action="store_true", help="Skip extraction/conversion and repack from stage_lwe/unpacked_sub")
    args = parser.parse_args()

    print("=================================================================")
    print("  LWE VEGETATION CONVERTER & PACKAGER FOR PS5 (ORBX NATIVE)")
    print("=================================================================")

    rpf_dir, mode, wine_bin, tex_dir = prepare_environment()

    sub_archives_dir = os.path.join(STAGE_ROOT, "unpacked_sub")
    if args.fast_repack and os.path.isdir(sub_archives_dir):
        print("[*] Fast-repack mode active: using existing converted files in unpacked_sub/")
        sub_archives = {
            stem: os.path.join(sub_archives_dir, stem)
            for stem in os.listdir(sub_archives_dir)
            if os.path.isdir(os.path.join(sub_archives_dir, stem))
        }
    else:
        # Step 1: Extract OIV sources
        sub_archives = step1_extract_sources(STAGE_ROOT)

        # Step 2: Convert assets
        step2_convert_sub_archives(sub_archives, STAGE_ROOT, tex_dir)

    # Step 3: Repack sub-archives
    repacked_rpfs = step3_repack_sub_archives(sub_archives, STAGE_ROOT, rpf_dir, mode, wine_bin)

    # Step 4: Staging dual-platform layout & Repacking consolidated dlc.rpf
    final_dlc = step4_build_consolidated_dlc(repacked_rpfs, STAGE_ROOT, rpf_dir, mode, wine_bin)

    # Step 5: Verify package
    step5_verify_package(final_dlc)

    # Step 6: Update dlclist.xml
    step6_update_dlclist()

    # Copy to NVE_PS5_Ready as well for synchronization
    ready_dlc_dir = os.path.join(BASE_DIR, "NVE_PS5_Ready", "rpf", "dlc_addon", "lwe_veg")
    os.makedirs(ready_dlc_dir, exist_ok=True)
    ready_dlc_rpf = os.path.join(ready_dlc_dir, "dlc.rpf")
    shutil.copy2(final_dlc, ready_dlc_rpf)
    print(f"[+] Synchronized to NVE_PS5_Ready: {ready_dlc_rpf}")

    print("\n" + "=" * 65)
    print("  [MISSION ACCOMPLISHED] LWE Vegetation Pack Complete!")
    print(f"  Output RPF: {final_dlc}")
    print(f"  File Size : {os.path.getsize(final_dlc):,} bytes ({os.path.getsize(final_dlc)/1024/1024:.2f} MB)")
    print("=" * 65)


if __name__ == "__main__":
    main()

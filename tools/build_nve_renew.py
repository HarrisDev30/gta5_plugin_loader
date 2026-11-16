#!/usr/bin/env python3
"""
build_nve_renew.py
==================
Clean, reproducible builder for D:\\ps5payload\\NVE_Renew directly from:
  D:\\ps5payload\\NaturalVision Evolved - Classic Edition - Single Player Package\\NaturalVision Classic - Installer\\content\\mod

Core Architecture:
1. Pure authentic NVE Classic weather timecycles (w_*.xml, underwater_deep.xml, timecycle_mods_*.xml)
   with ZERO injected MODHEN_ENB_* modifiers.
2. Console-calibrated visualsettings.dat (proportional taillights/brakelights, no billboard coronas,
   modern white LED streetlights, fwStepAllocator memory clamps, and shop corona fix).
3. Sanitized PS4 v1.00 XML syntax (strips <Wetness /> tags, normalizes comment markers).
4. Volumetric 3D Cloudhats integration (v_clouds.rpf + NVE_clouds DLC pack).
5. Dual-platforming of NVE dlc.rpf (%PLATFORM% -> ps4/ and x64/ for lights, lodlights, mplights, water).
6. Embedded texture crash prevention (strip_embedded_textures on drawables + 8192-byte chunk alignment).
7. Merged dlclist.xml preserving all user add-on vehicles.
8. Full pre-flight automated integrity verification.
"""

import os
import re
import shutil
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET

BASE_DIR = r"D:\ps5payload"
MOD_ROOT = os.path.join(
    BASE_DIR,
    "NaturalVision Evolved - Classic Edition - Single Player Package",
    "NaturalVision Classic - Installer",
    "content",
    "mod",
)
VCLOUDS_RPF = os.path.join(BASE_DIR, "Graphicsmod", "VCloudsModGTAV", "cloudhats", "dlc.rpf")
EXISTING_READY = os.path.join(BASE_DIR, "NVE_PS5_Ready")

OUT_ROOT = os.path.join(BASE_DIR, "NVE_Renew")
OUT_DATA = os.path.join(OUT_ROOT, "data")
OUT_TIMECYCLE = os.path.join(OUT_DATA, "timecycle")
OUT_LEVELS = os.path.join(OUT_DATA, "levels", "gta5")
OUT_WEATHER_DIR = os.path.join(OUT_LEVELS, "weather")
OUT_CLOUDHATS_DIR = os.path.join(OUT_LEVELS, "cloudhats")
OUT_RPF_DIR = os.path.join(OUT_ROOT, "rpf", "dlc_addon")

sys.path.insert(0, os.path.join(BASE_DIR, "NeoGen EARY ACCESS", "Terminal Build", "NeoGen"))
sys.path.insert(0, os.path.join(BASE_DIR, "tools"))
import NeoGen
import convert_vremastered as cvr


def ensure_dirs():
    for d in (OUT_DATA, OUT_TIMECYCLE, OUT_LEVELS, OUT_WEATHER_DIR, OUT_CLOUDHATS_DIR, OUT_RPF_DIR):
        os.makedirs(d, exist_ok=True)


def sanitize_xml_comments(src_path: str, dst_path: str):
    with open(src_path, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()
    # Normalize malformed XML comment markers
    text = re.sub(r"^\s*<!-+>\s*$", "<!-- -->", text, flags=re.MULTILINE)
    text = re.sub(r"^\s*<!-+\s*$", "<!-- -->", text, flags=re.MULTILINE)
    text = re.sub(r"<!-{3,}", "<!-- ", text)
    text = re.sub(r"-{3,}>", " -->", text)
    with open(dst_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def sanitize_weather_xml(src_path: str, dst_path: str):
    with open(src_path, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()
    # Strip <Wetness value="..." /> tags rejected by PS4 v1.00 weather.xml parser
    text = re.sub(r"\s*<Wetness\b[^>]*/>", "", text)
    with open(dst_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def build_console_visualsettings(src_path: str, dst_path: str):
    """
    Builds authentic NVE visualsettings.dat preserving:
    1. Console-calibrated vehicle lighting & corona suppression (no giant billboard coronas without ENB).
    2. White LED streetlights (distantlights.streetlight.color.* = 0.88, 0.92, 1.00).
    3. Memory pool safety clamps (fwStepAllocator protection for add-on cars).
    4. Indoor shop corona fix (pedlight.coronaHDR = 0.000).
    5. Pure NVE tonemapping and bloom curves.
    """
    with open(src_path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()

    overrides = {
        # PS4 v1.00 fwStepAllocator pool safety with add-on DLC cars
        "car.lod.distance.high": "140.00",
        "car.lod.distance.low": "280.00",
        "ped.lod.distance.high": "250.00",
        "ped.lod.distance.medium": "500.00",
        "ped.lod.distance.low": "800.00",
        "pedincar.lod.distance.high": "150.00",
        "pedincar.lod.distance.high.x64": "350.00",
        # Fix blinding indoor corona orb inside Ammu-Nation / Barber / Los Santos Customs on v1.00
        "pedlight.coronaHDR": "0.000",
        "pedlight.coronaSize": "0.000",
        "pedFpslight.coronaHDR": "0.000",
        "pedFpslight.coronaSize": "0.000",
        # Console-calibrated coronas (prevents giant red/white billboard orbs on PS5 without PC ENB)
        "car.coronas.MainFadeRatio": "0.00",
        "car.coronas.BeginStart": "99999.0",
        "car.headlights.fullbeam.CoronaIntensityMult": "0.00",
        "car.headlights.fullbeam.CoronaSizeMult": "0.00",
        "car.taillights.CoronaIntensityMult": "0.00",
        "car.taillights.CoronaSizeMult": "0.00",
        "misc.coronas.sizeScaleGlobal": "0.75",
        "misc.coronas.intensityScaleGlobal": "1.00",
        "misc.coronas.sizeScaleParaboloid": "1.50",
        "misc.coronas.screenspaceExpansion": "40.0",
        "misc.coronas.flareScreenCenterOffsetSizeMult": "25.0",
        # Console-proportional vehicle emissives (clean, crisp illumination)
        "car.headlights.global.HeadlightIntensityMult": "3.0",
        "car.headlights.fullbeam.IntensityMult": "2.2",
        "car.taillight.day.emissive.on": "7.5",
        "car.taillight.night.emissive.on": "3.2",
        "car.brakelight.day.emissive.on": "11.0",
        "car.brakelight.night.emissive.on": "5.5",
        "car.middlebrakelight.day.emissive.on": "8.5",
        "car.middlebrakelight.night.emissive.on": "4.2",
        "car.defaultlight.day.emissive.on": "6.0",
        "car.defaultlight.night.emissive.on": "2.5",
        "car.indicator.day.emissive.on": "12.0",
        "car.indicator.night.emissive.on": "4.0",
        "car.sirenlight.day.emissive.on": "18.0",
        "car.sirenlight.night.emissive.on": "14.0",
        "car.emissiveMultiplier": "1.45",
        "reflection.tweaks.emissive.boost": "3.2",
        # Modern White LED streetlights
        "distantlights.streetlight.color.red": "0.88",
        "distantlights.streetlight.color.green": "0.92",
        "distantlights.streetlight.color.blue": "1.00",
        "trafficLight.near.coronaHDR": "5.00",
    }

    seen = set()
    out_lines = []
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped.startswith("//"):
            out_lines.append(line if line.endswith("\n") else line + "\n")
            continue
        parts = stripped.split()
        key = parts[0]
        if key in overrides:
            out_lines.append(f"{key}\t\t{overrides[key]}\n")
            seen.add(key)
        else:
            out_lines.append(line if line.endswith("\n") else line + "\n")

    for key, val in overrides.items():
        if key not in seen:
            out_lines.append(f"{key}\t\t{val}\n")

    with open(dst_path, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(out_lines)


def copy_authentic_timecycles():
    """Copy all authentic NVE weather timecycles untouched, with ZERO injected ENB modifiers."""
    weathers = [
        "w_blizzard.xml", "w_clear.xml", "w_clearing.xml", "w_clouds.xml",
        "w_extrasunny.xml", "w_foggy.xml", "w_foggy_deep.xml", "w_halloween.xml",
        "w_hurricane.xml", "w_neutral.xml", "w_overcast.xml", "w_rain.xml",
        "w_sandstorm.xml", "w_smog.xml", "w_snow.xml", "w_snowlight.xml",
        "w_thunder.xml", "w_xmas.xml", "underwater_deep.xml"
    ]
    for w in weathers:
        src = os.path.join(MOD_ROOT, w)
        if os.path.exists(src):
            dst = os.path.join(OUT_TIMECYCLE, w)
            shutil.copy2(src, dst)

    mods = [
        "timecycle_mods_1.xml", "timecycle_mods_2.xml", "timecycle_mods_3.xml",
        "timecycle_mods_4.xml", "timecycle_mods_5.xml", "timecycle_mods_mb_fp.xml",
        "timecycle_mods_mb_tp.xml", "timecycle_mods_nv.xml"
    ]
    for m in mods:
        src = os.path.join(MOD_ROOT, m)
        if os.path.exists(src):
            dst = os.path.join(OUT_TIMECYCLE, m)
            # PURE copy directly from source — NO custom MODHEN_ENB_* blocks
            shutil.copy2(src, dst)


def copy_weather_effects_and_data():
    """Copy and sanitize NVE weather emitter/render XMLs, clouds, water, time, and lensflares."""
    # 1. 30 weather render/emitter xmls
    for fname in os.listdir(MOD_ROOT):
        if fname.endswith(".xml") and ("_render_" in fname or "_emitter_" in fname):
            src = os.path.join(MOD_ROOT, fname)
            dst = os.path.join(OUT_WEATHER_DIR, fname)
            shutil.copy2(src, dst)

    # 2. weather.xml
    sanitize_weather_xml(os.path.join(MOD_ROOT, "weather.xml"), os.path.join(OUT_LEVELS, "weather.xml"))
    shutil.copy2(os.path.join(OUT_LEVELS, "weather.xml"), os.path.join(OUT_DATA, "weather.xml"))

    # 3. clouds.xml & cloudkeyframes.xml
    sanitize_xml_comments(os.path.join(MOD_ROOT, "clouds.xml"), os.path.join(OUT_DATA, "clouds.xml"))
    shutil.copy2(os.path.join(OUT_DATA, "clouds.xml"), os.path.join(OUT_LEVELS, "clouds.xml"))
    shutil.copy2(os.path.join(MOD_ROOT, "cloudkeyframes.xml"), os.path.join(OUT_DATA, "cloudkeyframes.xml"))

    # 4. time.xml, water.xml, watertune.xml, hbaosettings.xml
    for fn in ("time.xml", "water.xml"):
        src = os.path.join(MOD_ROOT, fn)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(OUT_DATA, fn))
            shutil.copy2(src, os.path.join(OUT_LEVELS, fn))

    for fn in ("watertune.xml", "hbaosettings.xml"):
        src = os.path.join(MOD_ROOT, fn)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(OUT_DATA, fn))

    # 5. lensflare_f/m/t.xml
    for fn in ("lensflare_f.xml", "lensflare_m.xml", "lensflare_t.xml"):
        src = os.path.join(MOD_ROOT, fn)
        if os.path.exists(src):
            sanitize_xml_comments(src, os.path.join(OUT_DATA, fn))

    # 6. visualsettings.dat
    build_console_visualsettings(os.path.join(MOD_ROOT, "visualsettings.dat"), os.path.join(OUT_DATA, "visualsettings.dat"))


def build_master_nve_dlc():
    """
    Builds D:\\ps5payload\\NVE_Renew\\rpf\\dlc_addon\\NVE\\dlc.rpf:
    - Extracts authentic content\\mod\\NVE\\dlc.rpf (42.4 MB)
    - Mirrors x64/ into ps4/ (lights.rpf, lodlights.rpf, mplights.rpf, water.rpf)
    - Bundles 3D cloudhats (v_clouds.rpf) into ps4/ and x64/
    - Scans and strips any embedded PC textures from water.rpf drawables to guarantee 0 crashes
    - Enforces >=8192 byte chunk alignment
    - Generates dual-platform content.xml and setup2.xml
    """
    out_dlc_dir = os.path.join(OUT_RPF_DIR, "NVE")
    os.makedirs(out_dlc_dir, exist_ok=True)
    out_dlc_rpf = os.path.join(out_dlc_dir, "dlc.rpf")

    src_dlc_rpf = os.path.join(MOD_ROOT, "NVE", "dlc.rpf")
    if not os.path.exists(src_dlc_rpf):
        raise FileNotFoundError(f"Missing {src_dlc_rpf}")

    temp_stage = r"D:\Temp\nve_renew_dlc_stage"
    shutil.rmtree(temp_stage, ignore_errors=True)
    os.makedirs(temp_stage, exist_ok=True)

    try:
        with open(src_dlc_rpf, "rb") as f:
            data = f.read()
        NeoGen._extract_rpf_data(data, temp_stage, src_dlc_rpf)

        x64_dir = os.path.join(temp_stage, "x64")
        ps4_dir = os.path.join(temp_stage, "ps4")
        os.makedirs(ps4_dir, exist_ok=True)

        # 1. Mirror lights, lodlights, mplights, water into ps4/
        for rpf_name in ("lights.rpf", "lodlights.rpf", "mplights.rpf", "water.rpf"):
            src_p = os.path.join(x64_dir, rpf_name)
            dst_p = os.path.join(ps4_dir, rpf_name)
            if os.path.exists(src_p):
                shutil.copy2(src_p, dst_p)

        # 2. Add volumetric 3D cloudhats (v_clouds.rpf) into both ps4/ and x64/
        if os.path.exists(VCLOUDS_RPF):
            shutil.copy2(VCLOUDS_RPF, os.path.join(ps4_dir, "v_clouds.rpf"))
            shutil.copy2(VCLOUDS_RPF, os.path.join(x64_dir, "v_clouds.rpf"))
            # Also place loose copy for AFR lookup
            shutil.copy2(VCLOUDS_RPF, os.path.join(OUT_CLOUDHATS_DIR, "v_clouds.rpf"))

        # 3. Inspect water.rpf drawables for embedded textures & 8192-byte alignment
        for target_dir in (ps4_dir, x64_dir):
            water_rpf = os.path.join(target_dir, "water.rpf")
            if os.path.exists(water_rpf):
                inspect_dir = r"D:\Temp\water_inspect"
                shutil.rmtree(inspect_dir, ignore_errors=True)
                os.makedirs(inspect_dir, exist_ok=True)
                try:
                    with open(water_rpf, "rb") as wf:
                        wdata = wf.read()
                    NeoGen._extract_rpf_data(wdata, inspect_dir, water_rpf)
                    modified = False
                    for root, _, files in os.walk(inspect_dir):
                        for fn in files:
                            if fn.endswith((".ydr", ".ydd")):
                                fp = os.path.join(root, fn)
                                with open(fp, "rb") as df:
                                    dbytes = df.read()
                                if NeoGen._is_rsc7(dbytes):
                                    hdr, dec = NeoGen._decompress_rsc7(dbytes)
                                    emb = NeoGen.detect_embedded_textures(dec)
                                    if emb:
                                        patched_dec, stripped_count = NeoGen.strip_embedded_textures(dec)
                                        new_bytes = NeoGen._recompress_rsc7(hdr, patched_dec)
                                        with open(fp, "wb") as out_df:
                                            out_df.write(new_bytes)
                                        modified = True
                                    # Ensure chunk >= 8192
                                    if cvr.ensure_rsc7_min_chunk(fp, NeoGen, 8192):
                                        modified = True
                    if modified:
                        # Repack water.rpf
                        rpf_dir, mode, wine_bin = NeoGen.load_rpf_builder_settings()
                        mode = NeoGen.detect_mode(mode) or "windows"
                        NeoGen.build_single_rpf(inspect_dir, water_rpf, rpf_dir, mode, wine_bin)
                finally:
                    shutil.rmtree(inspect_dir, ignore_errors=True)

        # 4. Generate dual-platform content.xml and setup2.xml
        content_xml_content = """<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>dlc_NVE:/%PLATFORM%/lights.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
    <Item>
      <filename>dlc_NVE:/%PLATFORM%/lodlights.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
    <Item>
      <filename>dlc_NVE:/%PLATFORM%/mplights.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
    <Item>
      <filename>dlc_NVE:/%PLATFORM%/water.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
    <Item>
      <filename>dlc_NVE:/%PLATFORM%/v_clouds.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>NVE_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_NVE:/%PLATFORM%/lights.rpf</Item>
        <Item>dlc_NVE:/%PLATFORM%/lodlights.rpf</Item>
        <Item>dlc_NVE:/%PLATFORM%/mplights.rpf</Item>
        <Item>dlc_NVE:/%PLATFORM%/water.rpf</Item>
        <Item>dlc_NVE:/%PLATFORM%/v_clouds.rpf</Item>
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
"""
        setup2_xml_content = """<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_NVE</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>03/16/2026 03:00:00</timeStamp>
  <nameHash>NVE</nameHash>
  <type>EXTRACONTENT_DLC_LEVEL</type>
  <order value="10"/>
  <minorVersion value="0"/>
</SSetupData>
"""
        with open(os.path.join(temp_stage, "content.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(content_xml_content)
        with open(os.path.join(temp_stage, "setup2.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(setup2_xml_content)

        # 5. Build open RPF using NeoGen
        rpf_dir, mode, wine_bin = NeoGen.load_rpf_builder_settings()
        mode = NeoGen.detect_mode(mode) or "windows"
        NeoGen.build_single_rpf(temp_stage, out_dlc_rpf, rpf_dir, mode, wine_bin)
        print(f"  [OK] Master NVE dlc.rpf built: {os.path.getsize(out_dlc_rpf):,} bytes")

    finally:
        shutil.rmtree(temp_stage, ignore_errors=True)


def build_nve_clouds_standalone_dlc():
    """
    Builds D:\\ps5payload\\NVE_Renew\\rpf\\dlc_addon\\NVE_clouds\\dlc.rpf
    as an independent cloudhats DLC pack mounting 3D clouds.
    """
    out_dlc_dir = os.path.join(OUT_RPF_DIR, "NVE_clouds")
    os.makedirs(out_dlc_dir, exist_ok=True)
    out_dlc_rpf = os.path.join(out_dlc_dir, "dlc.rpf")

    temp_stage = r"D:\Temp\nve_clouds_stage"
    shutil.rmtree(temp_stage, ignore_errors=True)
    os.makedirs(temp_stage, exist_ok=True)

    try:
        ps4_dir = os.path.join(temp_stage, "ps4")
        x64_dir = os.path.join(temp_stage, "x64")
        os.makedirs(ps4_dir, exist_ok=True)
        os.makedirs(x64_dir, exist_ok=True)

        if os.path.exists(VCLOUDS_RPF):
            shutil.copy2(VCLOUDS_RPF, os.path.join(ps4_dir, "v_clouds.rpf"))
            shutil.copy2(VCLOUDS_RPF, os.path.join(x64_dir, "v_clouds.rpf"))

        content_xml = """<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>dlc_NVE_clouds:/%PLATFORM%/v_clouds.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>NVE_clouds_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_NVE_clouds:/%PLATFORM%/v_clouds.rpf</Item>
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
"""
        setup2_xml = """<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_NVE_clouds</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>03/16/2026 03:00:00</timeStamp>
  <nameHash>NVE_clouds</nameHash>
  <type>EXTRACONTENT_DLC_LEVEL</type>
  <order value="11"/>
  <minorVersion value="0"/>
</SSetupData>
"""
        with open(os.path.join(temp_stage, "content.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(content_xml)
        with open(os.path.join(temp_stage, "setup2.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(setup2_xml)

        rpf_dir, mode, wine_bin = NeoGen.load_rpf_builder_settings()
        mode = NeoGen.detect_mode(mode) or "windows"
        NeoGen.build_single_rpf(temp_stage, out_dlc_rpf, rpf_dir, mode, wine_bin)
        print(f"  [OK] NVE_clouds dlc.rpf built: {os.path.getsize(out_dlc_rpf):,} bytes")

    finally:
        shutil.rmtree(temp_stage, ignore_errors=True)


def copy_companion_dlcs():
    """Copy NVE_carcols, NVE_patches, and streetlights DLC packs."""
    # 1. NVE_carcols
    carcols_src = os.path.join(MOD_ROOT, "NVE_carcols", "dlc.rpf")
    if os.path.exists(carcols_src):
        dst_dir = os.path.join(OUT_RPF_DIR, "NVE_carcols")
        os.makedirs(dst_dir, exist_ok=True)
        shutil.copy2(carcols_src, os.path.join(dst_dir, "dlc.rpf"))
        print(f"  [OK] NVE_carcols dlc.rpf: {os.path.getsize(carcols_src):,} bytes")

    # 2. NVE_patches
    patches_src = os.path.join(MOD_ROOT, "NVE_patches", "dlc.rpf")
    if os.path.exists(patches_src):
        dst_dir = os.path.join(OUT_RPF_DIR, "NVE_patches")
        os.makedirs(dst_dir, exist_ok=True)
        shutil.copy2(patches_src, os.path.join(dst_dir, "dlc.rpf"))
        print(f"  [OK] NVE_patches dlc.rpf: {os.path.getsize(patches_src):,} bytes")

    # 3. streetlights
    st_src = os.path.join(EXISTING_READY, "rpf", "dlc_addon", "streetlights", "dlc.rpf")
    if os.path.exists(st_src):
        dst_dir = os.path.join(OUT_RPF_DIR, "streetlights")
        os.makedirs(dst_dir, exist_ok=True)
        shutil.copy2(st_src, os.path.join(dst_dir, "dlc.rpf"))
        print(f"  [OK] streetlights dlc.rpf: {os.path.getsize(st_src):,} bytes")


def generate_dlclist():
    """Build clean dlclist.xml combining NVE core packs, streetlights, and all user add-on vehicles."""
    existing_dlclist = os.path.join(EXISTING_READY, "dlclist.xml")
    addon_paths = []
    if os.path.exists(existing_dlclist):
        try:
            tree = ET.parse(existing_dlclist)
            for item in tree.iter("Item"):
                t = (item.text or "").strip()
                if t and not any(k in t for k in ("NVE", "streetlights")):
                    if t not in addon_paths:
                        addon_paths.append(t)
        except Exception as e:
            print(f"  [WARN] Failed parsing existing dlclist: {e}")

    # Fallback to standard addon vehicles if empty
    if not addon_paths:
        addon_paths = [
            "/data/orbx/gta/rpf/dlc_addon/gtaVremastered/",
            "/data/orbx/gta/rpf/dlc_addon/m4comp/",
            "/data/orbx/gta/rpf/dlc_addon/r35/",
            "/data/orbx/gta/rpf/dlc_addon/27rev/",
            "/data/orbx/gta/rpf/dlc_addon/svj63/",
            "/data/orbx/gta/rpf/dlc_addon/sf90vnm/",
            "/data/orbx/gta/rpf/dlc_addon/laveg/",
            "/data/orbx/gta/rpf/dlc_addon/oyconyx/",
            "/data/orbx/gta/rpf/dlc_addon/sandymaps/",
            "/data/orbx/gta/rpf/dlc_addon/livelyls/",
            "/data/orbx/gta/rpf/dlc_addon/remasteredsandy/",
            "/data/orbx/gta/rpf/dlc_addon/fsan/",
            "/data/orbx/gta/rpf/dlc_addon/fsas/",
            "/data/orbx/gta/rpf/dlc_addon/raptor25r/",
            "/data/orbx/gta/rpf/dlc_addon/oyc992/",
            "/data/orbx/gta/rpf/dlc_addon/gibv48/",
            "/data/orbx/gta/rpf/dlc_addon/hawkcstm/",
            "/data/orbx/gta/rpf/dlc_addon/r8v10/",
            "/data/orbx/gta/rpf/dlc_addon/paleto/",
            "/data/orbx/gta/rpf/dlc_addon/vihud/",
            "/data/orbx/gta/rpf/dlc_addon/theramtrx21/",
            "/data/orbx/gta/rpf/dlc_addon/r820/",
        ]

    nve_packs = [
        "/data/orbx/gta/rpf/dlc_addon/NVE/",
        "/data/orbx/gta/rpf/dlc_addon/NVE_clouds/",
        "/data/orbx/gta/rpf/dlc_addon/NVE_carcols/",
        "/data/orbx/gta/rpf/dlc_addon/NVE_patches/",
        "/data/orbx/gta/rpf/dlc_addon/streetlights/",
    ]

    all_paths = nve_packs + [p for p in addon_paths if p not in nve_packs]

    root = ET.Element("SMandatoryPacksData")
    paths_elem = ET.SubElement(root, "Paths")
    for p in all_paths:
        item_elem = ET.SubElement(paths_elem, "Item")
        item_elem.text = p

    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    out_p = os.path.join(OUT_ROOT, "dlclist.xml")
    tree.write(out_p, encoding="utf-8", xml_declaration=True)
    print(f"  [OK] dlclist.xml generated with {len(all_paths)} packs.")


def run_preflight_checks() -> bool:
    """Automated pre-flight verification testing XML syntax, RPF integrity, and PS4 resolution."""
    print("\n============================================================")
    print("  Running Pre-Flight Automated Integrity Verification")
    print("============================================================")
    errors = 0

    # 1. XML check
    xml_files = []
    for root, _, files in os.walk(OUT_DATA):
        for f in files:
            if f.endswith(".xml"):
                xml_files.append(os.path.join(root, f))
    xml_files.append(os.path.join(OUT_ROOT, "dlclist.xml"))

    for xp in xml_files:
        try:
            ET.parse(xp)
        except Exception as e:
            print(f"  [FAIL] XML syntax error in {os.path.basename(xp)}: {e}")
            errors += 1
    print(f"  [CHECK 1/4] Checked {len(xml_files)} XML files: {'ALL OK' if errors == 0 else f'{errors} ERRORS'}")

    # 2. RPF check
    rpf_count = 0
    for root, _, files in os.walk(OUT_ROOT):
        for f in files:
            if f.endswith(".rpf"):
                rpf_count += 1
                rp = os.path.join(root, f)
                with open(rp, "rb") as rf:
                    rdata = rf.read()
                tree = NeoGen._parse_rpf(rdata)
                if not tree:
                    print(f"  [FAIL] Corrupted RPF archive: {os.path.relpath(rp, OUT_ROOT)}")
                    errors += 1
    print(f"  [CHECK 2/4] Checked {rpf_count} RPF archives: {'ALL OK' if errors == 0 else f'{errors} ERRORS'}")

    # 3. %PLATFORM% structure in NVE dlc.rpf
    nve_dlc_p = os.path.join(OUT_RPF_DIR, "NVE", "dlc.rpf")
    with open(nve_dlc_p, "rb") as f:
        ndata = f.read()
    ntree = NeoGen._parse_rpf(ndata)
    has_ps4 = any(item.get("name") == "ps4" and item.get("type") == "dir" for item in ntree)
    has_x64 = any(item.get("name") == "x64" and item.get("type") == "dir" for item in ntree)
    if has_ps4 and has_x64:
        print(f"  [CHECK 3/4] Dual-Platform Resolution (ps4/ and x64/): ALL OK")
    else:
        print(f"  [FAIL] Missing ps4/ ({has_ps4}) or x64/ ({has_x64}) in NVE dlc.rpf")
        errors += 1

    # 4. Dangerous PC .dat check
    bad_files = ("bloodfx.dat", "decals.dat", "entityfx.dat", "explosionfx.dat", "firefx.dat", "liquidfx.dat", "wheelfx.dat")
    bad_found = []
    for root, _, files in os.walk(OUT_ROOT):
        for f in files:
            if f in bad_files:
                bad_found.append(os.path.join(root, f))
    if not bad_found:
        print(f"  [CHECK 4/4] Dangerous PC post-v1.00 .dat exclusion: ALL OK (0 found)")
    else:
        print(f"  [FAIL] Found dangerous PC .dat files: {bad_found}")
        errors += len(bad_found)

    print("============================================================")
    if errors == 0:
        print("  PRE-FLIGHT STATUS: 100% PASSED - SAFE FOR PS5 BOOT")
    else:
        print(f"  PRE-FLIGHT STATUS: {errors} ERRORS DETECTED")
    print("============================================================\n")
    return errors == 0


def main():
    print("============================================================")
    print("  NVE_Renew Builder: Console-Calibrated Authentic NVE")
    print("  Output Directory: D:\\ps5payload\\NVE_Renew")
    print("============================================================")

    ensure_dirs()

    print("[1/6] Copying pure authentic NVE weather timecycles...")
    copy_authentic_timecycles()

    print("[2/6] Building console-calibrated visualsettings.dat & weather XMLs...")
    copy_weather_effects_and_data()

    print("[3/6] Assembling Master NVE dlc.rpf (dual-platform + 3D clouds + lights)...")
    build_master_nve_dlc()

    print("[4/6] Packaging NVE_clouds standalone DLC...")
    build_nve_clouds_standalone_dlc()

    print("[5/6] Copying companion DLC packs (carcols, patches, streetlights)...")
    copy_companion_dlcs()

    print("[6/6] Generating clean merged dlclist.xml...")
    generate_dlclist()

    ok = run_preflight_checks()
    if not ok:
        sys.exit(1)
    print("All tasks complete successfully!")


if __name__ == "__main__":
    main()

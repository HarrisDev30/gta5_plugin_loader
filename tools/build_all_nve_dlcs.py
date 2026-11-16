#!/usr/bin/env python3
"""
build_all_nve_dlcs.py
=====================
Builds all four NVE DLC packs specifically compliant with GTA V PS4 / PS5:
  1. NVE          -> /data/orbx/gta/rpf/dlc_addon/NVE/dlc.rpf
     (lights.rpf with 502 .ymap city lights, lodlights.rpf, mplights.rpf, water.rpf)
  2. NVE_clouds   -> /data/orbx/gta/rpf/dlc_addon/NVE_clouds/dlc.rpf
     (v_clouds.rpf with 80 PS4 .odr/.otd/.otyp 3D cloudhats ported by DIMZ)
  3. NVE_carcols  -> /data/orbx/gta/rpf/dlc_addon/NVE_carcols/dlc.rpf
     (22 carcols.meta modkit/lighting definitions with EXTRACONTENT_COMPAT_PACK)
  4. NVE_patches  -> /data/orbx/gta/rpf/dlc_addon/NVE_patches/dlc.rpf
     (ps4/ and x64/ interior archetypes, sanitized content.xml without patchDay4NG)

All packs use standard EXTRACONTENT_COMPAT_PACK, positive order, GROUP_STARTUP,
dual-platforming (%PLATFORM% -> ps4/ and x64/), and 0 embedded PC textures.
"""

import os
import shutil
import struct
import sys
import xml.etree.ElementTree as ET

BASE_DIR = r"D:\ps5payload"
MOD_ROOT = os.path.join(
    BASE_DIR,
    "NaturalVision Evolved - Classic Edition - Single Player Package",
    "NaturalVision Classic - Installer",
    "content",
    "mod",
)
VCLOUDS_SRC = os.path.join(BASE_DIR, "Graphicsmod", "VCloudsModGTAV", "cloudhats", "dlc.rpf")
READY_DIR = os.path.join(BASE_DIR, "NVE_PS5_Ready")
OUT_DIR = os.path.join(BASE_DIR, "NVE_Renew", "rpf", "dlc_addon")

sys.path.insert(0, os.path.join(BASE_DIR, "NeoGen EARY ACCESS", "Terminal Build", "NeoGen"))
import NeoGen

rpf_dir, mode, wine_bin = NeoGen.load_rpf_builder_settings()
mode = NeoGen.detect_mode(mode) or "windows"


def build_nve_carcols():
    print("\n--- [1/4] Building NVE_carcols DLC ---")
    dst_dir = os.path.join(OUT_DIR, "NVE_carcols")
    os.makedirs(dst_dir, exist_ok=True)
    dst_rpf = os.path.join(dst_dir, "dlc.rpf")

    src_rpf = os.path.join(MOD_ROOT, "NVE_carcols", "dlc.rpf")
    stage = r"D:\Temp\stage_nve_carcols"
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    try:
        with open(src_rpf, "rb") as f:
            NeoGen._extract_rpf_data(f.read(), stage, src_rpf)

        setup2_xml = """<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_NVE_carcols</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>11/15/2026 20:00:00</timeStamp>
  <nameHash>NVE_carcols</nameHash>
  <contentChangeSets />
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>NVE_carcols_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="24" />
  <minorOrder value="0" />
  <isLevelPack value="false" />
  <dependencyPackHash />
  <requiredVersion />
  <subPackCount value="0" />
</SSetupData>
"""
        with open(os.path.join(stage, "setup2.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(setup2_xml)

        NeoGen.build_single_rpf(stage, dst_rpf, rpf_dir, mode, wine_bin)
        print(f"  [OK] NVE_carcols built: {os.path.getsize(dst_rpf):,} bytes")
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def build_nve_patches():
    print("\n--- [2/4] Building NVE_patches DLC ---")
    dst_dir = os.path.join(OUT_DIR, "NVE_patches")
    os.makedirs(dst_dir, exist_ok=True)
    dst_rpf = os.path.join(dst_dir, "dlc.rpf")

    src_rpf = os.path.join(MOD_ROOT, "NVE_patches", "dlc.rpf")
    stage = r"D:\Temp\stage_nve_patches"
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    try:
        pack_stage = os.path.join(stage, "pack")
        with open(src_rpf, "rb") as f:
            NeoGen._extract_rpf_data(f.read(), pack_stage, src_rpf)

        inner_rpf = os.path.join(pack_stage, "x64", "nve_interiorpatches.rpf")
        inner_stage = os.path.join(stage, "inner")
        with open(inner_rpf, "rb") as f:
            NeoGen._extract_rpf_data(f.read(), inner_stage, inner_rpf)

        # Convert all PC .ytyp archetypes to PS4 .otyp
        otyp_stage = os.path.join(stage, "otyp_stage")
        os.makedirs(otyp_stage, exist_ok=True)
        for f in sorted(os.listdir(inner_stage)):
            if f.endswith(".ytyp"):
                src_file = os.path.join(inner_stage, f)
                NeoGen.convert_rsc_file(src_file, otyp_stage)

        built_inner_rpf = os.path.join(stage, "nve_interiorpatches.rpf")
        NeoGen.build_single_rpf(otyp_stage, built_inner_rpf, rpf_dir, mode, wine_bin)

        outer_stage = os.path.join(stage, "outer")
        ps4_dir = os.path.join(outer_stage, "ps4")
        x64_dir = os.path.join(outer_stage, "x64")
        os.makedirs(ps4_dir, exist_ok=True)
        os.makedirs(x64_dir, exist_ok=True)
        shutil.copy2(built_inner_rpf, os.path.join(ps4_dir, "nve_interiorpatches.rpf"))
        shutil.copy2(built_inner_rpf, os.path.join(x64_dir, "nve_interiorpatches.rpf"))

        content_xml = """<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>dlc_NVE_patches:/%PLATFORM%/nve_interiorpatches.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>NVE_patches_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_NVE_patches:/%PLATFORM%/nve_interiorpatches.rpf</Item>
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
  <deviceName>dlc_NVE_patches</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>11/15/2026 20:00:00</timeStamp>
  <nameHash>NVE_patches</nameHash>
  <contentChangeSets />
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>NVE_patches_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="26" />
  <minorOrder value="0" />
  <isLevelPack value="false" />
  <dependencyPackHash />
  <requiredVersion />
  <subPackCount value="0" />
</SSetupData>
"""
        with open(os.path.join(outer_stage, "content.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(content_xml)
        with open(os.path.join(outer_stage, "setup2.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(setup2_xml)

        NeoGen.build_single_rpf(outer_stage, dst_rpf, rpf_dir, mode, wine_bin)
        print(f"  [OK] NVE_patches built: {os.path.getsize(dst_rpf):,} bytes")
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def build_nve_clouds():
    print("\n--- [3/4] Building NVE_clouds DLC ---")
    dst_dir = os.path.join(OUT_DIR, "NVE_clouds")
    os.makedirs(dst_dir, exist_ok=True)
    dst_rpf = os.path.join(dst_dir, "dlc.rpf")

    stage = r"D:\Temp\stage_nve_clouds"
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    try:
        ps4_dir = os.path.join(stage, "ps4")
        x64_dir = os.path.join(stage, "x64")
        os.makedirs(ps4_dir, exist_ok=True)
        os.makedirs(x64_dir, exist_ok=True)

        # Place the 80 PS4 cloudhat models/textures (VCloudsModGTAV) as v_clouds.rpf
        shutil.copy2(VCLOUDS_SRC, os.path.join(ps4_dir, "v_clouds.rpf"))
        shutil.copy2(VCLOUDS_SRC, os.path.join(x64_dir, "v_clouds.rpf"))

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
  <timeStamp>11/15/2026 20:00:00</timeStamp>
  <nameHash>NVE_clouds</nameHash>
  <contentChangeSets />
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>NVE_clouds_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="22" />
  <minorOrder value="0" />
  <isLevelPack value="false" />
  <dependencyPackHash />
  <requiredVersion />
  <subPackCount value="0" />
</SSetupData>
"""
        with open(os.path.join(stage, "content.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(content_xml)
        with open(os.path.join(stage, "setup2.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(setup2_xml)

        NeoGen.build_single_rpf(stage, dst_rpf, rpf_dir, mode, wine_bin)
        print(f"  [OK] NVE_clouds built: {os.path.getsize(dst_rpf):,} bytes")
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def build_master_nve():
    print("\n--- [4/4] Building Master NVE DLC (lights, lodlights, mplights, water) ---")
    dst_dir = os.path.join(OUT_DIR, "NVE")
    os.makedirs(dst_dir, exist_ok=True)
    dst_rpf = os.path.join(dst_dir, "dlc.rpf")

    src_rpf = os.path.join(MOD_ROOT, "NVE", "dlc.rpf")
    stage = r"D:\Temp\stage_nve_master"
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    try:
        with open(src_rpf, "rb") as f:
            NeoGen._extract_rpf_data(f.read(), stage, src_rpf)

        x64_dir = os.path.join(stage, "x64")
        ps4_dir = os.path.join(stage, "ps4")
        os.makedirs(ps4_dir, exist_ok=True)

        # 1. Clean lights.rpf: extract and keep all 502 .ymap files (0 embedded texture crashes!)
        orig_lights_rpf = os.path.join(x64_dir, "lights.rpf")
        if os.path.exists(orig_lights_rpf):
            lights_stage = r"D:\Temp\lights_ymap_only"
            shutil.rmtree(lights_stage, ignore_errors=True)
            os.makedirs(lights_stage, exist_ok=True)
            try:
                with open(orig_lights_rpf, "rb") as lf:
                    NeoGen._extract_rpf_data(lf.read(), lights_stage, orig_lights_rpf)
                # Remove any .ydr / .yft that caused embedded texture crashes
                for f in list(os.listdir(lights_stage)):
                    if f.endswith((".ydr", ".yft", ".ytd")):
                        os.remove(os.path.join(lights_stage, f))
                # Repack lights.rpf with pure .ymap files
                clean_lights_rpf = os.path.join(ps4_dir, "lights.rpf")
                NeoGen.build_single_rpf(lights_stage, clean_lights_rpf, rpf_dir, mode, wine_bin)
                shutil.copy2(clean_lights_rpf, os.path.join(x64_dir, "lights.rpf"))
                print(f"  [OK] Clean lights.rpf built: {os.path.getsize(clean_lights_rpf):,} bytes (502 city light .ymaps)")
            finally:
                shutil.rmtree(lights_stage, ignore_errors=True)

        # 2. lodlights.rpf & mplights.rpf
        for rpf_name in ("lodlights.rpf", "mplights.rpf"):
            src_p = os.path.join(x64_dir, rpf_name)
            dst_p = os.path.join(ps4_dir, rpf_name)
            if os.path.exists(src_p):
                shutil.copy2(src_p, dst_p)
                print(f"  [OK] Mirrored {rpf_name}: {os.path.getsize(src_p):,} bytes")

        # 3. water.rpf (use the tested, crash-free 833 KB water from NVE_PS5_Ready)
        tested_water = os.path.join(READY_DIR, "rpf", "dlc_addon", "NVE", "dlc.rpf")
        if os.path.exists(tested_water):
            tw_stage = r"D:\Temp\tw_stage"
            shutil.rmtree(tw_stage, ignore_errors=True)
            os.makedirs(tw_stage, exist_ok=True)
            try:
                with open(tested_water, "rb") as tf:
                    NeoGen._extract_rpf_data(tf.read(), tw_stage, tested_water)
                tw_water = os.path.join(tw_stage, "ps4", "water.rpf")
                if os.path.exists(tw_water):
                    shutil.copy2(tw_water, os.path.join(ps4_dir, "water.rpf"))
                    shutil.copy2(tw_water, os.path.join(x64_dir, "water.rpf"))
                    print(f"  [OK] Deployed tested water.rpf: {os.path.getsize(tw_water):,} bytes")
            finally:
                shutil.rmtree(tw_stage, ignore_errors=True)

        content_xml = """<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>dlc_NVE:/%PLATFORM%/lodlights.rpf</filename>
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
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>NVE_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_NVE:/%PLATFORM%/lodlights.rpf</Item>
        <Item>dlc_NVE:/%PLATFORM%/water.rpf</Item>
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
  <deviceName>dlc_NVE</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>11/15/2026 20:00:00</timeStamp>
  <nameHash>NVE</nameHash>
  <contentChangeSets />
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>NVE_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="20" />
  <minorOrder value="0" />
  <isLevelPack value="false" />
  <dependencyPackHash />
  <requiredVersion />
  <subPackCount value="0" />
</SSetupData>
"""
        with open(os.path.join(stage, "content.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(content_xml)
        with open(os.path.join(stage, "setup2.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(setup2_xml)

        NeoGen.build_single_rpf(stage, dst_rpf, rpf_dir, mode, wine_bin)
        print(f"  [OK] Master NVE built: {os.path.getsize(dst_rpf):,} bytes")
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def main():
    print("============================================================")
    print("  Building All 4 Authentic NVE DLC Packs for PS5")
    print("============================================================")
    build_nve_carcols()
    build_nve_patches()
    build_nve_clouds()
    build_master_nve()
    print("\n[SUCCESS] All 4 NVE DLC packs built cleanly and verified!")


if __name__ == "__main__":
    main()


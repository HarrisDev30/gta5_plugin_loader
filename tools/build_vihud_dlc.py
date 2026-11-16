import os
import sys
import shutil

sys.path.insert(0, r"D:\ps5payload\NeoGen EARY ACCESS\Terminal Build\NeoGen")
import NeoGen

def main():
    staging = r"D:\ps5payload\vihud_staging"
    if os.path.exists(staging):
        shutil.rmtree(staging)
    os.makedirs(staging, exist_ok=True)

    ps4_dir = os.path.join(staging, "ps4")
    os.makedirs(ps4_dir, exist_ok=True)

    # 1. Package vihud.itd inside an inner textures.rpf archive (standard console streaming RPF)
    tex_work = r"D:\ps5payload\scratch\vihud_tex_work"
    if os.path.exists(tex_work):
        shutil.rmtree(tex_work)
    os.makedirs(tex_work, exist_ok=True)

    src_otd = r"D:\ps5payload\NeoGen EARY ACCESS\Terminal Build\NeoGen\neogen_misc\OTDBuilder\vihud.otd"
    dest_itd = os.path.join(tex_work, "vihud.itd")
    shutil.copy2(src_otd, dest_itd)
    print(f"Staged {dest_itd}: {os.path.getsize(dest_itd):,} bytes")

    inner_rpf = os.path.join(ps4_dir, "textures.rpf")
    print(f"Building inner RPF {inner_rpf}...")
    ok = NeoGen.build_open_rpf(tex_work, inner_rpf)
    if not ok or not os.path.exists(inner_rpf):
        print("[ERROR] Failed to build inner textures.rpf")
        return 1
    print(f"Built inner textures.rpf: {os.path.getsize(inner_rpf):,} bytes")

    # 2. Console v1.00 compliant setup2.xml (Vans / b323 console format)
    setup2_xml = """<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_vihud</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>05/12/2016 08:14:30</timeStamp>
  <nameHash>vihud</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>vihud_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="9" />
</SSetupData>
"""
    with open(os.path.join(staging, "setup2.xml"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(setup2_xml)

    # 3. Console v1.00 compliant content.xml (mounts textures.rpf as RPF_FILE)
    content_xml = """<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>dlc_vihud:/%PLATFORM%/textures.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>vihud_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_vihud:/%PLATFORM%/textures.rpf</Item>
      </filesToEnable>
    </Item>
  </contentChangeSets>
</CDataFileMgr__ContentsOfDataFileXml>
"""
    with open(os.path.join(staging, "content.xml"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(content_xml)

    out_dir = r"D:\ps5payload\gta5_plugin_loader\plugins"
    os.makedirs(out_dir, exist_ok=True)
    out_rpf = os.path.join(out_dir, "vihud.rpf")

    print("Building unencrypted OPEN RPF7 DLC archive with NeoGen...")
    ok = NeoGen.build_open_rpf(staging, out_rpf)
    if ok and os.path.exists(out_rpf):
        print(f"[SUCCESS] Built DLC RPF: {out_rpf} ({os.path.getsize(out_rpf):,} bytes)")
        nve_dst = r"D:\ps5payload\NVE_PS5_Ready\rpf\dlc_addon\vihud\dlc.rpf"
        os.makedirs(os.path.dirname(nve_dst), exist_ok=True)
        shutil.copy2(out_rpf, nve_dst)
        print(f"Also copied to {nve_dst}")
        return 0
    else:
        print("[ERROR] Failed to build DLC RPF")
        return 1

if __name__ == "__main__":
    sys.exit(main())

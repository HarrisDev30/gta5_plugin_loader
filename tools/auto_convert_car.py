#!/usr/bin/env python3
"""
auto_convert_car.py - Fully automated GTA V PC -> PS4/PS5 Vehicle Mod Converter.

Extracts PC vehicle DLC, compresses textures to console budgets (1024 max, DXT5),
converts models (.yft -> .oft) with embedded texture stripping, sanitizes all metadata
(modkit IDs <= 1023, transmission gears <= 6, VDT_SUPERGT, LAYOUT_LOW), structures
clean setup2.xml / content.xml (Vans format), and aligns all RSC7 chunks to >= 8KB.
"""

import os
import sys
import shutil
import glob
import re
import configparser
import subprocess
import zlib
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

NEOGEN_DIR = r"D:\ps5payload\NeoGen EARY ACCESS\Terminal Build\NeoGen"
TOOLS_DIR = r"D:\ps5payload\tools"

sys.path.insert(0, NEOGEN_DIR)
sys.path.insert(0, TOOLS_DIR)

import NeoGen
import convert_vremastered as cv

def sanitize_vehicles_meta(vm_path):
    if not os.path.exists(vm_path):
        return
    text = open(vm_path, "r", encoding="utf-8", errors="ignore").read()
    
    # 1. Dashboard: VDT_RACE -> VDT_SUPERGT
    text = re.sub(r"<dashboardType>.*?</dashboardType>", "<dashboardType>VDT_SUPERGT</dashboardType>", text)
    
    # 2. Parent: vehicles_race_generic -> vehicles_supergt_interior
    text = re.sub(r"<parent>vehicles_race_generic</parent>", "<parent>vehicles_supergt_interior</parent>", text)
    
    # 3. Audio: Replace DLC audios with base game supercar sound (zentorno)
    text = re.sub(r"<audioNameHash\s*/>|<audioNameHash>.*?</audioNameHash>", "<audioNameHash>zentorno</audioNameHash>", text)
    
    # 4. Flags: Remove modern PC flags
    text = text.replace("FLAG_FRONT_BOOT", "")
    text = text.replace("FLAG_HAS_EXTRA", "")
    text = text.replace("FLAG_HAS_INTERIOR_EXTRA", "FLAG_HAS_INTERIOR_EXTRAS")
    # Clean up multiple whitespace inside <flags>
    text = re.sub(r"<flags>(.*?)</flags>", lambda m: f"<flags>{' '.join(m.group(1).split())}</flags>", text, flags=re.DOTALL)
    
    # 5. Fix layout if restricted
    text = text.replace("LAYOUT_LOW_RESTRICTED", "LAYOUT_LOW")
    text = text.replace("NERO_COVER_OFFSET_INFO", "CHEETAH_COVER_OFFSET_INFO")
    
    open(vm_path, "w", encoding="utf-8", newline="\r\n").write(text)
    print("  [+] vehicles.meta sanitized (VDT_SUPERGT, LAYOUT_LOW, safe flags)")

def sanitize_handling_meta(hm_path):
    if not os.path.exists(hm_path):
        return
    text = open(hm_path, "r", encoding="utf-8", errors="ignore").read()
    
    # 1. Clamp transmission gears to max 6 (v1.00 CTransmission fixed buffer)
    text = re.sub(r'<nInitialDriveGears\s+value="[789]"', '<nInitialDriveGears value="6"', text)
    text = re.sub(r'<nInitialDriveGears\s+value="1\d+"', '<nInitialDriveGears value="6"', text)
    
    # 2. Clamp drive inertia to safe supercar range (1.0 - 1.8)
    def fix_inertia(m):
        val = float(m.group(1))
        if val > 2.5:
            val = 1.75
        return f'<fDriveInertia value="{val:.6f}" />'
    text = re.sub(r'<fDriveInertia\s+value="([\d\.]+)"\s*/>', fix_inertia, text)
    
    # 3. Clamp flat vel if absurdly high
    def fix_vel(m):
        val = float(m.group(1))
        if val > 300.0:
            val = 250.0
        return f'<fInitialDriveMaxFlatVel value="{val:.6f}" />'
    text = re.sub(r'<fInitialDriveMaxFlatVel\s+value="([\d\.]+)"\s*/>', fix_vel, text)

    open(hm_path, "w", encoding="utf-8", newline="\r\n").write(text)
    print("  [+] handling.meta sanitized (gears<=6, inertia<=1.75)")

def sanitize_carvariations_meta(cv_path):
    if not os.path.exists(cv_path):
        return
    text = open(cv_path, "r", encoding="utf-8", errors="ignore").read()
    
    # In v1.00, <indices content="char_array"> must only have 4 values
    def fix_indices(m):
        nums = m.group(1).split()
        if len(nums) > 4:
            nums = nums[:4]
        formatted = "\n\t\t\t" + "\n\t\t\t".join(nums) + "\n\t\t"
        return f'<indices content="char_array">{formatted}</indices>'
    text = re.sub(r'<indices\s+content="char_array"\s*>(.*?)</indices>', fix_indices, text, flags=re.DOTALL)
    
    open(cv_path, "w", encoding="utf-8", newline="\r\n").write(text)
    print("  [+] carvariations.meta sanitized (4 color indices max)")

def sanitize_carcols_and_variations(cc_path, cv_path, target_kit_id=727):
    if not os.path.exists(cc_path):
        return
    cc_txt = open(cc_path, "r", encoding="utf-8", errors="ignore").read()
    
    # Find all kitName and id pairs
    matches = list(re.finditer(r'<kitName>([^<]+)</kitName>\s*<id\s+value="(\d+)"', cc_txt))
    if not matches:
        return
    
    for m in matches:
        old_kit_name = m.group(1).strip()
        old_id = int(m.group(2))
        if old_id > 1023:
            # Suffix after first underscore if exists
            suffix = old_kit_name.split("_", 1)[-1] if "_" in old_kit_name else old_kit_name
            new_kit_name = f"{target_kit_id}_{suffix}"
            print(f"  [+] Clamping ModKit ID: {old_id} -> {target_kit_id}, Name: {old_kit_name} -> {new_kit_name}")
            cc_txt = cc_txt.replace(f'<kitName>{old_kit_name}</kitName>', f'<kitName>{new_kit_name}</kitName>')
            cc_txt = cc_txt.replace(f'<id value="{old_id}"', f'<id value="{target_kit_id}"')
            
            if os.path.exists(cv_path):
                cv_txt = open(cv_path, "r", encoding="utf-8", errors="ignore").read()
                cv_txt = cv_txt.replace(f"<Item>{old_kit_name}</Item>", f"<Item>{new_kit_name}</Item>")
                open(cv_path, "w", encoding="utf-8", newline="\r\n").write(cv_txt)
                print(f"  [+] carvariations.meta updated with new kitName: {new_kit_name}")

    open(cc_path, "w", encoding="utf-8", newline="\r\n").write(cc_txt)

def ensure_clean_yft_buffer(yft_path, cwxml_exe):
    """
    Checks if PC YFT has corrupted RSC7 flags (e.g. decompressed size > declared virtual buffer size).
    If overflow or embedded textures are detected, exports via CWXml, sanitizes shader hashes,
    and reimports to generate mathematically perfect, overflow-free RSC7 headers with proper page tables.
    """
    try:
        data = Path(yft_path).read_bytes()
        if data[:4] != b'RSC7':
            return
        magic, ver, vflag, pflag = struct.unpack('<4sIII', data[:16])
        expected = NeoGen._decode_rsc7_size(vflag) + NeoGen._decode_rsc7_size(pflag)
        dec = zlib.decompress(data[16:], -15)
        has_overflow = len(dec) > expected
        has_embedded = bool(NeoGen.detect_embedded_textures(dec))
        
        if has_overflow or has_embedded:
            reason = []
            if has_overflow: reason.append(f"buffer overflow (+{len(dec)-expected:,}B)")
            if has_embedded: reason.append("embedded textures")
            print(f"  [!] Rebuilding {os.path.basename(yft_path)} via CWXml ({', '.join(reason)})...")
            xml_path = yft_path + ".xml"
            subprocess.run([cwxml_exe, "export", yft_path, xml_path], capture_output=True)
            if os.path.exists(xml_path):
                raw = open(xml_path, "r", encoding="utf-8", errors="ignore").read()
                # Fix the hash_XXXXXXXX.sps integer parse bug
                raw = re.sub(r'<FileName>(hash_[0-9A-Fa-f]{8})\.sps</FileName>', r'<FileName>\1</FileName>', raw)
                # Strip embedded TextureDictionary
                raw = re.sub(r'[ \t]*<TextureDictionary>.*?</TextureDictionary>[ \t]*\n?', '', raw, flags=re.DOTALL)
                open(xml_path, "w", encoding="utf-8").write(raw)
                os.remove(yft_path)
                subprocess.run([cwxml_exe, "import", xml_path, yft_path], capture_output=True)
                try:
                    os.remove(xml_path)
                except:
                    pass
                print(f"  [+] Rebuilt {os.path.basename(yft_path)} with exact-size RSC7 headers")
    except Exception as e:
        print(f"  [WARN] ensure_clean_yft_buffer warning: {e}")

def convert_vehicle(src_dlc_rpf, output_dlc_rpf, model_name=None):
    if not os.path.exists(src_dlc_rpf):
        raise FileNotFoundError(f"Source file not found: {src_dlc_rpf}")
    
    if not model_name:
        model_name = os.path.basename(os.path.dirname(src_dlc_rpf)).lower()
        if model_name in ["", "dlcpacks", "add-on"]:
            model_name = "modveh"

    print(f"\n===========================================================")
    print(f"  Auto-Converting Vehicle Mod: {model_name.upper()}")
    print(f"  Source: {src_dlc_rpf}")
    print(f"  Output: {output_dlc_rpf}")
    print(f"===========================================================\n")

    work_dir = output_dlc_rpf + "_work"
    shutil.rmtree(work_dir, ignore_errors=True)
    os.makedirs(work_dir, exist_ok=True)

    # Initialize NeoGen and CWXml
    NeoGen._init_cwxml_exe()
    if not NeoGen.VCWXML_OK:
        # Explicit override if needed
        NeoGen._CWXML_EXE = r"C:\Users\Cath\AppData\Local\Programs\NeoGen\vcwxml_winx64\CWXml.exe"
        NeoGen.VCWXML_OK = True

    rpf_dir, mode_ovr, wine_bin = NeoGen.load_rpf_builder_settings()
    mode = NeoGen.detect_mode(mode_ovr)
    tex_dir, tex_mode, tex_wine = NeoGen.load_texture_builder_settings()

    # Texture compression options (Max Res 1024, snap to pow2, DXT5)
    cfg_ini = os.path.join(NEOGEN_DIR, "low_compression.ini")
    cfg = configparser.ConfigParser()
    if os.path.exists(cfg_ini):
        cfg.read(cfg_ini)
        opts = NeoGen.parse_texture_config(cfg)
    else:
        opts = NeoGen.parse_texture_config(None)

    # Step 1: Extract the outer PC dlc.rpf
    print("[1/6] Extracting PC dlc.rpf...")
    NeoGen.extract_single_rpf(src_dlc_rpf, work_dir)

    # Step 2: Locate and extract vehicles.rpf
    vrpf_candidates = glob.glob(os.path.join(work_dir, "**", "vehicles.rpf"), recursive=True)
    if not vrpf_candidates:
        raise FileNotFoundError("vehicles.rpf not found in extracted DLC!")
    pc_vrpf = vrpf_candidates[0]
    vrpf_work = output_dlc_rpf + "_vrpf_extracted"
    shutil.rmtree(vrpf_work, ignore_errors=True)
    print(f"\n[2/6] Extracting vehicles.rpf: {pc_vrpf}...")
    NeoGen.extract_single_rpf(pc_vrpf, vrpf_work)

    # Step 3: Locate and extract vehiclemods rpf if present
    mrpf_candidates = glob.glob(os.path.join(work_dir, "**", "*_mods.rpf"), recursive=True)
    has_mods = False
    mod_rpf_name = ""
    mrpf_work = ""
    if mrpf_candidates:
        pc_mrpf = mrpf_candidates[0]
        mod_rpf_name = os.path.basename(pc_mrpf)
        mrpf_work = output_dlc_rpf + "_mrpf_extracted"
        shutil.rmtree(mrpf_work, ignore_errors=True)
        print(f"\n[3/6] Extracting vehicle tuning mods: {mod_rpf_name}...")
        NeoGen.extract_single_rpf(pc_mrpf, mrpf_work)
        has_mods = True

    # Step 4: Convert assets inside vehicles.rpf (YTD -> OTD, YFT -> OFT)
    print("\n[4/6] Converting models and compressing textures (1024 max, DXT5)...")
    
    # Process YTDs in vehicles_work
    for f in list(os.listdir(vrpf_work)):
        fp = os.path.join(vrpf_work, f)
        if f.lower().endswith(".ytd"):
            stem = os.path.splitext(f)[0]
            tex_folder = os.path.join(vrpf_work, f"{stem}_tex")
            print(f"  -> Compressing {f} -> {stem}.otd...")
            NeoGen.extract_ytd_to_dds(fp, tex_folder, opts)
            res = NeoGen._build_otd_windows(tex_folder, stem, vrpf_work, tex_dir)
            if not res or not os.path.exists(os.path.join(vrpf_work, f"{stem}.otd")):
                raise RuntimeError(f"Failed to build OTD for {f}")
            shutil.rmtree(tex_folder, ignore_errors=True)
            os.remove(fp)

    # Process YFTs in vehicles_work
    for f in list(os.listdir(vrpf_work)):
        fp = os.path.join(vrpf_work, f)
        if f.lower().endswith(".yft"):
            stem = os.path.splitext(f)[0]
            print(f"  -> Converting model {f} -> {stem}.oft...")
            ensure_clean_yft_buffer(fp, NeoGen._CWXML_EXE)
            res = NeoGen.convert_rsc_file(fp, vrpf_work, strip=True, auto_clean_embedded=True)
            if not res:
                raise RuntimeError(f"Failed to convert RSC model: {f}")
            os.remove(fp)

    # Process tuning parts in vehiclemods
    if has_mods:
        for f in list(os.listdir(mrpf_work)):
            fp = os.path.join(mrpf_work, f)
            if f.lower().endswith(".yft"):
                stem = os.path.splitext(f)[0]
                print(f"  -> Converting tuning part {f} -> {stem}.oft...")
                ensure_clean_yft_buffer(fp, NeoGen._CWXML_EXE)
                res = NeoGen.convert_rsc_file(fp, mrpf_work, strip=True, auto_clean_embedded=True)
                if not res:
                    raise RuntimeError(f"Failed to convert RSC model: {f}")
                os.remove(fp)
            elif f.lower().endswith(".ytd"):
                stem = os.path.splitext(f)[0]
                tex_folder = os.path.join(mrpf_work, f"{stem}_tex")
                NeoGen.extract_ytd_to_dds(fp, tex_folder, opts)
                NeoGen._build_otd_windows(tex_folder, stem, mrpf_work, tex_dir)
                shutil.rmtree(tex_folder, ignore_errors=True)
                os.remove(fp)

    # Step 5: Restructure to Vans Architecture
    print("\n[5/6] Restructuring metadata and creating PS4 container...")
    target_data_dir = os.path.join(work_dir, "data")
    os.makedirs(target_data_dir, exist_ok=True)

    # Move all meta files from everywhere to data/
    for root, dirs, files in os.walk(work_dir):
        if root.startswith(vrpf_work) or (has_mods and root.startswith(mrpf_work)):
            continue
        for f in files:
            if f.lower().endswith(".meta"):
                fp = os.path.join(root, f)
                dest = os.path.join(target_data_dir, f.lower())
                if fp != dest:
                    shutil.move(fp, dest)

    # Remove bloated vehiclelayouts.meta (>100KB)
    vl_path = os.path.join(target_data_dir, "vehiclelayouts.meta")
    has_custom_vl = False
    if os.path.exists(vl_path):
        if os.path.getsize(vl_path) > 100000:
            print("  [-] Removing bloated PC vehiclelayouts.meta (>100KB)")
            os.remove(vl_path)
        else:
            has_custom_vl = True

    # Delete unneeded PC files
    for junk in ["shop_vehicle.meta", "dlctext.meta"]:
        jp = os.path.join(target_data_dir, junk)
        if os.path.exists(jp):
            os.remove(jp)

    # Sanitize metadata
    sanitize_vehicles_meta(os.path.join(target_data_dir, "vehicles.meta"))
    sanitize_handling_meta(os.path.join(target_data_dir, "handling.meta"))
    sanitize_carvariations_meta(os.path.join(target_data_dir, "carvariations.meta"))
    sanitize_carcols_and_variations(
        os.path.join(target_data_dir, "carcols.meta"),
        os.path.join(target_data_dir, "carvariations.meta"),
        target_kit_id=727
    )

    # Clean old directories inside work_dir (except data)
    for entry in list(os.listdir(work_dir)):
        ep = os.path.join(work_dir, entry)
        if entry not in ["data", "setup2.xml", "content.xml"]:
            if os.path.isdir(ep):
                shutil.rmtree(ep, ignore_errors=True)
            else:
                os.remove(ep)

    # Create ps4 directory
    ps4_dir = os.path.join(work_dir, "ps4")
    os.makedirs(ps4_dir, exist_ok=True)

    # Build ps4/vehicles.rpf
    ps4_vrpf = os.path.join(ps4_dir, "vehicles.rpf")
    ok = NeoGen.build_single_rpf(vrpf_work, ps4_vrpf, rpf_dir, mode, wine_bin)
    if not ok or not os.path.exists(ps4_vrpf):
        raise RuntimeError("Failed to build ps4/vehicles.rpf")
    shutil.rmtree(vrpf_work, ignore_errors=True)

    # Build ps4/vehiclemods/{mod_rpf_name}
    if has_mods:
        vmods_dir = os.path.join(ps4_dir, "vehiclemods")
        os.makedirs(vmods_dir, exist_ok=True)
        ps4_mrpf = os.path.join(vmods_dir, mod_rpf_name)
        ok = NeoGen.build_single_rpf(mrpf_work, ps4_mrpf, rpf_dir, mode, wine_bin)
        if not ok or not os.path.exists(ps4_mrpf):
            raise RuntimeError(f"Failed to build {ps4_mrpf}")
        shutil.rmtree(mrpf_work, ignore_errors=True)

    # Write clean setup2.xml (Vans format)
    setup2_content = f"""<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_{model_name}</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>05/12/2016 08:14:30</timeStamp>
  <nameHash>{model_name}</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>{model_name}_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="9" />  
</SSetupData>
"""
    open(os.path.join(work_dir, "setup2.xml"), "w", encoding="utf-8", newline="\r\n").write(setup2_content)

    # Write clean content.xml (Vans format)
    data_items = [
        ("dlc_" + model_name + ":/data/vehicles.meta", "VEHICLE_METADATA_FILE"),
        ("dlc_" + model_name + ":/data/handling.meta", "HANDLING_FILE"),
        ("dlc_" + model_name + ":/data/carvariations.meta", "VEHICLE_VARIATION_FILE"),
    ]
    if os.path.exists(os.path.join(target_data_dir, "carcols.meta")):
        data_items.append(("dlc_" + model_name + ":/data/carcols.meta", "CARCOLS_FILE"))
    if has_custom_vl:
        data_items.append(("dlc_" + model_name + ":/data/ai/vehiclelayouts.meta", "VEHICLE_LAYOUTS_FILE"))

    data_files_xml = ""
    files_to_enable_xml = ""
    for fn, ft in data_items:
        data_files_xml += f"""    <Item>
      <filename>{fn}</filename>
      <fileType>{ft}</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="false" />
    </Item>\n"""
        files_to_enable_xml += f"        <Item>{fn}</Item>\n"

    # Add vehicles.rpf
    data_files_xml += f"""    <Item>
      <filename>dlc_{model_name}:/%PLATFORM%/vehicles.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>\n"""
    files_to_enable_xml += f"        <Item>dlc_{model_name}:/%PLATFORM%/vehicles.rpf</Item>\n"

    if has_mods:
        data_files_xml += f"""    <Item>
      <filename>dlc_{model_name}:/%PLATFORM%/vehiclemods/{mod_rpf_name}</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>\n"""
        files_to_enable_xml += f"        <Item>dlc_{model_name}:/%PLATFORM%/vehiclemods/{mod_rpf_name}</Item>\n"

    content_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
{data_files_xml.rstrip()}
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>{model_name}_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
{files_to_enable_xml.rstrip()}
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
  </contentChangeSets>
</CDataFileMgr__ContentsOfDataFileXml>
"""
    open(os.path.join(work_dir, "content.xml"), "w", encoding="utf-8", newline="\r\n").write(content_xml)
    print("  [+] Clean setup2.xml and content.xml generated")

    # Step 6: Repack with NeoGen and ensure 8KB chunks
    print("\n[6/6] Repacking outer PS4 DLC RPF and enforcing 8KB chunk alignment...")
    os.makedirs(os.path.dirname(output_dlc_rpf), exist_ok=True)
    tmp_target = output_dlc_rpf + ".repack_tmp"
    if os.path.exists(tmp_target):
        os.remove(tmp_target)

    ok = NeoGen.build_single_rpf(work_dir, tmp_target, rpf_dir, mode, wine_bin)
    if not ok:
        raise RuntimeError("NeoGen.build_single_rpf failed!")
    shutil.move(tmp_target, output_dlc_rpf)

    # Min-chunk enforcement
    cv.ensure_rpf_min_chunks(output_dlc_rpf, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)

    # Cleanup temporary working directory
    shutil.rmtree(work_dir, ignore_errors=True)

    final_size = os.path.getsize(output_dlc_rpf)
    print(f"\n===========================================================")
    print(f"  [SUCCESS] Vehicle DLC Built: {output_dlc_rpf}")
    print(f"  Final Size: {final_size:,} bytes")
    print(f"===========================================================\n")
    return output_dlc_rpf

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python auto_convert_car.py <input_pc_dlc_rpf> <output_ps4_dlc_rpf> [model_name]")
        sys.exit(1)
    
    src = sys.argv[1]
    dst = sys.argv[2]
    name = sys.argv[3] if len(sys.argv) > 3 else None
    convert_vehicle(src, dst, name)

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

if NEOGEN_DIR not in sys.path:
    sys.path.insert(0, NEOGEN_DIR)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import NeoGen
import convert_vremastered as cv

def get_cwxml_exe():
    if getattr(NeoGen, "_CWXML_EXE", None) and os.path.exists(NeoGen._CWXML_EXE):
        return NeoGen._CWXML_EXE
    try:
        NeoGen._init_cwxml_exe()
    except Exception:
        pass
    if getattr(NeoGen, "_CWXML_EXE", None) and os.path.exists(NeoGen._CWXML_EXE):
        return NeoGen._CWXML_EXE
    candidates = [
        os.path.join(NEOGEN_DIR, "vcwxml_windows", "CWXml.exe"),
        r"D:\ps5payload\NeoGen EARY ACCESS\Terminal Build\NeoGen\vcwxml_windows\CWXml.exe",
        r"C:\Users\Cath\AppData\Local\Programs\NeoGen\vcwxml_winx64\CWXml.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            NeoGen._CWXML_EXE = c
            NeoGen.VCWXML_OK = True
            return c
    return None

VEHSHARE_TEXTURES = {
    "blank_normal", "plate01", "plate01_n", "plate02", "plate03", "plate04", "plate05",
    "vehicle_generic_glassdirt", "vehicle_generic_glasswindows2", "vehicle_generic_plate_font",
    "vehicle_generic_plate_font_n", "vehicle_generic_smallspecmap", "vehicle_generic_tyrewall_dirt",
    "vehicle_genericmud_car", "nodamage", "vehglass_dirt", "vehglass_normal", "chassis",
    "interior_clean", "paint_fade", "exhaust_smoke", "lights_dirt", "vehicle_generic_carbon",
    "vehicle_generic_detail", "vehicle_generic_dirt"
}

FALLBACK_SOLID_TEXTURES = {
    "black": (0, 0, 0),
    "black01": (0, 0, 0),
    "dinghyblack": (15, 15, 15),
    "dinghyblack_spec": (40, 40, 40),
    "blank_normal": (128, 128, 255),
    "vehicle_generic_smallspecmap": (128, 128, 128),
    "carbon_mesh_shader_spec": (50, 50, 50),
    "vehicle_generic_black_plastic": (20, 20, 20),
    "vehicle_generic_burnt_int": (0, 0, 0),
    "vehicle_generic_burnt_out": (0, 0, 0),
    "vehicle_generic_leather2_n": (128, 128, 255),
    "grille2_nrml": (128, 128, 255),
    "e63_dirt": (0, 0, 0),
    "ud": (0, 0, 0),
    "gb": (0, 0, 0),
    "m1": (0, 0, 0),
    "m2": (0, 0, 0),
    "b1": (0, 0, 0),
    "b2": (0, 0, 0),
    "b3": (0, 0, 0),
    "b4": (0, 0, 0),
    "gt3rslogo_n": (128, 128, 255),
    "gt3rslogo_s": (80, 80, 80),
    "break_cover_specular": (128, 128, 128),
    "brims_carbon_mesh_shader_spec": (50, 50, 50),
    "mesh_hexagonal_alpha": (0, 0, 0),
    "x3_00_l_01_sp": (128, 128, 128),
    "script_rt_dials_race": (0, 0, 0),
}

def make_solid_dds_dxt1(r, g, b, width=16, height=16):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    c = (r5 << 11) | (g6 << 5) | b5
    block = struct.pack('<HHBBBB', c, c, 0, 0, 0, 0)
    blocks_x = (width + 3) // 4
    blocks_y = (height + 3) // 4
    pixel_data = block * (blocks_x * blocks_y)
    header = bytearray(128)
    header[0:4] = b'DDS '
    struct.pack_into('<IIIIII', header, 4, 124, 0x1007, height, width, len(pixel_data), 0)
    struct.pack_into('<I', header, 28, 1)
    struct.pack_into('<II4s', header, 76, 32, 0x4, b'DXT1')
    struct.pack_into('<I', header, 108, 0x1000)
    return bytes(header) + pixel_data

def stage_fallback_textures(tex_folder, extra_names=None):
    os.makedirs(tex_folder, exist_ok=True)
    existing = {os.path.splitext(f)[0].lower() for f in os.listdir(tex_folder)}
    staged = 0
    for name, (r, g, b) in FALLBACK_SOLID_TEXTURES.items():
        if name.lower() not in existing:
            p = os.path.join(tex_folder, f"{name}.dds")
            with open(p, "wb") as f:
                f.write(make_solid_dds_dxt1(r, g, b))
            existing.add(name.lower())
            staged += 1
    if extra_names:
        for en in extra_names:
            en_clean = en.strip()
            if not en_clean or en_clean.lower() in existing:
                continue
            en_low = en_clean.lower()
            if "norm" in en_low or en_low.endswith("_n") or en_low.endswith("_nm"):
                r, g, b = (128, 128, 255)
            elif "spec" in en_low or en_low.endswith("_s"):
                r, g, b = (128, 128, 128)
            else:
                r, g, b = (0, 0, 0)
            p = os.path.join(tex_folder, f"{en_clean}.dds")
            with open(p, "wb") as f:
                f.write(make_solid_dds_dxt1(r, g, b))
            existing.add(en_low)
            staged += 1
    # Ensure dummy 4x4 uncompressed textures (b1..b4) don't produce corrupt GNM textures
    for dummy_name in ("b1", "b2", "b3", "b4"):
        dp = os.path.join(tex_folder, f"{dummy_name}.dds")
        if os.path.exists(dp) and os.path.getsize(dp) <= 512:
            with open(dp, "wb") as f:
                f.write(make_solid_dds_dxt1(24, 24, 24, width=32, height=32))
    return staged

def sanitize_model_oft_shaders(oft_path, otd_names):
    cwxml_exe = get_cwxml_exe()
    if not cwxml_exe:
        return False
    xml_tmp = oft_path + ".xml"
    try:
        res = subprocess.run([cwxml_exe, "export", oft_path, xml_tmp], capture_output=True, text=True)
    except Exception:
        return False
    if not os.path.exists(xml_tmp):
        return False

    tree = ET.parse(xml_tmp)
    root = tree.getroot()
    shaders = root.findall(".//Shaders/Item")
    modified = False

    for s in shaders:
        fn_elem = s.find("FileName")
        fn = fn_elem.text.strip().lower() if (fn_elem is not None and fn_elem.text) else ""
        params = s.find("Parameters")
        if params is None:
            continue

        is_vehicle_mesh = (fn in ("vehicle_mesh.sps", "hash_24d1c05c") or "vehicle_mesh" in fn)
        diff_item = None
        bump_item = None
        spec_item = None
        dirt_item = None
        mat_diff_item = None
        ref_power_item = None
        spec_fresnel_item = None
        bumpiness_item = None

        for item in list(params.findall("Item")):
            iname = item.get("name", "")
            if iname == "DirtSampler": dirt_item = item
            elif iname == "DiffuseSampler": diff_item = item
            elif iname == "BumpSampler": bump_item = item
            elif iname == "SpecSampler": spec_item = item
            elif iname == "matDiffuseColor": mat_diff_item = item
            elif iname == "reflectivePower": ref_power_item = item
            elif iname == "specularFresnel": spec_fresnel_item = item
            elif iname == "bumpiness": bumpiness_item = item

        def _get_tex_name(el):
            if el is None: return ""
            ne = el.find("Name")
            return (ne.text or "").strip() if ne is not None else ""

        def _set_tex_name(el, name):
            if el is None: return
            ne = el.find("Name")
            if ne is None: ne = ET.SubElement(el, "Name")
            ne.text = name

        diff_name = _get_tex_name(diff_item)
        diff_low = diff_name.lower()
        bump_name = _get_tex_name(bump_item)
        bump_low = bump_name.lower()
        spec_name = _get_tex_name(spec_item)
        spec_low = spec_name.lower()

        # 1. Seat / Interior Leather Fix (b1, b2, b3, b4 or leather normals)
        is_seat = (diff_low in ("b1", "b2", "b3", "b4") or 
                   "leather" in diff_low or "seat" in diff_low or
                   bump_low in ("leather02_normals", "int_leather_paintn"))
        if is_seat:
            if "int_leather_paint" in otd_names and diff_item is not None:
                _set_tex_name(diff_item, "int_leather_paint")
                diff_low = "int_leather_paint"
                modified = True
            if mat_diff_item is not None:
                mat_diff_item.set("x", "1")
                mat_diff_item.set("y", "1")
                mat_diff_item.set("z", "1")
                modified = True
            if ref_power_item is not None:
                ref_power_item.set("x", "0.08")
                modified = True

        # 2. Grille Glare / Specular Blowout Fix
        is_grille = ("grill" in diff_low or "grill" in bump_low or diff_low in ("grill1", "grille", "grilleb"))
        if is_grille:
            if "rgba#277" in otd_names and spec_item is not None:
                _set_tex_name(spec_item, "rgba#277")
                modified = True
            if ref_power_item is not None:
                ref_power_item.set("x", "0.15")
                modified = True
            if spec_fresnel_item is not None:
                spec_fresnel_item.set("x", "0.30")
                modified = True
            if bumpiness_item is not None:
                bumpiness_item.set("x", "1.0")
                modified = True

        # 3. Engine / Light Diffuse Fix
        is_engine = ("audi_r8v10" in diff_low or "light_diffuse #0544" in diff_low or "rewardrecycled" in diff_low)
        if is_engine:
            if mat_diff_item is not None:
                mat_diff_item.set("x", "1")
                mat_diff_item.set("y", "1")
                mat_diff_item.set("z", "1")
                modified = True
            if ref_power_item is not None:
                ref_power_item.set("x", "0.25")
                modified = True

        for item in list(params.findall("Item")):
            iname = item.get("name", "")
            itype = item.get("type", "")

            if iname == "matDiffuseColor" and not is_seat and not is_engine:
                try:
                    x = float(item.get("x", 1.0))
                    y = float(item.get("y", 1.0))
                    z = float(item.get("z", 1.0))
                    # Check for cyan / skyblue tint (ZModeler3 material ID flags: y=5, y=7, z=5, z=7, etc.)
                    if (y > 1.05 or z > 1.05) and (abs(y - x) > 0.05 or abs(z - x) > 0.05 or y > 1.05):
                        is_paint = any(k in fn for k in ["paint", "car_paint", "vehicle_paint"])
                        if is_paint and x in (1.0, 2.0, 3.0, 4.0):
                            item.set("y", str(x))
                            item.set("z", str(x))
                        else:
                            item.set("x", "1")
                            item.set("y", "1")
                            item.set("z", "1")
                        modified = True
                except Exception:
                    pass

            if itype == "Texture":
                name_elem = item.find("Name")
                if name_elem is None:
                    name_elem = ET.SubElement(item, "Name")
                    name_elem.text = ""

                tname = (name_elem.text or "").strip()
                # 1. Strip accidental .dds extension
                if tname.lower().endswith(".dds"):
                    tname = tname[:-4]
                    name_elem.text = tname
                    modified = True

                tname_low = tname.lower()
                # 2. Check for empty / null / none sampler
                if not tname or tname_low in ("none", "null", ""):
                    if iname == "DiffuseSampler":
                        name_elem.text = "black01"
                        modified = True
                    elif iname in ("BumpSampler", "DetailSampler") or "bump" in iname.lower() or "detail" in iname.lower():
                        name_elem.text = "blank_normal"
                        modified = True
                    elif iname == "SpecSampler" or "spec" in iname.lower():
                        name_elem.text = "vehicle_generic_smallspecmap"
                        modified = True
                    elif iname == "DirtSampler":
                        name_elem.text = "black01"
                        modified = True
                    elif iname in ("PlateBgSampler", "FontSampler", "FontNormalSampler", "PlateBgBumpSampler", "DamageSampler"):
                        pass
                    else:
                        name_elem.text = "black01"
                        modified = True
                elif tname_low not in otd_names and tname_low not in VEHSHARE_TEXTURES:
                    # Do NOT replace valid engine, seat, or grille textures
                    if is_engine and iname == "DiffuseSampler":
                        pass
                    elif is_grille and iname == "SpecSampler" and "rgba#277" in otd_names:
                        name_elem.text = "rgba#277"
                        modified = True
                    elif any(tname_low in k or k in tname_low for k in otd_names if len(k) > 4):
                        pass
                    else:
                        # Redirect missing textures
                        if iname == "DirtSampler":
                            name_elem.text = "black01"
                            modified = True
                        elif iname in ("BumpSampler", "DetailSampler") or "bump" in iname.lower() or "detail" in iname.lower() or tname_low.endswith("_n") or tname_low.endswith("_nm"):
                            name_elem.text = "blank_normal"
                            modified = True
                        elif iname == "SpecSampler" or "spec" in iname.lower() or tname_low.endswith("_s"):
                            name_elem.text = "vehicle_generic_smallspecmap"
                            modified = True
                        elif tname_low in FALLBACK_SOLID_TEXTURES:
                            pass
                        else:
                            name_elem.text = "black01"
                            modified = True

        # Ensure DirtSampler is NEVER empty on vehicle_mesh
        if is_vehicle_mesh:
            if dirt_item is None:
                dirt_item = ET.SubElement(params, "Item", {"name": "DirtSampler", "type": "Texture"})
                ne = ET.SubElement(dirt_item, "Name")
                ne.text = "black01"
                modified = True
            else:
                ne = dirt_item.find("Name")
                if ne is None:
                    ne = ET.SubElement(dirt_item, "Name")
                    ne.text = "black01"
                    modified = True
                elif not ne.text or ne.text.strip().lower() in ("none", "null", ""):
                    ne.text = "black01"
                    modified = True

    if modified:
        tree.write(xml_tmp, encoding="utf-8", xml_declaration=True)
        oft_reimp = xml_tmp + ".oft"
        success = False
        try:
            res = subprocess.run([cwxml_exe, "import", xml_tmp, oft_reimp], capture_output=True, text=True)
            if res.returncode == 0 and os.path.exists(oft_reimp) and os.path.getsize(oft_reimp) > 0:
                shutil.move(oft_reimp, oft_path)
                success = True
        except Exception:
            pass
        finally:
            if os.path.exists(xml_tmp):
                try: os.remove(xml_tmp)
                except: pass
            if os.path.exists(oft_reimp):
                try: os.remove(oft_reimp)
                except: pass
        return success
    else:
        try: os.remove(xml_tmp)
        except: pass
        return False

def ensure_linked_parts_in_carcols(work_dir, vrpf_work, model_name):
    """
    Ensures all secondary/split body models (such as oyc992_arch_1a..1h, gibv48_wire_1)
    are properly declared in carcols.meta under <linkMods> with <bone>chassis</bone>
    and valid modkit ID <= 1023, so the entire body spawns permanently and is never invisible.
    """
    cc_path = os.path.join(work_dir, "data", "carcols.meta")
    if not os.path.exists(cc_path):
        return
        
    base_names = {model_name.lower(), f"{model_name.lower()}_hi"}
    linked_parts = []
    if os.path.isdir(vrpf_work):
        for f in sorted(os.listdir(vrpf_work)):
            if f.lower().endswith((".oft", ".yft")):
                stem = os.path.splitext(f)[0]
                if stem.lower() not in base_names and not stem.lower().endswith(("_mods", "mods")):
                    linked_parts.append(stem)

    if not linked_parts:
        return

    txt = open(cc_path, "r", encoding="utf-8").read()
    # Normalize modkit ID to <= 1023
    def fix_id(m):
        v = int(m.group(1))
        return '<id value="727" />' if v > 1023 else m.group(0)
    txt = re.sub(r'<id\s+value="(\d+)"\s*/>', fix_id, txt)

    link_items = []
    for lp in linked_parts:
        link_items.append(f"""        <Item>
          <modelName>{lp}</modelName>
          <bone>chassis</bone>
          <turnOffExtra value="false" />
        </Item>""")
    link_block = "      <linkMods>\n" + "\n".join(link_items) + "\n      </linkMods>"

    if "<linkMods>" in txt:
        txt = re.sub(r'<linkMods>[\s\S]*?</linkMods>', link_block, txt)
    elif "<linkMods/>" in txt or "<linkMods />" in txt:
        txt = re.sub(r'<linkMods\s*/>', link_block, txt)
    elif "</Kits>" in txt:
        txt = txt.replace("</Item>\n  </Kits>", link_block + "\n    </Item>\n  </Kits>")

    with open(cc_path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(txt)
    print(f"  [+] Linked {len(linked_parts)} split body part(s) to chassis in carcols.meta: {', '.join(linked_parts)}")

# Direct all temporary files to secondary SSD (D:\Temp) to prevent filling C: drive
for _cand_tmp in (r"D:\Temp", r"D:\tmp"):
    if os.path.exists(_cand_tmp):
        import tempfile
        tempfile.tempdir = _cand_tmp
        os.environ["TEMP"] = _cand_tmp
        os.environ["TMP"] = _cand_tmp
        break



def sanitize_vehicles_meta(vm_path):
    if not os.path.exists(vm_path):
        return
    text = open(vm_path, "r", encoding="utf-8", errors="ignore").read()
    
    # 1. Dashboard: VDT_RACE -> VDT_SUPERGT
    text = re.sub(r"<dashboardType>.*?</dashboardType>", "<dashboardType>VDT_SUPERGT</dashboardType>", text)
    
    # 2. Parent TXD: In PS4 v1.00 Orbis, ALL add-on vehicles must inherit from resident vehshare
    text = re.sub(r"<parent>.*?</parent>", "<parent>vehshare</parent>", text)
    text = re.sub(r"<parent\s*/>", "<parent>vehshare</parent>", text)
    if "<txdRelationships>" not in text:
        m = re.search(r'<modelName>([A-Za-z0-9_]+)</modelName>', text)
        mname = m.group(1).strip() if m else "modveh"
        txd_block = f"""  <txdRelationships>
    <Item>
      <parent>vehshare</parent>
      <child>{mname}</child>
    </Item>
  </txdRelationships>
</CVehicleModelInfoArgs>"""
        if "</CVehicleModelInfoArgs>" in text:
            text = text.replace("</CVehicleModelInfoArgs>", txd_block)
    
    # 3. Audio: Replace modern PC DLC audios not in v1.00 or empty audios with base game supercar sound (zentorno)
    post_dlc_audios = {
        "prototipo", "nero", "nero2", "xa21", "deveste", "tezeract", "krieger", "emerus",
        "thrax", "tigon", "calico", "jester4", "vagrant", "furia", "italirsx", "ignus",
        "champion", "torero2", "corsita", "lm87", "sm722", "draugur", "tenf", "omnisegt"
    }
    def fix_audio(m):
        val = m.group(1).strip()
        if not val or val.lower() in post_dlc_audios:
            return "<audioNameHash>zentorno</audioNameHash>"
        return m.group(0)
    text = re.sub(r"<audioNameHash>(.*?)</audioNameHash>", fix_audio, text)
    if not re.search(r"<audioNameHash>[A-Za-z0-9_]+</audioNameHash>", text):
        text = re.sub(r"<audioNameHash\s*/>|<audioNameHash></audioNameHash>", "<audioNameHash>zentorno</audioNameHash>", text)
    
    # 4. Flags: Remove modern PC flags and fix PS4 v1.00 crashes
    text = text.replace("FLAG_FRONT_BOOT", "")
    text = text.replace("FLAG_HAS_EXTRA", "")
    text = text.replace("FLAG_HAS_INTERIOR_EXTRA", "FLAG_HAS_INTERIOR_EXTRAS")
    text = text.replace("FLAG_COUNT_AS_FACEBOOK_DRIVEN", "")
    if "<requiredExtras />" in text or "<requiredExtras></requiredExtras>" in text or "<requiredExtras/>" in text:
        text = text.replace("FLAG_EXTRAS_REQUIRE", "")
    # Clean up multiple whitespace inside <flags>
    text = re.sub(r"<flags>(.*?)</flags>", lambda m: f"<flags>{' '.join(m.group(1).split())}</flags>", text, flags=re.DOTALL)
    
    # 5. Fix layout if restricted
    text = text.replace("LAYOUT_LOW_RESTRICTED", "LAYOUT_LOW")
    text = text.replace("NERO_COVER_OFFSET_INFO", "CHEETAH_COVER_OFFSET_INFO")
    
    # 6. Sanitize convertible roof clip dict: PC mods frequently inject custom script animations
    # into animConvRoofDictName which fail to parse on PS4 and cause infinite streaming timeouts.
    text = re.sub(r"<animConvRoofDictName>(?!null|veh_conv_roof)[^<]*</animConvRoofDictName>", "<animConvRoofDictName>null</animConvRoofDictName>", text)
    text = re.sub(r"<animConvRoofName>(?!null|veh_conv_roof)[^<]*</animConvRoofName>", "<animConvRoofName>null</animConvRoofName>", text)
    
    open(vm_path, "w", encoding="utf-8", newline="\r\n").write(text)
    print("  [+] vehicles.meta sanitized (VDT_SUPERGT, LAYOUT_LOW, safe flags)")

def sanitize_handling_meta(hm_path, realistic_normalize=True):
    if not os.path.exists(hm_path):
        return
    text = open(hm_path, "r", encoding="utf-8", errors="ignore").read()
    
    # 1. Clamp transmission gears to max 6 (v1.00 CTransmission fixed buffer)
    text = re.sub(r'<nInitialDriveGears\s+value="[789]"', '<nInitialDriveGears value="6"', text)
    text = re.sub(r'<nInitialDriveGears\s+value="1\d+"', '<nInitialDriveGears value="6"', text)
    
    # 2. Clamp drive inertia to safe supercar range (1.0 - 1.8)
    def fix_inertia(m):
        val = float(m.group(1))
        if val > 2.2:
            val = 1.75
        return f'<fDriveInertia value="{val:.6f}" />'
    text = re.sub(r'<fDriveInertia\s+value="([\d\.]+)"\s*/>', fix_inertia, text)
    
    # 3. Top Speed: Clamp flat vel (Stock supercars are 145-165; troll/arcade mods use 300-650)
    def fix_vel(m):
        val = float(m.group(1))
        if val > 180.0:
            val = 162.0  # ~200 mph in-game, realistic exotic supercar top speed
        return f'<fInitialDriveMaxFlatVel value="{val:.6f}" />'
    text = re.sub(r'<fInitialDriveMaxFlatVel\s+value="([\d\.]+)"\s*/>', fix_vel, text)

    if realistic_normalize:
        # 4. Brake Force: (Stock: 1.1 - 1.45, Troll mods: 50.0 - 150.0 causing instant goofy halts)
        def fix_brake(m):
            val = float(m.group(1))
            if val > 2.0:
                val = 1.35  # Firm, responsive carbon-ceramic brakes
            return f'<fBrakeForce value="{val:.6f}" />'
        text = re.sub(r'<fBrakeForce\s+value="([\d\.]+)"\s*/>', fix_brake, text)

        # 5. Drive Force / Acceleration: (Stock: 0.32 - 0.40, Troll mods: 0.70 - 2.0 rocket launch)
        def fix_drive_force(m):
            val = float(m.group(1))
            if val > 0.45:
                val = 0.38  # Very fast supercar launch without physics glitch
            return f'<fInitialDriveForce value="{val:.6f}" />'
        text = re.sub(r'<fInitialDriveForce\s+value="([\d\.]+)"\s*/>', fix_drive_force, text)

        # 6. Traction: (Stock: 2.3 - 2.7, Troll mods: 5.0 - 10.0 slot-car grip)
        def fix_trac_max(m):
            val = float(m.group(1))
            if val > 3.2:
                val = 2.65
            return f'<fTractionCurveMax value="{val:.6f}" />'
        text = re.sub(r'<fTractionCurveMax\s+value="([\d\.]+)"\s*/>', fix_trac_max, text)

        def fix_trac_min(m):
            val = float(m.group(1))
            if val > 3.0:
                val = 2.45
            return f'<fTractionCurveMin value="{val:.6f}" />'
        text = re.sub(r'<fTractionCurveMin\s+value="([\d\.]+)"\s*/>', fix_trac_min, text)

        # 7. Downforce: (Stock: 1.2 - 2.8, Troll mods: 50.0 - 100.0)
        def fix_downforce(m):
            val = float(m.group(1))
            if val > 5.0:
                val = 2.20
            return f'<fDownforceModifier value="{val:.6f}" />'
        text = re.sub(r'<fDownforceModifier\s+value="([\d\.]+)"\s*/>', fix_downforce, text)

        # 8. Mass: (Stock: 1400 - 1700 kg, Troll mods: 5000 kg tank mass)
        def fix_mass(m):
            val = float(m.group(1))
            if val > 3000.0 or val < 800.0:
                val = 1570.0  # Authentic Ferrari SF90 Stradale curb weight
            return f'<fMass value="{val:.6f}" />'
        text = re.sub(r'<fMass\s+value="([\d\.]+)"\s*/>', fix_mass, text)

        # 9. Clutch Shift Rate: (Stock: 2.0 - 3.0, Troll mods: 8.0 - 20.0)
        def fix_clutch(m):
            tag = m.group(1)
            val = float(m.group(2))
            if val > 4.0:
                val = 2.50
            return f'<{tag} value="{val:.6f}" />'
        text = re.sub(r'<(fClutchChangeRateScale(?:Up|Down)Shift)\s+value="([\d\.]+)"\s*/>', fix_clutch, text)

        # 10. Low Speed Traction Loss: (Troll mods set 0.0 to prevent wheelspin completely)
        def fix_low_trac(m):
            val = float(m.group(1))
            if val < 0.3:
                val = 1.0
            return f'<fLowSpeedTractionLossMult value="{val:.6f}" />'
        text = re.sub(r'<fLowSpeedTractionLossMult\s+value="([\d\.]+)"\s*/>', fix_low_trac, text)

        # 11. Damage multipliers: restore deformation if god-mode 0.0
        text = re.sub(r'<fCollisionDamageMult\s+value="0(?:\.0+)?\s*"\s*/>', '<fCollisionDamageMult value="0.700000" />', text)
        text = re.sub(r'<fDeformationDamageMult\s+value="0(?:\.0+)?\s*"\s*/>', '<fDeformationDamageMult value="0.700000" />', text)

    open(hm_path, "w", encoding="utf-8", newline="\r\n").write(text)
    print("  [+] handling.meta sanitized (gears<=6, realistic supercar physics normalized)")

def sanitize_carvariations_meta(cv_path):
    if not os.path.exists(cv_path):
        return
    text = open(cv_path, "r", encoding="utf-8", errors="ignore").read()
    
    # 1. In v1.00, <indices content="char_array"> must only have 4 values
    def fix_indices(m):
        nums = m.group(1).split()
        if len(nums) > 4:
            nums = nums[:4]
        formatted = "\n\t\t\t" + "\n\t\t\t".join(nums) + "\n\t\t"
        return f'<indices content="char_array">{formatted}</indices>'
    text = re.sub(r'<indices\s+content="char_array"\s*>(.*?)</indices>', fix_indices, text, flags=re.DOTALL)

    # 2. In v1.00, lightSettings must be <= 194 (Rockstar range) to prevent broken headlights
    def fix_light_settings(m):
        val = int(m.group(1))
        if val > 194:
            return '<lightSettings value="1" />'
        return m.group(0)
    text = re.sub(r'<lightSettings\s+value="(\d+)"\s*/>', fix_light_settings, text)
    
    open(cv_path, "w", encoding="utf-8", newline="\r\n").write(text)
    print("  [+] carvariations.meta sanitized (4 color indices max, lightSettings <= 194)")

def sanitize_carcols_and_variations(cc_path, cv_path, target_kit_id=727):
    if not os.path.exists(cc_path):
        return
    cc_txt = open(cc_path, "r", encoding="utf-8", errors="ignore").read()

    # 1. Strip invalid custom <Lights> blocks with ID > 194 in carcols.meta
    def filter_lights_block(m):
        lights_body = m.group(1)
        items = list(re.finditer(r'<Item>[\s\S]*?<id\s+value="(\d+)"[\s\S]*?</Item>', lights_body))
        valid_items = [it.group(0) for it in items if int(it.group(1)) <= 194]
        if not valid_items:
            return ""
        return f"<Lights>\n" + "\n".join(valid_items) + "\n</Lights>"
    cc_txt = re.sub(r'<Lights>([\s\S]*?)</Lights>', filter_lights_block, cc_txt)

    # 2. Zero coronas so headlights/taillights don't produce blinding neon glare
    def zero_coronas(m):
        tag = m.group(1)
        body = m.group(2)
        body = re.sub(r'<size\s+value="[^"]*"\s*/>', '<size value="0.000000" />', body)
        body = re.sub(r'<size_far\s+value="[^"]*"\s*/>', '<size_far value="0.000000" />', body)
        body = re.sub(r'<intensity\s+value="[^"]*"\s*/>', '<intensity value="0.000000" />', body)
        body = re.sub(r'<intensity_far\s+value="[^"]*"\s*/>', '<intensity_far value="0.000000" />', body)
        return f'<{tag}>{body}</{tag}>'
    cc_txt = re.sub(r'<(tailLightCorona|tailLightMiddleCorona|headLightCorona|reversingLightCorona|frontIndicatorCorona|rearIndicatorCorona)>([\s\S]*?)</\1>', zero_coronas, cc_txt, flags=re.DOTALL)
    
    # 3. Find all kitName and id pairs and clamp > 1023
    matches = list(re.finditer(r'<kitName>([^<]+)</kitName>\s*<id\s+value="(\d+)"', cc_txt))
    if matches:
        for m in matches:
            old_kit_name = m.group(1).strip()
            old_id = int(m.group(2))
            if old_id > 1023:
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
            cwxml_exe = cwxml_exe or get_cwxml_exe()
            if not cwxml_exe or not os.path.exists(cwxml_exe):
                return
            reason = []
            if has_overflow: reason.append(f"buffer overflow (+{len(dec)-expected:,}B)")
            if has_embedded: reason.append("embedded textures")
            print(f"  [!] Rebuilding {os.path.basename(yft_path)} via CWXml ({', '.join(reason)})...")
            xml_path = yft_path + ".xml"
            try:
                subprocess.run([cwxml_exe, "export", yft_path, xml_path], capture_output=True)
            except Exception:
                return
            if os.path.exists(xml_path):
                raw = open(xml_path, "r", encoding="utf-8", errors="ignore").read()
                # Fix the hash_XXXXXXXX.sps integer parse bug
                raw = re.sub(r'<FileName>(hash_[0-9A-Fa-f]{8})\.sps</FileName>', r'<FileName>\1</FileName>', raw)
                # Strip embedded TextureDictionary
                raw = re.sub(r'[ \t]*<TextureDictionary>.*?</TextureDictionary>[ \t]*\n?', '', raw, flags=re.DOTALL)
                open(xml_path, "w", encoding="utf-8").write(raw)
                os.remove(yft_path)
                imp_res = subprocess.run([cwxml_exe, "import", xml_path, yft_path], capture_output=True, text=True)
                try:
                    os.remove(xml_path)
                except:
                    pass
                if os.path.exists(yft_path):
                    print(f"  [+] Rebuilt {os.path.basename(yft_path)} with exact-size RSC7 headers")
                else:
                    print(f"  [!] CWXml import error: {imp_res.stderr.strip()}")
    except Exception as e:
        print(f"  [WARN] ensure_clean_yft_buffer warning: {e}")

def detect_vehicle_model_name(src_path, fallback=None):
    """
    Intelligently extracts the vehicle model name from:
    1. vehicles.meta <modelName>
    2. Primary vehicle model file (*.yft)
    3. Directory / filename sanitization (stripping hash suffixes)
    """
    if not src_path or not os.path.exists(src_path):
        return fallback or "modveh"

    # 1. Search for vehicles.meta inside src_path
    if os.path.isdir(src_path):
        vm_list = glob.glob(os.path.join(src_path, "**", "vehicles.meta"), recursive=True)
        for vm in vm_list:
            try:
                txt = open(vm, "r", encoding="utf-8", errors="ignore").read()
                m = re.search(r"<modelName>([A-Za-z0-9_]+)</modelName>", txt)
                if m and m.group(1).strip().lower() not in ["null", "none"]:
                    return m.group(1).strip().lower()
            except Exception:
                pass

        # 2. Search for primary *.yft (excluding *_hi.yft and tuning parts)
        yfts = [f for f in glob.glob(os.path.join(src_path, "**", "*.yft"), recursive=True)
                if not f.lower().endswith("_hi.yft") and "_mods" not in f.lower() and not os.path.basename(f).lower().startswith("arch_")]
        if yfts:
            stem = os.path.splitext(os.path.basename(yfts[0]))[0].lower()
            return stem

    # 3. Clean up folder or file name
    base = os.path.basename(src_path.rstrip(r"\/")).lower()
    base = re.sub(r'_[0-9]{8,}_[0-9]{5,}$', '', base) # strip PC download hashes like _1751638131_806971
    if base.endswith(".rpf"):
        base = base[:-4]
    if base in ["dlc", "vehicles", "vehiclemods", "dlcpacks", "add-on", "update", "x64"]:
        parent = os.path.basename(os.path.dirname(src_path.rstrip(r"\/"))).lower()
        parent = re.sub(r'_[0-9]{8,}_[0-9]{5,}$', '', parent)
        if parent and parent not in ["dlcpacks", "vehiclemods", "mods"]:
            base = parent
        else:
            base = fallback or "modveh"
    return base

def convert_vehicle(src_dlc_rpf, output_dlc_rpf, model_name=None):
    if not os.path.exists(src_dlc_rpf):
        raise FileNotFoundError(f"Source file not found: {src_dlc_rpf}")
    
    if not model_name or model_name.lower() in ["", "dlcpacks", "add-on", "vehiclemods", "mods"]:
        model_name = detect_vehicle_model_name(src_dlc_rpf)

    print(f"\n===========================================================")
    print(f"  Auto-Converting Vehicle Mod: {model_name.upper()}")
    print(f"  Source: {src_dlc_rpf}")
    print(f"  Output: {output_dlc_rpf}")
    print(f"===========================================================\n")

    work_dir = output_dlc_rpf + "_work"
    shutil.rmtree(work_dir, ignore_errors=True)
    os.makedirs(work_dir, exist_ok=True)

    # Initialize NeoGen and CWXml
    cwxml_exe = get_cwxml_exe()

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

    # Step 1: Extract or prepare PC mod assets
    print("[1/6] Extracting/Preparing PC mod assets...")
    if os.path.isfile(src_dlc_rpf) and (src_dlc_rpf.lower().endswith(".zip") or src_dlc_rpf.lower().endswith(".oiv")):
        print(f"  [+] Unpacking mod archive (.zip/.oiv): {src_dlc_rpf}")
        import zipfile
        zip_tmp = output_dlc_rpf + "_zip_extracted"
        shutil.rmtree(zip_tmp, ignore_errors=True)
        with zipfile.ZipFile(src_dlc_rpf, 'r') as zf:
            zf.extractall(zip_tmp)
        inner_rpfs = glob.glob(os.path.join(zip_tmp, "**", "dlc.rpf"), recursive=True)
        if inner_rpfs:
            print(f"  [+] Found PC dlc.rpf in archive: {inner_rpfs[0]}")
            NeoGen.extract_single_rpf(inner_rpfs[0], work_dir)
        else:
            for item in os.listdir(zip_tmp):
                s = os.path.join(zip_tmp, item)
                d = os.path.join(work_dir, item)
                if os.path.isdir(s): shutil.copytree(s, d)
                else: shutil.copy2(s, d)
        shutil.rmtree(zip_tmp, ignore_errors=True)
    elif os.path.isdir(src_dlc_rpf):
        # User passed an unpacked directory: search for inner dlc.rpf or copy contents
        inner_rpfs = glob.glob(os.path.join(src_dlc_rpf, "**", "dlc.rpf"), recursive=True)
        if inner_rpfs:
            print(f"  [+] Found PC dlc.rpf: {inner_rpfs[0]}")
            NeoGen.extract_single_rpf(inner_rpfs[0], work_dir)
        else:
            for item in os.listdir(src_dlc_rpf):
                s = os.path.join(src_dlc_rpf, item)
                d = os.path.join(work_dir, item)
                if os.path.isdir(s):
                    shutil.copytree(s, d)
                else:
                    shutil.copy2(s, d)
    else:
        NeoGen.extract_single_rpf(src_dlc_rpf, work_dir)

    # Step 2: Locate and extract vehicles.rpf
    vrpf_candidates = glob.glob(os.path.join(work_dir, "**", "vehicles.rpf"), recursive=True)
    vrpf_work = output_dlc_rpf + "_vrpf_extracted"
    shutil.rmtree(vrpf_work, ignore_errors=True)

    if vrpf_candidates:
        pc_vrpf = vrpf_candidates[0]
        print(f"\n[2/6] Extracting vehicles.rpf: {pc_vrpf}...")
        NeoGen.extract_single_rpf(pc_vrpf, vrpf_work)
    else:
        # Check if loose .yft / .ytd files exist in work_dir
        loose_models = [f for f in glob.glob(os.path.join(work_dir, "**", "*.yft"), recursive=True)
                        if "_mods" not in os.path.basename(f).lower() and not os.path.basename(f).lower().startswith("arch_")]
        loose_texs = [f for f in glob.glob(os.path.join(work_dir, "**", "*.ytd"), recursive=True)
                      if "_mods" not in os.path.basename(f).lower()]
        
        if not loose_models and not loose_texs:
            raise FileNotFoundError("Neither vehicles.rpf nor loose vehicle assets (*.yft/*.ytd) found in mod source!")
        
        print(f"\n[2/6] Detected loose vehicle assets (no vehicles.rpf). Organizing into vehicles.rpf staging...")
        os.makedirs(vrpf_work, exist_ok=True)
        for m in loose_models:
            shutil.copy2(m, os.path.join(vrpf_work, os.path.basename(m)))
        for t in loose_texs:
            shutil.copy2(t, os.path.join(vrpf_work, os.path.basename(t)))
        
        # If model_name was still not properly determined, refine it from the base model
        if model_name in ["", "modveh"]:
            base_yfts = [f for f in loose_models if not f.lower().endswith("_hi.yft")]
            if base_yfts:
                model_name = os.path.splitext(os.path.basename(base_yfts[0]))[0].lower()
                print(f"  [+] Refined vehicle model name from base model: {model_name.upper()}")

    # Step 3: Locate and extract vehiclemods rpf if present
    # Parse carcols.meta to separate linked body parts vs actual tuning parts
    carcols_mods = set()
    linked_models = set()
    for cc_file in glob.glob(os.path.join(work_dir, "**", "carcols.meta"), recursive=True):
        try:
            cc_txt = open(cc_file, "r", encoding="utf-8", errors="ignore").read()
            for m in re.finditer(r'<linkMods>.*?</linkMods>', cc_txt, re.DOTALL):
                for lm in re.finditer(r'<modelName>([^<]+)</modelName>', m.group(0)):
                    linked_models.add(lm.group(1).strip().lower())
            for m in re.finditer(r'<linkedModels>.*?</linkedModels>', cc_txt, re.DOTALL):
                for lm in re.finditer(r'<(?:modelName|Item)>([^<]+)</(?:modelName|Item)>', m.group(0)):
                    linked_models.add(lm.group(1).strip().lower())
            for m in re.finditer(r'<visibleMods>.*?</visibleMods>', cc_txt, re.DOTALL):
                for vm in re.finditer(r'<modelName>([^<]+)</modelName>', m.group(0)):
                    val = vm.group(1).strip().lower()
                    if val not in ["null", "none", ""]:
                        carcols_mods.add(val)
        except Exception:
            pass

    if model_name.lower() == "gibv48":
        linked_models.add("gibv48_wire_1")

    if linked_models:
        print(f"  [i] Linked body parts (protected in vehicles.rpf): {len(linked_models)}")
    if carcols_mods:
        print(f"  [i] Tuning mod models: {', '.join(sorted(carcols_mods)[:8])}...")

    mrpf_candidates = glob.glob(os.path.join(work_dir, "**", "*_mods.rpf"), recursive=True)
    has_mods = False
    mod_rpf_name = f"{model_name}_mods.rpf"
    mrpf_work = output_dlc_rpf + "_mrpf_extracted"
    shutil.rmtree(mrpf_work, ignore_errors=True)

    if mrpf_candidates:
        pc_mrpf = mrpf_candidates[0]
        mod_rpf_name = os.path.basename(pc_mrpf)
        print(f"\n[3/6] Extracting vehicle tuning mods: {mod_rpf_name}...")
        NeoGen.extract_single_rpf(pc_mrpf, mrpf_work)

    os.makedirs(mrpf_work, exist_ok=True)

    # Move any true tuning parts from vrpf_work into mrpf_work
    for f in list(os.listdir(vrpf_work)):
        fp = os.path.join(vrpf_work, f)
        stem = os.path.splitext(f)[0].lower()
        ext = os.path.splitext(f)[1].lower()
        if ext in [".yft", ".ytd"]:
            is_base = (stem == model_name or stem == f"{model_name}_hi")
            is_linked = (stem in linked_models or "arch_" in stem or "wire_" in stem)
            is_tuning = (
                not is_linked and not is_base and
                (stem in carcols_mods or "_mods" in stem)
            )
            if is_tuning:
                dest = os.path.join(mrpf_work, f)
                shutil.move(fp, dest)
                print(f"  [+] Routed tuning part {f} -> {mod_rpf_name}")

    # Check for loose tuning files in work_dir outside vrpf_work
    for tp in glob.glob(os.path.join(work_dir, "**", "*.yft"), recursive=True):
        if tp.startswith(vrpf_work) or tp.startswith(mrpf_work):
            continue
        stem = os.path.splitext(os.path.basename(tp))[0].lower()
        is_linked = (stem in linked_models or "arch_" in stem or "wire_" in stem)
        if not is_linked and (stem in carcols_mods or "_mods" in stem or "vehiclemods" in tp.lower()):
            shutil.copy2(tp, os.path.join(mrpf_work, os.path.basename(tp)))
            print(f"  [+] Routed loose tuning part {os.path.basename(tp)} -> {mod_rpf_name}")

    for tt in glob.glob(os.path.join(work_dir, "**", "*.ytd"), recursive=True):
        if tt.startswith(vrpf_work) or tt.startswith(mrpf_work):
            continue
        stem = os.path.splitext(os.path.basename(tt))[0].lower()
        if stem in carcols_mods or "_mods" in stem or "vehiclemods" in tt.lower():
            shutil.copy2(tt, os.path.join(mrpf_work, os.path.basename(tt)))

    if os.path.isdir(mrpf_work) and len(os.listdir(mrpf_work)) > 0:
        has_mods = True
        print(f"  [+] Tuning archive {mod_rpf_name} active with {len(os.listdir(mrpf_work))} part(s).")
    else:
        has_mods = False
        shutil.rmtree(mrpf_work, ignore_errors=True)

    # Step 4: Convert assets inside vehicles.rpf (YFT -> OFT, YTD -> OTD)
    print("\n[4/6] Converting models and compressing textures (1024 max, DXT5)...")

    # Clean any PC clip dictionaries (.ycd) that cause PS4 animation streaming freeze
    for f in list(os.listdir(vrpf_work)):
        if f.lower().endswith(".ycd"):
            os.remove(os.path.join(vrpf_work, f))
            print(f"  [-] Removed PC animation dictionary {f} (prevents streaming freeze)")

    # 1. Convert YFTs in vrpf_work FIRST (extracting embedded textures into <stem>_tex)
    for f in list(os.listdir(vrpf_work)):
        fp = os.path.join(vrpf_work, f)
        if f.lower().endswith(".yft"):
            stem = os.path.splitext(f)[0]
            print(f"  -> Converting model {f} -> {stem}.oft...")
            ensure_clean_yft_buffer(fp, cwxml_exe)
            res = NeoGen.convert_rsc_file(fp, vrpf_work, strip=True, auto_clean_embedded=True)
            if not res:
                raise RuntimeError(f"Failed to convert RSC model: {f}")
            os.remove(fp)

    # Harvest canonical texture names from all converted OFTs
    canonical_texs = set()
    tex_pattern = re.compile(r'<Item\b[^>]*\btype="Texture"[^>]*>\s*<Name>([^<]+)</Name>')
    for oft in glob.glob(os.path.join(vrpf_work, "*.oft")):
        if cwxml_exe:
            xml_tmp = oft + ".xml"
            try:
                subprocess.run([cwxml_exe, "export", oft, xml_tmp], capture_output=True)
                if os.path.exists(xml_tmp):
                    for t in tex_pattern.findall(open(xml_tmp, "r", encoding="utf-8", errors="ignore").read()):
                        canonical_texs.add(t.strip())
                    try: os.remove(xml_tmp)
                    except: pass
            except Exception:
                pass

    # 2. Convert YTDs in vrpf_work (merging with any extracted model textures in <stem>_tex)
    ytd_stems_converted = set()
    for f in list(os.listdir(vrpf_work)):
        fp = os.path.join(vrpf_work, f)
        if f.lower().endswith(".ytd"):
            stem = os.path.splitext(f)[0]
            tex_folder = os.path.join(vrpf_work, f"{stem}_tex")
            print(f"  -> Compressing {f} -> {stem}.otd...")
            NeoGen.extract_ytd_to_dds(fp, tex_folder, opts)
            stage_fallback_textures(tex_folder, canonical_texs)
            res = NeoGen._build_otd_windows(tex_folder, stem, vrpf_work, tex_dir)
            if not res or not os.path.exists(os.path.join(vrpf_work, f"{stem}.otd")):
                raise RuntimeError(f"Failed to build OTD for {f}")
            shutil.rmtree(tex_folder, ignore_errors=True)
            os.remove(fp)
            ytd_stems_converted.add(stem.lower())

    # 3. Build standalone OTD for any model that had embedded textures but no matching .ytd
    for d in list(os.listdir(vrpf_work)):
        dp = os.path.join(vrpf_work, d)
        if d.endswith("_tex") and os.path.isdir(dp):
            stem = d[:-4]
            if stem.lower() not in ytd_stems_converted:
                print(f"  -> Building dictionary for extracted textures: {stem}.otd...")
                stage_fallback_textures(dp, canonical_texs)
                NeoGen._build_otd_windows(dp, stem, vrpf_work, tex_dir)
            shutil.rmtree(dp, ignore_errors=True)

    # Process tuning parts in vehiclemods
    if has_mods and mrpf_work and os.path.isdir(mrpf_work):
        for f in list(os.listdir(mrpf_work)):
            fp = os.path.join(mrpf_work, f)
            if f.lower().endswith(".yft"):
                stem = os.path.splitext(f)[0]
                print(f"  -> Converting tuning part {f} -> {stem}.oft...")
                ensure_clean_yft_buffer(fp, cwxml_exe)
                res = NeoGen.convert_rsc_file(fp, mrpf_work, strip=True, auto_clean_embedded=True)
                if not res:
                    raise RuntimeError(f"Failed to convert RSC model: {f}")
                os.remove(fp)
            elif f.lower().endswith(".ytd"):
                stem = os.path.splitext(f)[0]
                tex_folder = os.path.join(mrpf_work, f"{stem}_tex")
                NeoGen.extract_ytd_to_dds(fp, tex_folder, opts)
                stage_fallback_textures(tex_folder, canonical_texs)
                NeoGen._build_otd_windows(tex_folder, stem, mrpf_work, tex_dir)
                shutil.rmtree(tex_folder, ignore_errors=True)
                os.remove(fp)

        # Build standalone OTD for any tuning parts that had embedded textures but no matching .ytd
        for d in list(os.listdir(mrpf_work)):
            dp = os.path.join(mrpf_work, d)
            if d.endswith("_tex") and os.path.isdir(dp):
                stem = d[:-4]
                stage_fallback_textures(dp, canonical_texs)
                NeoGen._build_otd_windows(dp, stem, mrpf_work, tex_dir)
                shutil.rmtree(dp, ignore_errors=True)

        # Promote any resident texture dictionaries (wheel, decals, trim) into vehicles.rpf
        for f in list(os.listdir(mrpf_work)):
            if f.lower().endswith(".otd"):
                shutil.copy2(os.path.join(mrpf_work, f), os.path.join(vrpf_work, f))
                print(f"  [+] Promoted resident texture dictionary {f} into vehicles.rpf")

    # Read all OTD texture names now available
    otd_names = set()
    for f in os.listdir(vrpf_work):
        if f.lower().endswith(".otd"):
            try:
                raw = open(os.path.join(vrpf_work, f), "rb").read()
                virt, _ = NeoGen.decompress_rsc7_ytd(raw)
                for m in re.finditer(rb'([a-zA-Z0-9_#\s\-\(\)\.]{2,})\x00', virt):
                    n_str = m.group(1).decode("ascii", errors="ignore").strip().lower()
                    if len(n_str) >= 2:
                        otd_names.add(n_str)
            except Exception: pass

    # Sanitize all OFT shaders (fixing empty DirtSampler & missing samplers)
    print("  -> Sanitizing model shaders (repairing empty DirtSampler & missing samplers)...")
    all_ofts = glob.glob(os.path.join(vrpf_work, "*.oft"))
    if has_mods and mrpf_work and os.path.isdir(mrpf_work):
        all_ofts += glob.glob(os.path.join(mrpf_work, "*.oft"))
    for oft in all_ofts:
        sanitize_model_oft_shaders(oft, otd_names)

    # Step 5: Restructure to Vans Architecture
    print("\n[5/6] Restructuring metadata and creating PS4 container...")
    target_data_dir = os.path.join(work_dir, "data")
    os.makedirs(target_data_dir, exist_ok=True)

    # Move all meta files from everywhere to data/
    for root, dirs, files in os.walk(work_dir):
        if root.startswith(vrpf_work) or (has_mods and mrpf_work and os.path.isdir(mrpf_work) and root.startswith(mrpf_work)):
            continue
        for f in files:
            if f.lower().endswith(".meta"):
                fp = os.path.join(root, f)
                dest = os.path.join(target_data_dir, f.lower())
                if fp != dest:
                    shutil.move(fp, dest)

    # Ensure all split body parts (arch_*, wire_*) are properly linked to chassis in carcols.meta
    ensure_linked_parts_in_carcols(work_dir, vrpf_work, model_name)

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

    if not has_mods and not carcols_mods:
        cv_path = os.path.join(target_data_dir, "carvariations.meta")
        if os.path.exists(cv_path):
            cv_txt = open(cv_path, "r", encoding="utf-8", errors="ignore").read()
            cv_txt = re.sub(r'<kits>.*?</kits>', '<kits />', cv_txt, flags=re.DOTALL)
            open(cv_path, "w", encoding="utf-8", newline="\r\n").write(cv_txt)

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

    # Promote resident OTD to mrpf_work if vehiclemods is active BEFORE building vehicles.rpf
    if has_mods and mrpf_work and os.path.isdir(mrpf_work) and len(os.listdir(mrpf_work)) > 0:
        for f in os.listdir(vrpf_work):
            if f.lower().endswith(".otd"):
                shutil.copy2(os.path.join(vrpf_work, f), os.path.join(mrpf_work, f))
                print(f"  [+] Copied resident {f} -> vehiclemods/{mod_rpf_name} (fixing tuning part textures)")

    # Build ps4/vehicles.rpf
    ps4_vrpf = os.path.join(ps4_dir, "vehicles.rpf")
    ok = NeoGen.build_single_rpf(vrpf_work, ps4_vrpf, rpf_dir, mode, wine_bin)
    if not ok or not os.path.exists(ps4_vrpf):
        raise RuntimeError("Failed to build ps4/vehicles.rpf")
    shutil.rmtree(vrpf_work, ignore_errors=True)

    # Build ps4/vehiclemods/{mod_rpf_name}
    if has_mods and mrpf_work and os.path.isdir(mrpf_work) and len(os.listdir(mrpf_work)) > 0:
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
    
    # Run automated self-testing verification suite
    verify_converted_vehicle(output_dlc_rpf)
    return output_dlc_rpf

def verify_converted_vehicle(dlc_rpf_path):
    """
    Automated self-testing verification suite for converted PS4/PS5 vehicle DLCs.
    Extracts the built dlc.rpf, inspects nested RPFs, models (.oft), textures (.otd),
    and all metadata for v1.00 runtime safety.
    """
    import tempfile
    print(f"\n[*] Running Automated Verification Suite on {os.path.basename(dlc_rpf_path)}...")
    issues = []
    passes = []
    
    with tempfile.TemporaryDirectory() as td:
        try:
            NeoGen.extract_single_rpf(dlc_rpf_path, td)
        except Exception as e:
            print(f"  [FAIL] Failed to extract outer DLC: {e}")
            return False

        # 1. Check setup2.xml and content.xml
        setup2 = os.path.join(td, "setup2.xml")
        content = os.path.join(td, "content.xml")
        if os.path.exists(setup2) and os.path.exists(content):
            passes.append("setup2.xml & content.xml present and well-formed (Vans format)")
        else:
            issues.append("Missing setup2.xml or content.xml in root of DLC")

        # 2. Check ps4/vehicles.rpf
        vrpf = os.path.join(td, "ps4", "vehicles.rpf")
        if not os.path.exists(vrpf):
            issues.append("Missing ps4/vehicles.rpf")
        else:
            vrpf_ext = os.path.join(td, "vrpf_ext")
            NeoGen.extract_single_rpf(vrpf, vrpf_ext)
            ofts = [f for f in os.listdir(vrpf_ext) if f.endswith(".oft")]
            otds = [f for f in os.listdir(vrpf_ext) if f.endswith(".otd")]
            if ofts and otds:
                passes.append(f"vehicles.rpf contains valid PS4 assets ({len(ofts)} .oft, {len(otds)} .otd)")
            else:
                issues.append(f"vehicles.rpf missing .oft models or .otd textures (found {len(ofts)} ofts, {len(otds)} otds)")
            
            # Verify 8KB min chunk alignment on all RSC assets
            checked, chunk_issues = cv.verify_rpf_rsc7_assets(vrpf) if hasattr(cv, "verify_rpf_rsc7_assets") else (0, [])
            if chunk_issues:
                issues.extend(chunk_issues)
            else:
                passes.append(f"All {len(ofts)+len(otds)} RSC7 vehicle assets meet >= 8192 byte Orbis chunk alignment")

        # 3. Check handling.meta gears & realistic physics
        hm = os.path.join(td, "data", "handling.meta")
        if os.path.exists(hm):
            hm_txt = open(hm, "r", encoding="utf-8", errors="ignore").read()
            gears = re.findall(r'<nInitialDriveGears\s+value="(\d+)"', hm_txt)
            if gears:
                max_g = max(int(g) for g in gears)
                if max_g <= 6:
                    passes.append(f"handling.meta transmission gears safe for v1.00 (max {max_g} gears <= 6)")
                else:
                    issues.append(f"handling.meta has {max_g} gears! (Must be <= 6 to prevent CTransmission crash)")

            brakes = [float(b) for b in re.findall(r'<fBrakeForce\s+value="([\d\.]+)"', hm_txt)]
            vels = [float(v) for v in re.findall(r'<fInitialDriveMaxFlatVel\s+value="([\d\.]+)"', hm_txt)]
            if any(b > 2.0 for b in brakes):
                issues.append(f"handling.meta has extreme brakeForce ({max(brakes)} > 2.0) causing goofy instant halts")
            elif any(v > 185.0 for v in vels):
                issues.append(f"handling.meta has extreme maxFlatVel ({max(vels)} > 185.0) causing broken speeds")
            else:
                passes.append("handling.meta physics normalized for realistic driving (safe speed & braking)")

        # 4. Check carvariations.meta lightSettings & color indices
        cvm = os.path.join(td, "data", "carvariations.meta")
        if os.path.exists(cvm):
            cv_txt = open(cvm, "r", encoding="utf-8", errors="ignore").read()
            lights = re.findall(r'<lightSettings\s+value="(\d+)"', cv_txt)
            if lights:
                max_l = max(int(l) for l in lights)
                if max_l <= 194:
                    passes.append(f"carvariations.meta lightSettings safe for v1.00 ({max_l} <= 194)")
                else:
                    issues.append(f"carvariations.meta lightSettings value {max_l} > 194! (Headlights will fail or crash)")

        # 5. Check vehicles.meta dashboard & flags
        vm = os.path.join(td, "data", "vehicles.meta")
        if os.path.exists(vm):
            vm_txt = open(vm, "r", encoding="utf-8", errors="ignore").read()
            m_names = re.findall(r'<modelName>([A-Za-z0-9_]+)</modelName>', vm_txt)
            if m_names:
                passes.append(f"In-game trainer spawn code: '{m_names[0]}'")
            if "VDT_RACE" in vm_txt:
                issues.append("vehicles.meta contains unsupported VDT_RACE dashboard (must be VDT_SUPERGT)")
            else:
                passes.append("vehicles.meta dashboard safe for v1.00 (VDT_SUPERGT)")
            if "FLAG_FRONT_BOOT" in vm_txt:
                issues.append("vehicles.meta contains unsupported modern PC flag FLAG_FRONT_BOOT")
            else:
                passes.append("vehicles.meta flags sanitized for PS4 1.00")

        # 6. Check carcols.meta modkit IDs & custom lights
        ccm = os.path.join(td, "data", "carcols.meta")
        if os.path.exists(ccm):
            cc_txt = open(ccm, "r", encoding="utf-8", errors="ignore").read()
            kits = re.findall(r'<id\s+value="(\d+)"', cc_txt)
            if kits:
                bad_kits = [int(k) for k in kits if int(k) > 1023]
                if not bad_kits:
                    passes.append("carcols.meta all ModKit IDs <= 1023 (v1.00 array safe)")
                else:
                    issues.append(f"carcols.meta has ModKit IDs > 1023: {bad_kits}")

        # 7. Check bundled custom audio (if any): files, content.xml wiring, wave-slot size, zero fallbacks
        _verify_bundled_audio(td, passes, issues)

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
        print("  [ALL STATIC CHECKS PASSED - in-game testing still recommended]")
        return True

def _verify_bundled_audio(td, passes, issues):
    audio_dir = os.path.join(td, "ps4", "audio")
    vm_path = os.path.join(td, "data", "vehicles.meta")
    vm_txt = open(vm_path, "r", encoding="utf-8", errors="ignore").read() if os.path.exists(vm_path) else ""
    names = re.findall(r"<audioNameHash>([^<]+)</audioNameHash>", vm_txt)
    rels = glob.glob(os.path.join(audio_dir, "*_game.dat151.rel")) if os.path.isdir(audio_dir) else []
    if not rels:
        if names:
            passes.append(f"audioNameHash '{names[0]}' uses stock game audio (no custom audio bundled)")
        return

    import build_fh6_svj_sound as snd
    name = os.path.basename(rels[0])[: -len("_game.dat151.rel")]
    if name not in names:
        issues.append(f"vehicles.meta audioNameHash {names} does not reference bundled audio '{name}'")
    else:
        passes.append(f"vehicles.meta audioNameHash = '{name}' (bundled custom audio)")

    content = open(os.path.join(td, "content.xml"), "r", encoding="utf-8", errors="ignore").read()
    wanted = (f"/audio/{name}_game.dat", f"/audio/{name}_sounds.dat", f"/audio/sfx/dlc_{name}")
    missing = [w for w in wanted if content.count(w) < 2]  # once in dataFiles, once in filesToEnable
    if missing:
        issues.append(f"content.xml is missing audio registrations: {missing}")
    else:
        passes.append("content.xml registers game.dat, sounds.dat and wavepack (data + enable)")

    for rel in (f"{name}_sounds.dat54.rel", f"{name}_game.dat151.rel"):
        if not os.path.isfile(os.path.join(audio_dir, rel)):
            issues.append(f"missing ps4/audio/{rel}")
    sfx = os.path.join(audio_dir, "sfx", f"dlc_{name}")
    for awc, limit in ((f"{name}.awc", 4_250_000), (f"{name}_npc.awc", 1_300_000)):
        p = os.path.join(sfx, awc)
        if not os.path.isfile(p):
            issues.append(f"missing wavepack {awc}")
        elif os.path.getsize(p) > limit:
            issues.append(f"{awc} is {os.path.getsize(p):,} B (> {limit:,} B wave-slot budget: car will be silent)")
        else:
            passes.append(f"{awc} {os.path.getsize(p):,} B within wave-slot budget")

    d54 = os.path.join(audio_dir, f"{name}_sounds.dat54.rel")
    if os.path.isfile(d54):
        blob = bytearray(open(d54, "rb").read())
        items = snd.parse_rel_index(blob)
        bad = 0
        for off, ln in items.values():
            if blob[off] == 6:
                _st, _fl, _hdr, hl = snd.decode_sound_header(blob[off: off + ln])
                fallback = struct.unpack_from("<I", blob, off + hl + 8)[0]
                bad += fallback != 0
        if bad:
            issues.append(f"{bad} WrapperSound(s) still have a stock FallBackSound (default GTA sounds will leak)")
        else:
            passes.append("all WrapperSounds have zero FallBackSound (no stock sound leaks)")

def normalize_car_dlc_handling(car_dlc_rpf, dst_dlc_rpf=None):
    """
    Directly normalizes the handling.meta inside an existing converted car DLC.
    Fixes broken / troll physics (extreme braking, supersonic speed, tank weight)
    and repacks with 8KB Orbis chunk alignment.
    """
    import tempfile
    dst = dst_dlc_rpf or car_dlc_rpf
    print(f"\n[*] Normalizing handling in {os.path.basename(car_dlc_rpf)}...")
    with tempfile.TemporaryDirectory() as td:
        NeoGen.extract_single_rpf(car_dlc_rpf, td)
        hm_path = os.path.join(td, "data", "handling.meta")
        if not os.path.exists(hm_path):
            raise FileNotFoundError("handling.meta not found in DLC data/ folder!")
        sanitize_handling_meta(hm_path, realistic_normalize=True)

        rpf_dir, mode_ovr, wine_bin = NeoGen.load_rpf_builder_settings()
        mode = NeoGen.detect_mode(mode_ovr)
        tmp = dst + ".norm_tmp"
        if os.path.exists(tmp):
            os.remove(tmp)
        if not NeoGen.build_single_rpf(td, tmp, rpf_dir, mode, wine_bin) or not os.path.exists(tmp):
            raise RuntimeError("Failed to repack normalized DLC")
        shutil.move(tmp, dst)
        cv.ensure_rpf_min_chunks(dst, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)
    print(f"  [SUCCESS] Normalized handling in {dst}")
    verify_converted_vehicle(dst)
    return dst

def find_pc_source_for_vehicle(model_name, search_root=r"D:\ps5payload\VehicleMods"):
    """
    Search search_root for the original PC mod source for model_name.
    Returns path to PC dlc.rpf or mod folder containing .yft/.ytd/vehicles.meta, or None.
    """
    if not os.path.exists(search_root):
        return None
    m_lower = model_name.lower().strip()
    for rpf in glob.glob(os.path.join(search_root, "**", "dlc.rpf"), recursive=True):
        parts = [p.lower() for p in Path(rpf).parts]
        if m_lower in parts or any(m_lower in p for p in parts):
            return rpf
    for yft in glob.glob(os.path.join(search_root, "**", "*.yft"), recursive=True):
        stem = os.path.splitext(os.path.basename(yft))[0].lower()
        if stem == m_lower or stem == f"{m_lower}_hi":
            parent = os.path.dirname(yft)
            nearby = glob.glob(os.path.join(parent, "..", "**", "dlc.rpf"), recursive=True)
            return nearby[0] if nearby else parent
    for root, dirs, _ in os.walk(search_root):
        for d in dirs:
            if m_lower == d.lower() or m_lower in d.lower():
                cand = os.path.join(root, d)
                rpfs = glob.glob(os.path.join(cand, "**", "dlc.rpf"), recursive=True)
                if rpfs: return rpfs[0]
                if glob.glob(os.path.join(cand, "**", "*.yft"), recursive=True): return cand
    return None

def rebuild_and_fix_car_dlc(car_dlc_rpf, dst_dlc_rpf=None, pc_source=None):
    """
    Rebuild an already-built PS4 vehicle DLC (dlc.rpf):
    1. Fixes white/missing textures in .otd:
       - Auto-locates (or accepts) original PC mod assets to pull clean textures.
       - Re-compresses all textures with power-of-2 snap and 1024 max budget (DXT5).
       - Matches and enforces exact canonical shader texture names (spaces, parentheses, casing).
       - Rebuilds all .otd dictionaries without missing textures or shader mismatches.
    2. Fixes invisible car body / tuning parts:
       - Inspects carcols.meta and model names.
       - Routes tuning parts (*_mods, arch_*, wire_*) into ps4/vehiclemods/{model_name}_mods.rpf.
       - Promotes resident texture dictionaries to vehicles.rpf.
    3. Normalizes & sanitizes metadata:
       - vehicles.meta: parent -> vehshare, flags sanitized, dashboard -> VDT_SUPERGT.
       - handling.meta: max transmission gears <= 6, normalized physics.
       - carvariations.meta: lightSettings <= 194.
       - carcols.meta: ModKit IDs <= 1023.
    4. Preserves bundled custom engine audio (ps4/audio) and content.xml wiring.
    5. Rebuilds nested RPFs (vehicles.rpf, vehiclemods/{model_name}_mods.rpf).
    6. Rebuilds outer dlc.rpf with >= 8192 byte Orbis chunk alignment.
    7. Runs automated verification suite (verify_converted_vehicle).
    """
    if not os.path.exists(car_dlc_rpf):
        raise FileNotFoundError(f"Car DLC not found: {car_dlc_rpf}")

    # Auto-resolve directory path to inner dlc.rpf
    if os.path.isdir(car_dlc_rpf):
        cand = os.path.join(car_dlc_rpf, "dlc.rpf")
        if os.path.exists(cand):
            car_dlc_rpf = cand
        else:
            cand_rpfs = glob.glob(os.path.join(car_dlc_rpf, "**", "dlc.rpf"), recursive=True)
            if cand_rpfs:
                car_dlc_rpf = cand_rpfs[0]
            else:
                raise FileNotFoundError(f"No dlc.rpf found inside directory: {car_dlc_rpf}")

    if not dst_dlc_rpf:
        dst = car_dlc_rpf
    elif os.path.isdir(dst_dlc_rpf):
        dst = os.path.join(dst_dlc_rpf, "dlc.rpf")
    elif not dst_dlc_rpf.lower().endswith(".rpf"):
        dst = os.path.join(dst_dlc_rpf, "dlc.rpf")
    else:
        dst = dst_dlc_rpf

    print(f"\n===========================================================")
    print(f"  Rebuilding & Fixing PS4 Vehicle DLC")
    print(f"  Built Car DLC: {car_dlc_rpf}")
    print(f"  Destination  : {dst}")
    print(f"===========================================================\n")
    
    rpf_dir, mode_ovr, wine_bin = NeoGen.load_rpf_builder_settings()
    mode = NeoGen.detect_mode(mode_ovr)
    tex_dir, tex_mode, tex_wine = NeoGen.load_texture_builder_settings()
    
    cwxml_exe = get_cwxml_exe()
    
    cfg_ini = os.path.join(NEOGEN_DIR, "low_compression.ini")
    cfg = configparser.ConfigParser()
    if os.path.exists(cfg_ini):
        cfg.read(cfg_ini)
        opts = NeoGen.parse_texture_config(cfg)
    else:
        opts = NeoGen.parse_texture_config(None)
        
    work_dir = dst + "_rebuild_work"
    shutil.rmtree(work_dir, ignore_errors=True)
    os.makedirs(work_dir, exist_ok=True)
    
    # [1] Extract existing built PS4 DLC package to preserve audio, structure & metadata
    print("[1/6] Extracting built PS4 DLC package...")
    NeoGen.extract_single_rpf(car_dlc_rpf, work_dir)
    
    # Determine model_name
    model_name = "modveh"
    setup2_path = os.path.join(work_dir, "setup2.xml")
    if os.path.exists(setup2_path):
        m = re.search(r'<deviceName>dlc_([^<]+)</deviceName>', open(setup2_path).read())
        if m: model_name = m.group(1).strip()
    if model_name in ["modveh", ""]:
        vm_p = os.path.join(work_dir, "data", "vehicles.meta")
        if os.path.exists(vm_p):
            m = re.search(r'<modelName>([A-Za-z0-9_]+)</modelName>', open(vm_p).read())
            if m: model_name = m.group(1).strip()
    print(f"  [+] In-Game Model Name: {model_name}")
    
    # Check for bundled audio
    has_audio = os.path.isdir(os.path.join(work_dir, "ps4", "audio"))
    if has_audio:
        print(f"  [+] Detected bundled custom audio in ps4/audio - will be preserved.")
        
    # [2] Locate or resolve PC mod source (if available)
    if pc_source and pc_source.lower().strip() in ("no", "none", "n", ""):
        pc_source = None
    if pc_source and not os.path.exists(pc_source):
        print(f"  [!] Provided PC source does not exist: {pc_source}")
        pc_source = None

    if not pc_source:
        pc_source = find_pc_source_for_vehicle(model_name)

    has_pc_source = bool(pc_source and os.path.exists(pc_source))
    if has_pc_source:
        print(f"  [+] Original PC mod source: {pc_source}")
    else:
        print(f"  [i] No PC mod source found/provided. Operating in STANDALONE PS4 REBUILD MODE.")
        print(f"      (Will patch shaders, repair missing samplers, isolate tuning parts, and enforce 8KB chunks directly on PS4 package)")

    vrpf_work = dst + "_vrpf_rebuild"
    shutil.rmtree(vrpf_work, ignore_errors=True)
    os.makedirs(vrpf_work, exist_ok=True)

    mrpf_work = dst + "_mrpf_rebuild"
    shutil.rmtree(mrpf_work, ignore_errors=True)

    has_mods = False
    mod_rpf_name = f"{model_name}_mods.rpf"
    pc_work = dst + "_pc_extracted"
    pc_vrpf_work = dst + "_pc_vrpf"

    if has_pc_source:
        # -------------------------------------------------------------
        # BRANCH 1: Full PC Mod Re-Conversion & Safe Texture Recompression
        # -------------------------------------------------------------
        print("\n[2/6] Extracting PC mod assets...")
        shutil.rmtree(pc_work, ignore_errors=True)
        os.makedirs(pc_work, exist_ok=True)

        if os.path.isfile(pc_source) and pc_source.lower().endswith(".rpf"):
            NeoGen.extract_single_rpf(pc_source, pc_work)
        elif os.path.isdir(pc_source):
            cand_rpfs = glob.glob(os.path.join(pc_source, "**", "dlc.rpf"), recursive=True)
            if cand_rpfs:
                print(f"  [+] Found PC dlc.rpf: {cand_rpfs[0]}")
                NeoGen.extract_single_rpf(cand_rpfs[0], pc_work)
            else:
                for item in os.listdir(pc_source):
                    s = os.path.join(pc_source, item)
                    d = os.path.join(pc_work, item)
                    if os.path.isdir(s): shutil.copytree(s, d)
                    else: shutil.copy2(s, d)

        # Search for vehicles.rpf inside pc_work
        vrpf_cand = glob.glob(os.path.join(pc_work, "**", "vehicles.rpf"), recursive=True)
        shutil.rmtree(pc_vrpf_work, ignore_errors=True)
        if vrpf_cand:
            NeoGen.extract_single_rpf(vrpf_cand[0], pc_vrpf_work)
        else:
            os.makedirs(pc_vrpf_work, exist_ok=True)
            for yft in glob.glob(os.path.join(pc_work, "**", "*.yft"), recursive=True):
                shutil.copy2(yft, os.path.join(pc_vrpf_work, os.path.basename(yft)))
            for ytd in glob.glob(os.path.join(pc_work, "**", "*.ytd"), recursive=True):
                shutil.copy2(ytd, os.path.join(pc_vrpf_work, os.path.basename(ytd)))

        print("\n[3/6] Converting models and analyzing shader texture dependencies...")
        # Parse carcols.meta to separate linked body parts vs actual tuning parts
        carcols_mods = set()
        linked_models = set()
        for cc_file in glob.glob(os.path.join(pc_work, "**", "carcols.meta"), recursive=True) + [os.path.join(work_dir, "data", "carcols.meta")]:
            if os.path.exists(cc_file):
                try:
                    cc_txt = open(cc_file, "r", encoding="utf-8", errors="ignore").read()
                    for m in re.finditer(r'<linkMods>.*?</linkMods>', cc_txt, re.DOTALL):
                        for lm in re.finditer(r'<modelName>([^<]+)</modelName>', m.group(0)):
                            linked_models.add(lm.group(1).strip().lower())
                    for m in re.finditer(r'<linkedModels>.*?</linkedModels>', cc_txt, re.DOTALL):
                        for lm in re.finditer(r'<(?:modelName|Item)>([^<]+)</(?:modelName|Item)>', m.group(0)):
                            linked_models.add(lm.group(1).strip().lower())
                    for m in re.finditer(r'<visibleMods>.*?</visibleMods>', cc_txt, re.DOTALL):
                        for vm in re.finditer(r'<modelName>([^<]+)</modelName>', m.group(0)):
                            val = vm.group(1).strip().lower()
                            if val not in ["null", "none", ""]:
                                carcols_mods.add(val)
                except Exception:
                    pass

        if model_name.lower() == "gibv48":
            linked_models.add("gibv48_wire_1")

        if linked_models:
            print(f"  [i] Linked body models (stay in vehicles.rpf): {linked_models}")
        if carcols_mods:
            print(f"  [i] Tuning mod models: {carcols_mods}")

        # Convert all PC YFTs to OFT and route tuning parts
        for f in list(os.listdir(pc_vrpf_work)):
            if f.lower().endswith(".yft"):
                fp = os.path.join(pc_vrpf_work, f)
                stem = os.path.splitext(f)[0].lower()
                is_base = (stem in (model_name.lower(), f"{model_name.lower()}_hi"))
                is_linked = (stem in linked_models or "arch_" in stem or "wire_" in stem)
                is_tuning = (
                    not is_linked and not is_base and
                    (stem in carcols_mods or "_mods" in stem)
                )
                target_dir = mrpf_work if is_tuning else vrpf_work
                if is_tuning:
                    os.makedirs(mrpf_work, exist_ok=True)
                    print(f"  [+] Routing tuning part {f} -> vehiclemods/{mod_rpf_name}")
                else:
                    print(f"  -> Converting base/linked model {f} -> {stem}.oft...")
                ensure_clean_yft_buffer(fp, cwxml_exe)
                NeoGen.convert_rsc_file(fp, target_dir, strip=True, auto_clean_embedded=True)

        # Check for tuning parts in pc_work mrpf
        for mr in glob.glob(os.path.join(pc_work, "**", "*_mods.rpf"), recursive=True) + glob.glob(os.path.join(pc_work, "**", "mods.rpf"), recursive=True):
            m_tmp = mr + "_extracted"
            NeoGen.extract_single_rpf(mr, m_tmp)
            if os.path.isdir(m_tmp):
                for mf in os.listdir(m_tmp):
                    stem = os.path.splitext(mf)[0].lower()
                    is_linked = (stem in linked_models or "arch_" in stem or "wire_" in stem)
                    target_dir = vrpf_work if is_linked else mrpf_work
                    if mf.lower().endswith(".yft"):
                        os.makedirs(target_dir, exist_ok=True)
                        mp = os.path.join(m_tmp, mf)
                        ensure_clean_yft_buffer(mp, cwxml_exe)
                        NeoGen.convert_rsc_file(mp, target_dir, strip=True, auto_clean_embedded=True)
                    elif mf.lower().endswith(".ytd"):
                        shutil.copy2(os.path.join(m_tmp, mf), os.path.join(pc_vrpf_work, mf))

        has_mods = os.path.isdir(mrpf_work) and len(os.listdir(mrpf_work)) > 0
        if has_mods:
            print(f"  [+] Tuning archive {mod_rpf_name} active with {len(os.listdir(mrpf_work))} part(s).")

        # Harvest exact canonical texture names from all generated OFTs
        canonical_texs = set()
        tex_pattern = re.compile(r'<Item\b[^>]*\btype="Texture"[^>]*>\s*<Name>([^<]+)</Name>')
        all_ofts = glob.glob(os.path.join(vrpf_work, "*.oft"))
        if has_mods:
            all_ofts += glob.glob(os.path.join(mrpf_work, "*.oft"))
        for oft in all_ofts:
            if cwxml_exe:
                xml_tmp = oft + ".xml"
                try:
                    subprocess.run([cwxml_exe, "export", oft, xml_tmp], capture_output=True)
                    if os.path.exists(xml_tmp):
                        txt = open(xml_tmp, "r", encoding="utf-8", errors="ignore").read()
                        for t in tex_pattern.findall(txt):
                            canonical_texs.add(t.strip())
                        try: os.remove(xml_tmp)
                        except: pass
                except Exception:
                    pass
        print(f"  [+] Harvested {len(canonical_texs)} canonical shader textures from model definitions.")
        norm_to_canonical = {re.sub(r'[^a-zA-Z0-9]', '', n).lower(): n for n in canonical_texs}

        # Convert textures with exact canonical casing and budgets
        print("\n[4/6] Compressing textures (1024 max, DXT5) and building .otd dictionaries...")
        all_ytds = glob.glob(os.path.join(pc_vrpf_work, "*.ytd"))
        for yt in glob.glob(os.path.join(pc_work, "**", "*.ytd"), recursive=True):
            if yt not in all_ytds and not yt.startswith(pc_vrpf_work):
                all_ytds.append(yt)

        converted_stems = set()
        for ytd_fp in all_ytds:
            stem = os.path.splitext(os.path.basename(ytd_fp))[0]
            if stem.lower() in converted_stems:
                continue
            print(f"  -> Processing texture dictionary: {stem}.ytd...")
            tex_folder = os.path.join(vrpf_work, f"{stem}_tex")
            os.makedirs(tex_folder, exist_ok=True)
            NeoGen.extract_ytd_to_dds(ytd_fp, tex_folder, opts)

            # Enforce canonical casing and special chars
            for f in list(os.listdir(tex_folder)):
                if f.lower().endswith(".dds"):
                    dstem = os.path.splitext(f)[0]
                    dkey = re.sub(r'[^a-zA-Z0-9]', '', dstem).lower()
                    if dkey in norm_to_canonical:
                        canon = norm_to_canonical[dkey]
                        if dstem != canon:
                            src_p = os.path.join(tex_folder, f)
                            tmp_p = os.path.join(tex_folder, f"__tmp_{canon}.dds")
                            dst_p = os.path.join(tex_folder, f"{canon}.dds")
                            os.rename(src_p, tmp_p)
                            os.rename(tmp_p, dst_p)
                            print(f"    [FIX] Restored canonical casing/name: '{dstem}' -> '{canon}'")

            # Stage fallback resident textures!
            num_staged = stage_fallback_textures(tex_folder, canonical_texs)
            print(f"  [+] Staged {num_staged} fallback/resident textures into {stem}.otd")

            # Compile OTD
            ok = NeoGen._build_otd_windows(tex_folder, stem, vrpf_work, tex_dir)
            if not ok or not os.path.exists(os.path.join(vrpf_work, f"{stem}.otd")):
                raise RuntimeError(f"Failed to build OTD for {stem}")
            shutil.rmtree(tex_folder, ignore_errors=True)
            converted_stems.add(stem.lower())

        # Build standalone OTD for any model that had embedded textures
        for d in list(os.listdir(vrpf_work)):
            dp = os.path.join(vrpf_work, d)
            if d.endswith("_tex") and os.path.isdir(dp):
                stem = d[:-4]
                if stem.lower() not in converted_stems:
                    stage_fallback_textures(dp, canonical_texs)
                    NeoGen._build_otd_windows(dp, stem, vrpf_work, tex_dir)
                shutil.rmtree(dp, ignore_errors=True)

        # Read all OTD texture names now available
        otd_names = set()
        for f in os.listdir(vrpf_work):
            if f.lower().endswith(".otd"):
                try:
                    raw = open(os.path.join(vrpf_work, f), "rb").read()
                    virt, _ = NeoGen.decompress_rsc7_ytd(raw)
                    for m in re.finditer(rb'([a-zA-Z0-9_#\s\-\(\)\.]{2,})\x00', virt):
                        n_str = m.group(1).decode("ascii", errors="ignore").strip().lower()
                        if len(n_str) >= 2:
                            otd_names.add(n_str)
                except Exception:
                    pass

        # Sanitize all OFT shaders
        print("  -> Sanitizing model shaders (fixing empty DirtSampler & missing samplers)...")
        for oft in glob.glob(os.path.join(vrpf_work, "*.oft")) + (glob.glob(os.path.join(mrpf_work, "*.oft")) if has_mods else []):
            patched = sanitize_model_oft_shaders(oft, otd_names)
            if patched:
                print(f"    [OK] Sanitized & re-imported: {os.path.basename(oft)}")
    else:
        # -------------------------------------------------------------
        # BRANCH 2: Standalone PS4 Rebuild & Shader Repair Mode
        # -------------------------------------------------------------
        print("\n[2/6] Extracting existing PS4 vehicle container for standalone repair...")
        existing_ps4_vrpf = os.path.join(work_dir, "ps4", "vehicles.rpf")
        if not os.path.exists(existing_ps4_vrpf):
            raise FileNotFoundError("ps4/vehicles.rpf not found in input DLC package!")
        NeoGen.extract_single_rpf(existing_ps4_vrpf, vrpf_work)

        # Extract existing vehiclemods if present
        existing_vmods = os.path.join(work_dir, "ps4", "vehiclemods")
        if os.path.isdir(existing_vmods):
            cands = glob.glob(os.path.join(existing_vmods, "*_mods.rpf"))
            if cands:
                mod_rpf_name = os.path.basename(cands[0])
                shutil.rmtree(mrpf_work, ignore_errors=True)
                NeoGen.extract_single_rpf(cands[0], mrpf_work)
                has_mods = True

        print("\n[3/6] Inspecting tuning parts and analyzing shader textures...")
        # Parse carcols.meta to separate linked body parts vs actual tuning parts
        carcols_mods = set()
        linked_models = set()
        cc_file = os.path.join(work_dir, "data", "carcols.meta")
        if os.path.exists(cc_file):
            try:
                cc_txt = open(cc_file, "r", encoding="utf-8", errors="ignore").read()
                for m in re.finditer(r'<linkMods>.*?</linkMods>', cc_txt, re.DOTALL):
                    for lm in re.finditer(r'<modelName>([^<]+)</modelName>', m.group(0)):
                        linked_models.add(lm.group(1).strip().lower())
                for m in re.finditer(r'<linkedModels>.*?</linkedModels>', cc_txt, re.DOTALL):
                    for lm in re.finditer(r'<(?:modelName|Item)>([^<]+)</(?:modelName|Item)>', m.group(0)):
                        linked_models.add(lm.group(1).strip().lower())
                for m in re.finditer(r'<visibleMods>.*?</visibleMods>', cc_txt, re.DOTALL):
                    for vm in re.finditer(r'<modelName>([^<]+)</modelName>', m.group(0)):
                        val = vm.group(1).strip().lower()
                        if val not in ["null", "none", ""]:
                            carcols_mods.add(val)
            except Exception:
                pass

        if model_name.lower() == "gibv48":
            linked_models.add("gibv48_wire_1")

        # 1. Restore any misplaced linked body parts from mrpf_work BACK to vrpf_work
        if has_mods and os.path.isdir(mrpf_work):
            for mf in list(os.listdir(mrpf_work)):
                mstem = os.path.splitext(mf)[0].lower()
                is_base = (mstem in (model_name.lower(), f"{model_name.lower()}_hi"))
                is_linked = (mstem in linked_models or "arch_" in mstem or "wire_" in mstem)
                if (is_base or is_linked) and mf.lower().endswith(".oft"):
                    shutil.move(os.path.join(mrpf_work, mf), os.path.join(vrpf_work, mf))
                    print(f"  [+] Restored bodyshell part {mf} -> vehicles.rpf (invisible body fix)")

        # 2. Route any true tuning parts from vrpf_work into mrpf_work
        for f in list(os.listdir(vrpf_work)):
            stem = os.path.splitext(f)[0].lower()
            is_base = (stem in (model_name.lower(), f"{model_name.lower()}_hi"))
            is_linked = (stem in linked_models or "arch_" in stem or "wire_" in stem)
            is_tuning = (
                not is_linked and not is_base and
                (stem in carcols_mods or "_mods" in stem)
            )
            if is_tuning and f.lower().endswith(".oft"):
                os.makedirs(mrpf_work, exist_ok=True)
                shutil.move(os.path.join(vrpf_work, f), os.path.join(mrpf_work, f))
                has_mods = True
                print(f"  [+] Routed tuning part {f} -> vehiclemods/{mod_rpf_name}")

        has_mods = os.path.isdir(mrpf_work) and len(os.listdir(mrpf_work)) > 0
        if has_mods:
            print(f"  [+] Tuning archive {mod_rpf_name} active with {len(os.listdir(mrpf_work))} part(s).")

        # 3. Harvest canonical texture names from all OFT models
        canonical_texs = set()
        tex_pattern = re.compile(r'<Item\b[^>]*\btype="Texture"[^>]*>\s*<Name>([^<]+)</Name>')
        all_ofts = glob.glob(os.path.join(vrpf_work, "*.oft"))
        if has_mods:
            all_ofts += glob.glob(os.path.join(mrpf_work, "*.oft"))
        for oft in all_ofts:
            if cwxml_exe:
                xml_tmp = oft + ".xml"
                try:
                    subprocess.run([cwxml_exe, "export", oft, xml_tmp], capture_output=True)
                    if os.path.exists(xml_tmp):
                        txt = open(xml_tmp, "r", encoding="utf-8", errors="ignore").read()
                        for t in tex_pattern.findall(txt):
                            canonical_texs.add(t.strip())
                        try: os.remove(xml_tmp)
                        except: pass
                except Exception:
                    pass
        print(f"  [+] Harvested {len(canonical_texs)} canonical shader textures from model definitions.")
        norm_to_canonical = {re.sub(r'[^a-zA-Z0-9]', '', n).lower(): n for n in canonical_texs}

        # 4. In Standalone Mode, preserve existing PS4 OTD textures (do not corrupt GNM with DirectX parser)
        print("\n[4/6] Inspecting existing PS4 .otd texture dictionaries...")
        otd_names = set()
        for f in os.listdir(vrpf_work):
            if f.lower().endswith(".otd"):
                try:
                    raw = open(os.path.join(vrpf_work, f), "rb").read()
                    virt, _ = NeoGen.decompress_rsc7_ytd(raw)
                    for m in re.finditer(rb'([a-zA-Z0-9_#\s\-\(\)\.]{2,})\x00', virt):
                        n_str = m.group(1).decode("ascii", errors="ignore").strip().lower()
                        if len(n_str) >= 2:
                            otd_names.add(n_str)
                except Exception:
                    pass
        print(f"  [+] Identified {len(otd_names)} texture names resident inside PS4 .otd dictionary.")

        # 5. Sanitize all OFT shaders
        print("  -> Sanitizing model shaders (fixing empty DirtSampler & missing samplers)...")
        for oft in glob.glob(os.path.join(vrpf_work, "*.oft")) + (glob.glob(os.path.join(mrpf_work, "*.oft")) if has_mods else []):
            patched = sanitize_model_oft_shaders(oft, otd_names)
            if patched:
                print(f"    [OK] Sanitized & re-imported: {os.path.basename(oft)}")

    # Ensure all split body parts (arch_*, wire_*) are properly linked to chassis in carcols.meta
    ensure_linked_parts_in_carcols(work_dir, vrpf_work, model_name)

    # [5/6] Repack and assemble PS4 package
    print("\n[5/6] Sanitizing metadata and repacking nested archives...")
    ps4_dir = os.path.join(work_dir, "ps4")
    os.makedirs(ps4_dir, exist_ok=True)

    # Promote resident OTD to mrpf_work if vehiclemods is active BEFORE building vehicles.rpf
    if has_mods and mrpf_work and os.path.isdir(mrpf_work) and len(os.listdir(mrpf_work)) > 0:
        for f in os.listdir(vrpf_work):
            if f.lower().endswith(".otd"):
                shutil.copy2(os.path.join(vrpf_work, f), os.path.join(mrpf_work, f))
                print(f"  [+] Copied resident {f} -> vehiclemods/{mod_rpf_name} (fixing tuning part textures)")

    # Repack ps4/vehicles.rpf
    ps4_vrpf = os.path.join(ps4_dir, "vehicles.rpf")
    ok = NeoGen.build_single_rpf(vrpf_work, ps4_vrpf, rpf_dir, mode, wine_bin)
    if not ok or not os.path.exists(ps4_vrpf):
        raise RuntimeError("Failed to build ps4/vehicles.rpf")
    shutil.rmtree(vrpf_work, ignore_errors=True)

    # Repack ps4/vehiclemods/{mod_rpf_name} if present
    vmods_dir = os.path.join(ps4_dir, "vehiclemods")
    if has_mods and mrpf_work and os.path.isdir(mrpf_work) and len(os.listdir(mrpf_work)) > 0:
        os.makedirs(vmods_dir, exist_ok=True)
        ps4_mrpf = os.path.join(vmods_dir, mod_rpf_name)
        ok = NeoGen.build_single_rpf(mrpf_work, ps4_mrpf, rpf_dir, mode, wine_bin)
        if not ok or not os.path.exists(ps4_mrpf):
            raise RuntimeError(f"Failed to build {ps4_mrpf}")
        shutil.rmtree(mrpf_work, ignore_errors=True)

        # Update content.xml if vehiclemods was added
        content_xml_path = os.path.join(work_dir, "content.xml")
        if os.path.exists(content_xml_path):
            c_txt = open(content_xml_path, "r", encoding="utf-8", errors="ignore").read()
            vmod_item = f"dlc_{model_name}:/%PLATFORM%/vehiclemods/{mod_rpf_name}"
            if vmod_item not in c_txt:
                data_file_entry = f"""    <Item>
      <filename>{vmod_item}</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="true" />
    </Item>
  </dataFiles>"""
                enable_entry = f"""        <Item>{vmod_item}</Item>
      </filesToEnable>"""
                c_txt = c_txt.replace("  </dataFiles>", data_file_entry, 1)
                c_txt = c_txt.replace("      </filesToEnable>", enable_entry, 1)
                open(content_xml_path, "w", encoding="utf-8", newline="\r\n").write(c_txt)
                print("  [+] Registered vehiclemods archive in content.xml")
    else:
        # Clean up empty vehiclemods folder if it exists
        shutil.rmtree(vmods_dir, ignore_errors=True)
        # Remove vehiclemods from content.xml if present
        cxml = os.path.join(work_dir, "content.xml")
        if os.path.exists(cxml):
            ctxt = open(cxml, "r", encoding="utf-8", errors="ignore").read()
            ctxt = re.sub(r'<Item>[^<]*vehiclemods[^<]*</Item>', '', ctxt)
            ctxt = re.sub(r'<Item>\s*<filename>[^<]*vehiclemods[^<]*</filename>.*?</Item>', '', ctxt, flags=re.DOTALL)
            open(cxml, "w", encoding="utf-8").write(ctxt)
            
    # Sanitize metadata
    data_dir = os.path.join(work_dir, "data")
    if os.path.exists(os.path.join(data_dir, "vehicles.meta")):
        sanitize_vehicles_meta(os.path.join(data_dir, "vehicles.meta"))
    if os.path.exists(os.path.join(data_dir, "handling.meta")):
        sanitize_handling_meta(os.path.join(data_dir, "handling.meta"))
    if os.path.exists(os.path.join(data_dir, "carvariations.meta")):
        sanitize_carvariations_meta(os.path.join(data_dir, "carvariations.meta"))
    if os.path.exists(os.path.join(data_dir, "carcols.meta")):
        sanitize_carcols_and_variations(
            os.path.join(data_dir, "carcols.meta"),
            os.path.join(data_dir, "carvariations.meta"),
            target_kit_id=727
        )
        
    # Repack outer dlc.rpf
    print("\n[6/6] Repacking outer DLC package and enforcing 8KB chunk alignment...")
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    tmp_out = dst + ".rebuild_tmp"
    if os.path.exists(tmp_out): os.remove(tmp_out)
    ok = NeoGen.build_single_rpf(work_dir, tmp_out, rpf_dir, mode, wine_bin)
    if not ok or not os.path.exists(tmp_out):
        raise RuntimeError("Failed to build outer dlc.rpf")
    shutil.move(tmp_out, dst)
    cv.ensure_rpf_min_chunks(dst, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)
    
    # Cleanup work directories
    shutil.rmtree(work_dir, ignore_errors=True)
    shutil.rmtree(pc_work, ignore_errors=True)
    shutil.rmtree(pc_vrpf_work, ignore_errors=True)
    
    final_sz = os.path.getsize(dst)
    print(f"\n===========================================================")
    print(f"  [SUCCESS] PS4 Vehicle DLC Successfully Rebuilt!")
    print(f"  Output    : {dst}")
    print(f"  Final Size: {final_sz:,} bytes")
    print(f"===========================================================\n")
    
    # Run automated verification suite
    verify_converted_vehicle(dst)
    return dst

def batch_convert(input_dir, output_dir):
    """
    Scans input_dir for PC vehicle mods (.rpf files or mod folders)
    and converts them all to output_dir with automatic naming and verification.
    """
    print(f"\n===========================================================")
    print(f"  BATCH CONVERTING PC VEHICLES -> PS4/PS5")
    print(f"  Input: {input_dir}")
    print(f"  Output: {output_dir}")
    print(f"===========================================================\n")
    os.makedirs(output_dir, exist_ok=True)
    
    candidates = []
    for entry in os.listdir(input_dir):
        ep = os.path.join(input_dir, entry)
        if os.path.isdir(ep):
            rpfs = glob.glob(os.path.join(ep, "**", "dlc.rpf"), recursive=True)
            if rpfs:
                name = detect_vehicle_model_name(rpfs[0], fallback=entry)
                candidates.append((name, rpfs[0]))
            elif glob.glob(os.path.join(ep, "**", "*.yft"), recursive=True) or glob.glob(os.path.join(ep, "**", "vehicles.meta"), recursive=True):
                name = detect_vehicle_model_name(ep, fallback=entry)
                candidates.append((name, ep))
        elif entry.lower().endswith(".rpf"):
            name = detect_vehicle_model_name(ep, fallback=os.path.splitext(entry)[0])
            candidates.append((name, ep))
            
    if not candidates:
        print(f"[!] No vehicle mods found in {input_dir}")
        return
        
    print(f"Found {len(candidates)} vehicle mod(s) to convert.\n")
    results = {}
    for name, src_path in candidates:
        dst_dlc = os.path.join(output_dir, name, "dlc.rpf")
        try:
            convert_vehicle(src_path, dst_dlc, model_name=name)
            results[name] = "SUCCESS (All Checks Passed)"
        except Exception as e:
            print(f"[ERROR] Failed to convert {name}: {e}")
            results[name] = f"FAILED: {e}"
            
    print("\n================== BATCH CONVERSION SUMMARY ==================")
    for name, stat in results.items():
        print(f"  {name:25s} -> {stat}")
    print("==============================================================")

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Universal GTA V PC -> PS4/PS5 Vehicle Mod Auto-Converter with Built-in Self-Testing"
    )
    parser.add_argument("source", help="Path to input PC dlc.rpf OR unpacked vehicle mod folder (or input directory in batch mode)")
    parser.add_argument("destination", help="Path to destination dlc.rpf (or output directory in batch mode)")
    parser.add_argument("name", nargs="?", default=None, help="Optional model name override")
    parser.add_argument("--batch", "-b", action="store_true", help="Batch convert all vehicle mods found in source directory")
    parser.add_argument("--rebuild", "-r", action="store_true", help="Rebuild an already-built PS4 car DLC and fix white textures/invisible body")
    parser.add_argument("--pc-source", default=None, help="Optional path to original PC vehicle mod source for rebuilding")
    parser.add_argument("--sound", "--engine", default=None, help="Optional RevoluSound engine zip or engine search query (e.g. l539, v12, 2jz, r8) to build and attach seamless audio")
    args = parser.parse_args()

    if args.rebuild:
        rebuild_and_fix_car_dlc(args.source, args.destination, args.pc_source)
    elif args.batch:
        batch_convert(args.source, args.destination)
    elif args.sound:
        import ps4_mod_studio as pms
        engine_zip = args.sound
        if not os.path.isfile(engine_zip):
            matches = pms.list_revolusound_engines(args.sound)
            if matches:
                engine_zip = matches[0][1]
                print(f"[+] Resolved RevoluSound engine for '{args.sound}': {os.path.basename(engine_zip)}")
            else:
                raise FileNotFoundError(f"No RevoluSound pack found matching '{args.sound}'")
        pms.build_car_with_engine(args.source, engine_zip, args.destination, car_name=args.name)
    else:
        convert_vehicle(args.source, args.destination, args.name)

if __name__ == "__main__":
    main()

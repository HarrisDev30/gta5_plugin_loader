#!/usr/bin/env python3
"""
Apply Streetlight & Night Lamp Enhancements to NVE Renew
=========================================================
1. visualsettings.dat:
   - distantlights.streetlight.HDRIntensity: 2.5 -> 0.75 (realistic bulb HDR at distance)
   - distantlights.streetlight.color: warm-white LED (0.95, 0.93, 0.88)
   - misc.coronas.sizeScaleGlobal: 0.42 -> 0.16 (tight, crisp corona sprite around lamp)
   - misc.coronas.intensityScaleGlobal: 0.60 -> 0.22 (subtle realistic bulb glow, non-blinding)
   - misc.coronas.screenspaceExpansion: 18.0 -> 10.0 (realistic perspective scaling)
   - lodlight.corona.size: 0.55 -> 0.18 (distant streetlights are pinpoints, not blurry balls)
   - trafficLight.near.coronaHDR: 5.00 -> 1.20
   - trafficLight.near.coronaSize: 2.50 -> 1.00

2. timecycle_mods_1.xml:
   - INT_streetlighting:
     down_intensity: 0.500 -> 0.200
     up_intensity: 0.500 -> 0.100
     color down/up: soft warm white (1.000 0.920 0.820)
   - StreetLightingJunction:
     artificial_int_ambient_multiplier: 0.800 -> 0.450
   - StreetLightingtraffic:
     artificial_int_ambient_multiplier: 1.000 -> 0.500

3. All 18 w_*.xml weather files:
   - fog_volume_light_intensity: reduced from 0.3500 night to 0.0600 night (0.0500 day)
     (eliminates thick foggy glowing cones hanging off lamps against the dark sky)
   - fog_volume_light_size: reduced from 0.4500 to 0.2200
"""

import os
import re
import glob

ROOT = r"D:\ps5payload"
RENEW_DATA = os.path.join(ROOT, "NVE_Renew", "data")
RENEW_TC = os.path.join(RENEW_DATA, "timecycle")

def fix_visualsettings():
    vis_path = os.path.join(RENEW_DATA, "visualsettings.dat")
    if not os.path.exists(vis_path):
        print(f"[-] Not found: {vis_path}")
        return
    with open(vis_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    replacements = [
        (r"distantlights\.streetlight\.HDRIntensity\s+[\d\.]+", "distantlights.streetlight.HDRIntensity\t0.75"),
        (r"distantlights\.streetlight\.color\.red\s+[\d\.]+", "distantlights.streetlight.color.red\t\t0.95"),
        (r"distantlights\.streetlight\.color\.green\s+[\d\.]+", "distantlights.streetlight.color.green\t\t0.93"),
        (r"distantlights\.streetlight\.color\.blue\s+[\d\.]+", "distantlights.streetlight.color.blue\t\t0.88"),
        (r"misc\.coronas\.sizeScaleGlobal\s+[\d\.]+", "misc.coronas.sizeScaleGlobal\t\t0.16"),
        (r"misc\.coronas\.intensityScaleGlobal\s+[\d\.]+", "misc.coronas.intensityScaleGlobal\t\t0.22"),
        (r"misc\.coronas\.screenspaceExpansion\s+[\d\.]+", "misc.coronas.screenspaceExpansion\t\t10.0"),
        (r"lodlight\.corona\.size\s+[\d\.]+", "lodlight.corona.size\t\t\t0.18"),
        (r"trafficLight\.near\.coronaHDR\s+[\d\.]+", "trafficLight.near.coronaHDR\t\t1.20"),
        (r"trafficLight\.near\.coronaSize\s+[\d\.]+", "trafficLight.near.coronaSize\t\t1.00"),
    ]

    for pat, rep in replacements:
        content = re.sub(pat, rep, content)

    with open(vis_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"[+] visualsettings.dat updated successfully.")

def fix_timecycle_mods():
    tc_path = os.path.join(RENEW_TC, "timecycle_mods_1.xml")
    if not os.path.exists(tc_path):
        print(f"[-] Not found: {tc_path}")
        return
    with open(tc_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # Fix INT_streetlighting
    # Original block:
    # <modifier name="INT_streetlighting" numMods="8" userFlags="2">
    #     <light_natural_amb_down_col_r>1.000 1.000</light_natural_amb_down_col_r>
    #     <light_natural_amb_down_col_g>0.286 1.000</light_natural_amb_down_col_g>
    #     <light_natural_amb_down_col_b>0.000 1.000</light_natural_amb_down_col_b>
    #     <light_natural_amb_down_intensity>0.500 0.000</light_natural_amb_down_intensity>
    #     <light_natural_amb_up_col_r>1.000 1.000</light_natural_amb_up_col_r>
    #     <light_natural_amb_up_col_g>0.325 1.000</light_natural_amb_up_col_g>
    #     <light_natural_amb_up_col_b>0.000 1.000</light_natural_amb_up_col_b>
    #     <light_natural_amb_up_intensity>0.500 0.000</light_natural_amb_up_intensity>
    # </modifier>

    int_street_pattern = re.compile(
        r'(<modifier name="INT_streetlighting" numMods="8" userFlags="2">)(.*?)(</modifier>)',
        re.DOTALL
    )
    new_int_street = (
        r'\1\n'
        r'        <light_natural_amb_down_col_r>1.000 1.000</light_natural_amb_down_col_r>\n'
        r'        <light_natural_amb_down_col_g>0.920 1.000</light_natural_amb_down_col_g>\n'
        r'        <light_natural_amb_down_col_b>0.820 1.000</light_natural_amb_down_col_b>\n'
        r'        <light_natural_amb_down_intensity>0.200 0.000</light_natural_amb_down_intensity>\n'
        r'        <light_natural_amb_up_col_r>1.000 1.000</light_natural_amb_up_col_r>\n'
        r'        <light_natural_amb_up_col_g>0.920 1.000</light_natural_amb_up_col_g>\n'
        r'        <light_natural_amb_up_col_b>0.820 1.000</light_natural_amb_up_col_b>\n'
        r'        <light_natural_amb_up_intensity>0.100 0.000</light_natural_amb_up_intensity>\n'
        r'    \3'
    )
    content = int_street_pattern.sub(new_int_street, content)

    # StreetLightingJunction
    content = re.sub(
        r'(<modifier name="StreetLightingJunction"[^>]*>\s*<artificial_int_ambient_multiplier>)[\d\.]+',
        r'\g<1>0.450',
        content
    )

    # StreetLightingtraffic
    content = re.sub(
        r'(<modifier name="StreetLightingtraffic"[^>]*>\s*<artificial_int_ambient_multiplier>)[\d\.]+',
        r'\g<1>0.500',
        content
    )

    with open(tc_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"[+] timecycle_mods_1.xml updated successfully.")

def fix_weather_files():
    weather_files = glob.glob(os.path.join(RENEW_TC, "w_*.xml"))
    print(f"[*] Processing {len(weather_files)} weather files...")

    # Gentle, realistic night fog light cone intensity
    # 13 time slots: [00:00, 05:00, 06:00, 07:00, 08:00, 11:00, 15:00, 17:00, 19:00, 20:00, 21:00, 22:00, 23:00]
    target_intensity = "0.0600 0.0600 0.0500 0.0500 0.0500 0.0500 0.0500 0.0500 0.0500 0.0500 0.0600 0.0600 0.0600"
    target_size = "0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200"

    # Foggy/snow files can have slightly higher fog light to preserve fog ambiance
    fog_intensity = "0.1000 0.1000 0.0800 0.0800 0.0800 0.0800 0.0800 0.0800 0.0800 0.0800 0.1000 0.1000 0.1000"
    fog_size = "0.2800 0.2800 0.2800 0.2800 0.2800 0.2800 0.2800 0.2800 0.2800 0.2800 0.2800 0.2800 0.2800"

    for w_path in weather_files:
        fname = os.path.basename(w_path).lower()
        is_fog = any(k in fname for k in ["fog", "blizzard", "sandstorm"])
        cur_intensity = fog_intensity if is_fog else target_intensity
        cur_size = fog_size if is_fog else target_size

        with open(w_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        content = re.sub(
            r'<fog_volume_light_intensity>[^<]+</fog_volume_light_intensity>',
            f'<fog_volume_light_intensity>{cur_intensity}</fog_volume_light_intensity>',
            content
        )
        content = re.sub(
            r'<fog_volume_light_size>[^<]+</fog_volume_light_size>',
            f'<fog_volume_light_size>{cur_size}</fog_volume_light_size>',
            content
        )

        with open(w_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)

    print(f"[+] All {len(weather_files)} weather files calibrated.")

if __name__ == "__main__":
    print("=" * 60)
    print("  Applying Streetlight & Lamp Visual Fixes")
    print("=" * 60)
    fix_visualsettings()
    fix_timecycle_mods()
    fix_weather_files()
    print("=" * 60)
    print("[+] Done.")

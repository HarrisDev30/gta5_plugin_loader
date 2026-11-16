#!/usr/bin/env python3
"""
Apply Weather-Calibrated Subtle Bloom to NVE Renew
===================================================
Tuning:
1. visualsettings.dat:
   - bloom.threshold.min: 0.20 (allows car paint reflections & building edges to bloom softly)
   - bloom.threshold.power: 0.35 (smooth knee transition)
   - bloom.blurblendmult.large: 0.32 (subtle wide cinematic diffusion)
   - bloom.blurblendmult.med: 0.72 (soft atmospheric wrap around architecture)
   - bloom.blurblendmult.small: 0.80 (tight, gorgeous specular highlight glow on cars)
   - misc.BloomIntensityClamp.HD: 4.50 (prevents blown-out whiteout)
   - misc.BloomIntensityClamp.SD: 4.50

2. Weather Timecycle Files (all 18 w_*.xml):
   Calibrates 13 time slots per weather type:
   - extrasunny: Peak golden sunlight bloom on vehicle body curves & bright building surfaces at noon/afternoon.
   - clear: Natural, balanced daytime specular radiance on chrome, paint, and architecture.
   - clouds / clearing: Soft diffused skylight bloom; velvety, dreamy lighting on surfaces.
   - overcast: Gentle ambient atmospheric bloom that cures flat, dull grey look.
   - rain / thunder / hurricane: Glossy wet paint and puddle reflection bloom.
   - smog / foggy: Atmospheric soft particle bloom.
   - snow / blizzard: Crisp high-albedo winter snow glare.
   - night slots (all weathers): Subtle, tight bloom preserving realistic dark skies and crisp streetlights.
"""

import os
import re
import glob

ROOT = r"D:\ps5payload"
RENEW_DATA = os.path.join(ROOT, "NVE_Renew", "data")
RENEW_TC = os.path.join(RENEW_DATA, "timecycle")

BLOOM_PRESETS = {
    # 1. Extra Sunny (vivid California sun, specular glare on cars & concrete)
    "extrasunny": {
        "thresh":       "0.2800 0.2200 0.1600 0.1700 0.1800 0.1800 0.1800 0.1700 0.1600 0.2200 0.2800 0.2800 0.2800",
        "thresh_width": "0.1200 0.1500 0.1800 0.1800 0.1600 0.1600 0.1600 0.1700 0.1800 0.1500 0.1200 0.1200 0.1200",
        "intensity":    "0.3200 0.3800 0.5200 0.5400 0.5600 0.5800 0.5800 0.5600 0.5200 0.4000 0.3200 0.3200 0.3200",
    },
    # 2. Clear (pure sky, natural balanced sheen on vehicles & architecture)
    "clear": {
        "thresh":       "0.3000 0.2400 0.1800 0.1900 0.2000 0.2000 0.2000 0.1900 0.1800 0.2400 0.3000 0.3000 0.3000",
        "thresh_width": "0.1200 0.1400 0.1600 0.1600 0.1600 0.1600 0.1600 0.1600 0.1600 0.1400 0.1200 0.1200 0.1200",
        "intensity":    "0.3000 0.3600 0.4800 0.5000 0.5200 0.5200 0.5200 0.5000 0.4800 0.3800 0.3000 0.3000 0.3000",
    },
    # 3. Clouds & Clearing (diffuse white skylight scattering across buildings & car bodies)
    "clouds": {
        "thresh":       "0.3000 0.2400 0.1700 0.1800 0.1800 0.1800 0.1800 0.1800 0.1700 0.2400 0.3000 0.3000 0.3000",
        "thresh_width": "0.1200 0.1600 0.2000 0.2000 0.2200 0.2200 0.2200 0.2000 0.2000 0.1600 0.1200 0.1200 0.1200",
        "intensity":    "0.2800 0.3600 0.4400 0.4600 0.4800 0.4800 0.4800 0.4600 0.4400 0.3600 0.2800 0.2800 0.2800",
    },
    "clearing": {
        "thresh":       "0.3000 0.2400 0.1700 0.1800 0.1800 0.1800 0.1800 0.1800 0.1700 0.2400 0.3000 0.3000 0.3000",
        "thresh_width": "0.1200 0.1600 0.2000 0.2000 0.2200 0.2200 0.2200 0.2000 0.2000 0.1600 0.1200 0.1200 0.1200",
        "intensity":    "0.2800 0.3600 0.4400 0.4600 0.4800 0.4800 0.4800 0.4600 0.4400 0.3600 0.2800 0.2800 0.2800",
    },
    # 4. Overcast (soft atmospheric glow prevents dull flat grey look)
    "overcast": {
        "thresh":       "0.3000 0.2400 0.1800 0.1700 0.1600 0.1600 0.1600 0.1700 0.1800 0.2400 0.3000 0.3000 0.3000",
        "thresh_width": "0.1400 0.1800 0.2200 0.2400 0.2500 0.2500 0.2500 0.2400 0.2200 0.1800 0.1400 0.1400 0.1400",
        "intensity":    "0.2600 0.3400 0.4000 0.4200 0.4400 0.4400 0.4400 0.4200 0.4000 0.3400 0.2600 0.2600 0.2600",
    },
    # 5. Rain / Thunder / Hurricane (wet lacquer gloss, glossy puddles, reflections)
    "rain": {
        "thresh":       "0.2400 0.2000 0.1500 0.1400 0.1400 0.1400 0.1400 0.1400 0.1500 0.2000 0.2400 0.2400 0.2400",
        "thresh_width": "0.1600 0.1800 0.2000 0.2200 0.2200 0.2200 0.2200 0.2200 0.2000 0.1800 0.1600 0.1600 0.1600",
        "intensity":    "0.3800 0.4200 0.5000 0.5200 0.5400 0.5400 0.5400 0.5200 0.5000 0.4400 0.3800 0.3800 0.3800",
    },
    "thunder": {
        "thresh":       "0.2400 0.2000 0.1500 0.1400 0.1400 0.1400 0.1400 0.1400 0.1500 0.2000 0.2400 0.2400 0.2400",
        "thresh_width": "0.1600 0.1800 0.2000 0.2200 0.2200 0.2200 0.2200 0.2200 0.2000 0.1800 0.1600 0.1600 0.1600",
        "intensity":    "0.3800 0.4200 0.5000 0.5200 0.5400 0.5400 0.5400 0.5200 0.5000 0.4400 0.3800 0.3800 0.3800",
    },
    "hurricane": {
        "thresh":       "0.2400 0.2000 0.1500 0.1400 0.1400 0.1400 0.1400 0.1400 0.1500 0.2000 0.2400 0.2400 0.2400",
        "thresh_width": "0.1600 0.1800 0.2000 0.2200 0.2200 0.2200 0.2200 0.2200 0.2000 0.1800 0.1600 0.1600 0.1600",
        "intensity":    "0.3800 0.4200 0.5000 0.5200 0.5400 0.5400 0.5400 0.5200 0.5000 0.4400 0.3800 0.3800 0.3800",
    },
    # 6. Smog / Foggy / Foggy Deep (atmospheric soft particle bloom)
    "smog": {
        "thresh":       "0.2800 0.2200 0.1700 0.1600 0.1600 0.1600 0.1600 0.1600 0.1700 0.2200 0.2800 0.2800 0.2800",
        "thresh_width": "0.1600 0.2000 0.2400 0.2600 0.2600 0.2600 0.2600 0.2600 0.2400 0.2000 0.1600 0.1600 0.1600",
        "intensity":    "0.3000 0.3600 0.4400 0.4600 0.4800 0.4800 0.4800 0.4600 0.4400 0.3600 0.3000 0.3000 0.3000",
    },
    "foggy": {
        "thresh":       "0.2600 0.2200 0.1700 0.1600 0.1600 0.1600 0.1600 0.1600 0.1700 0.2200 0.2600 0.2600 0.2600",
        "thresh_width": "0.1600 0.2000 0.2400 0.2600 0.2600 0.2600 0.2600 0.2600 0.2400 0.2000 0.1600 0.1600 0.1600",
        "intensity":    "0.3000 0.3600 0.4400 0.4600 0.4800 0.4800 0.4800 0.4600 0.4400 0.3600 0.3000 0.3000 0.3000",
    },
    "foggy_deep": {
        "thresh":       "0.2600 0.2200 0.1700 0.1600 0.1600 0.1600 0.1600 0.1600 0.1700 0.2200 0.2600 0.2600 0.2600",
        "thresh_width": "0.1600 0.2000 0.2400 0.2600 0.2600 0.2600 0.2600 0.2600 0.2400 0.2000 0.1600 0.1600 0.1600",
        "intensity":    "0.3000 0.3600 0.4400 0.4600 0.4800 0.4800 0.4800 0.4600 0.4400 0.3600 0.3000 0.3000 0.3000",
    },
    # 7. Snow / Snowlight / Blizzard / Xmas (high-albedo snow reflections)
    "snow": {
        "thresh":       "0.2600 0.2000 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.2000 0.2600 0.2600 0.2600",
        "thresh_width": "0.1400 0.1600 0.1800 0.2000 0.2000 0.2000 0.2000 0.2000 0.1800 0.1600 0.1400 0.1400 0.1400",
        "intensity":    "0.3200 0.4000 0.5000 0.5400 0.5600 0.5600 0.5600 0.5400 0.5000 0.4000 0.3200 0.3200 0.3200",
    },
    "snowlight": {
        "thresh":       "0.2600 0.2000 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.2000 0.2600 0.2600 0.2600",
        "thresh_width": "0.1400 0.1600 0.1800 0.2000 0.2000 0.2000 0.2000 0.2000 0.1800 0.1600 0.1400 0.1400 0.1400",
        "intensity":    "0.3200 0.4000 0.5000 0.5400 0.5600 0.5600 0.5600 0.5400 0.5000 0.4000 0.3200 0.3200 0.3200",
    },
    "blizzard": {
        "thresh":       "0.2600 0.2000 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.2000 0.2600 0.2600 0.2600",
        "thresh_width": "0.1400 0.1600 0.1800 0.2000 0.2000 0.2000 0.2000 0.2000 0.1800 0.1600 0.1400 0.1400 0.1400",
        "intensity":    "0.3200 0.4000 0.5000 0.5400 0.5600 0.5600 0.5600 0.5400 0.5000 0.4000 0.3200 0.3200 0.3200",
    },
    "xmas": {
        "thresh":       "0.2600 0.2000 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.1500 0.2000 0.2600 0.2600 0.2600",
        "thresh_width": "0.1400 0.1600 0.1800 0.2000 0.2000 0.2000 0.2000 0.2000 0.1800 0.1600 0.1400 0.1400 0.1400",
        "intensity":    "0.3200 0.4000 0.5000 0.5400 0.5600 0.5600 0.5600 0.5400 0.5000 0.4000 0.3200 0.3200 0.3200",
    },
    # 8. Neutral, Halloween, Sandstorm
    "neutral": {
        "thresh":       "0.3000 0.2400 0.1900 0.2000 0.2000 0.2000 0.2000 0.2000 0.1900 0.2400 0.3000 0.3000 0.3000",
        "thresh_width": "0.1200 0.1400 0.1600 0.1600 0.1600 0.1600 0.1600 0.1600 0.1600 0.1400 0.1200 0.1200 0.1200",
        "intensity":    "0.3000 0.3600 0.4600 0.4800 0.4800 0.4800 0.4800 0.4800 0.4600 0.3800 0.3000 0.3000 0.3000",
    },
    "halloween": {
        "thresh":       "0.2600 0.2200 0.1600 0.1600 0.1600 0.1600 0.1600 0.1600 0.1600 0.2200 0.2600 0.2600 0.2600",
        "thresh_width": "0.1400 0.1800 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.2200 0.1800 0.1400 0.1400 0.1400",
        "intensity":    "0.3400 0.3800 0.4400 0.4600 0.4600 0.4600 0.4600 0.4600 0.4400 0.3800 0.3400 0.3400 0.3400",
    },
    "sandstorm": {
        "thresh":       "0.2800 0.2200 0.1600 0.1500 0.1500 0.1500 0.1500 0.1500 0.1600 0.2200 0.2800 0.2800 0.2800",
        "thresh_width": "0.1400 0.1800 0.2400 0.2600 0.2600 0.2600 0.2600 0.2600 0.2400 0.1800 0.1400 0.1400 0.1400",
        "intensity":    "0.3200 0.3800 0.4800 0.5000 0.5200 0.5200 0.5200 0.5000 0.4800 0.3800 0.3200 0.3200 0.3200",
    },
}

def update_visualsettings(file_path):
    if not os.path.exists(file_path):
        return
    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    replacements = [
        (r"bloom\.threshold\.min\s+[\d\.]+", "bloom.threshold.min\t\t\t0.20"),
        (r"bloom\.threshold\.power\s+[\d\.]+", "bloom.threshold.power\t\t\t0.35"),
        (r"bloom\.blurblendmult\.large\s+[\d\.]+", "bloom.blurblendmult.large\t\t0.32"),
        (r"bloom\.blurblendmult\.med\s+[\d\.]+", "bloom.blurblendmult.med\t\t\t0.72"),
        (r"bloom\.blurblendmult\.small\s+[\d\.]+", "bloom.blurblendmult.small\t\t0.80"),
        (r"misc\.BloomIntensityClamp\.HD\s+[\d\.]+", "misc.BloomIntensityClamp.HD\t4.50"),
        (r"misc\.BloomIntensityClamp\.SD\s+[\d\.]+", "misc.BloomIntensityClamp.SD\t4.50"),
    ]

    for pat, rep in replacements:
        content = re.sub(pat, rep, content)

    with open(file_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"[+] visualsettings updated: {file_path}")

def update_weather_files():
    weather_files = glob.glob(os.path.join(RENEW_TC, "w_*.xml"))
    print(f"[*] Processing {len(weather_files)} weather timecycle files...")

    for w_path in weather_files:
        fname = os.path.basename(w_path).lower().replace(".xml", "").replace("w_", "")
        preset = BLOOM_PRESETS.get(fname)
        if not preset:
            # Fallback to clear
            preset = BLOOM_PRESETS["clear"]

        with open(w_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()

        # Update both GLOBAL and URBAN regions
        content = re.sub(
            r'<postfx_bright_pass_thresh_width>[^<]+</postfx_bright_pass_thresh_width>',
            f'<postfx_bright_pass_thresh_width>{preset["thresh_width"]}</postfx_bright_pass_thresh_width>',
            content
        )
        content = re.sub(
            r'<postfx_bright_pass_thresh>[^<]+</postfx_bright_pass_thresh>',
            f'<postfx_bright_pass_thresh>{preset["thresh"]}</postfx_bright_pass_thresh>',
            content
        )
        content = re.sub(
            r'<postfx_intensity_bloom>[^<]+</postfx_intensity_bloom>',
            f'<postfx_intensity_bloom>{preset["intensity"]}</postfx_intensity_bloom>',
            content
        )

        with open(w_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)

    print(f"[+] All {len(weather_files)} weather files calibrated with weather-dependent bloom curves.")

def main():
    print("=" * 60)
    print("  Applying Weather-Based Subtle Bloom Calibration")
    print("=" * 60)
    update_visualsettings(os.path.join(RENEW_DATA, "visualsettings.dat"))
    update_weather_files()
    print("=" * 60)
    print("[+] Done.")

if __name__ == "__main__":
    main()

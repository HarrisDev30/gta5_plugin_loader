#!/usr/bin/env python3
"""
PC ENB (enbseries.ini) & ReShade (*.ini) -> PS5 Native GTA V Timecycle Converter
================================================================================
Safely converts PC ENBSeries and ReShade preset parameters into RAGE engine
<modifier> blocks in timecycle_mods_1.xml without touching weather timecycles
(w_*.xml). 100% non-destructive: presets layer in memory via SET_TIMECYCLE_MODIFIER
and revert immediately when turned OFF or when the game exits.

Usage:
  1. Convert a PC enbseries.ini or ReShade.ini preset and inject into timecycle_mods_1.xml:
     python tools/convert_enb_reshade.py path/to/enbseries.ini --name MODHEN_CUSTOM_ENB --inject NVE_PS5_Ready/data/timecycle/timecycle_mods_1.xml

  2. Inject/refresh the 7 curated MODHEN_* ENB/ReShade presets in timecycle_mods_1.xml:
     python tools/convert_enb_reshade.py --inject-defaults NVE_PS5_Ready/data/timecycle/timecycle_mods_1.xml
"""

import argparse
import os
import re
import sys
from typing import Dict, List, Tuple


def clamp(val: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, val))


def parse_ini_loose(text: str) -> Dict[str, Dict[str, str]]:
    """Parses both standard ENB .ini and ReShade preprocessor/shader .ini files."""
    sections: Dict[str, Dict[str, str]] = {"GLOBAL": {}}
    cur_sec = "GLOBAL"
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("//") or line.startswith(";") or line.startswith("#"):
            continue
        if line.startswith("[") and "]" in line:
            cur_sec = line[1:line.index("]")].strip().upper()
            sections.setdefault(cur_sec, {})
            continue
        if "=" in line:
            k, v = line.split("=", 1)
            k = k.strip().lower()
            # Strip inline comments
            for cmt in ("//", ";"):
                if cmt in v:
                    v = v.split(cmt, 1)[0]
            sections.setdefault(cur_sec, {})[k] = v.strip()
    return sections


def parse_float_list(raw: str) -> List[float]:
    nums = re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", raw)
    return [float(x) for x in nums]


def convert_ini_to_rage_mods(ini_text: str) -> Tuple[List[Tuple[str, float, float]], Dict[str, float]]:
    """
    Maps PC ENB (enbseries.ini) and ReShade (*.ini) parameters to safe RAGE
    <timecycle_modifier_data> tags: (tag_name, val1, val2).
    """
    secs = parse_ini_loose(ini_text)
    flat: Dict[str, str] = {}
    for sec_dict in secs.values():
        for k, v in sec_dict.items():
            flat[k] = v

    # Default neutral-enhanced parameters
    exposure = 0.05
    bloom = 0.45
    thresh = 0.65
    thresh_w = 0.30
    saturation = 1.18
    contrast_filmic = 1.12
    ssao_inten = 1.85
    vig_inten = 0.18
    vig_radius = 14.0
    chrom_ab = 0.0
    noise = 0.0
    col_r, col_g, col_b = 1.00, 1.00, 1.00
    ss_int = 0.40
    refl_emissive = 1.65

    # --- 1. ENBSeries [COLORCORRECTION] / [BLOOM] / [GAMEBLOOM] / [VEHICLE] / [SSAO_SSIL] / [SUBSURFACESCATTERING] ---
    cc = secs.get("COLORCORRECTION", {})
    env = secs.get("ENVIRONMENT", secs.get("LIGHTNATURAL", {}))
    sky = secs.get("SKY", {})
    ssao = secs.get("SSAO_SSIL", {})
    gbloom = secs.get("GAMEBLOOM", {})
    veh = secs.get("VEHICLE", {})
    sss = secs.get("SUBSURFACESCATTERING", {})
    skylight = secs.get("SKYLIGHTING", {})

    if "brightness" in cc:
        vals = parse_float_list(cc["brightness"])
        if vals:
            exposure = clamp((vals[0] - 1.0) * 0.45, -0.60, 0.85)
    if "gammacurve" in cc:
        vals = parse_float_list(cc["gammacurve"])
        if vals:
            contrast_filmic = clamp(vals[0], 0.75, 1.65)
            # Higher gamma curve deepens shadows & lowers bright-pass threshold slightly
            thresh = clamp(0.85 - (vals[0] - 1.0) * 0.65, 0.35, 1.20)
    if "amountday" in secs.get("BLOOM", {}) or "ebloomamount" in flat:
        raw_b = secs.get("BLOOM", {}).get("amountday", flat.get("ebloomamount", "0.45"))
        vals = parse_float_list(raw_b)
        if vals and vals[0] > 0.10:
            bloom = clamp(vals[0] * 0.45 if vals[0] > 1.5 else vals[0], 0.05, 1.80)
    if "amountnight" in gbloom or "amountday" in gbloom:
        dn = parse_float_list(gbloom.get("amountday", "1.0"))
        nn = parse_float_list(gbloom.get("amountnight", "1.0"))
        avg_gb = ((dn[0] if dn else 1.0) + (nn[0] if nn else 1.0)) * 0.5
        bloom = clamp(avg_gb * 0.38, 0.15, 1.60)
    if "colorpowday" in cc or "contrast" in flat:
        raw_c = cc.get("colorpowday", flat.get("contrast", str(contrast_filmic)))
        vals = parse_float_list(raw_c)
        if vals:
            contrast_filmic = clamp(vals[0], 0.75, 1.65)
    if "aoamountday" in ssao or "aoamount" in ssao:
        vals = parse_float_list(ssao.get("aoamountday", ssao.get("aoamount", "0.7")))
        il_vals = parse_float_list(ssao.get("ilamountday", "1.0"))
        if vals:
            il_boost = clamp((il_vals[0] if il_vals else 1.0) * 0.18, 0.0, 1.2)
            ssao_inten = clamp(vals[0] * 2.2 + il_boost, 0.80, 3.80)
    if "reflectionamount" in veh or "metallicamount" in veh:
        ra = parse_float_list(veh.get("reflectionamount", "1.15"))
        ma = parse_float_list(veh.get("metallicamount", "1.5"))
        refl_emissive = clamp(((ra[0] if ra else 1.15) + (ma[0] if ma else 1.5) * 0.5), 1.0, 3.50)
    if "amount" in sss or "subdermalamount" in sss:
        sa = parse_float_list(sss.get("subdermalamount", sss.get("amount", "1.0")))
        if sa:
            ss_int = clamp(sa[0] * 0.22, 0.15, 1.10)
    if "directlightingintensityday" in env:
        vals = parse_float_list(env["directlightingintensityday"])
        if vals and "reflectionamount" not in veh:
            refl_emissive = clamp(vals[0] * 1.2, 0.80, 3.50)

    # --- 2. ReShade Shaders (Vibrance, Technicolor2, Levels, Curves, LumaSharpen, FakeHDR, Vignette, Bloom) ---
    if "vibrance" in flat:
        vals = parse_float_list(flat["vibrance"])
        if vals:
            saturation = clamp(1.0 + vals[0] * 0.65, 0.50, 1.75)
    if "saturation" in flat:
        vals = parse_float_list(flat["saturation"])
        if vals:
            v = vals[0]
            saturation = clamp(v if v > 0.3 else 1.0 + v, 0.50, 1.75)
    if "colorstrength" in flat:
        vals = parse_float_list(flat["colorstrength"])
        if len(vals) >= 3:
            col_r = clamp(vals[0], 0.60, 1.35)
            col_g = clamp(vals[1], 0.60, 1.35)
            col_b = clamp(vals[2], 0.60, 1.35)
    if "hdrpower" in flat:
        vals = parse_float_list(flat["hdrpower"])
        if vals:
            contrast_filmic = clamp(vals[0], 0.80, 1.65)
            thresh = clamp(1.05 - (vals[0] - 1.0) * 0.35, 0.30, 1.10)
    if "sharp_strength" in flat:
        vals = parse_float_list(flat["sharp_strength"])
        if vals:
            noise = clamp(vals[0] * 0.015, 0.0, 0.045)
    if "vignetteamount" in flat or "amount" in secs.get("VIGNETTE.FX", {}):
        raw_v = flat.get("vignetteamount", secs.get("VIGNETTE.FX", {}).get("amount", "0.18"))
        vals = parse_float_list(raw_v)
        if vals:
            vig_inten = clamp(abs(vals[0]) * 0.35, 0.0, 0.65)

    mods: List[Tuple[str, float, float]] = [
        ("postfx_exposure", clamp(exposure, -0.60, 0.85), 0.000),
        ("postfx_exposure_min", -0.800, 0.000),
        ("postfx_exposure_max", 1.850, 0.000),
        ("postfx_bright_pass_thresh_width", clamp(thresh_w, 0.10, 0.90), 0.000),
        ("postfx_bright_pass_thresh", clamp(thresh, 0.25, 1.50), 0.000),
        ("postfx_intensity_bloom", clamp(bloom, 0.05, 1.80), 0.000),
        ("postfx_desaturation", clamp(saturation, 0.40, 1.75), 0.000),
        ("postfx_filmic_0", clamp(contrast_filmic, 0.70, 1.70), 0.000),
        ("postfx_correct_col_r", clamp(col_r, 0.50, 1.40), 0.180),
        ("postfx_correct_col_g", clamp(col_g, 0.50, 1.40), 0.180),
        ("postfx_correct_col_b", clamp(col_b, 0.50, 1.40), 0.180),
        ("postfx_vignetting_intensity", clamp(vig_inten, 0.00, 0.75), 0.000),
        ("postfx_vignetting_radius", clamp(vig_radius, 4.0, 20.0), 0.000),
        ("postfx_vignetting_contrast", 0.350, 0.000),
        ("postfx_noise", clamp(noise, 0.00, 0.06), 0.000),
        ("chrom_aberration_coeff", clamp(chrom_ab, -0.02, 0.02), 0.000),
        ("ssao_inten", clamp(ssao_inten, 0.80, 4.00), 0.000),
        ("light_amb_occ_mult", 1.250, 0.000),
        ("light_amb_occ_mult_veh", 1.350, 0.000),
        ("light_ss_intensity", clamp(ss_int, 0.00, 1.20), 0.000),
        ("reflection_tweak_emissive", clamp(refl_emissive, 0.80, 4.00), 0.000),
        ("lens_artefacts_intensity", 0.220, 0.000),
    ]

    summary = {
        "exposure": exposure,
        "bloom": bloom,
        "thresh": thresh,
        "saturation": saturation,
        "contrast": contrast_filmic,
        "ssao": ssao_inten,
        "vignette": vig_inten,
    }
    return mods, summary


def build_modifier_xml(name: str, mods: List[Tuple[str, float, float]]) -> str:
    lines = [f'  <modifier name="{name}" numMods="{len(mods)}" userFlags="0">']
    for tag, v1, v2 in mods:
        lines.append(f"    <{tag}>{v1:.3f} {v2:.3f}</{tag}>")
    lines.append("  </modifier>")
    return "\n".join(lines)


def get_curated_modhen_presets() -> Dict[str, List[Tuple[str, float, float]]]:
    """Returns the 7 curated MODHEN_* ENB & ReShade presets for PS5."""
    return {
        "MODHEN_ENB_NVE": [
            ("postfx_exposure", 0.040, 0.000),
            ("postfx_exposure_min", -0.850, 0.000),
            ("postfx_exposure_max", 1.750, 0.000),
            ("postfx_bright_pass_thresh_width", 0.320, 0.000),
            ("postfx_bright_pass_thresh", 0.620, 0.000),
            ("postfx_intensity_bloom", 0.520, 0.000),
            ("postfx_desaturation", 1.220, 0.000),
            ("postfx_vignetting_intensity", 0.160, 0.000),
            ("postfx_vignetting_radius", 14.500, 0.000),
            ("postfx_vignetting_contrast", 0.300, 0.000),
            ("ssao_inten", 2.250, 0.000),
            ("light_amb_occ_mult", 1.350, 0.000),
            ("light_amb_occ_mult_veh", 1.450, 0.000),
            ("light_ss_intensity", 0.480, 0.000),
            ("reflection_tweak_emissive", 1.850, 0.000),
            ("lens_artefacts_intensity", 0.200, 0.000),
        ],
        "MODHEN_ENB_QUANTV": [
            ("postfx_exposure", 0.020, 0.000),
            ("postfx_exposure_min", -1.100, 0.000),
            ("postfx_exposure_max", 1.950, 0.000),
            ("postfx_bright_pass_thresh_width", 0.250, 0.000),
            ("postfx_bright_pass_thresh", 0.550, 0.000),
            ("postfx_intensity_bloom", 0.650, 0.000),
            ("postfx_desaturation", 1.320, 0.000),
            ("postfx_correct_col_r", 0.980, 0.140),
            ("postfx_correct_col_g", 1.010, 0.140),
            ("postfx_correct_col_b", 1.050, 0.140),
            ("postfx_vignetting_intensity", 0.220, 0.000),
            ("postfx_vignetting_radius", 13.000, 0.000),
            ("ssao_inten", 2.850, 0.000),
            ("light_amb_occ_mult", 1.500, 0.000),
            ("light_amb_occ_mult_veh", 1.600, 0.000),
            ("reflection_tweak_emissive", 2.350, 0.000),
            ("lens_artefacts_intensity", 0.300, 0.000),
        ],
        "MODHEN_ENB_PRSA": [
            ("postfx_exposure", 0.080, 0.000),
            ("postfx_bright_pass_thresh_width", 0.420, 0.000),
            ("postfx_bright_pass_thresh", 0.500, 0.000),
            ("postfx_intensity_bloom", 0.720, 0.000),
            ("postfx_desaturation", 1.120, 0.000),
            ("postfx_correct_col_r", 1.060, 0.220),
            ("postfx_correct_col_g", 1.010, 0.220),
            ("postfx_correct_col_b", 0.920, 0.220),
            ("postfx_vignetting_intensity", 0.320, 0.000),
            ("postfx_vignetting_radius", 12.000, 0.000),
            ("postfx_vignetting_contrast", 0.450, 0.000),
            ("postfx_noise", 0.015, 0.000),
            ("ssao_inten", 2.400, 0.000),
            ("light_ss_intensity", 0.650, 0.000),
            ("lens_artefacts_intensity", 0.350, 0.000),
        ],
        "MODHEN_RESHADE_VIBRANT": [
            ("postfx_exposure", 0.030, 0.000),
            ("postfx_bright_pass_thresh_width", 0.300, 0.000),
            ("postfx_bright_pass_thresh", 0.680, 0.000),
            ("postfx_intensity_bloom", 0.420, 0.000),
            ("postfx_desaturation", 1.480, 0.000),
            ("postfx_correct_col_r", 1.040, 0.160),
            ("postfx_correct_col_g", 1.020, 0.160),
            ("postfx_correct_col_b", 1.060, 0.160),
            ("postfx_noise", 0.012, 0.000),
            ("ssao_inten", 2.000, 0.000),
            ("reflection_tweak_emissive", 1.750, 0.000),
        ],
        "MODHEN_RESHADE_CRISP": [
            ("postfx_exposure", -0.020, 0.000),
            ("postfx_exposure_min", -1.200, 0.000),
            ("postfx_exposure_max", 2.100, 0.000),
            ("postfx_bright_pass_thresh_width", 0.220, 0.000),
            ("postfx_bright_pass_thresh", 0.580, 0.000),
            ("postfx_intensity_bloom", 0.380, 0.000),
            ("postfx_desaturation", 1.250, 0.000),
            ("postfx_vignetting_intensity", 0.150, 0.000),
            ("postfx_vignetting_radius", 15.000, 0.000),
            ("postfx_noise", 0.018, 0.000),
            ("ssao_inten", 3.100, 0.000),
            ("light_amb_occ_mult", 1.600, 0.000),
            ("light_amb_occ_mult_veh", 1.650, 0.000),
            ("reflection_tweak_emissive", 2.100, 0.000),
        ],
        "MODHEN_ENB_CYBERPUNK": [
            ("postfx_exposure", 0.050, 0.000),
            ("postfx_bright_pass_thresh_width", 0.250, 0.000),
            ("postfx_bright_pass_thresh", 0.420, 0.000),
            ("postfx_intensity_bloom", 0.950, 0.000),
            ("postfx_desaturation", 1.450, 0.000),
            ("postfx_correct_col_r", 0.960, 0.220),
            ("postfx_correct_col_g", 0.920, 0.220),
            ("postfx_correct_col_b", 1.150, 0.220),
            ("postfx_vignetting_intensity", 0.280, 0.000),
            ("postfx_vignetting_radius", 12.500, 0.000),
            ("chrom_aberration_coeff", 0.006, 0.000),
            ("ssao_inten", 2.600, 0.000),
            ("reflection_tweak_emissive", 3.200, 0.000),
            ("lens_artefacts_intensity", 0.450, 0.000),
        ],
        "MODHEN_CUSTOM_ENB": [
            ("postfx_exposure", 0.050, 0.000),
            ("postfx_exposure_min", -0.900, 0.000),
            ("postfx_exposure_max", 1.850, 0.000),
            ("postfx_bright_pass_thresh_width", 0.280, 0.000),
            ("postfx_bright_pass_thresh", 0.580, 0.000),
            ("postfx_intensity_bloom", 0.580, 0.000),
            ("postfx_desaturation", 1.280, 0.000),
            ("postfx_vignetting_intensity", 0.180, 0.000),
            ("postfx_vignetting_radius", 14.000, 0.000),
            ("ssao_inten", 2.400, 0.000),
            ("light_amb_occ_mult", 1.400, 0.000),
            ("light_amb_occ_mult_veh", 1.500, 0.000),
            ("reflection_tweak_emissive", 2.000, 0.000),
            ("lens_artefacts_intensity", 0.250, 0.000),
        ],
    }


def inject_modifiers_into_xml(xml_path: str, blocks: Dict[str, str]) -> None:
    with open(xml_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # Remove any existing <modifier name="KEY"...>...</modifier> for keys in blocks
    for mod_name in blocks:
        pattern = re.compile(
            r'\s*<modifier\s+name="' + re.escape(mod_name) + r'"[^>]*>.*?</modifier>',
            re.DOTALL,
        )
        content = pattern.sub("", content)

    close_tag = "</timecycle_modifier_data>"
    idx = content.rfind(close_tag)
    if idx == -1:
        raise ValueError(f"Could not find {close_tag} in {xml_path}")

    combined = "\n" + "\n".join(blocks.values()) + "\n"
    updated = content[:idx].rstrip() + combined + close_tag + "\n"
    with open(xml_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(updated)


def main() -> int:
    ap = argparse.ArgumentParser(description="Convert PC ENB/ReShade .ini presets to PS5 GTA V timecycle modifiers.")
    ap.add_argument("ini_file", nargs="?", help="Path to PC enbseries.ini or ReShade .ini preset")
    ap.add_argument("--name", default="MODHEN_CUSTOM_ENB", help="Modifier name (default: MODHEN_CUSTOM_ENB)")
    ap.add_argument("--inject", help="Path to timecycle_mods_1.xml to non-destructively inject into")
    ap.add_argument("--inject-defaults", help="Inject all 7 curated MODHEN_* presets into timecycle_mods_1.xml")
    args = ap.parse_args()

    if args.inject_defaults:
        presets = get_curated_modhen_presets()
        xml_blocks = {k: build_modifier_xml(k, v) for k, v in presets.items()}
        inject_modifiers_into_xml(args.inject_defaults, xml_blocks)
        print(f"[OK] Injected {len(xml_blocks)} non-destructive MODHEN_* presets into {args.inject_defaults}")
        return 0

    if not args.ini_file:
        ap.print_help()
        return 1

    with open(args.ini_file, "r", encoding="utf-8", errors="ignore") as f:
        ini_text = f.read()

    mods, summary = convert_ini_to_rage_mods(ini_text)
    xml_block = build_modifier_xml(args.name, mods)
    print(f"=== Converted {os.path.basename(args.ini_file)} -> {args.name} (numMods={len(mods)}) ===")
    print(xml_block)

    if args.inject:
        inject_modifiers_into_xml(args.inject, {args.name: xml_block})
        print(f"\n[OK] Non-destructively injected '{args.name}' into {args.inject}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

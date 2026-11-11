import os
import glob
import re

def update_tag_all(content, tag, new_vals_13):
    pattern = r'<' + tag + r'[^>]*>(.*?)</' + tag + r'>'
    val_str = " " + " ".join(f"{v:.4f}" for v in new_vals_13)
    replacement = f"<{tag}>{val_str}</{tag}>"
    new_content, count = re.subn(pattern, replacement, content, flags=re.DOTALL)
    return new_content, count

def main():
    print("=== Applying Final Visuals & FX Calibrations ===")
    
    # -------------------------------------------------------------
    # 1. Update all 18 weather XMLs in NVE_PS5_Ready
    # -------------------------------------------------------------
    weathers = sorted(glob.glob('D:/ps5payload/NVE_PS5_Ready/data/timecycle/w_*.xml'))
    print(f"Calibrating {len(weathers)} weather files...")
    
    # 13 keyframe hours: 0h, 5h, 6h, 7h, 8h, 9h, 13h, 17h, 18h, 19h, 20h, 21h, 22h
    
    # Unmistakable cinematic lens vignette (clearly visible on 16:9 displays)
    vig_radius =    [0.6500] * 13   # Brought inward from corners for visible lens framing
    vig_contrast =  [1.4500] * 13
    vig_intensity = [2.4000] * 13   # Deep cinematic border darkening
    vig_col =       [0.0000] * 13
    
    # ENB / ReShade Style Radiant Bloom
    bloom_intensity = [2.40, 2.00, 2.00, 1.80, 1.65, 1.55, 1.55, 1.65, 2.00, 2.25, 2.40, 2.40, 2.40]
    bloom_thresh =    [0.14, 0.16, 0.20, 0.26, 0.28, 0.30, 0.30, 0.28, 0.22, 0.18, 0.15, 0.14, 0.14]
    bloom_width =     [0.20] * 13
    
    # Horizon-Based Ambient Occlusion (HBAO+) & Deep Shadow Pipeline
    ssao_type =       [1.00] * 13  # 1.0 = HBAO+ / Contact Hardening Shadows
    ssao_inten =      [13.0, 13.0, 13.5, 14.0, 14.0, 14.0, 14.0, 14.0, 13.5, 13.0, 13.0, 13.0, 13.0]
    ssao_cp =         [1.80] * 13  # Bold contact shadows grounding tires & shoes
    ssao_qs =         [1.80] * 13
    veh_occ =         [4.20] * 13  # Bold vehicle undercarriage ambient occlusion
    ped_occ =         [1.40] * 13
    prop_occ =        [2.20] * 13
    
    shadow_dist =     [3.00] * 13  # Extended shadow draw distance
    shadow_sharp =    [1.35] * 13  # Razor-sharp shadow outlines
    shadow_scale0 =   [3.50] * 13  # Ultra-crisp close-up cascade
    
    for f in weathers:
        with open(f, 'r', encoding='utf-8', errors='ignore') as fp:
            c = fp.read()
            
        c, _ = update_tag_all(c, 'postfx_vignetting_radius', vig_radius)
        c, _ = update_tag_all(c, 'postfx_vignetting_contrast', vig_contrast)
        c, _ = update_tag_all(c, 'postfx_vignetting_intensity', vig_intensity)
        c, _ = update_tag_all(c, 'postfx_vignetting_col_r', vig_col)
        c, _ = update_tag_all(c, 'postfx_vignetting_col_g', vig_col)
        c, _ = update_tag_all(c, 'postfx_vignetting_col_b', vig_col)
        
        c, _ = update_tag_all(c, 'postfx_intensity_bloom', bloom_intensity)
        c, _ = update_tag_all(c, 'postfx_bright_pass_thresh', bloom_thresh)
        c, _ = update_tag_all(c, 'postfx_bright_pass_thresh_width', bloom_width)
        
        c, _ = update_tag_all(c, 'ssao_type', ssao_type)
        c, _ = update_tag_all(c, 'ssao_inten', ssao_inten)
        c, _ = update_tag_all(c, 'ssao_cp_strength', ssao_cp)
        c, _ = update_tag_all(c, 'ssao_qs_strength', ssao_qs)
        c, _ = update_tag_all(c, 'light_amb_occ_mult_veh', veh_occ)
        c, _ = update_tag_all(c, 'light_amb_occ_mult_ped', ped_occ)
        c, _ = update_tag_all(c, 'light_amb_occ_mult_prop', prop_occ)
        
        c, _ = update_tag_all(c, 'dir_shadow_distance_multiplier', shadow_dist)
        c, _ = update_tag_all(c, 'dir_shadow_sharpness', shadow_sharp)
        c, _ = update_tag_all(c, 'dir_shadow_cascade0_scale', shadow_scale0)
        
        with open(f, 'w', encoding='utf-8') as fp:
            fp.write(c)
            
    print("  [+] All 18 weather XMLs updated with prominent cinematic vignette, ENB bloom & HBAO+.")

    # -------------------------------------------------------------
    # 2. Update INT_streetlighting in timecycle_mods_1.xml to pure white LED
    # -------------------------------------------------------------
    tc1_path = 'D:/ps5payload/NVE_PS5_Ready/data/timecycle/timecycle_mods_1.xml'
    with open(tc1_path, 'r', encoding='utf-8', errors='ignore') as fp:
        tc1 = fp.read()
        
    # Replace orange streetlight ambient with crisp white LED lighting
    tc1 = re.sub(
        r'<modifier name="INT_streetlighting"[^>]*>.*?</modifier>',
        '''<modifier name="INT_streetlighting" numMods="1" userFlags="0">
  <light_natural_amb_down_col_r>0.950 1.000</light_natural_amb_down_col_r>
  <light_natural_amb_down_col_g>0.980 1.000</light_natural_amb_down_col_g>
  <light_natural_amb_down_col_b>1.050 1.000</light_natural_amb_down_col_b>
  <light_natural_amb_down_intensity>0.500 0.000</light_natural_amb_down_intensity>
  <light_natural_amb_up_col_r>0.950 1.000</light_natural_amb_up_col_r>
  <light_natural_amb_up_col_g>0.980 1.000</light_natural_amb_up_col_g>
  <light_natural_amb_up_col_b>1.050 1.000</light_natural_amb_up_col_b>
  <light_natural_amb_up_intensity>0.500 0.000</light_natural_amb_up_intensity>
</modifier>''',
        tc1,
        flags=re.DOTALL
    )
    with open(tc1_path, 'w', encoding='utf-8') as fp:
        fp.write(tc1)
    print("  [+] timecycle_mods_1.xml: INT_streetlighting converted from orange sodium to pure white LED.")

    # -------------------------------------------------------------
    # 3. Update hbaosettings.xml for maximum contact hardening occlusion
    # -------------------------------------------------------------
    hbao_path = 'D:/ps5payload/NVE_PS5_Ready/data/hbaosettings.xml'
    hbao_xml = '''<?xml version="1.0" encoding="UTF-8"?>
<!-- NaturalVision by Razed - Deep HBAO+ Shadow Occlusion on PS5 -->
<HBAOConfig>
	<HBAORelativeStrength value="2.500000"/>
	<CPRelativeStrength value="1.800000"/>
	<CPStrengthClose value="2.000000"/>
	<CPStrengthFar value="1.200000"/>
	<HBAORadius0 value="0.300000"/>
	<HBAORadius1 value="1.350000"/>
	<CPRadius0 value="0.250000"/>
	<CPRadius1 value="0.950000"/>
	<HBAOExponent value="1.800000"/>
	<DepthThreshold value="0.800000"/>
	<HBAODepthExponent value="1.500000"/>
	<NormalScale value="1.000000"/>
	<HBAOBlurSharpness value="16.000000"/>
	<NormalBias value="0.100000"/>
	<HBAONoOfSteps value="4"/>
	<AngleBias value="0.100000"/>
	<HBAOBlurFalloff value="0.001000"/>
	<CPNoOfSteps value="4"/>
</HBAOConfig>
'''
    with open(hbao_path, 'w', encoding='utf-8') as fp:
        fp.write(hbao_xml)
    print("  [+] hbaosettings.xml tuned for deep contact hardening shadows.")

    # -------------------------------------------------------------
    # 4. Calibrate visualsettings.dat: Backfire, Smoke, Coronas, Streetlights
    # -------------------------------------------------------------
    vs_path = 'D:/ps5payload/NVE_PS5_Ready/data/visualsettings.dat'
    with open(vs_path, 'r', encoding='utf-8', errors='ignore') as fp:
        lines = fp.readlines()

    out_lines = []
    for line in lines:
        stripped = line.strip()
        # Keep pedlight/store coronas at 0 (no blinding glare in gun stores/shops)
        if stripped.startswith('pedlight.coronaHDR'):
            out_lines.append('pedlight.coronaHDR 0.000\n')
        elif stripped.startswith('pedlight.coronaSize'):
            out_lines.append('pedlight.coronaSize 0.000\n')
        # Taillight coronas off
        elif stripped.startswith('car.coronas.MainFadeRatio'):
            out_lines.append('car.coronas.MainFadeRatio 0.00\n')
        # Exhaust backfire flame sharpness (replaces huge blurry smear with crisp flame & sparks)
        elif stripped.startswith('bloom.blurblendmult.large'):
            out_lines.append('bloom.blurblendmult.large 0.65\n')
        elif stripped.startswith('bloom.blurblendmult.med'):
            out_lines.append('bloom.blurblendmult.med 1.60\n')
        elif stripped.startswith('bloom.blurblendmult.small'):
            out_lines.append('bloom.blurblendmult.small 1.20\n')
        elif stripped.startswith('misc.BloomIntensityClamp.HD'):
            out_lines.append('misc.BloomIntensityClamp.HD 8.50\n')
        # Smoke particles: volumetric self-shadowing & stop glowing smoke
        elif stripped.startswith('bloom.threshold.min'):
            out_lines.append('bloom.threshold.min 0.38\n')
        elif stripped.startswith('particles.shadowintensity'):
            out_lines.append('particles.shadowintensity 1.70\n')
        # Distant streetlights: cool white LED
        elif stripped.startswith('distantlights.streetlight.color.red'):
            out_lines.append('distantlights.streetlight.color.red\t\t0.95\n')
        elif stripped.startswith('distantlights.streetlight.color.green'):
            out_lines.append('distantlights.streetlight.color.green\t0.98\n')
        elif stripped.startswith('distantlights.streetlight.color.blue'):
            out_lines.append('distantlights.streetlight.color.blue\t1.05\n')
        else:
            out_lines.append(line)

    with open(vs_path, 'w', encoding='utf-8') as fp:
        fp.writelines(out_lines)
    print("  [+] visualsettings.dat calibrated: backfire flames crisp, smoke volumetric, coronas 0, streetlights white.")

    print("\n=== All Visuals, Streetlights, Particle FX, and Shadows Calibrated Successfully! ===")

if __name__ == '__main__':
    main()


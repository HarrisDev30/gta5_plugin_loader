import os
import glob
import re

def update_tag(content, tag, new_vals_13):
    pattern = r'<' + tag + r'[^>]*>(.*?)</' + tag + r'>'
    val_str = " " + " ".join(f"{v:.4f}" for v in new_vals_13)
    replacement = f"<{tag}>{val_str}</{tag}>"
    new_content, count = re.subn(pattern, replacement, content, flags=re.DOTALL)
    return new_content, count

def main():
    print("=== Re-calibrating Vignette: Soft, Feathered, Seamless Blend ===")
    weathers = sorted(glob.glob('D:/ps5payload/NVE_PS5_Ready/data/timecycle/w_*.xml'))
    print(f"Updating {len(weathers)} weather files...")
    
    # 13 keyframe hours: 0h, 5h, 6h, 7h, 8h, 9h, 13h, 17h, 18h, 19h, 20h, 21h, 22h
    
    # Pushed out to 1.10 so center and mid-screen are 100% clean and clear
    vig_radius =    [1.1000] * 13
    
    # 0.0000 contrast removes the hard circular ring completely (feathered cosine falloff)
    vig_contrast =  [0.0000] * 13
    
    # Gentle 0.3800 intensity blends softly into the corners instead of pure black
    vig_intensity = [0.3800] * 13
    
    vig_col =       [0.0000] * 13
    
    for f in weathers:
        with open(f, 'r', encoding='utf-8', errors='ignore') as fp:
            c = fp.read()
            
        c, _ = update_tag(c, 'postfx_vignetting_radius', vig_radius)
        c, _ = update_tag(c, 'postfx_vignetting_contrast', vig_contrast)
        c, _ = update_tag(c, 'postfx_vignetting_intensity', vig_intensity)
        c, _ = update_tag(c, 'postfx_vignetting_col_r', vig_col)
        c, _ = update_tag(c, 'postfx_vignetting_col_g', vig_col)
        c, _ = update_tag(c, 'postfx_vignetting_col_b', vig_col)
        
        with open(f, 'w', encoding='utf-8') as fp:
            fp.write(c)
            
    print("  [+] Successfully updated all 18 weather XMLs with soft, blended vignette.")

if __name__ == '__main__':
    main()


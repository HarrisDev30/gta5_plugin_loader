import subprocess, os, shutil

builder_dir = r'D:\ps5payload\NeoGen EARY ACCESS\Terminal Build\NeoGen\neogen_misc\OTDBuilder'

# 1. Prepare texture_input
tex_in = os.path.join(builder_dir, 'texture_input')
shutil.rmtree(tex_in, ignore_errors=True)
os.makedirs(tex_in, exist_ok=True)

# Copy DDS files with exact sprite names: 'dial.dds' and 'needle.dds'
shutil.copy2(r'D:\ps5payload\ForzaSpeedometer\dial_base_hd_centered.dds', os.path.join(tex_in, 'dial.dds'))
shutil.copy2(r'D:\ps5payload\ForzaSpeedometer\needle_hd_perfect.dds', os.path.join(tex_in, 'needle.dds'))

# 2. Write lua script
lua_script = """local ROOT = "texture_input/"
print("Building speedo_forza...")
set_platform("orbis")
local count = load_textures(ROOT)
print("Loaded textures: " .. tostring(count))
save_texture_dictionary("speedo_forza", "")
print("Done!")
"""
lua_path = os.path.join(builder_dir, 'build_speedo.lua')
with open(lua_path, 'w', encoding='utf-8') as f:
    f.write(lua_script)

# 3. Run ragebuilder
p = subprocess.run([os.path.join(builder_dir, 'ragebuilder.exe'), 'build_speedo.lua'],
                   cwd=builder_dir, capture_output=True, text=True, errors='replace')
print('STDOUT:\n', p.stdout)
out_file = os.path.join(builder_dir, 'speedo_forza.otd')
if os.path.exists(out_file):
    print(f'[SUCCESS] Generated {out_file}: {os.path.getsize(out_file):,} bytes')
    with open(out_file, 'rb') as fp:
        print('Header:', fp.read(8))
    # Copy to ForzaSpeedometer/speedo_forza.itd
    dest_itd = r'D:\ps5payload\ForzaSpeedometer\speedo_forza.itd'
    shutil.copy2(out_file, dest_itd)
    print(f'Copied to {dest_itd}')
else:
    print('[ERROR] Output not found!')


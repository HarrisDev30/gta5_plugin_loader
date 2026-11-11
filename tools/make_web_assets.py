import os

html_path = os.path.join(os.path.dirname(__file__), '..', 'src', 'dashboard.html')
header_path = os.path.join(os.path.dirname(__file__), '..', 'src', 'web_assets.h')

with open(html_path, 'rb') as f:
    data = f.read()

lines = []
lines.append('#pragma once')
lines.append('#include <stddef.h>')
lines.append('static const unsigned char WEB_INDEX_HTML[] = {')

for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
    if i + 16 < len(data):
        hex_str += ','
    lines.append('    ' + hex_str)

lines.append('};')
lines.append(f'static const size_t WEB_INDEX_HTML_LEN = {len(data)};\n')

with open(header_path, 'w') as f:
    f.write('\n'.join(lines))

print(f'Successfully generated {header_path} ({len(data)} bytes)')


import os

src_html = open('d:/ps5payload/src/web_assets.html', 'r', encoding='utf-8').read()

# Customize for modHEN & Plugin Loader on port 8050
html = src_html.replace('GTA V PS5 AFR Mod Manager', 'modHEN: PS5 GTA V Mod & Plugin Manager')
html = html.replace('Asset File Replacement & DLC Packs', 'Native SPRX/PRX Loader & AFR Engine')
html = html.replace('Port 8080', 'Port 8050')

# Add Plugins Root shortcut to sidebar
sidebar_anchor = '<div class="sidebar-label">Quick Navigation</div>'
plugins_nav = '''<div class="sidebar-label">Quick Navigation</div>
      <div class="nav-item active" id="nav-plugins" onclick="navigatePath('/data/modHEN/plugins')">
        <span class="nav-icon">🔌</span>
        <span class="nav-text">Plugins (PRX/SPRX)</span>
      </div>'''
html = html.replace(sidebar_anchor, plugins_nav)

# Add badge for prx
badge_anchor = '.badge-rpf { background: rgba(245, 158, 11, 0.15); color: #fbbf24; }'
badge_prx = '''.badge-rpf { background: rgba(245, 158, 11, 0.15); color: #fbbf24; }
  .badge-prx { background: rgba(168, 85, 247, 0.15); color: #c084fc; border: 1px solid rgba(168, 85, 247, 0.3); }'''
html = html.replace(badge_anchor, badge_prx)

# Update badge classifier in JS
badge_js_anchor = "if (name.endsWith('.rpf')) return '<span class=\"file-badge badge-rpf\">RPF</span>';"
badge_js_prx = "if (name.endsWith('.prx') || name.endsWith('.sprx')) return '<span class=\"file-badge badge-prx\">PRX</span>';\n    " + badge_js_anchor
html = html.replace(badge_js_anchor, badge_js_prx)

# Save dashboard.html
out_html = 'd:/ps5payload/gta5_plugin_loader/src/dashboard.html'
open(out_html, 'w', encoding='utf-8').write(html)
print(f'Wrote {out_html} ({len(html)} chars)')

# Generate web_assets.h
data = html.encode('utf-8')
lines = [
    '#ifndef WEB_ASSETS_H',
    '#define WEB_ASSETS_H',
    '',
    f'static const unsigned int WEB_INDEX_HTML_LEN = {len(data)};',
    'static const unsigned char WEB_INDEX_HTML[] = {'
]
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
    lines.append(f'    {hex_str},')
lines.append('};')
lines.append('')
lines.append('#endif // WEB_ASSETS_H')

out_h = 'd:/ps5payload/gta5_plugin_loader/src/web_assets.h'
open(out_h, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print(f'Generated {out_h} ({len(data)} bytes)')


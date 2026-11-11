import os
import glob
import urllib.request
import urllib.parse
import sys

PS5_IP = "10.138.159.177"
PS5_PORT = 8050

if len(sys.argv) > 1:
    PS5_IP = sys.argv[1]

print(f"Connecting to PS5 at http://{PS5_IP}:{PS5_PORT}...")

files = glob.glob('D:/ps5payload/NVE_PS5_Ready/data/timecycle/w_*.xml')
success = 0
for f in files:
    fname = os.path.basename(f)
    with open(f, 'rb') as fp:
        data = fp.read()
    for rpath in [f'/data/orbx/gta/data/timecycle/{fname}', f'/data/orbx/gta/timecycle/{fname}']:
        url = f"http://{PS5_IP}:{PS5_PORT}/api/upload?path={urllib.parse.quote(rpath)}"
        req = urllib.request.Request(url, data=data, method='POST')
        req.add_header('Content-Type', 'application/octet-stream')
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                pass
        except Exception as e:
            print(f"Error uploading {fname} to {rpath}: {e}")
            break
    else:
        print(f"  [OK] {fname}")
        success += 1

print(f"\nDone! {success}/{len(files)} weather files uploaded.")


#!/usr/bin/env python3
"""
Sync NVE Renew Enhancements to PS5 Orbx Engine
=============================================
Uploads calibrated graphics files:
- Motion blur (timecycle_mods_mb_tp, mb_fp, timecycle_mods_1, timecycle_mods_2, visualsettings.dat)
- Enhanced Ambient Occlusion (hbaosettings.xml)
- Darker realistic shadows & PCF anti-pixelation (all 18 w_*.xml timecycles)
- DLC list (dlclist.xml)
"""

import os
import sys
import glob
import urllib.parse
import urllib.request
import ftplib
import socket

def get_ps5_ip():
    if len(sys.argv) > 1 and sys.argv[1].strip():
        return sys.argv[1].strip()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ip_file = os.path.join(root, "ps5_ip.txt")
    if os.path.exists(ip_file):
        with open(ip_file, "r", encoding="utf-8", errors="ignore") as f:
            ip = f.read().strip()
            if ip:
                return ip
    return "10.207.6.177"

PS5_IP = get_ps5_ip()
BASE_URL = f"http://{PS5_IP}:8050"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENEW_DATA = os.path.join(ROOT, "NVE_Renew", "data")
RENEW_TC = os.path.join(RENEW_DATA, "timecycle")

def upload_http(local_path, remote_path):
    if not os.path.exists(local_path):
        print(f"[!] File not found: {local_path}")
        return False
    size = os.path.getsize(local_path)
    url = f"{BASE_URL}/api/upload?path={urllib.parse.quote(remote_path, safe='/')}"
    with open(local_path, "rb") as f:
        data = f.read()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=8) as resp:
            print(f"  [+] HTTP: {os.path.basename(local_path)} ({size:,} B) -> {remote_path} (status {resp.status})")
            return True
    except Exception as e:
        print(f"  [-] HTTP error for {os.path.basename(local_path)}: {e}")
        return False

def upload_ftp(local_path, remote_path):
    if not os.path.exists(local_path):
        return False
    for port in [2121, 21]:
        try:
            ftp = ftplib.FTP()
            ftp.connect(PS5_IP, port, timeout=3)
            ftp.login()
            rdir = os.path.dirname(remote_path).replace("\\", "/")
            try:
                ftp.cwd(rdir)
            except Exception:
                pass
            with open(local_path, "rb") as f:
                ftp.storbinary(f"STOR {remote_path}", f)
            ftp.quit()
            print(f"  [+] FTP({port}): {os.path.basename(local_path)} -> {remote_path}")
            return True
        except Exception:
            pass
    return False

def send_file(local_path, remote_path):
    if not upload_http(local_path, remote_path):
        return upload_ftp(local_path, remote_path)
    return True

def main():
    print("=" * 60)
    print(f"  NVE Renew Sync to PS5 ({PS5_IP})")
    print("=" * 60)
    
    # 1. Test connectivity
    print(f"[*] Checking connection to {PS5_IP}:8050...")
    connected = False
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.5)
        res = sock.connect_ex((PS5_IP, 8050))
        sock.close()
        if res == 0:
            connected = True
            print("  [+] PS5 Orbx Web API is ONLINE!")
    except Exception as e:
        pass
        
    if not connected:
        print(f"  [-] PS5 ({PS5_IP}) is unreachable. Check if console is powered on and modHEN/Orbx is active.")
        print("  [-] You can run this script again once the console is awake.")
        return

    # Files to upload
    uploads = []
    
    # Visualsettings
    vis = os.path.join(RENEW_DATA, "visualsettings.dat")
    if os.path.exists(vis):
        uploads.append((vis, "/data/orbx/gta/data/visualsettings.dat"))
        uploads.append((vis, "/data/orbx/gta/visualsettings.dat"))
        
    # HBAO settings
    hbao = os.path.join(RENEW_DATA, "hbaosettings.xml")
    if os.path.exists(hbao):
        uploads.append((hbao, "/data/orbx/gta/data/hbaosettings.xml"))
        
    # DLC List
    dlc = os.path.join(ROOT, "NVE_Renew", "dlclist.xml")
    if os.path.exists(dlc):
        uploads.append((dlc, "/data/orbx/gta/dlclist.xml"))
        
    # Timecycle modifiers
    for mod_name in ["timecycle_mods_1.xml", "timecycle_mods_2.xml", "timecycle_mods_mb_tp.xml", "timecycle_mods_mb_fp.xml"]:
        p = os.path.join(RENEW_TC, mod_name)
        if os.path.exists(p):
            uploads.append((p, f"/data/orbx/gta/data/timecycle/{mod_name}"))
            uploads.append((p, f"/data/orbx/gta/timecycle/{mod_name}"))
            
    # Weather timecycles
    weather_files = glob.glob(os.path.join(RENEW_TC, "w_*.xml"))
    for w in weather_files:
        fname = os.path.basename(w)
        uploads.append((w, f"/data/orbx/gta/data/timecycle/{fname}"))
        uploads.append((w, f"/data/orbx/gta/timecycle/{fname}"))
        
    print(f"[*] Uploading {len(uploads)} file targets...")
    success = 0
    for local_f, remote_f in uploads:
        if send_file(local_f, remote_f):
            success += 1
            
    print("-" * 60)
    print(f"[+] Sync finished: {success}/{len(uploads)} uploads successful.")
    print("=" * 60)

if __name__ == "__main__":
    main()


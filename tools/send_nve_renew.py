#!/usr/bin/env python3
"""
send_nve_renew.py
=================
High-speed uploader for the renewed, console-calibrated NaturalVision Evolved package.
Uploads directly via modHEN Web Dashboard HTTP API on port 8050 (/api/upload?path=...)
with automatic fallback to FTP (ports 2121 / 21):
  1. Purges any obsolete post-v1.00 PC crash files from PS5 SSD.
  2. Uploads all files inside NVE_Renew/data/ to /data/orbx/gta/data/
  3. Mirrors timecycles to /data/orbx/gta/timecycle/ for dual lookup.
  4. Uploads NVE, NVE_clouds, NVE_carcols, NVE_patches, and streetlights DLC packs to /data/orbx/gta/rpf/dlc_addon/<name>/dlc.rpf
  5. Uploads clean merged dlclist.xml to /data/orbx/gta/dlclist.xml
"""

import argparse
from ftplib import FTP
import os
import socket
import sys
import urllib.parse
import urllib.request

BASE_DIR = r"D:\ps5payload"
RENEW_DIR = os.path.join(BASE_DIR, "NVE_Renew")
RENEW_DATA = os.path.join(RENEW_DIR, "data")
RENEW_RPF = os.path.join(RENEW_DIR, "rpf", "dlc_addon")
DLCLIST_PATH = os.path.join(RENEW_DIR, "dlclist.xml")


def upload_http(ip: str, port: int, local_path: str, remote_path: str, timeout: int = 120) -> bool:
    url = f"http://{ip}:{port}/api/upload?path={urllib.parse.quote(remote_path, safe='/')}"
    with open(local_path, "rb") as f:
        data = f.read()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/octet-stream")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status == 200


def delete_http(ip: str, port: int, remote_path: str, timeout: int = 15) -> bool:
    url = f"http://{ip}:{port}/api/delete?path={urllib.parse.quote(remote_path, safe='/')}"
    req = urllib.request.Request(url, data=b"", method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status == 200
    except Exception:
        return False


def upload_ftp(ftp: FTP, local_path: str, remote_path: str) -> bool:
    remote_dir = os.path.dirname(remote_path).replace("\\", "/")
    parts = [p for p in remote_dir.split("/") if p]
    cur = ""
    for p in parts:
        cur += "/" + p
        try:
            ftp.mkd(cur)
        except Exception:
            pass
    with open(local_path, "rb") as f:
        ftp.storbinary(f"STOR {remote_path}", f)
    return True


def main():
    parser = argparse.ArgumentParser(description="Upload NVE_Renew to PS5")
    parser.add_argument("--ip", default="10.207.6.177", help="PS5 IP address (default: 10.207.6.177)")
    parser.add_argument("--http-port", type=int, default=8050, help="modHEN Web Dashboard port (default: 8050)")
    parser.add_argument("--ftp-port", type=int, default=2121, help="FTP port (default: 2121)")
    parser.add_argument("--clean", action="store_true", default=True, help="Clean old data/timecycle folders before upload (default: True)")
    args = parser.parse_args()

    # Load IP from ps5_ip.txt if present
    ip_file = os.path.join(BASE_DIR, "ps5_ip.txt")
    if os.path.exists(ip_file) and args.ip == "10.207.6.177":
        try:
            with open(ip_file, "r") as f:
                saved = f.read().strip()
                if saved:
                    args.ip = saved
        except Exception:
            pass

    print("============================================================")
    print("  NVE_Renew PS5 High-Speed Uploader")
    print(f"  Target Console: {args.ip}")
    print("============================================================")

    # Detect connection mode (HTTP API vs FTP)
    use_http = False
    try:
        s = socket.create_connection((args.ip, args.http_port), timeout=3)
        s.close()
        use_http = True
        print(f"  [+] Connected via modHEN HTTP Dashboard (port {args.http_port})")
    except Exception:
        print(f"  [-] HTTP port {args.http_port} not responding. Testing FTP...")

    ftp = None
    if not use_http:
        for p in (args.ftp_port, 21):
            try:
                ftp = FTP()
                ftp.connect(args.ip, p, timeout=5)
                ftp.login()
                print(f"  [+] Connected via FTP (port {p})")
                break
            except Exception:
                ftp = None

    if not use_http and not ftp:
        print(f"\n[ERROR] Unable to connect to PS5 at {args.ip} via HTTP ({args.http_port}) or FTP ({args.ftp_port}/21).")
        print("Please ensure your PS5 is powered on, connected to LAN, and running modHEN.")
        sys.exit(1)

    # 1. Purge dangerous post-v1.00 PC crash files & stale timecycle modifiers
    bad_files = (
        "bloodfx.dat", "decals.dat", "entityfx.dat", "explosionfx.dat",
        "firefx.dat", "liquidfx.dat", "wheelfx.dat", "vfxlightningsettings.xml",
    )
    if use_http:
        for bf in bad_files:
            for prefix in ("/data/orbx/gta/data", "/data/orbx/gta/data/effects", "/data/orbx/gta/data/levels/gta5"):
                rp = f"{prefix}/{bf}"
                if delete_http(args.ip, args.http_port, rp):
                    print(f"  [CLEAN] Removed post-v1.00 crash file: {rp}")
        # Purge stale timecycle_mods to ensure zero leftover ENB modifier bleed
        for tm in ("timecycle_mods_1.xml", "timecycle_mods_2.xml", "timecycle_mods_3.xml"):
            for prefix in ("/data/orbx/gta/data/timecycle", "/data/orbx/gta/timecycle"):
                delete_http(args.ip, args.http_port, f"{prefix}/{tm}")

    uploaded = 0
    failed = 0

    # 2. Upload all files from NVE_Renew/data/
    print("\n--- Uploading NVE_Renew Data & Timecycle Files ---")
    for root, _, files in os.walk(RENEW_DATA):
        for fname in sorted(files):
            local_p = os.path.join(root, fname)
            rel = os.path.relpath(local_p, RENEW_DATA).replace("\\", "/")
            remote_p = f"/data/orbx/gta/data/{rel}"
            sz = os.path.getsize(local_p)

            try:
                if use_http:
                    upload_http(args.ip, args.http_port, local_p, remote_p)
                else:
                    upload_ftp(ftp, local_p, remote_p)
                uploaded += 1
                print(f"  [OK] {remote_p} ({sz:,} B)")
            except Exception as e:
                failed += 1
                print(f"  [ERR] {remote_p}: {e}")

            # Mirror timecycle/*.xml to /data/orbx/gta/timecycle/*.xml
            if rel.startswith("timecycle/"):
                mirror_p = f"/data/orbx/gta/{rel}"
                try:
                    if use_http:
                        upload_http(args.ip, args.http_port, local_p, mirror_p)
                    else:
                        upload_ftp(ftp, local_p, mirror_p)
                except Exception:
                    pass

    # 3. Upload DLC Packs
    print("\n--- Uploading DLC Add-On Packs ---")
    dlc_packs = ("NVE", "NVE_clouds", "NVE_carcols", "NVE_patches", "streetlights")
    for pack in dlc_packs:
        local_rpf = os.path.join(RENEW_RPF, pack, "dlc.rpf")
        if os.path.exists(local_rpf):
            remote_rpf = f"/data/orbx/gta/rpf/dlc_addon/{pack}/dlc.rpf"
            sz = os.path.getsize(local_rpf)
            print(f"  [*] Uploading {pack}/dlc.rpf ({sz:,} bytes)...")
            try:
                if use_http:
                    upload_http(args.ip, args.http_port, local_rpf, remote_rpf, timeout=300)
                else:
                    upload_ftp(ftp, local_rpf, remote_rpf)
                uploaded += 1
                print(f"  [OK] {remote_rpf} ({sz:,} B)")
            except Exception as e:
                failed += 1
                print(f"  [ERR] {remote_rpf}: {e}")

    # 4. Upload clean merged dlclist.xml
    print("\n--- Uploading dlclist.xml ---")
    if os.path.exists(DLCLIST_PATH):
        remote_list = "/data/orbx/gta/dlclist.xml"
        try:
            if use_http:
                upload_http(args.ip, args.http_port, DLCLIST_PATH, remote_list)
            else:
                upload_ftp(ftp, DLCLIST_PATH, remote_list)
            uploaded += 1
            print(f"  [OK] {remote_list} ({os.path.getsize(DLCLIST_PATH):,} B)")
        except Exception as e:
            failed += 1
            print(f"  [ERR] {remote_list}: {e}")

    if ftp:
        try:
            ftp.quit()
        except Exception:
            pass

    print("\n============================================================")
    print(f"  NVE_Renew Upload Complete! Uploaded: {uploaded}, Failed: {failed}")
    print("============================================================\n")


if __name__ == "__main__":
    main()

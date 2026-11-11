import os
import sys
import ftplib

def upload_file(ftp, local_path, remote_path):
    parent = os.path.dirname(remote_path).replace('\\', '/')
    parts = [p for p in parent.split('/') if p]
    cur = ""
    for part in parts:
        cur += "/" + part
        try:
            ftp.mkd(cur)
        except:
            pass

    sz = os.path.getsize(local_path)
    print(f"Uploading {os.path.basename(local_path)} ({sz:,} bytes) -> {remote_path} ...")
    with open(local_path, 'rb') as f:
        ftp.storbinary(f"STOR {remote_path}", f)
    print(f"  [+] Uploaded {os.path.basename(local_path)} OK!")

def upload_dir_recursive(ftp, local_dir, remote_dir):
    parts = [p for p in remote_dir.replace('\\', '/').split('/') if p]
    cur = ""
    for part in parts:
        cur += "/" + part
        try:
            ftp.mkd(cur)
        except:
            pass

    for item in os.listdir(local_dir):
        l_item = os.path.join(local_dir, item)
        r_item = remote_dir.rstrip('/') + '/' + item
        if os.path.isfile(l_item):
            # Skip massive reference .txt index dumps to keep upload blazing fast
            if l_item.endswith(".rpf.txt"):
                continue
            upload_file(ftp, l_item, r_item)
        elif os.path.isdir(l_item):
            upload_dir_recursive(ftp, l_item, r_item)

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    ip_file = os.path.join(base_dir, "..", "ps5_ip.txt")
    ps5_ip = ""
    if os.path.exists(ip_file):
        with open(ip_file, 'r') as f:
            ps5_ip = f.read().strip()

    if len(sys.argv) > 1:
        ps5_ip = sys.argv[1].strip()

    if not ps5_ip:
        ps5_ip = input("Enter your PS5 IP Address: ").strip()

    if not ps5_ip:
        print("No IP provided. Exiting.")
        return

    ports = [2121, 21]
    ftp = None
    for port in ports:
        try:
            print(f"Connecting to PS5 FTP @ {ps5_ip}:{port} ...")
            ftp = ftplib.FTP()
            ftp.connect(ps5_ip, port, timeout=5)
            ftp.login()
            print(f"Connected to PS5 FTP on port {port}!")
            break
        except Exception:
            ftp = None

    if not ftp:
        print("[ERROR] Could not connect to PS5 FTP. Ensure FTP is running on your PS5.")
        return

    # 1. Upload Plugins folder to /data/modHEN/plugins/ and /data/plugins/
    plugins_dir = os.path.join(base_dir, "plugins")
    if os.path.exists(plugins_dir):
        for fname in os.listdir(plugins_dir):
            lpath = os.path.join(plugins_dir, fname)
            if os.path.isfile(lpath):
                upload_file(ftp, lpath, f"/data/modHEN/plugins/{fname}")
                upload_file(ftp, lpath, f"/data/plugins/{fname}")

    # 2. Upload full orbx directory to /data/orbx/ if present
    orbx_candidates = [
        os.path.join(base_dir, "..", "Orbx Engine v1.4", "Orbx Engine v1.4", "orbx"),
        os.path.join(base_dir, "..", "Orbx Engine v1.4", "orbx"),
        os.path.join(base_dir, "orbx")
    ]
    orbx_src = None
    for cand in orbx_candidates:
        if os.path.isdir(cand):
            orbx_src = cand
            break

    if orbx_src:
        print(f"\nUploading complete Orbx Engine directory from: {orbx_src} -> /data/orbx ...")
        upload_dir_recursive(ftp, orbx_src, "/data/orbx")
        # Ensure orbx.prx is also present at /data/orbx/orbx.prx
        orbx_prx_local = os.path.join(plugins_dir, "orbx.prx")
        if os.path.exists(orbx_prx_local):
            upload_file(ftp, orbx_prx_local, "/data/orbx/orbx.prx")

    ftp.quit()
    print("\n=======================================================")
    print(" [SUCCESS] Plugins and Orbx Engine uploaded to PS5!")
    print("=======================================================")

if __name__ == "__main__":
    main()

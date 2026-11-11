import socket
import sys
import os

PS5_IP = "10.138.159.177"
ELF_PATH = "D:/ps5payload/gta5_plugin_loader/gta5_plugin_loader.elf"

def send_payload(ip=PS5_IP, elf_path=ELF_PATH):
    if not os.path.exists(elf_path):
        print(f"[ERROR] ELF file not found: {elf_path}")
        return False
        
    with open(elf_path, "rb") as f:
        data = f.read()
    print(f"Loaded {len(data)} bytes from {elf_path}")

    for port in [9020, 9021]:
        print(f"Connecting to {ip}:{port}...")
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2.0)
            s.connect((ip, port))
            print(f"Connected! Sending ELF ({len(data)} bytes)...")
            s.sendall(data)
            s.close()
            print(f"[SUCCESS] Payload sent successfully to {ip}:{port}!")
            return True
        except Exception as e:
            print(f"Port {port} failed: {e}")
            
    print("[ERROR] Could not connect to ELF loader on ports 9020/9021. Ensure ELF loader is active on PS5.")
    return False

if __name__ == "__main__":
    ip = sys.argv[1] if len(sys.argv) > 1 else PS5_IP
    send_payload(ip)


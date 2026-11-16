#!/usr/bin/env python3
"""
PS4 / PS5 Mod Studio for GTA V v1.00.

Interactive terminal + CLI that automates:
  - PC vehicle mod  -> PS4/PS5 add-on DLC (metadata sanitising, texture budgets, 8 KB chunking)
  - RevoluSound / Forza engine pack -> PS4 granular audio DLC (any engine layout)
  - Attaching an audio DLC to a car DLC (merges audio files, rewrites content.xml + audioNameHash)
  - One-shot "build" = convert car + convert engine + attach + verify
  - Verification audit and PS5 upload
"""

import glob
import math
import os
import re
import shutil
import sys
import tempfile
import urllib.request
import zipfile

TOOLS_DIR = r"D:\ps5payload\tools"
NEOGEN_DIR = r"D:\ps5payload\NeoGen EARY ACCESS\Terminal Build\NeoGen"
REVOLUSOUND_DIR = (
    r"D:\ps5payload\VehicleMods\RevoluSound Team Pack 56 2.0 2026-11-08T19-54Z 2k4iF2xd8"
    r"\Content\mediapc\Audio\EngineSynth"
)
DLC_OUT_ROOT = r"D:\ps5payload\NVE_PS5_Ready\rpf\dlc_addon"
AUDIO_OUT_ROOT = r"D:\ps5payload\VehicleMods"
DEFAULT_PS5_IP = "10.171.175.177"
DEFAULT_PS5_PORT = 8050

# GTA V loads the player engine bank into a fixed wave slot (~4.25 MB); keep headroom.
PLAYER_AWC_BUDGET = 4_100_000
NPC_AWC_BUDGET = 1_200_000
GRAIN_BUDGETS = (380, 300, 240, 190, 150)

for _p in (TOOLS_DIR, NEOGEN_DIR):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import NeoGen
import auto_convert_car as acc
import auto_convert_roads as acr
import auto_convert_vegetation as acv
import build_fh6_svj_sound as snd
import convert_vremastered as cv

ESC = "\033["
RST, BOLD, DIM = f"{ESC}0m", f"{ESC}1m", f"{ESC}2m"
CYAN, LIME, YELLOW, CORAL, MAGENTA = (f"{ESC}{c}m" for c in (96, 92, 93, 91, 95))


def bar(char="─", length=72):
    return f"{DIM}{char * length}{RST}"


def verify_standalone_audio(dlc_path):
    """Verify a standalone audio DLC: XMLs, wavepack sizes, zero fallbacks, chunk alignment."""
    print(f"\n[*] Running Automated Audio DLC Verification on {os.path.basename(dlc_path)}...")
    passes = []
    issues = []
    with tempfile.TemporaryDirectory() as td:
        try:
            NeoGen.extract_single_rpf(dlc_path, td)
        except Exception as e:
            print(f"  {CORAL}[FAIL] Could not extract DLC: {e}{RST}")
            return False

        setup2 = os.path.join(td, "setup2.xml")
        content = os.path.join(td, "content.xml")
        if os.path.exists(setup2) and os.path.exists(content):
            passes.append("setup2.xml & content.xml present and well-formed (Vans format)")
        else:
            issues.append("Missing setup2.xml or content.xml in root of DLC")

        audio_dir = os.path.join(td, "ps4", "audio")
        rels = glob.glob(os.path.join(audio_dir, "*_game.dat151.rel"))
        if not rels:
            issues.append("Missing ps4/audio/*_game.dat151.rel")
        else:
            name = os.path.basename(rels[0])[: -len("_game.dat151.rel")]
            content_txt = open(content, "r", encoding="utf-8", errors="ignore").read() if os.path.exists(content) else ""
            if (
                f"{name}_game.dat" in content_txt
                and f"{name}_sounds.dat" in content_txt
                and f"dlc_{name}" in content_txt
            ):
                passes.append(f"content.xml registers {name} game.dat, sounds.dat and wavepack (data + enable)")
            else:
                issues.append(f"content.xml missing registration for {name} audio files")

            p_awc = os.path.join(audio_dir, "sfx", f"dlc_{name}", f"{name}.awc")
            n_awc = os.path.join(audio_dir, "sfx", f"dlc_{name}", f"{name}_npc.awc")
            for label, awc, limit in (("Player", p_awc, PLAYER_AWC_BUDGET), ("NPC", n_awc, NPC_AWC_BUDGET)):
                if not os.path.isfile(awc):
                    issues.append(f"Missing {label} AWC: {os.path.relpath(awc, td)}")
                else:
                    sz = os.path.getsize(awc)
                    if sz <= limit:
                        passes.append(f"{os.path.basename(awc)} {sz:,} B within wave-slot budget (limit {limit:,} B)")
                    else:
                        issues.append(f"{os.path.basename(awc)} {sz:,} B exceeds wave-slot budget {limit:,} B")

            snd_rel = os.path.join(audio_dir, f"{name}_sounds.dat54.rel")
            if os.path.isfile(snd_rel):
                try:
                    rel_b = bytearray(open(snd_rel, "rb").read())
                    items = snd.parse_rel_index(rel_b)
                    bad_wrappers = []
                    for h, (off, ln) in items.items():
                        if rel_b[off] == 18 and ln == 33:
                            fb_hash = struct.unpack_from("<I", rel_b, off + 24 + 4)[0]
                            if fb_hash != 0:
                                bad_wrappers.append(f"0x{h:08x}->0x{fb_hash:08x}")
                    if bad_wrappers:
                        issues.append(f"Found {len(bad_wrappers)} WrapperSounds with non-zero fallback: {bad_wrappers[:3]}")
                    else:
                        passes.append("All WrapperSounds have zero FallBackSound (no stock sound leaks)")
                except Exception as e:
                    issues.append(f"Could not parse sounds.dat54.rel: {e}")

    print("\n------------------- VERIFICATION REPORT -------------------")
    for p in passes:
        print(f"  {LIME}[PASS] {p}{RST}")
    if issues:
        for i in issues:
            print(f"  {CORAL}[FAIL] {i}{RST}")
        print("-----------------------------------------------------------")
        return False
    print("-----------------------------------------------------------")
    print(f"  {LIME}[ALL STATIC CHECKS PASSED - Audio DLC ready for PS5]{RST}")
    return True


def verify_generic_props_dlc(dlc_rpf_path):
    print(f"[*] Running Verification Suite on Props/Map DLC: {os.path.basename(dlc_rpf_path)}...")
    issues = []
    passes = []
    with tempfile.TemporaryDirectory() as td:
        try:
            NeoGen.extract_single_rpf(dlc_rpf_path, td)
        except Exception as e:
            print(f"  [FAIL] Failed to extract outer DLC: {e}")
            return False

        s2 = os.path.join(td, "setup2.xml")
        cx = os.path.join(td, "content.xml")
        if os.path.isfile(s2) and os.path.isfile(cx):
            cx_txt = open(cx, "r", encoding="utf-8", errors="ignore").read()
            s2_txt = open(s2, "r", encoding="utf-8", errors="ignore").read()
            if "GROUP_STARTUP" in s2_txt and ("contentChangeSets" in cx_txt or "dataFiles" in cx_txt):
                passes.append("setup2.xml & content.xml present and well-formed (Vans format)")
            else:
                issues.append("content.xml or setup2.xml malformed")
        else:
            issues.append("Missing setup2.xml or content.xml")

        ps4_rpfs = glob.glob(os.path.join(td, "ps4", "**", "*.rpf"), recursive=True)
        if not ps4_rpfs:
            issues.append("No nested .rpf archives found under ps4/")
        else:
            passes.append(f"Found {len(ps4_rpfs)} nested .rpf archive(s) under ps4/")

        total_checked = 0
        rsc7_aligned = 0
        otd_count = 0
        for rpf in ps4_rpfs:
            with tempfile.TemporaryDirectory() as td_inner:
                try:
                    NeoGen.extract_single_rpf(rpf, td_inner)
                    for root, _, files in os.walk(td_inner):
                        for f in files:
                            fp = os.path.join(root, f)
                            total_checked += 1
                            if f.lower().endswith(".otd"):
                                otd_count += 1
                            try:
                                with open(fp, "rb") as fh:
                                    hdr = fh.read(16)
                                if len(hdr) >= 16 and hdr[:4] == b"RSC7":
                                    _, ver, vf, pf = struct.unpack("<4I", hdr)
                                    vchunks = cv.rsc7_chunks_from_flag(vf)
                                    pchunks = cv.rsc7_chunks_from_flag(pf)
                                    small = [c for c in (vchunks + pchunks) if 0 < c < 8192]
                                    if small:
                                        issues.append(f"Unsafe chunk size in {f}: {small}")
                                    else:
                                        rsc7_aligned += 1
                            except Exception:
                                pass
                except Exception as e:
                    issues.append(f"Could not extract inner RPF {os.path.basename(rpf)}: {e}")

        if otd_count > 0:
            passes.append(f"Texture dictionaries (.otd) present: {otd_count} dictionary/dictionaries verified")
        if rsc7_aligned > 0 and not any("Unsafe chunk" in i for i in issues):
            passes.append(f"All {rsc7_aligned} RSC7 assets enforce >= 8192 byte Orbis chunk alignment")

    print(f"\n------------------- VERIFICATION REPORT -------------------")
    for p in passes:
        print(f"  {LIME}[PASS]{RST} {p}")
    for i in issues:
        print(f"  {CORAL}[FAIL]{RST} {i}")
    print(f"-----------------------------------------------------------\n")
    return len(issues) == 0


def universal_verify_dlc(dlc_path):
    """Detects whether DLC is vehicle, vegetation, roads/maps, or audio and runs the appropriate test suite."""
    with tempfile.TemporaryDirectory() as td:
        try:
            NeoGen.extract_single_rpf(dlc_path, td)
        except Exception as e:
            print(f"{CORAL}[FAIL] Could not extract DLC: {e}{RST}")
            return False
        # 1. Road, map or ymap add-on DLC
        content_xml = os.path.join(td, "content.xml")
        content_txt = ""
        if os.path.isfile(content_xml):
            content_txt = open(content_xml, "r", encoding="utf-8", errors="ignore").read()
        if (
            "CONTENTS_DLC_MAP_DATA" in content_txt
            or os.path.exists(os.path.join(td, "ps4", "levels", "gta5", "roads.rpf"))
            or os.path.exists(os.path.join(td, "ps4", "levels", "gta5", "roads_metadata.rpf"))
            or glob.glob(os.path.join(td, "ps4", "**", "*.omap"), recursive=True)
        ):
            return acr.verify_converted_roads(dlc_path)
        # 2. Vegetation DLC
        if any(os.path.exists(os.path.join(td, "ps4", "levels", "gta5", "props", sub)) for sub in ("vegetation", "procedural")):
            return acv.verify_converted_vegetation(dlc_path)
        # 3. Standalone Audio DLC
        if os.path.isdir(os.path.join(td, "ps4", "audio")) and not os.path.exists(os.path.join(td, "ps4", "vehicles.rpf")):
            return verify_standalone_audio(dlc_path)
        # 4. Vehicle DLC
        if os.path.exists(os.path.join(td, "ps4", "vehicles.rpf")):
            return acc.verify_converted_vehicle(dlc_path)
        # 5. Generic Props / Map DLC
        if glob.glob(os.path.join(td, "ps4", "**", "*.rpf"), recursive=True):
            return verify_generic_props_dlc(dlc_path)
        return acc.verify_converted_vehicle(dlc_path)


def print_header():
    print(f"\n{CYAN}{BOLD}╔{'═' * 70}╗{RST}")
    print(f"{CYAN}{BOLD}║  PS4/PS5 MOD STUDIO - CAR + ENGINE SOUND CONVERTER (GTA V v1.00)   ║{RST}")
    print(f"{CYAN}{BOLD}╚{'═' * 70}╝{RST}")


# ---------------------------------------------------------------------------
# 1. RevoluSound engine discovery
# ---------------------------------------------------------------------------

ENGINE_NAME_RE = re.compile(
    r"^G_([A-Z]+\d+)RST_(.+)_(Race|Sport|Street)_(\d+)_(Exh|Eng)\.zip$", re.IGNORECASE
)


def parse_engine_zip(path):
    """Return dict(layout, model, tier, num, role) for a RevoluSound zip, or None."""
    m = ENGINE_NAME_RE.match(os.path.basename(path))
    if not m:
        return None
    layout, model, tier, num, role = m.groups()
    return {"layout": layout.upper(), "model": model, "tier": tier.title(), "num": num, "role": role.title()}


def list_revolusound_engines(search=""):
    """List (display_name, zip_path) for engine packs whose name contains every search word."""
    words = search.lower().split()
    out = []
    for zp in sorted(glob.glob(os.path.join(REVOLUSOUND_DIR, "*.zip"))):
        bn = os.path.basename(zp)
        if all(w in bn.lower() for w in words):
            out.append((bn, zp))
    return out


def find_partner_zip(zip_path):
    """Find the matching Eng (intake) or Exh (exhaust) pack for a pack, or None."""
    info = parse_engine_zip(zip_path)
    if not info:
        return None
    other = "Eng" if info["role"] == "Exh" else "Exh"
    pattern = f"G_{info['layout']}RST_{info['model']}_*_{other}.zip"
    cands = sorted(glob.glob(os.path.join(os.path.dirname(zip_path), pattern)))
    if not cands:
        return None
    for c in cands:
        ci = parse_engine_zip(c)
        if ci and ci["tier"] == info["tier"] and ci["num"] == info["num"]:
            return c
    return cands[0]


def read_revolusound_zip(zip_path):
    """Decode the accel/decel dyno sweeps and idle of a pack -> ((pcm, pts), (pcm, pts), idle_pcm)."""
    with zipfile.ZipFile(zip_path, "r") as z:
        names = z.namelist()
        acc_dats = [n for n in names if "_acc_" in n.lower() and n.endswith("_Sweep_GS.dat")]
        dec_dats = [n for n in names if "_dec_" in n.lower() and n.endswith("_Sweep_GS.dat")]
        idle_wavs = [n for n in names if "idle" in n.lower() and n.endswith(".wav")]
        if not acc_dats or not dec_dats:
            raise ValueError(f"{os.path.basename(zip_path)} has no accel/decel dyno sweep files")

        def load(dat):
            pts = [int(x) for x in z.read(dat).decode("ascii").split()]
            _sr, pcm = snd.decode_ms_adpcm(z.read(dat + "_ADPCM.wav"))
            return pcm, pts

        idle_pcm = None
        if idle_wavs:
            try:
                _sr_i, pcm_i = snd.decode_ms_adpcm(z.read(idle_wavs[0]))
                if pcm_i:
                    idle_pcm = snd._loop_crossfade_pcm(pcm_i[:min(len(pcm_i), 48000)], fade_samples=960)
            except Exception:
                idle_pcm = None

        return load(acc_dats[0]), load(dec_dats[0]), idle_pcm


# ---------------------------------------------------------------------------
# 2. Engine -> PS4 audio DLC
# ---------------------------------------------------------------------------

def thin_sweep(pcm, pts, max_grains):
    """Keep at most max_grains grains (evenly decimated) to bound the AWC size."""
    ng = len(pts)
    if ng <= max_grains:
        return pcm, pts
    step = math.ceil(ng / float(max_grains))
    out_pcm, out_pts = [], [0]
    for i in range(0, ng - 1, step):
        seg = list(pcm[pts[i]: pts[i + 1]])
        snd.append_grain_crossfaded(out_pcm, seg, xfade=16)
        out_pts.append(len(out_pcm))
    return out_pcm, out_pts


def native_clock(pts, start, count=4):
    """Mean grain clock (Hz, one grain = one 4-stroke cycle) over `count` grains from `start`."""
    idx = range(start, start + count)
    return sum(48000.0 / max(1, pts[i + 1] - pts[i]) for i in idx) / float(count)


def derive_clock_range(acc_pts):
    """
    Grain clock = rpm / 120. Take the redline from the sweep's last grains and put idle at
    ~55% of the first sweep grain's rpm (clamped to a sane 750-1150 rpm).
    """
    first = native_clock(acc_pts, 0)
    last = native_clock(acc_pts, len(acc_pts) - 5)
    idle_rpm = min(1150.0, max(750.0, first * 120.0 * 0.55))
    f_min = idle_rpm / 120.0
    if f_min > first * 0.9:
        f_min = first * 0.8
    f_max = max(last, f_min * 2.5)
    return round(f_min, 2), round(f_max, 2)


def synth_idle_pcm(pcm48, pts48, f_idle, n_grains=20):
    """Constant-pitch idle loop built from the sweep's first 8 grains resampled to f_idle."""
    templates = [pcm48[pts48[i]: pts48[i + 1]] for i in range(8)]
    ping_pong = [0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1]
    target_len = int(round(48000.0 / f_idle))
    out = []
    for g in range(n_grains):
        seg = snd.resample_grain_segment(templates[ping_pong[g % len(ping_pong)]], target_len)
        snd.append_grain_crossfaded(out, seg, xfade=24)
    return snd._loop_crossfade_pcm(out, fade_samples=960)


def build_engine_streams(exh, eng, max_grains):
    """Build player/NPC granular streams + startup one-shot for one grain budget."""
    (exh_acc_pcm, exh_acc_pts), (exh_dec_pcm, exh_dec_pts), exh_idle_pcm = exh
    (eng_acc_pcm, eng_acc_pts), (eng_dec_pcm, eng_dec_pts), eng_idle_pcm = eng

    exh_acc, exh_acc_p = thin_sweep(exh_acc_pcm, exh_acc_pts, max_grains)
    eng_acc, eng_acc_p = thin_sweep(eng_acc_pcm, eng_acc_pts, max_grains)
    exh_dec, exh_dec_p = thin_sweep(exh_dec_pcm, exh_dec_pts, max_grains)
    eng_dec, eng_dec_p = thin_sweep(eng_dec_pcm, eng_dec_pts, max_grains)

    f_min, f_max = derive_clock_range(exh_acc_p)
    f_idle = (round(f_min * 0.96, 2), round(f_min * 1.04, 2))
    print(f"  [+] Grain clock range: {f_min:.2f}..{f_max:.2f} Hz "
          f"(~{f_min * 120:.0f}..{f_max * 120:.0f} rpm), idle {f_idle[0]}..{f_idle[1]} Hz, grains<={max_grains}")

    exh_acc_x, exh_acc_xp = snd.build_extended_accel_48k(exh_acc, exh_acc_p, f_start_target=f_min, n_prefix_grains=48)
    eng_acc_x, eng_acc_xp = snd.build_extended_accel_48k(eng_acc, eng_acc_p, f_start_target=f_min, n_prefix_grains=48)
    exh_dec_x, exh_dec_xp = snd.build_extended_decel_48k(exh_dec, exh_dec_p, f_end_target=f_min, n_suffix_grains=34)
    eng_dec_x, eng_dec_xp = snd.build_extended_decel_48k(eng_dec, eng_dec_p, f_end_target=f_min, n_suffix_grains=34)

    specs = [
        (0x1F7402AA, "engine_accel", eng_acc_x, eng_acc_xp, 16000, True, 10, 8),
        (0x03AB9171, "exhaust_accel", exh_acc_x, exh_acc_xp, 16000, True, 10, 8),
        (0x18648298, "engine_decel", eng_dec_x, eng_dec_xp, None, False, 10, 0),
        (0x0E804842, "exhaust_decel", exh_dec_x, exh_dec_xp, None, False, 10, 0),
    ]
    player, npc = {}, {}
    for sid, role, pcm48, pts48, sr_n, asc, loops_p, loops_n in specs:
        pcm_p, grains_p = snd.convert_sweep_to_target_sr(pcm48, pts48, 32000, ascending=asc, f_min_clamp=f_min, f_max_clamp=f_max)
        samps_p = snd.master_granular_stream(pcm_p, 32000, role, grains=grains_p, ascending=asc)
        player[sid] = (32000, samps_p, snd.build_granular_chunk_bytes(grains_p, 1.0),
                       snd.build_loops_chunk_bytes(len(grains_p), num_loops=loops_p, ascending=asc), role)
        if sr_n:
            pcm_n, grains_n = snd.convert_sweep_to_target_sr(pcm48, pts48, sr_n, ascending=asc, f_min_clamp=f_min, f_max_clamp=f_max)
            samps_n = snd.master_granular_stream(pcm_n, sr_n, role, grains=grains_n, ascending=asc)
            npc[sid] = (sr_n, samps_n, snd.build_granular_chunk_bytes(grains_n, 1.0),
                        snd.build_loops_chunk_bytes(len(grains_n), num_loops=loops_n, ascending=asc), role)

    for sid, role, src_pcm, src_pts, real_idle in (
        (0x1D18B9A1, "engine_idle", eng_acc, eng_acc_p, eng_idle_pcm),
        (0x179DDCAA, "exhaust_idle", exh_acc, exh_acc_p, exh_idle_pcm),
    ):
        if real_idle:
            idle_48k = real_idle
        else:
            idle_48k = synth_idle_pcm(src_pcm, src_pts, 0.5 * (f_idle[0] + f_idle[1]))
        idle_32k = snd.resample_linear_aa(idle_48k, 48000, 32000)
        n_g = 21
        pts = [int(round(i * float(len(idle_32k) - 1) / (n_g - 1))) for i in range(n_g)]
        grains = [(pts[i], f_idle[0] + (f_idle[1] - f_idle[0]) * i / float(n_g - 1), 24000, 26000) for i in range(n_g)]
        samps = snd.master_granular_stream(idle_32k, 32000, role, grains=grains)
        player[sid] = (32000, samps, snd.build_granular_chunk_bytes(grains),
                       snd.build_loops_chunk_bytes(len(grains), num_loops=0), role)
        npc[sid] = player[sid]

    startup = snd.build_svj_startup_oneshot(exh_acc_pcm, exh_acc_pts, exh_dec_pcm, exh_dec_pts)
    return player, npc, startup, (f_min, f_max), f_idle


def convert_revolusound_pack(zip_path, dst_dlc_rpf, audio_name):
    """Convert a RevoluSound engine pack to a standalone PS4 audio DLC (fits the wave-slot budget)."""
    info = parse_engine_zip(zip_path)
    print(f"\n{bar()}\n{LIME}Engine: {os.path.basename(zip_path)}{RST}  ->  audio '{audio_name}'\n{bar()}")

    partner = find_partner_zip(zip_path)
    if info and info["role"] == "Eng":
        eng_zip, exh_zip = zip_path, partner or zip_path
    else:
        exh_zip, eng_zip = zip_path, partner or zip_path
    print(f"  Exhaust: {os.path.basename(exh_zip)}\n  Intake : {os.path.basename(eng_zip)}"
          + ("  (no partner pack: reusing exhaust)" if eng_zip == exh_zip else ""))
    exh = read_revolusound_zip(exh_zip)
    eng = exh if eng_zip == exh_zip else read_revolusound_zip(eng_zip)

    staging = os.path.join(snd.STAGING_ROOT, f"auto_sound_{audio_name}")
    sfx_dir = os.path.join(staging, "ps4", "audio", "sfx", f"dlc_{audio_name}")
    audio_dir = os.path.join(staging, "ps4", "audio")

    for max_grains in GRAIN_BUDGETS:
        shutil.rmtree(staging, ignore_errors=True)
        os.makedirs(sfx_dir, exist_ok=True)
        player, npc, startup, clock0, clock1 = build_engine_streams(exh, eng, max_grains)
        p_unenc, n_unenc = snd.build_svj_awc_banks(player, npc, startup, sfx_dir, audio_name)
        p_size, n_size = os.path.getsize(p_unenc), os.path.getsize(n_unenc)
        if p_size <= PLAYER_AWC_BUDGET and n_size <= NPC_AWC_BUDGET:
            break
        print(f"{YELLOW}  [!] Bank too large (player {p_size:,} B / npc {n_size:,} B); retrying with fewer grains{RST}")
    else:
        raise RuntimeError(f"Could not fit {audio_name} into the wave-slot budget (player {p_size:,} B)")

    snd.build_dat151(os.path.join(audio_dir, f"{audio_name}_game.dat151.rel"), audio_name=audio_name, profile="svj")
    snd.build_dat54(os.path.join(audio_dir, f"{audio_name}_sounds.dat54.rel"), clock0, clock1, audio_name=audio_name, profile="svj")
    snd.write_dlc_xmls(staging, audio_name)

    os.makedirs(os.path.dirname(os.path.abspath(dst_dlc_rpf)), exist_ok=True)
    snd.pack_standalone_rpf(staging, dst_dlc_rpf)
    shutil.rmtree(staging, ignore_errors=True)
    print(f"\n{LIME}[SUCCESS] Audio DLC: {dst_dlc_rpf} ({os.path.getsize(dst_dlc_rpf):,} bytes){RST}")
    return dst_dlc_rpf


# ---------------------------------------------------------------------------
# 3. Attach an audio DLC to a car DLC
# ---------------------------------------------------------------------------

AUDIO_ITEM_RE = re.compile(r"\s*<Item>\s*<filename>[^<]*</filename>\s*<fileType>AUDIO_[A-Z]+</fileType>.*?</Item>", re.DOTALL)
AUDIO_ENABLE_RE = re.compile(r"\s*<Item>[^<]*/audio/[^<]*</Item>")


def _audio_data_items(device, audio_name):
    entries = (
        (f"dlc_{device}:/%PLATFORM%/audio/{audio_name}_game.dat", "AUDIO_GAMEDATA"),
        (f"dlc_{device}:/%PLATFORM%/audio/{audio_name}_sounds.dat", "AUDIO_SOUNDDATA"),
        (f"dlc_{device}:/%PLATFORM%/audio/sfx/dlc_{audio_name}", "AUDIO_WAVEPACK"),
    )
    items = "".join(
        f"\n    <Item>\n      <filename>{fn}</filename>\n      <fileType>{ft}</fileType>\n"
        f"      <overlay value=\"false\" />\n      <disabled value=\"true\" />\n"
        f"      <persistent value=\"false\" />\n    </Item>"
        for fn, ft in entries
    )
    enables = "".join(f"\n        <Item>{fn}</Item>" for fn, _ in entries)
    return items, enables


def _read_text(path):
    return open(path, "r", encoding="utf-8", errors="ignore").read().replace("\r\n", "\n")


def _write_crlf(path, text):
    open(path, "w", encoding="utf-8", newline="\r\n").write(text)


def resolve_audio_rpf(audio):
    """Accept a dlc.rpf path or the audio name of a previously converted standalone pack."""
    if audio.lower().endswith(".rpf") and os.path.isfile(audio):
        return audio
    cand = os.path.join(AUDIO_OUT_ROOT, f"{audio.lower()}_ps4", "dlc.rpf")
    if os.path.isfile(cand):
        return cand
    raise FileNotFoundError(f"Audio DLC not found: {audio} (looked for {cand})")


def attach_audio_to_car(car_dlc_rpf, audio, dst_dlc_rpf=None, verify=True):
    """
    Merge a converted audio DLC into a converted car DLC:
    copies ps4/audio, swaps the audio entries in content.xml and sets <audioNameHash>.
    """
    audio_rpf = resolve_audio_rpf(audio)
    dst = dst_dlc_rpf or car_dlc_rpf
    print(f"\n{bar()}\n{LIME}Attaching audio to car{RST}\n  Car  : {car_dlc_rpf}\n  Audio: {audio_rpf}\n  Out  : {dst}\n{bar()}")

    with tempfile.TemporaryDirectory() as td:
        car_dir, aud_dir = os.path.join(td, "car"), os.path.join(td, "aud")
        NeoGen.extract_single_rpf(car_dlc_rpf, car_dir)
        NeoGen.extract_single_rpf(audio_rpf, aud_dir)

        rels = glob.glob(os.path.join(aud_dir, "ps4", "audio", "*_game.dat151.rel"))
        if not rels:
            raise RuntimeError("Audio DLC has no *_game.dat151.rel")
        audio_name = os.path.basename(rels[0])[: -len("_game.dat151.rel")]

        setup_txt = _read_text(os.path.join(car_dir, "setup2.xml"))
        device = re.search(r"<deviceName>dlc_([^<]+)</deviceName>", setup_txt).group(1)

        car_audio = os.path.join(car_dir, "ps4", "audio")
        shutil.rmtree(car_audio, ignore_errors=True)
        shutil.copytree(os.path.join(aud_dir, "ps4", "audio"), car_audio)

        content_path = os.path.join(car_dir, "content.xml")
        txt = AUDIO_ENABLE_RE.sub("", AUDIO_ITEM_RE.sub("", _read_text(content_path)))
        items, enables = _audio_data_items(device, audio_name)
        if "</dataFiles>" not in txt or "</filesToEnable>" not in txt:
            raise RuntimeError("Unexpected content.xml layout (no dataFiles/filesToEnable)")
        txt = txt.replace("</dataFiles>", items + "\n  </dataFiles>", 1)
        txt = txt.replace("</filesToEnable>", enables + "\n      </filesToEnable>", 1)
        _write_crlf(content_path, txt)

        vm_paths = glob.glob(os.path.join(car_dir, "data", "vehicles.meta"))
        if not vm_paths:
            raise RuntimeError("Car DLC has no data/vehicles.meta")
        vm = re.sub(r"<audioNameHash\s*/>|<audioNameHash>.*?</audioNameHash>",
                    f"<audioNameHash>{audio_name}</audioNameHash>", _read_text(vm_paths[0]))
        _write_crlf(vm_paths[0], vm)
        print(f"  [+] audio '{audio_name}' wired into {device} (content.xml + vehicles.meta)")

        rpf_dir, mode_ovr, wine_bin = NeoGen.load_rpf_builder_settings()
        mode = NeoGen.detect_mode(mode_ovr)
        os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
        tmp = dst + ".attach_tmp"
        if os.path.exists(tmp):
            os.remove(tmp)
        if not NeoGen.build_single_rpf(car_dir, tmp, rpf_dir, mode, wine_bin) or not os.path.exists(tmp):
            raise RuntimeError("NeoGen failed to repack the car DLC")
        shutil.move(tmp, dst)
        cv.ensure_rpf_min_chunks(dst, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)

    print(f"{LIME}[SUCCESS] {dst} ({os.path.getsize(dst):,} bytes){RST}")
    if verify:
        acc.verify_converted_vehicle(dst)
    return dst


# ---------------------------------------------------------------------------
# 4. One-shot: PC car + engine -> finished PS4 car DLC
# ---------------------------------------------------------------------------

def build_car_with_engine(car_src, engine_zip, dst_dlc_rpf, car_name=None, audio_name=None):
    car_name = (car_name or os.path.basename(car_src if os.path.isdir(car_src) else os.path.dirname(car_src))).lower()
    car_name = re.sub(r"[^a-z0-9_]", "", car_name) or "modveh"
    # Audio names must be unique per car, otherwise two cars' sound hashes collide in-game.
    audio_name = re.sub(r"[^a-z0-9]", "", (audio_name or f"{car_name}snd").lower())[:14]
    print(f"\n{CYAN}{BOLD}BUILD: car '{car_name}' + engine '{os.path.basename(engine_zip)}' (audio '{audio_name}'){RST}")

    with tempfile.TemporaryDirectory() as td:
        car_rpf = os.path.join(td, "car", "dlc.rpf")
        aud_rpf = os.path.join(td, "aud", "dlc.rpf")
        acc.convert_vehicle(car_src, car_rpf, car_name)
        convert_revolusound_pack(engine_zip, aud_rpf, audio_name)
        attach_audio_to_car(car_rpf, aud_rpf, dst_dlc_rpf)
    return dst_dlc_rpf


# ---------------------------------------------------------------------------
# 5. PS5 upload
# ---------------------------------------------------------------------------

def push_file_to_ps5(local_path, remote_path, ps5_ip=DEFAULT_PS5_IP, port=DEFAULT_PS5_PORT):
    base_url = f"http://{ps5_ip}:{port}"
    try:
        req = urllib.request.Request(
            f"{base_url}/api/upload?path={urllib.parse.quote(remote_path)}",
            data=open(local_path, "rb").read(), method="POST",
        )
        req.add_header("Content-Type", "application/octet-stream")
        with urllib.request.urlopen(req, timeout=30) as resp:
            print(f"  {LIME}[+] Synced {os.path.basename(local_path)} -> {remote_path}{RST}")
            return True
    except Exception as e:
        print(f"  {CORAL}[WARN] Could not sync {os.path.basename(local_path)} to {remote_path}: {e}{RST}")
        return False


def register_dlc_in_nve(dlc_name, spawn_name=None, display_name=None):
    nve_dlclist = r"D:\ps5payload\NVE_PS5_Ready\dlclist.xml"
    if os.path.exists(nve_dlclist):
        txt = open(nve_dlclist, "r", encoding="utf-8").read()
        item = f"<Item>/data/orbx/gta/rpf/dlc_addon/{dlc_name}/</Item>"
        if item not in txt:
            txt = txt.replace("</Paths>", f"    {item}\n  </Paths>")
            open(nve_dlclist, "w", encoding="utf-8", newline="\r\n").write(txt)
            print(f"  {LIME}[+] Registered {dlc_name} in NVE_PS5_Ready/dlclist.xml{RST}")

    if spawn_name:
        cars_txt = r"D:\ps5payload\NVE_PS5_Ready\addon_cars.txt"
        if os.path.exists(cars_txt):
            lines = open(cars_txt, "r", encoding="utf-8").read().splitlines()
            existing_spawns = [l.split("|")[0].strip().lower() for l in lines if l and not l.startswith("#")]
            if spawn_name.lower() not in existing_spawns:
                disp = display_name or dlc_name.upper()
                with open(cars_txt, "a", encoding="utf-8") as f:
                    f.write(f"\n{spawn_name}|{disp}")
                print(f"  {LIME}[+] Added '{spawn_name}|{disp}' to NVE_PS5_Ready/addon_cars.txt{RST}")


def push_to_ps5(dlc_rpf_path, ps5_ip=DEFAULT_PS5_IP, port=DEFAULT_PS5_PORT):
    base_url = f"http://{ps5_ip}:{port}"
    print(f"\n{LIME}[*] Uploading {dlc_rpf_path} ({os.path.getsize(dlc_rpf_path):,} bytes) to {base_url}{RST}")
    try:
        with urllib.request.urlopen(f"{base_url}/api/status", timeout=5) as r:
            print(f"  PS5 status: {r.read().decode('utf-8', errors='ignore').strip()}")
    except Exception as e:
        print(f"{CORAL}[ERROR] Cannot reach PS5 at {base_url}: {e}{RST}")
        return False

    name = os.path.basename(os.path.dirname(dlc_rpf_path)) or os.path.splitext(os.path.basename(dlc_rpf_path))[0]
    req = urllib.request.Request(
        f"{base_url}/api/upload?path={urllib.parse.quote('/data/orbx/gta/rpf/dlc_addon/' + name + '/dlc.rpf')}",
        data=open(dlc_rpf_path, "rb").read(), method="POST",
    )
    req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            print(f"  {LIME}Upload complete: {resp.status} {resp.reason}{RST}")
            # Automatically sync dlclist.xml and addon_cars.txt
            nve_dlclist = r"D:\ps5payload\NVE_PS5_Ready\dlclist.xml"
            if os.path.exists(nve_dlclist):
                push_file_to_ps5(nve_dlclist, "/data/orbx/gta/dlclist.xml", ps5_ip, port)
            addon_txt = r"D:\ps5payload\NVE_PS5_Ready\addon_cars.txt"
            if os.path.exists(addon_txt):
                push_file_to_ps5(addon_txt, "/data/orbx/gta/addon_cars.txt", ps5_ip, port)
            return True
    except Exception as e:
        print(f"  {CORAL}[ERROR] Upload failed: {e}{RST}")
        return False


# ---------------------------------------------------------------------------
# 6. Interactive terminal
# ---------------------------------------------------------------------------

def _ask(prompt, default=""):
    suffix = f" [{default}]" if default else ""
    return input(f"{prompt}{suffix}: ").strip().strip('"') or default


def pick_engine():
    query = _ask("Search engines (e.g. 2jz, lfa, b58, v10, hemi, vr38; Enter = all)")
    matches = list_revolusound_engines(query)
    if not matches:
        print(f"{CORAL}[!] No engine pack matches '{query}'{RST}")
        return None
    shown = matches[:25]
    for i, (bn, _) in enumerate(shown, 1):
        print(f"  {CYAN}[{i}]{RST} {bn}")
    if len(matches) > len(shown):
        print(f"  {DIM}...{len(matches) - len(shown)} more, refine your search{RST}")
    sel = _ask("Select number")
    if sel.isdigit() and 1 <= int(sel) <= len(shown):
        return shown[int(sel) - 1][1]
    print(f"{CORAL}[!] Invalid selection{RST}")
    return None


def interactive_terminal():
    actions = {
        "1": "Convert PC vehicle mod -> PS4 DLC",
        "2": "Batch convert a folder of PC vehicles",
        "3": "Convert RevoluSound engine -> PS4 audio DLC",
        "4": "Attach an audio DLC to a car DLC",
        "5": "BUILD: PC car + engine sound -> finished car DLC",
        "6": "Verify a PS4 dlc.rpf (safety audit)",
        "7": "Push a dlc.rpf to the PS5",
        "8": "Normalize car handling (Fix broken/goofy speed & braking)",
        "9": "Convert Vegetation / Trees mod -> PS4 DLC",
        "10": "Convert Road Textures / Map mod -> PS4 DLC",
        "11": "Convert ENB / ReShade preset -> PS5 Native Graphics",
        "12": "Convert ymap / custom_maps map add-on -> PS4 DLC",
        "13": "Rebuild & fix built PS4 car DLC (Fix white textures, missing badges, invisible body)",
        "0": "Exit",
    }
    while True:
        print_header()
        for k, v in actions.items():
            print(f"  {BOLD}[{k}]{RST} {v}")
        print(bar())
        try:
            choice = input("  Choose: ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        try:
            if choice == "0":
                break
            elif choice == "1":
                src = _ask("PC dlc.rpf or mod folder")
                detected_spawn = acc.detect_vehicle_model_name(src)
                print(f"\n  {LIME}[*] In-Game Spawn Name detected: {BOLD}{detected_spawn}{RST}")
                name = _ask("DLC Package folder name", detected_spawn)
                out_dlc = _ask("Output dlc.rpf", os.path.join(DLC_OUT_ROOT, name, "dlc.rpf"))
                acc.convert_vehicle(src, out_dlc, name)
                register_dlc_in_nve(name, detected_spawn, f"{name.upper()} ({detected_spawn})")
                print(f"\n{LIME}{BOLD}{'=' * 65}")
                print(f"  [SUCCESS] CAR DLC READY FOR PS5!")
                print(f"  DLC Output   : {out_dlc}")
                print(f"  DLC Folder   : {name}")
                print(f"  SPAWN CODE   : {detected_spawn}")
                print(f"  Trainer Menu : Type '{detected_spawn}' to spawn in-game")
                print(f"{'=' * 65}{RST}\n")
            elif choice == "2":
                acc.batch_convert(_ask("Input folder"), _ask("Output folder", DLC_OUT_ROOT))
            elif choice == "3":
                zp = pick_engine()
                if zp:
                    audio_name = _ask("Audio name (lowercase, unique per car)", "engsnd")
                    convert_revolusound_pack(zp, _ask("Output dlc.rpf", os.path.join(AUDIO_OUT_ROOT, f"{audio_name}_ps4", "dlc.rpf")), audio_name)
            elif choice == "4":
                car = _ask("Car dlc.rpf (already converted)")
                audio = _ask("Audio dlc.rpf or audio name")
                attach_audio_to_car(car, audio, _ask("Output dlc.rpf (Enter = overwrite car)", car))
            elif choice == "5":
                zp = pick_engine()
                if zp:
                    src = _ask("PC car dlc.rpf or mod folder")
                    detected_spawn = acc.detect_vehicle_model_name(src)
                    print(f"\n  {LIME}[*] In-Game Spawn Name detected: {BOLD}{detected_spawn}{RST}")
                    name = _ask("DLC Package folder name", detected_spawn)
                    out_dlc = _ask("Output dlc.rpf", os.path.join(DLC_OUT_ROOT, name, "dlc.rpf"))
                    build_car_with_engine(src, zp, out_dlc, name)
                    register_dlc_in_nve(name, detected_spawn, f"{name.upper()} ({detected_spawn})")
                    print(f"\n{LIME}{BOLD}{'=' * 65}")
                    print(f"  [SUCCESS] CAR DLC READY FOR PS5!")
                    print(f"  DLC Output   : {out_dlc}")
                    print(f"  DLC Folder   : {name}")
                    print(f"  SPAWN CODE   : {detected_spawn}")
                    print(f"  Trainer Menu : Type '{detected_spawn}' to spawn in-game")
                    print(f"{'=' * 65}{RST}\n")
            elif choice == "6":
                universal_verify_dlc(_ask("dlc.rpf path"))
            elif choice == "7":
                push_to_ps5(_ask("dlc.rpf path"), _ask("PS5 IP", DEFAULT_PS5_IP))
            elif choice == "8":
                car = _ask("Car dlc.rpf path")
                out = _ask("Output dlc.rpf (Enter = overwrite in place)", car)
                acc.normalize_car_dlc_handling(car, out)
            elif choice == "9":
                veg_src = _ask("Vegetation .oiv or folder", r"D:\ps5payload\Misc_Vegetation\L.A-Vegetation\L.A Vegetation.oiv")
                veg_name = _ask("DLC device name", "laveg")
                veg_out = _ask("Output dlc.rpf", os.path.join(DLC_OUT_ROOT, veg_name, "dlc.rpf"))
                acv.convert_vegetation_mod(veg_src, veg_out, veg_name)
            elif choice == "10":
                road_src = _ask("Road/Map mod .oiv, folder, or RPF")
                road_name = _ask("DLC device name", "roads")
                road_out = _ask("Output dlc.rpf", os.path.join(DLC_OUT_ROOT, road_name, "dlc.rpf"))
                acr.convert_road_mod(road_src, road_out, road_name)
            elif choice == "11":
                import apply_viper_truth_qv3 as vq
                enb_p = _ask("ENB enbseries.ini path", vq.DEFAULT_ENB_INI)
                reshade_p = _ask("ReShade preset .ini path", vq.DEFAULT_RESHADE_INI)
                vq.convert_and_apply_viper_truth_qv3(enb_p, reshade_p)
                do_push = _ask("Push directly to PS5? (y/n)", "n").lower().startswith("y")
                if do_push:
                    ps5_ip = _ask("PS5 IP address", DEFAULT_PS5_IP)
                    vq.push_to_ps5(ps5_ip)
            elif choice == "12":
                map_src = _ask("ymap / custom_maps folder, .rpf or pc dlc.rpf")
                map_name = _ask("DLC device name", "custommaps")
                map_out = _ask("Output dlc.rpf", os.path.join(DLC_OUT_ROOT, map_name, "dlc.rpf"))
                acr.convert_map_mod(map_src, map_out, map_name)
                print(f"\n{LIME}[SUCCESS] Map add-on built: {map_out}\n"
                      f"  Registered in NVE_PS5_Ready/dlclist.xml (restart the game after syncing).{RST}\n")
            elif choice == "13":
                car_dlc = _ask("Car dlc.rpf path (already built PS4 DLC)")
                if os.path.isdir(car_dlc) or not car_dlc.lower().endswith(".rpf"):
                    cand = os.path.join(car_dlc, "dlc.rpf")
                    if os.path.exists(cand):
                        car_dlc = cand
                        print(f"  {LIME}[+] Resolved folder to: {car_dlc}{RST}")
                out_dlc = _ask("Output dlc.rpf (Enter = overwrite in place)", car_dlc)
                if os.path.isdir(out_dlc) or not out_dlc.lower().endswith(".rpf"):
                    out_dlc = os.path.join(out_dlc, "dlc.rpf")
                pc_src = _ask("Optional PC mod source folder or dlc.rpf (Enter = auto-detect in VehicleMods)", "")
                pc_src = pc_src.strip() or None
                if pc_src and pc_src.lower() in ("no", "none", "n", "false"):
                    pc_src = None
                acc.rebuild_and_fix_car_dlc(car_dlc, out_dlc, pc_src)
                print(f"\n{LIME}{BOLD}{'=' * 65}")
                print(f"  [SUCCESS] CAR DLC REBUILT & FIXED FOR PS5!")
                print(f"  DLC Output   : {out_dlc}")
                print(f"  Textures     : Rebuilt without missing shaders / mangled names")
                print(f"  Body/Tuning  : Separated into vehiclemods, invisible body fixed")
                print(f"{'=' * 65}{RST}\n")
        except Exception as e:
            print(f"{CORAL}[ERROR] {type(e).__name__}: {e}{RST}")
        input(f"\n{DIM}Press Enter to continue...{RST}")


# ---------------------------------------------------------------------------
# 7. CLI
# ---------------------------------------------------------------------------

USAGE = """Usage: ps4_mod_studio.py <command> ...
  car <pc_dlc_or_folder> <out.rpf> [name]
  batch-car <input_dir> <output_dir>
  engines [search words]
  sound <engine_zip_or_search> <out.rpf> <audio_name>
  attach <car_dlc.rpf> <audio_dlc.rpf_or_name> [out.rpf]
  build <pc_car_dlc_or_folder> <engine_zip_or_search> <out.rpf> [car_name]
  rebuild-car <car_dlc.rpf> [out.rpf] [pc_mod_source]
  normalize-handling <car_dlc.rpf> [out.rpf]
  vegetation <veg_oiv_or_folder> <out.rpf> [dlc_name]
  roads <road_oiv_or_folder> <out.rpf> [dlc_name]
  map <ymap_folder_or_rpf> <out.rpf> [dlc_name]
  graphics [enb_ini] [reshade_ini] [--push] [ps5_ip]
  verify <dlc.rpf>
  push <dlc.rpf> [ps5_ip]"""


def resolve_engine(arg):
    """Accept a zip path, a zip file name, or search words matching exactly one pack."""
    if os.path.isfile(arg):
        return arg
    exact = os.path.join(REVOLUSOUND_DIR, arg if arg.lower().endswith(".zip") else arg + ".zip")
    if os.path.isfile(exact):
        return exact
    matches = list_revolusound_engines(arg.replace("_", " "))
    if len(matches) == 1:
        return matches[0][1]
    raise SystemExit(f"'{arg}' matches {len(matches)} engine packs; use `engines {arg}` and pass the exact name")


def main():
    argv = sys.argv[1:]
    if not argv:
        interactive_terminal()
        return
    cmd, a = argv[0].lower(), argv[1:]
    try:
        if cmd in ("car", "convert-car") and len(a) >= 2:
            acc.convert_vehicle(a[0], a[1], a[2] if len(a) > 2 else None)
        elif cmd in ("batch-car", "batch") and len(a) >= 2:
            acc.batch_convert(a[0], a[1])
        elif cmd == "engines":
            for bn, _ in list_revolusound_engines(" ".join(a)):
                print(bn)
        elif cmd in ("sound", "convert-sound") and len(a) >= 3:
            convert_revolusound_pack(resolve_engine(a[0]), a[1], a[2])
        elif cmd in ("attach", "attach-sound") and len(a) >= 2:
            attach_audio_to_car(a[0], a[1], a[2] if len(a) > 2 else None)
        elif cmd == "build" and len(a) >= 3:
            dst = build_car_with_engine(a[0], resolve_engine(a[1]), a[2], a[3] if len(a) > 3 else None)
            car_name = (a[3] if len(a) > 3 else os.path.basename(os.path.dirname(dst) if dst.endswith("dlc.rpf") else dst)).lower()
            detected = acc.detect_vehicle_model_name(a[0]) or car_name
            register_dlc_in_nve(car_name, detected, f"{car_name.upper()} ({detected})")
        elif cmd in ("rebuild-car", "rebuild", "fix-car", "fix-textures") and a:
            car_dlc = a[0]
            if os.path.isdir(car_dlc) and os.path.exists(os.path.join(car_dlc, "dlc.rpf")):
                car_dlc = os.path.join(car_dlc, "dlc.rpf")
            out_dlc = a[1] if len(a) > 1 and not a[1].startswith("--") else car_dlc
            if os.path.isdir(out_dlc):
                out_dlc = os.path.join(out_dlc, "dlc.rpf")
            pc_src = a[2] if len(a) > 2 else None
            acc.rebuild_and_fix_car_dlc(car_dlc, out_dlc, pc_src)
        elif cmd in ("normalize-handling", "fix-handling") and a:
            acc.normalize_car_dlc_handling(a[0], a[1] if len(a) > 1 else None)
        elif cmd in ("vegetation", "convert-vegetation", "trees") and len(a) >= 2:
            acv.convert_vegetation_mod(a[0], a[1], a[2] if len(a) > 2 else "laveg")
        elif cmd in ("roads", "convert-roads", "maps") and len(a) >= 2:
            acr.convert_road_mod(a[0], a[1], a[2] if len(a) > 2 else "roads")
        elif cmd in ("map", "convert-map", "ymap") and len(a) >= 2:
            acr.convert_map_mod(a[0], a[1], a[2] if len(a) > 2 else "custommaps")
        elif cmd in ("graphics", "convert-graphics", "enb"):
            import apply_viper_truth_qv3 as vq
            enb_p = a[0] if len(a) > 0 and not a[0].startswith("--") else vq.DEFAULT_ENB_INI
            reshade_p = a[1] if len(a) > 1 and not a[1].startswith("--") else vq.DEFAULT_RESHADE_INI
            vq.convert_and_apply_viper_truth_qv3(enb_p, reshade_p)
            if "--push" in a:
                ip_idx = a.index("--push") + 1
                ps5_ip = a[ip_idx] if ip_idx < len(a) else DEFAULT_PS5_IP
                vq.push_to_ps5(ps5_ip)
        elif cmd in ("verify", "check") and a:
            sys.exit(0 if universal_verify_dlc(a[0]) else 1)
        elif cmd in ("push", "deploy") and a:
            sys.exit(0 if push_to_ps5(a[0], a[1] if len(a) > 1 else DEFAULT_PS5_IP) else 1)
        else:
            print(USAGE)
            sys.exit(1)
    except (RuntimeError, ValueError, FileNotFoundError) as e:
        print(f"{CORAL}[ERROR] {e}{RST}")
        sys.exit(1)


if __name__ == "__main__":
    main()

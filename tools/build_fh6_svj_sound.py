#!/usr/bin/env python3
"""
build_fh6_svj_sound.py - Convert Forza Horizon 6 RevoluSound Lamborghini Aventador SVJ
(G_V12RST_LAM_L539_Race_2_Exh & G_V12RST_LAM_L539_Race_1_Exh) into a working GTA V
(PS4/PS5 AFR) granular vehicle sound mod following Vans' st77gtr architecture.

Output staging directory (does NOT overwrite active vehicle conversion folders):
  D:\\ps5payload\\scratch_fh6_svj_sound\\
    ps4\\audio\\<name>_game.dat151.rel
    ps4\\audio\\<name>_sounds.dat54.rel
    ps4\\audio\\sfx\\dlc_<name>\\<name>.awc
    ps4\\audio\\sfx\\dlc_<name>\\<name>_npc.awc
  D:\\ps5payload\\VehicleMods\\fh6_svj_sound_ps4\\dlc.rpf
"""

import argparse
import array
import math
import os
import shutil
import struct
import subprocess
import sys
import wave
import zipfile

ROOT = r"D:\ps5payload"
TOOLS_DIR = os.path.join(ROOT, "tools")
CRYPT_EXE = os.path.join(TOOLS_DIR, "ps4_awc_crypt.exe")

FH6_PACK_DIR = os.path.join(
    ROOT,
    r"vehiclemods\RevoluSound Team Pack 56 2.0 2026-11-08T19-54Z 2k4iF2xd8\Content\mediapc\Audio\EngineSynth",
)
RACE1_ZIP = os.path.join(FH6_PACK_DIR, "G_V12RST_LAM_L539_Race_1_Exh.zip")
RACE2_ZIP = os.path.join(FH6_PACK_DIR, "G_V12RST_LAM_L539_Race_2_Exh.zip")

def _find_audio_template(filename, subfolders=("ps4/audio", "x64/audio", "audio", "")):
    for parent in (
        os.path.join(TOOLS_DIR, "audio_templates"),
        os.path.join(TOOLS_DIR, "scratch_lam_inspect"),
        os.path.join(ROOT, "scratch_lamavgineng"),
        TOOLS_DIR,
    ):
        for sf in subfolders:
            p = os.path.join(parent, sf.replace("/", os.sep), filename) if sf else os.path.join(parent, filename)
            if os.path.isfile(p):
                return p
    return os.path.join(TOOLS_DIR, "audio_templates", "ps4", "audio", filename)

SRC_DAT151 = _find_audio_template("lamavgineng_game.dat151.rel")
SRC_DAT54 = _find_audio_template("lamavgineng_sounds.dat54.rel")
SRC_LAM_AWC = os.path.join(TOOLS_DIR, "test_lam_dec.awc")
if not os.path.isfile(SRC_LAM_AWC):
    SRC_LAM_AWC = _find_audio_template("lamavgineng.awc", ("ps4/audio/sfx/dlc_lamavgineng", "x64/audio/sfx/dlc_lamavgineng"))
SRC_ST77_AWC = os.path.join(TOOLS_DIR, "test_st77_dec.awc")

STAGING_ROOT = os.path.join(ROOT, "scratch_fh6_svj_sound")
FH6_BACKFIRE_DIR = os.path.join(TOOLS_DIR, "fh6_v12_backfires")
if not os.path.isdir(FH6_BACKFIRE_DIR):
    FH6_BACKFIRE_DIR = os.path.join(STAGING_ROOT, "fh6_v12_backfires")
STANDALONE_DLC_RPF = os.path.join(ROOT, r"VehicleMods\fh6_svj_sound_ps4\dlc.rpf")


def ensure_fh6_backfires():
    """
    Ensure all backfire and gearcrack WAV samples exist in tools/fh6_v12_backfires
    and scratch_fh6_svj_sound/fh6_v12_backfires so builds are completely seamless.
    """
    global FH6_BACKFIRE_DIR
    target = os.path.join(TOOLS_DIR, "fh6_v12_backfires")
    staging_target = os.path.join(STAGING_ROOT, "fh6_v12_backfires")
    os.makedirs(target, exist_ok=True)
    try:
        os.makedirs(staging_target, exist_ok=True)
    except Exception:
        pass

    required = [f"gc_{i:02d}.wav" for i in range(10)] + [
        "bf_00.wav", "bf_01.wav", "bf_02.wav", "bf_04.wav", "bf_05.wav",
        "bf_06.wav", "bf_08.wav", "bf_10.wav", "bf_11.wav"
    ]
    cand_dirs = [
        target,
        staging_target,
        r"C:\Users\Cath\.gemini\antigravity\brain\097b9845-a8b6-4603-b8ea-4d417117f293\scratch\fh6_pops",
        os.path.join(ROOT, "tools", "fh6_v12_backfires"),
    ]

    for fn in required:
        dst_path = os.path.join(target, fn)
        if not os.path.isfile(dst_path) or os.path.getsize(dst_path) < 100:
            for cd in cand_dirs:
                sp = os.path.join(cd, fn)
                if os.path.isfile(sp) and os.path.getsize(sp) > 100:
                    try:
                        shutil.copy2(sp, dst_path)
                    except Exception:
                        pass
                    break
        if os.path.isfile(dst_path):
            st_path = os.path.join(staging_target, fn)
            if not os.path.isfile(st_path) or os.path.getsize(st_path) < 100:
                try:
                    shutil.copy2(dst_path, st_path)
                except Exception:
                    pass

    FH6_BACKFIRE_DIR = target
    return FH6_BACKFIRE_DIR

# Maps the 9 DAT54 ExhaustPops & RevLimiterPopSound stream IDs to Forza Horizon 6 RevoluSound Backfire_V12 WAVs
FH6_BACKFIRE_STREAM_MAP = {
    0x1F6A1F6A: "bf_00.wav",  # Seq1 (RevLimiter + ExhaustPops) - heavy V12 bang + crackle tail
    0x0A1834C6: "bf_04.wav",  # Seq1 - sharp crack + rapid overrun crackles
    0x1BB6D804: "bf_06.wav",  # Seq1 - deep thunderous V12 pop + embers
    0x0E897DA9: "bf_08.wav",  # Seq1 - punchy low-mid gunshot bang + tail
    0x1053013D: "bf_10.wav",  # Seq1 - double-tap V12 gunshot
    0x1050E468: "bf_02.wav",  # Seq2 (rapid ExhaustPops overrun cluster) - crisp multi-pop
    0x0D5D9E81: "bf_11.wav",  # Seq2 - fast double crack
    0x0B5ADA7B: "bf_01.wav",  # Seq2 - tight sharp snap
    0x1A1537EF: "bf_05.wav",  # Seq2 - bright high-RPM whip crack
}

GRANULAR_SIDS = {
    0x1F7402AA,  # engine_accel
    0x03AB9171,  # exhaust_accel
    0x18648298,  # engine_decel
    0x0E804842,  # exhaust_decel
    0x1D18B9A1,  # engine_idle
    0x179DDCAA,  # exhaust_idle
}


# ---------------------------------------------------------------------------
# 1. Microsoft 4-Bit ADPCM Decoder (48,000 Hz Mono WAV inside FH6 EngineSynth)
# ---------------------------------------------------------------------------

ADPCM_COEFF1 = [256, 512, 0, 192, 240, 460, 392]
ADPCM_COEFF2 = [0, -256, 0, 64, 0, -208, -232]
ADPCM_STEP = [230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230]


def decode_ms_adpcm(wav_bytes):
    pos = 12
    sr = 48000
    blk_align = 70
    data = b""
    while pos < len(wav_bytes):
        cid = wav_bytes[pos : pos + 4]
        csz = struct.unpack_from("<I", wav_bytes, pos + 4)[0]
        if cid == b"fmt ":
            _, _, sr, _, blk_align = struct.unpack_from("<HHIIH", wav_bytes, pos + 8)
        elif cid == b"data":
            data = wav_bytes[pos + 8 : pos + 8 + csz]
            break
        pos += 8 + csz + (csz & 1)

    out = []
    for bstart in range(0, len(data), blk_align):
        blk = data[bstart : bstart + blk_align]
        if len(blk) < 7:
            break
        pred, delta, s1, s2 = struct.unpack_from("<Bhhh", blk, 0)
        c1, c2 = ADPCM_COEFF1[pred], ADPCM_COEFF2[pred]
        out.append(float(s2))
        out.append(float(s1))
        for byte in blk[7:]:
            for nib in (byte >> 4, byte & 0x0F):
                snib = nib - 16 if nib >= 8 else nib
                lin = ((s1 * c1 + s2 * c2) >> 8) + snib * delta
                if lin > 32767:
                    lin = 32767
                elif lin < -32768:
                    lin = -32768
                out.append(float(lin))
                s2, s1 = s1, lin
                delta = (delta * ADPCM_STEP[nib]) >> 8
                if delta < 16:
                    delta = 16
    return sr, out


# ---------------------------------------------------------------------------
# 2. Zero-Phase Biquad DSP & Resampling
# ---------------------------------------------------------------------------

def biquad_hpf(fc, sr, q=0.70710678):
    w0 = 2.0 * math.pi * fc / sr
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2.0 * q)
    a0 = 1.0 + alpha
    return (
        ((1.0 + cw) * 0.5) / a0,
        (-(1.0 + cw)) / a0,
        ((1.0 + cw) * 0.5) / a0,
        (-2.0 * cw) / a0,
        (1.0 - alpha) / a0,
    )


def biquad_lpf(fc, sr, q=0.70710678):
    w0 = 2.0 * math.pi * fc / sr
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2.0 * q)
    a0 = 1.0 + alpha
    return (
        ((1.0 - cw) * 0.5) / a0,
        (1.0 - cw) / a0,
        ((1.0 - cw) * 0.5) / a0,
        (-2.0 * cw) / a0,
        (1.0 - alpha) / a0,
    )


def biquad_peaking(fc, sr, q, gain_db):
    if abs(gain_db) < 1e-6 or fc >= sr * 0.48:
        return (1.0, 0.0, 0.0, 0.0, 0.0)
    a_lin = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * fc / sr
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2.0 * q)
    a0 = 1.0 + alpha / a_lin
    return (
        (1.0 + alpha * a_lin) / a0,
        (-2.0 * cw) / a0,
        (1.0 - alpha * a_lin) / a0,
        (-2.0 * cw) / a0,
        (1.0 - alpha / a_lin) / a0,
    )


def biquad_highshelf(fc, sr, gain_db, slope=0.75):
    if abs(gain_db) < 1e-6 or fc >= sr * 0.48:
        return (1.0, 0.0, 0.0, 0.0, 0.0)
    a_lin = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * fc / sr
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw * 0.5 * math.sqrt((a_lin + 1.0 / a_lin) * (1.0 / slope - 1.0) + 2.0)
    two_sq_a = 2.0 * math.sqrt(a_lin) * alpha
    a0 = (a_lin + 1.0) - (a_lin - 1.0) * cw + two_sq_a
    return (
        (a_lin * ((a_lin + 1.0) + (a_lin - 1.0) * cw + two_sq_a)) / a0,
        (-2.0 * a_lin * ((a_lin - 1.0) + (a_lin + 1.0) * cw)) / a0,
        (a_lin * ((a_lin + 1.0) + (a_lin - 1.0) * cw - two_sq_a)) / a0,
        (2.0 * ((a_lin - 1.0) - (a_lin + 1.0) * cw)) / a0,
        ((a_lin + 1.0) - (a_lin - 1.0) * cw - two_sq_a) / a0,
    )


def df2t_pass(buf, coeffs):
    b0, b1, b2, a1, a2 = coeffs
    z1 = 0.0
    z2 = 0.0
    out = [0.0] * len(buf)
    for i, x in enumerate(buf):
        y = b0 * x + z1
        z1 = b1 * x - a1 * y + z2
        z2 = b2 * x - a2 * y
        out[i] = y
    return out


def filtfilt_sos(samples_f, sos_list):
    n = len(samples_f)
    if n < 32:
        return list(samples_f)
    pad = min(256, n - 1)
    e0 = samples_f[0]
    e1 = samples_f[-1]
    left = [2.0 * e0 - samples_f[pad - i] for i in range(pad)]
    right = [2.0 * e1 - samples_f[n - 2 - i] for i in range(pad)]
    buf = left + list(samples_f) + right
    for coeffs in sos_list:
        buf = df2t_pass(buf, coeffs)
        buf.reverse()
        buf = df2t_pass(buf, coeffs)
        buf.reverse()
    return buf[pad : pad + n]


def resample_linear_aa(samples_f, sr_in, sr_out):
    """Zero-phase anti-aliased resample from sr_in to sr_out."""
    if sr_in == sr_out:
        return list(samples_f)
    if sr_out < sr_in:
        fc = 0.45 * sr_out
        samples_f = filtfilt_sos(samples_f, [biquad_lpf(fc, sr_in, 0.7071)])
    n_in = len(samples_f)
    n_out = max(1, int(round(n_in * float(sr_out) / float(sr_in))))
    ratio = float(sr_in) / float(sr_out)
    out = [0.0] * n_out
    max_idx = n_in - 1
    for i in range(n_out):
        pos = i * ratio
        idx = int(pos)
        if idx >= max_idx:
            out[i] = samples_f[max_idx]
        else:
            frac = pos - idx
            out[i] = samples_f[idx] + frac * (samples_f[idx + 1] - samples_f[idx])
    return out


def resample_grain_segment(grain_f, target_len):
    n_in = len(grain_f)
    if n_in == target_len or target_len <= 1:
        return list(grain_f)
    out = [0.0] * target_len
    scale = float(n_in - 1) / float(target_len - 1)
    max_idx = n_in - 1
    for i in range(target_len):
        pos = i * scale
        idx = int(pos)
        if idx >= max_idx:
            out[i] = grain_f[max_idx]
        else:
            frac = pos - idx
            out[i] = grain_f[idx] + frac * (grain_f[idx + 1] - grain_f[idx])
    return out


def compute_rms_and_peak(samples_f):
    if not samples_f:
        return 0.0, 0.0
    sq = 0.0
    pk = 0.0
    for v in samples_f:
        av = v if v >= 0.0 else -v
        if av > pk:
            pk = av
        sq += v * v
    return math.sqrt(sq / len(samples_f)), pk


def soft_limit_samples(samples_f, drive, ceiling=31800.0, knee=0.68):
    thresh = ceiling * knee
    span = ceiling - thresh
    inv_span = 1.0 / span
    tanh = math.tanh
    out = [0.0] * len(samples_f)
    for i, x in enumerate(samples_f):
        v = x * drive
        av = v if v >= 0.0 else -v
        if av <= thresh:
            out[i] = v
        else:
            mag = thresh + span * tanh((av - thresh) * inv_span)
            out[i] = mag if v >= 0.0 else -mag
    return out


def fit_drive_to_rms(samples_f, target_rms, max_drive=18.0, ceiling=31800.0, knee=0.68):
    rms0, _ = compute_rms_and_peak(samples_f)
    if rms0 <= 1e-6:
        return 1.0, list(samples_f)
    lo = 0.20
    hi = min(max_drive, max(1.0, (target_rms / rms0) * 2.5))
    best_drive = min(max_drive, target_rms / rms0)
    for _ in range(12):
        mid = 0.5 * (lo + hi)
        cand = soft_limit_samples(samples_f, mid, ceiling=ceiling, knee=knee)
        c_rms, _ = compute_rms_and_peak(cand)
        best_drive = mid
        if c_rms < target_rms:
            lo = mid
        else:
            hi = mid
    final = soft_limit_samples(samples_f, best_drive, ceiling=ceiling, knee=knee)
    mean_dc = sum(final) / len(final)
    if abs(mean_dc) > 1.0:
        final = [v - mean_dc for v in final]
    return best_drive, final


def to_int16_array(samples_f):
    arr = array.array("h", [max(-32768, min(32767, int(round(v)))) for v in samples_f])
    if len(arr) % 2 != 0:
        arr.append(arr[-1])
    return arr


# ---------------------------------------------------------------------------
# 3. Build Granular Sweeps & Loops from FH6 EngineSynth Stems
# ---------------------------------------------------------------------------

def append_grain_crossfaded(buf, seg, xfade=24):
    """Append grain segment onto buf with smooth boundary alignment."""
    if not buf:
        buf.extend(seg)
        return
    # Smoothly blend first xfade samples of seg from buf[-1] so boundary is C0 continuous
    last_val = buf[-1]
    diff = last_val - seg[0]
    fade_n = min(xfade, len(seg) // 4)
    for k in range(fade_n):
        w = 0.5 * (1.0 + math.cos(math.pi * k / fade_n))
        seg[k] += diff * w
    buf.extend(seg)


def _loop_crossfade_pcm(samples_f, fade_samples=960):
    """Seamlessly blend the end of a PCM slice back into its start for click-free looping."""
    n = len(samples_f)
    k = min(fade_samples, n // 6)
    if k <= 4:
        return list(samples_f)
    out = list(samples_f[: n - k])
    for i in range(k):
        t = 0.5 * (1.0 - math.cos(math.pi * float(i) / float(k)))
        out[i] = samples_f[n - k + i] * (1.0 - t) + samples_f[i] * t
    return out


def build_extended_accel_48k(pcm_48k, pts_48k, f_start_target=10.50, n_prefix_grains=60):
    """
    Extend an FH6 48kHz acceleration sweep (which begins around 20 Hz / 2400 RPM)
    down to f_start_target (10.50 Hz / 1260 RPM) using the first 8 grains of the
    sweep itself so there is zero timbre seam at pts_48k[0].
    """
    f0 = 48000.0 / float(pts_48k[1] - pts_48k[0])
    template_grains = [pcm_48k[pts_48k[i] : pts_48k[i + 1]] for i in range(8)]
    ping_pong = [0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1]

    out_pcm = []
    out_pts = [0]

    for g in range(n_prefix_grains):
        u = float(g) / float(n_prefix_grains)
        # Smoothly ramp frequency from f_start_target up to f0
        f_g = f_start_target + (f0 - f_start_target) * (u + 0.15 * math.sin(math.pi * u)) / 1.0
        if f_g > f0 * 0.995:
            f_g = f0 * (0.94 + 0.055 * u)
        target_len = int(round(48000.0 / f_g))
        tmpl = template_grains[ping_pong[g % len(ping_pong)]]
        # Gently scale amplitude from 0.72x at 10.5 Hz up to 1.00x at f0
        amp = 0.72 + 0.28 * u
        seg = [x * amp for x in resample_grain_segment(tmpl, target_len)]
        append_grain_crossfaded(out_pcm, seg, xfade=24)
        out_pts.append(len(out_pcm))

    base_offset = len(out_pcm)
    # Append the continuous original dyno sweep from pts_48k[0] to pts_48k[-1]
    orig_slice = list(pcm_48k[pts_48k[0] : pts_48k[-1]])
    append_grain_crossfaded(out_pcm, orig_slice, xfade=24)
    p0 = pts_48k[0]
    for p in pts_48k[1:]:
        out_pts.append(base_offset + (p - p0))

    return out_pcm, out_pts


def build_extended_decel_48k(pcm_48k_raw, pts_48k_raw, f_end_target=9.20, n_suffix_grains=38):
    """
    Construct a true high-to-low deceleration sweep where grain windows are ordered
    from highest RPM down to lowest RPM, BUT each individual grain retains its natural
    FORWARD-TIME combustion attack (sharp explosion impulse -> exponential chamber decay).
    This eliminates the hollow, metallic reverse 'pre-ringing' / 'vacuum suck' artifact.
    """
    ng_orig = len(pts_48k_raw) - 1

    out_pcm = []
    out_pts = [0]

    # Re-sequence grain windows in reverse order (high RPM -> low RPM)
    # BUT keep each grain's samples FORWARD!
    for i in reversed(range(ng_orig)):
        s0, s1 = pts_48k_raw[i], pts_48k_raw[i + 1]
        grain_len = s1 - s0
        if grain_len < 16:
            continue

        # Position in decel progression (0.0 = high RPM lift-off, 1.0 = low RPM coast)
        u = float(ng_orig - 1 - i) / float(max(1, ng_orig - 1))

        # Extract forward grain samples
        grain = [float(x) for x in pcm_48k_raw[s0:s1]]

        # Dynamic overrun & engine braking rumble curve:
        # Initial lift-off (u < 0.25) has crisp attack boost
        # Low RPM coasting (u > 0.65) has deep manifold rumble (+5.5 dB)
        boost = 1.05 + 0.85 * (u ** 1.5)
        seg = [x * boost for x in grain]

        append_grain_crossfaded(out_pcm, seg, xfade=28)
        out_pts.append(len(out_pcm))

    # Seamlessly extend coast-down from lowest recorded frequency down to f_end_target (9.20 Hz)
    # matching the exact cam-lobe idle frequency
    ng_sub = len(out_pts) - 1
    ds_last = out_pts[-1] - out_pts[-2]
    f_last = 48000.0 / float(max(1, ds_last))
    f_end = min(f_last, f_end_target)

    # Take 8 forward low-RPM templates
    tail_templates = [out_pcm[out_pts[ng_sub - 8 + k] : out_pts[ng_sub - 7 + k]] for k in range(8)]
    ping_pong = [7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6]

    for g in range(n_suffix_grains):
        u = float(g + 1) / float(n_suffix_grains)
        f_g = f_last - (f_last - f_end) * u
        target_len = int(round(48000.0 / f_g))
        tmpl = tail_templates[ping_pong[g % len(ping_pong)]]
        seg = resample_grain_segment(tmpl, target_len)
        append_grain_crossfaded(out_pcm, seg, xfade=28)
        out_pts.append(len(out_pcm))

    return out_pcm, out_pts


def convert_sweep_to_target_sr(pcm_48k, pts_48k, sr_out, ascending=True, f_min_clamp=None, f_max_clamp=None):
    """
    Resample 48kHz sweep PCM and map grain boundary points to sr_out,
    computing smoothed strictly monotonic pitch_clock values.
    """
    pcm_out = resample_linear_aa(pcm_48k, 48000, sr_out)
    scale = float(sr_out) / 48000.0
    n_samp = len(pcm_out)

    pts_out = []
    for p in pts_48k:
        sp = int(round(p * scale))
        sp = max(0, min(n_samp - 1, sp))
        if pts_out and sp <= pts_out[-1]:
            sp = min(n_samp - 1, pts_out[-1] + 1)
        pts_out.append(sp)

    ng = len(pts_out)
    raw_clocks = []
    for i in range(ng - 1):
        ds = max(1, pts_out[i + 1] - pts_out[i])
        raw_clocks.append(float(sr_out) / float(ds))
    raw_clocks.append(raw_clocks[-1])

    # 7-point moving average to remove integer sample quantization jitter
    smoothed = []
    for i in range(ng):
        w0 = max(0, i - 3)
        w1 = min(ng, i + 4)
        smoothed.append(sum(raw_clocks[w0:w1]) / float(w1 - w0))

    if f_min_clamp is not None and f_max_clamp is not None:
        if ascending:
            smoothed[0] = f_min_clamp
            smoothed[-1] = f_max_clamp
        else:
            smoothed[0] = f_max_clamp
            smoothed[-1] = f_min_clamp

    # Enforce strict monotonicity across all grains
    eps = 0.002
    if ascending:
        for i in range(1, ng):
            if smoothed[i] < smoothed[i - 1] + eps:
                smoothed[i] = smoothed[i - 1] + eps
        # Rescale linearly if end exceeded f_max_clamp slightly
        if f_min_clamp is not None and f_max_clamp is not None:
            lo, hi = smoothed[0], smoothed[-1]
            if hi > lo:
                smoothed = [f_min_clamp + (x - lo) * (f_max_clamp - f_min_clamp) / (hi - lo) for x in smoothed]
    else:
        for i in range(1, ng):
            if smoothed[i] > smoothed[i - 1] - eps:
                smoothed[i] = smoothed[i - 1] - eps
        if f_min_clamp is not None and f_max_clamp is not None:
            hi, lo = smoothed[0], smoothed[-1]
            if hi > lo:
                smoothed = [f_max_clamp - (hi - x) * (f_max_clamp - f_min_clamp) / (hi - lo) for x in smoothed]

    grains = [(pts_out[i], float(smoothed[i]), 24000, 26000) for i in range(ng)]
    return pcm_out, grains


def build_granular_chunk_bytes(grains, tail_float=1.0):
    buf = bytearray()
    for off, pitch, u1, u2 in grains:
        buf.extend(struct.pack("<IfHH", int(off), float(pitch), int(u1), int(u2)))
    buf.extend(struct.pack("<f", float(tail_float)))
    return bytes(buf)


def build_loops_chunk_bytes(num_grain_entries, num_loops=10, ascending=True):
    """
    Build Vans st77gtr-style contiguous granularloops (0xd9) covering 0 .. num_grain_entries-2.
    For idle streams (num_loops=0), writes count=0.
    In GTA 5, Loop 0 is ALWAYS the lowest RPM band, and Loop num_loops-1 is ALWAYS the highest RPM band.
    When ascending=False (decel stream running from high Hz down to low Hz),
    Loop 0 must map to the tail grains (lowest Hz) and Loop num_loops-1 to the head grains (highest Hz).
    """
    if num_loops <= 0:
        return struct.pack("<I", 0)

    usable = max(1, num_grain_entries - 1)
    # Weight loop grain counts proportional to grain frequency (fewer grains at low Hz, more at high Hz)
    weights = [1.0 + 1.8 * (i / float(max(1, num_loops - 1))) for i in range(num_loops)]
    w_sum = sum(weights)
    counts = [max(8, int(round(usable * w / w_sum))) for w in weights]
    diff = usable - sum(counts)
    counts[-1] = max(4, counts[-1] + diff)

    buf = bytearray(struct.pack("<I", num_loops))
    if ascending:
        cur = 0
        for gc in counts:
            idxs = list(range(cur, min(usable, cur + gc)))
            if not idxs:
                idxs = [usable - 1]
            cur += len(idxs)
            buf.extend(struct.pack("<III", 2, len(idxs), 0x4C633D07))
            buf.extend(struct.pack(f"<{len(idxs)}I", *idxs))
    else:
        # Descending: grain 0 is max RPM (~71 Hz) and grain usable-1 is min RPM (~9.2 Hz).
        # Loop 0 (lowest RPM) must take grains from the tail (min RPM).
        # Loop 9 (highest RPM) must take grains from the head (max RPM).
        cur = usable
        loop_slices = []
        for gc in counts:
            start = max(0, cur - gc)
            idxs = list(range(start, cur))
            if not idxs:
                idxs = [0]
            cur = start
            loop_slices.append(idxs)
        for idxs in loop_slices:
            buf.extend(struct.pack("<III", 2, len(idxs), 0x4C633D07))
            buf.extend(struct.pack(f"<{len(idxs)}I", *idxs))
    return bytes(buf)


# ---------------------------------------------------------------------------
# 4. EQ & Mastering Profiles for SVJ Granular & One-Shot Streams
# ---------------------------------------------------------------------------

def half(db):
    return db * 0.5


def level_granular_sweep_rms(samples_f, grains, ascending, rms_low, rms_high):
    """
    Smoothly equalize macro loudness across RPM grains so low/mid-RPM decel & accel
    never drop -14 dB into silence under heavy braking, while keeping every grain's
    internal firing-cycle transient texture completely intact.
    """
    ng = len(grains)
    n_tot = len(samples_f)
    if ng < 4:
        return list(samples_f)

    # 1. Compute per-grain RMS and mid-sample index
    g_rms = [0.0] * ng
    g_mid = [0] * ng
    g_clk = [grains[i][1] for i in range(ng)]
    for i in range(ng):
        s0 = grains[i][0]
        s1 = grains[i + 1][0] if i + 1 < ng else n_tot
        s1 = max(s0 + 1, min(n_tot, s1))
        seg = samples_f[s0:s1]
        g_rms[i] = math.sqrt(sum(x * x for x in seg) / len(seg))
        g_mid[i] = (s0 + s1) // 2

    # 2. Smooth RMS across +/-4 neighboring grains (tracks sudden FH6 decel dips cleanly while keeping firing character)
    smooth_rms = [0.0] * ng
    for i in range(ng):
        w0 = max(0, i - 4)
        w1 = min(ng, i + 5)
        smooth_rms[i] = sum(g_rms[w0:w1]) / float(w1 - w0)

    # 3. Compute monotonic pitch-dependent target RMS envelope [rms_low .. rms_high]
    c_min = min(g_clk[0], g_clk[-1])
    c_max = max(g_clk[0], g_clk[-1])
    c_span = max(1e-3, c_max - c_min)
    g_gain = [1.0] * ng
    for i in range(ng):
        u = max(0.0, min(1.0, (g_clk[i] - c_min) / c_span))
        target = rms_low + (rms_high - rms_low) * (u ** 0.75)
        g_gain[i] = max(0.35, min(95.0, target / max(40.0, smooth_rms[i])))

    # 4. Sample-accurate piecewise-linear gain interpolation + soft limiting
    out = [0.0] * n_tot
    gi = 0
    for idx in range(n_tot):
        while gi + 1 < ng and idx >= g_mid[gi + 1]:
            gi += 1
        if idx <= g_mid[0] or gi + 1 >= ng:
            gain = g_gain[0 if idx <= g_mid[0] else ng - 1]
        else:
            m0, m1 = g_mid[gi], g_mid[gi + 1]
            t = (idx - m0) / float(max(1, m1 - m0))
            gain = g_gain[gi] + t * (g_gain[gi + 1] - g_gain[gi])
        out[idx] = samples_f[idx] * gain
    return soft_limit_samples(out, 1.0, ceiling=31800.0, knee=0.68)


def master_granular_stream(samples_f, sr, role, grains=None, ascending=True):
    if role == "engine_accel":
        sos = [
            biquad_hpf(30.0, sr, 0.7071),
            biquad_peaking(82.0, sr, 0.85, +6.5),   # Visceral V12 engine block combustion rumble (audible on all speakers)
            biquad_peaking(145.0, sr, 0.90, +5.0),  # Throaty intake & combustion growl
            biquad_peaking(280.0, sr, 1.00, -2.0),
            biquad_peaking(1750.0, sr, 0.85, +3.2),
            biquad_peaking(3500.0, sr, 0.90, +3.5),
            biquad_highshelf(6400.0, sr, +2.5, 0.75),
        ]
        rms_low, rms_high = 12600.0, 16400.0
    elif role == "exhaust_accel":
        sos = [
            biquad_hpf(28.0, sr, 0.7071),
            biquad_peaking(80.0, sr, 0.85, +6.8),   # Powerful low-end exhaust rumble & pressure
            biquad_peaking(140.0, sr, 0.90, +5.2),  # Tuned exhaust header resonance
            biquad_peaking(290.0, sr, 0.95, -2.0),
            biquad_peaking(1950.0, sr, 0.85, +3.2),
            biquad_peaking(3850.0, sr, 0.90, +4.5),
            biquad_highshelf(6600.0, sr, +3.5, 0.75),
        ]
        rms_low, rms_high = 13800.0, 17500.0
    elif role == "engine_decel":
        sos = [
            biquad_hpf(26.0, sr, 0.7071),
            biquad_peaking(78.0, sr, 0.85, +8.0),   # Deep off-throttle engine braking rumble
            biquad_peaking(140.0, sr, 0.90, +6.5),  # Manifold vacuum thrum on overrun
            biquad_peaking(200.0, sr, 0.95, +5.5),  # Engine decel thrum (Forza throttle-lift growl)
            biquad_peaking(280.0, sr, 1.00, -1.5),
            biquad_peaking(1750.0, sr, 0.85, +3.5),
            biquad_peaking(3500.0, sr, 0.90, +4.0),
            biquad_highshelf(6200.0, sr, +3.0, 0.75),
        ]
        rms_low, rms_high = 14200.0, 17500.0   # Boosted engine decel RMS
    elif role == "exhaust_decel":
        sos = [
            biquad_hpf(24.0, sr, 0.7071),
            biquad_peaking(76.0, sr, 0.85, +8.5),   # Heavy V12 off-throttle overrun exhaust rumble
            biquad_peaking(135.0, sr, 0.90, +7.0),  # Deep manifold resonance
            biquad_peaking(195.0, sr, 0.95, +6.0),  # Throaty Forza overrun gurgle/burble peak
            biquad_peaking(290.0, sr, 0.95, -1.5),
            biquad_peaking(1750.0, sr, 0.85, +4.0), # Crisp overrun crackle texture
            biquad_peaking(3850.0, sr, 0.90, +5.2),
            biquad_highshelf(6600.0, sr, +4.0, 0.75),
        ]
        rms_low, rms_high = 15500.0, 18500.0   # Boosted exhaust decel RMS
    elif role == "engine_idle":
        sos = [
            biquad_hpf(32.0, sr, 0.7071),
            biquad_peaking(82.0, sr, 0.85, +5.5),   # Warm pulsing engine idling thrum
            biquad_peaking(145.0, sr, 0.95, +4.0),
            biquad_peaking(240.0, sr, 1.00, -1.5),
            biquad_peaking(2300.0, sr, 0.90, +2.8),
            biquad_highshelf(5800.0, sr, +2.2, 0.75),
        ]
        rms_low, rms_high = 12600.0, 12600.0
    else:  # exhaust_idle
        sos = [
            biquad_hpf(28.0, sr, 0.7071),
            biquad_peaking(80.0, sr, 0.85, +6.0),   # Deep low exhaust idling pulse
            biquad_peaking(140.0, sr, 0.90, +4.5),
            biquad_peaking(250.0, sr, 1.00, -1.5),
            biquad_peaking(2600.0, sr, 0.90, +3.0),
            biquad_highshelf(6000.0, sr, +2.8, 0.75),
        ]
        rms_low, rms_high = 13800.0, 13800.0

    filtered = filtfilt_sos(samples_f, sos)
    if grains is not None and len(grains) > 30:
        limited = level_granular_sweep_rms(filtered, grains, ascending, rms_low, rms_high)
    else:
        _, limited = fit_drive_to_rms(filtered, rms_low, ceiling=31800.0, knee=0.68)
    return to_int16_array(limited)


def _thin_sweep_48k(pcm_in, pts_in, keep_first=32, drop_mod=4):
    out_pcm = []
    out_pts = [0]
    for i in range(len(pts_in) - 1):
        if i >= keep_first and (i % drop_mod) == (drop_mod - 1):
            continue
        seg = list(pcm_in[pts_in[i] : pts_in[i + 1]])
        append_grain_crossfaded(out_pcm, seg, xfade=16)
        out_pts.append(len(out_pcm))
    return out_pcm, out_pts


def extract_fh6_svj_granular_streams():
    """
    Extract, decode, and build the 6 player (32kHz) and 4 NPC (16kHz/32kHz) granular streams
    from Forza Horizon 6 RevoluSound L539 V12 packs.
    """
    print("[1/5] Extracting & decoding Forza Horizon 6 SVJ V12 EngineSynth packs...")
    with zipfile.ZipFile(RACE1_ZIP, "r") as z1:
        r1_acc_pts = [int(x) for x in z1.read("Exh_Acc_G_V12RST_LAM_L539_Race_1_Acc_Sweep_GS.dat").decode().split()]
        _, r1_acc_pcm = decode_ms_adpcm(z1.read("Exh_Acc_G_V12RST_LAM_L539_Race_1_Acc_Sweep_GS.dat_ADPCM.wav"))
        r1_dec_pts = [int(x) for x in z1.read("Exh_Dec_G_V12RST_LAM_L539_Race_1_Dec_Sweep_GS.dat").decode().split()]
        _, r1_dec_pcm = decode_ms_adpcm(z1.read("Exh_Dec_G_V12RST_LAM_L539_Race_1_Dec_Sweep_GS.dat_ADPCM.wav"))
        _, r1_idle_pcm = decode_ms_adpcm(z1.read("Idle_Dec_1000_ADPCM.wav"))

    with zipfile.ZipFile(RACE2_ZIP, "r") as z2:
        r2_acc_pts = [int(x) for x in z2.read("Exh_Acc_G_V12RST_LAM_L539_Race_2_Acc_Sweep_GS.dat").decode().split()]
        _, r2_acc_pcm = decode_ms_adpcm(z2.read("Exh_Acc_G_V12RST_LAM_L539_Race_2_Acc_Sweep_GS.dat_ADPCM.wav"))
        r2_dec_pts = [int(x) for x in z2.read("Exh_Dec_G_V12RST_LAM_L539_Race_2_Dec_Sweep_GS.dat").decode().split()]
        _, r2_dec_pcm = decode_ms_adpcm(z2.read("Exh_Dec_G_V12RST_LAM_L539_Race_2_Dec_Sweep_GS.dat_ADPCM.wav"))
        _, r2_idle_pcm = decode_ms_adpcm(z2.read("Idle_Dec_1117_ADPCM.wav"))

    # Idle loops: 48,000 samples @ 48kHz (1.00s) seamlessly crossfaded for continuous warm V12 idle
    eng_idle_loop_48k = _loop_crossfade_pcm(r1_idle_pcm[:min(len(r1_idle_pcm), 48000)], fade_samples=960)
    exh_idle_loop_48k = _loop_crossfade_pcm(r2_idle_pcm[:min(len(r2_idle_pcm), 48000)], fade_samples=960)
    idle_ng = 21
    idle_pts_48k = [int(round(i * len(eng_idle_loop_48k) / float(idle_ng))) for i in range(idle_ng)]
    idle_pts_48k.append(len(eng_idle_loop_48k) - 1)

    # Extend Acc & Dec sweeps smoothly down to 9.40 Hz (1,128 RPM, meeting 1,000-1,117 RPM idle!) .. 70.18 Hz (8,421 RPM)
    F_MIN = 9.40
    F_MAX = 70.18
    F_IDLE_MIN = 9.20
    F_IDLE_MAX = 9.80

    r1_acc_pcm_th, r1_acc_pts_th = _thin_sweep_48k(r1_acc_pcm, r1_acc_pts, keep_first=32, drop_mod=4)
    r2_acc_pcm_th, r2_acc_pts_th = _thin_sweep_48k(r2_acc_pcm, r2_acc_pts, keep_first=32, drop_mod=4)
    r1_dec_pcm_th, r1_dec_pts_th = _thin_sweep_48k(r1_dec_pcm, r1_dec_pts, keep_first=32, drop_mod=4)
    r2_dec_pcm_th, r2_dec_pts_th = _thin_sweep_48k(r2_dec_pcm, r2_dec_pts, keep_first=32, drop_mod=4)

    eng_acc_48k, eng_acc_pts_48k = build_extended_accel_48k(r1_acc_pcm_th, r1_acc_pts_th, f_start_target=F_MIN, n_prefix_grains=38)
    exh_acc_48k, exh_acc_pts_48k = build_extended_accel_48k(r2_acc_pcm_th, r2_acc_pts_th, f_start_target=F_MIN, n_prefix_grains=38)
    eng_dec_48k, eng_dec_pts_48k = build_extended_decel_48k(r1_dec_pcm_th, r1_dec_pts_th, f_end_target=F_MIN, n_suffix_grains=38)
    exh_dec_48k, exh_dec_pts_48k = build_extended_decel_48k(r2_dec_pcm_th, r2_dec_pts_th, f_end_target=F_MIN, n_suffix_grains=38)

    player_streams = {}
    npc_streams = {}

    specs = [
        (0x1F7402AA, "engine_accel", eng_acc_48k, eng_acc_pts_48k, True, F_MIN, F_MAX, 10, True),
        (0x03AB9171, "exhaust_accel", exh_acc_48k, exh_acc_pts_48k, True, F_MIN, F_MAX, 10, True),
        (0x18648298, "engine_decel", eng_dec_48k, eng_dec_pts_48k, False, F_MIN, F_MAX, 10, False),
        (0x0E804842, "exhaust_decel", exh_dec_48k, exh_dec_pts_48k, False, F_MIN, F_MAX, 10, False),
        (0x1D18B9A1, "engine_idle", eng_idle_loop_48k, idle_pts_48k, True, F_IDLE_MIN, F_IDLE_MAX, 0, True),
        (0x179DDCAA, "exhaust_idle", exh_idle_loop_48k, idle_pts_48k, True, F_IDLE_MIN, F_IDLE_MAX, 0, True),
    ]

    for sid, role, pcm48, pts48, asc, fmin, fmax, nloops, in_npc in specs:
        sr_p = 32000
        pcm_p, grains_p = convert_sweep_to_target_sr(pcm48, pts48, sr_p, ascending=asc, f_min_clamp=fmin, f_max_clamp=fmax)
        samps_p = master_granular_stream(pcm_p, sr_p, role, grains=grains_p, ascending=asc)
        gr_bytes_p = build_granular_chunk_bytes(grains_p, tail_float=1.0)
        lp_bytes_p = build_loops_chunk_bytes(len(grains_p), num_loops=nloops, ascending=asc)
        player_streams[sid] = (sr_p, samps_p, gr_bytes_p, lp_bytes_p, role)

        rms_p, pk_p = compute_rms_and_peak(samps_p)
        print(
            f"  [Player] 0x{sid:08x} {role:14s}: sr={sr_p} nsamp={len(samps_p):6d} ({len(samps_p)/sr_p:5.2f}s) "
            f"grains={len(grains_p):3d} loops={nloops:2d} clock={grains_p[0][1]:.2f}->{grains_p[-1][1]:.2f} "
            f"pk={int(pk_p):5d} rms={int(rms_p):5d}"
        )

        if in_npc:
            # Follow st77gtr_npc: downsample accel streams to 16000 Hz, keep idle at 32000 Hz
            sr_n = 16000 if "accel" in role else 32000
            nloops_n = 7 if nloops > 0 else 0
            pcm_n, grains_n = convert_sweep_to_target_sr(pcm48, pts48, sr_n, ascending=asc, f_min_clamp=fmin, f_max_clamp=fmax)
            samps_n = master_granular_stream(pcm_n, sr_n, role, grains=grains_n, ascending=asc)
            gr_bytes_n = build_granular_chunk_bytes(grains_n, tail_float=1.0)
            lp_bytes_n = build_loops_chunk_bytes(len(grains_n), num_loops=nloops_n, ascending=asc)
            npc_streams[sid] = (sr_n, samps_n, gr_bytes_n, lp_bytes_n, role)

    # Also build authentic 2.25s SVJ V12 startup crank + loud bark flare + coast-down settle for 0x1d64b840
    startup_samps = build_svj_startup_oneshot(r2_acc_pcm, r2_acc_pts, r2_dec_pcm, r2_dec_pts)
    return player_streams, npc_streams, startup_samps, (F_MIN, F_MAX), (F_IDLE_MIN, F_IDLE_MAX)


def build_svj_startup_oneshot(r2_acc_pcm_48k, r2_acc_pts_48k, r2_dec_pcm_48k, r2_dec_pts_48k):
    """
    Build a calibrated 1.00s (32,000 samples @ 32kHz) Lamborghini Aventador SVJ cold-start roar:
      - 0.00..0.18s: crisp high-torque starter crank
      - 0.12..0.62s: explosive Forza V12 flare bark (climbing ~2,400 -> ~4,600 RPM)
      - 0.50..0.98s: smooth V12 settle-down fading directly into the 1,000 RPM idle level
      (GTA V vehicle ignition cutoff is ~1.00s; by finishing the settle within 1.0s,
       the transition into looping engine_idle is 100% seamless with zero abrupt cuts).
    """
    st_streams = read_awc_streams(SRC_ST77_AWC)
    starter_arr = array.array("h", st_streams[0x1D64B840][0x55])
    if sys.byteorder != "little":
        starter_arr.byteswap()
    starter_f = [float(x) for x in starter_arr]

    # Flare UP: Forza Race_2 Acc (~0.5s @ 48k -> 32k)
    flare_48k = r2_acc_pcm_48k[:28000]
    flare_32k = resample_linear_aa(flare_48k, 48000, 32000)
    flare_f = [float(x) for x in master_granular_stream(flare_32k, 32000, "exhaust_accel")]

    # Settle DOWN: Forward-transient decel (~0.45s @ 48k -> 32k)
    dec_fwd_pcm, _ = build_extended_decel_48k(r2_dec_pcm_48k, r2_dec_pts_48k, f_end_target=10.0, n_suffix_grains=10)
    dec_start = int(len(dec_fwd_pcm) * 0.45)
    settle_48k = dec_fwd_pcm[dec_start : min(len(dec_fwd_pcm), dec_start + 32000)]
    settle_32k = resample_linear_aa(settle_48k, 48000, 32000)
    settle_f = [float(x) for x in master_granular_stream(settle_32k, 32000, "exhaust_decel")]

    total_len = 32000  # Exactly 1.00s @ 32000 Hz
    out = [0.0] * total_len

    # 0.00..0.18s: crisp starter crank
    starter_len = min(len(starter_f), 5800)
    for i in range(starter_len):
        w = 1.0 if i < 3600 else max(0.0, 1.0 - (i - 3600) / 2200.0)
        out[i] += starter_f[i] * w * 0.88

    # 0.12..0.62s: explosive V12 combustion bark flaring up
    flare_start = 3800
    flare_len = min(total_len - flare_start, len(flare_f), 16000)
    for j in range(flare_len):
        if j < 1600:
            env = math.sin(0.5 * math.pi * j / 1600.0)
        elif j > flare_len - 4800:
            rem = flare_len - j
            env = 0.5 * (1.0 - math.cos(math.pi * rem / 4800.0)) * 1.08
        else:
            env = 1.08
        out[flare_start + j] += flare_f[j] * env

    # 0.50..0.98s: smooth settle down into warm idle
    settle_start = 16000
    settle_len = min(len(settle_f), total_len - settle_start)
    for j in range(settle_len):
        idx = settle_start + j
        if j < 3600:
            env = 0.5 * (1.0 - math.cos(math.pi * j / 3600.0)) * 0.92
        elif idx > total_len - 3200:
            p = float(idx - (total_len - 3200)) / 3200.0
            env = 0.92 - 0.14 * p  # smooth ease down directly into idle level
        else:
            env = 0.92
        out[idx] += settle_f[j] * env

    # Final 500-sample micro-taper to ensure 0-crossing finish
    for k in range(500):
        out[total_len - 500 + k] *= 0.5 * (1.0 + math.cos(math.pi * k / 500.0))

    _, limited = fit_drive_to_rms(out, 13800.0, ceiling=31800.0, knee=0.68)
    return to_int16_array(limited)


def load_fh6_backfire_stream(sid):
    """
    Load and master a Forza Horizon 6 RevoluSound Backfire_V12 WAV for the given DAT54/AWC
    pop stream ID, reusing the shared oneshot mastering chain.
    """
    wav_path = os.path.join(FH6_BACKFIRE_DIR, FH6_BACKFIRE_STREAM_MAP[sid])
    return load_fh6_wav_oneshot(wav_path, target_rms=22500.0, trim_tail=True, hpf_hz=35.0, knee=0.55)


# ---------------------------------------------------------------------------
# 5. AWC Reader / PS4 AWC Builder + XXTEA Encryption
# ---------------------------------------------------------------------------

def read_awc_streams(awc_path):
    data = open(awc_path, "rb").read()
    magic, ver, flags, num_streams, chunk_offset = struct.unpack("<4s2sHII", data[:16])
    assert magic == b"ADAT", f"Invalid AWC magic {magic} in {awc_path}"
    pos = 16
    u1, u2 = struct.unpack_from("<HH", data, pos)
    if (flags & 1) or (u1 == 0 and u2 in range(1, 16)):
        pos += 2 * num_streams
    tags = struct.unpack_from(f"<{num_streams}I", data, pos)
    pos += 4 * num_streams

    streams = {}
    for t in tags:
        cc, sid = t >> 29, t & 0x1FFFFFFF
        by_type = {}
        for _ in range(cc):
            q = struct.unpack_from("<Q", data, pos)[0]
            pos += 8
            coff = q & 0x0FFFFFFF
            csz = (q >> 28) & 0x0FFFFFFF
            ctype = (q >> 56) & 0xFF
            by_type[ctype] = bytearray(data[coff : coff + csz])
        streams[sid] = by_type
    return streams


def compute_peak_val(samples, start, count):
    end = min(len(samples), start + count)
    if start >= end:
        return 0
    m = max(abs(x) for x in samples[start:end])
    return min(m * 2, 65535)


def build_format_and_peak_chunks(samps_i16, sr, is_granular=True, unk_hi_word=0x0000):
    ns = len(samps_i16)
    win0_pk = compute_peak_val(samps_i16, 0, 4096)
    # 24-byte FormatChunk (0xfa) on all streams with Headroom = -191, codec = 0 (16-bit PCM)
    fmt = struct.pack("<IiHhHHHBBHH", ns, -1, sr, -191, 0, 0, 0, 0, 0, win0_pk & 0xFFFF, unk_hi_word & 0xFFFF)

    # Exact AwcFile.cs peakCount formula: (smpCount - 4096) // 4096 = (ns // 4096) - 1
    n_windows = max(0, (ns - 4096) // 4096)
    pk_buf = bytearray(n_windows * 2)
    for k in range(n_windows):
        w_pk = compute_peak_val(samps_i16, (k + 1) * 4096, 4096)
        struct.pack_into("<H", pk_buf, k * 2, w_pk)
    return fmt, bytes(pk_buf)


def pack_ps4_awc(stream_chunks_map, unenc_out_path, enc_out_path):
    """
    Pack a PS4-compliant unencrypted & XXTEA-encrypted .awc file:
      - Header flags = 0xFF0A (no ChunkIndices table)
      - Chunks sorted by ctype ascending (0x36, 0x55, 0x5a, 0xd9, 0xfa) matching st77gtr
      - DataChunk (0x55) 16-byte aligned, GranularGrains/Loops (0x5a/0xd9) 4-byte aligned
    """
    sorted_sids = sorted(stream_chunks_map.keys())
    num_streams = len(sorted_sids)
    tags = []
    total_chunks = 0
    for sid in sorted_sids:
        ch_list = sorted(stream_chunks_map[sid], key=lambda x: x[0])
        stream_chunks_map[sid] = ch_list
        cc = len(ch_list)
        tags.append(((cc & 0x7) << 29) | (sid & 0x1FFFFFFF))
        total_chunks += cc

    chunk_offset = 16 + num_streams * 4 + total_chunks * 8
    cur_offset = chunk_offset
    descriptors = []
    chunk_data = bytearray()

    for sid in sorted_sids:
        for ctype, cb in stream_chunks_map[sid]:
            align = 16 if ctype == 0x55 else (4 if ctype in (0x5A, 0xD9) else 2)
            rem = cur_offset % align
            if rem != 0:
                pad = align - rem
                chunk_data.extend(b"\x00" * pad)
                cur_offset += pad
            c_off = cur_offset
            c_sz = len(cb)
            desc = (c_off & 0x0FFFFFFF) | ((c_sz & 0x0FFFFFFF) << 28) | ((ctype & 0xFF) << 56)
            descriptors.append(desc)
            chunk_data.extend(cb)
            cur_offset += c_sz

    hdr = struct.pack("<4s2sHII", b"ADAT", b"\x01\x00", 0xFF0A, num_streams, chunk_offset)
    tags_bin = struct.pack(f"<{num_streams}I", *tags)
    desc_bin = struct.pack(f"<{total_chunks}Q", *descriptors)
    out_bytes = hdr + tags_bin + desc_bin + bytes(chunk_data)

    os.makedirs(os.path.dirname(unenc_out_path), exist_ok=True)
    os.makedirs(os.path.dirname(enc_out_path), exist_ok=True)
    open(unenc_out_path, "wb").write(out_bytes)

    res = subprocess.run([CRYPT_EXE, "-e", unenc_out_path, enc_out_path], capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"ps4_awc_crypt failed on {unenc_out_path}: {res.stderr}")
    return len(out_bytes), os.path.getsize(enc_out_path)


def build_svj_awc_banks(
    player_granular, npc_granular, startup_samps, dst_sfx_dir, audio_name, backfire_override_map=None
):
    print("\n[2/5] Building Player & NPC PS4 AWC wavepacks (FH6 Granular + Backfire_V12 + GearCrack)...")
    ensure_fh6_backfires()
    orig_lam_streams = read_awc_streams(SRC_LAM_AWC)
    bf_map = backfire_override_map if backfire_override_map is not None else FH6_BACKFIRE_STREAM_MAP

    player_map = {}
    for sid, orig_chunks in orig_lam_streams.items():
        fmt_orig = orig_chunks[0xFA]
        unk_hi = struct.unpack_from("<H", fmt_orig, 22)[0] if len(fmt_orig) >= 24 else 0
        if sid in player_granular:
            sr, samps_i16, gr_bytes, lp_bytes, _role = player_granular[sid]
            fmt_bytes, pk_bytes = build_format_and_peak_chunks(samps_i16, sr, is_granular=True, unk_hi_word=unk_hi)
            raw_pcm = array.array("h", samps_i16)
            if sys.byteorder != "little":
                raw_pcm.byteswap()
            ch_list = []
            if pk_bytes:
                ch_list.append((0x36, pk_bytes))
            ch_list.extend([
                (0x55, raw_pcm.tobytes()),
                (0x5A, gr_bytes),
                (0xD9, lp_bytes),
                (0xFA, fmt_bytes),
            ])
            player_map[sid] = ch_list
        elif sid == 0x1D64B840:
            # Replace silent placeholder with our 32kHz SVJ V12 startup catch + flare
            sr = 32000
            fmt_bytes, pk_bytes = build_format_and_peak_chunks(startup_samps, sr, is_granular=False, unk_hi_word=unk_hi)
            raw_pcm = array.array("h", startup_samps)
            if sys.byteorder != "little":
                raw_pcm.byteswap()
            ch_list = []
            if pk_bytes:
                ch_list.append((0x36, pk_bytes))
            ch_list.extend([
                (0x55, raw_pcm.tobytes()),
                (0xFA, fmt_bytes),
            ])
            player_map[sid] = ch_list
            rms_s, pk_s = compute_rms_and_peak(startup_samps)
            print(f"  [OneShot] 0x{sid:08x} svj_startup   : sr={sr} nsamp={len(startup_samps):6d} pk={int(pk_s):5d} rms={int(rms_s):5d}")
        elif sid in bf_map and os.path.exists(os.path.join(FH6_BACKFIRE_DIR, bf_map[sid])):
            wp = os.path.join(FH6_BACKFIRE_DIR, bf_map[sid])
            sr, samps_i16 = load_fh6_wav_oneshot(wp, target_rms=22500.0, trim_tail=True, hpf_hz=35.0, knee=0.55)
            player_map[sid] = _pack_oneshot_chunks(samps_i16, sr, unk_hi=unk_hi)
            rms_b, pk_b = compute_rms_and_peak(samps_i16)
            print(f"  [FH6 Pop] 0x{sid:08x} {bf_map[sid]:14s}: sr={sr} nsamp={len(samps_i16):6d} pk={int(pk_b):5d} rms={int(rms_b):5d}")
        else:
            # Master existing V12 shutdown / secondary SimpleSound & normalize sr to 32000 Hz
            ns, _lp, sr_orig = struct.unpack_from("<IiH", fmt_orig, 0)
            arr = array.array("h", orig_chunks[0x55][: ns * 2])
            if sys.byteorder != "little":
                arr.byteswap()
            samps_f = [float(x) for x in arr]
            if sr_orig > 32010:
                samps_f = resample_linear_aa(samps_f, sr_orig, 32000)
            sr = 32000
            sos = [
                biquad_hpf(40.0, sr, 0.7071),
                biquad_peaking(260.0, sr, 1.00, half(-1.8)),
                biquad_peaking(2800.0, sr, 0.90, half(+2.0)),
                biquad_highshelf(6200.0, sr, half(+1.8), 0.75),
            ]
            filtered = filtfilt_sos(samps_f, sos)
            orig_rms, _ = compute_rms_and_peak(samps_f)
            target_rms = min(13200.0, max(orig_rms * 1.22, 11000.0))
            _, limited = fit_drive_to_rms(filtered, target_rms, ceiling=31800.0, knee=0.68)
            samps_i16 = to_int16_array(limited)
            fmt_bytes, pk_bytes = build_format_and_peak_chunks(samps_i16, sr, is_granular=False, unk_hi_word=unk_hi)
            raw_pcm = array.array("h", samps_i16)
            if sys.byteorder != "little":
                raw_pcm.byteswap()
            ch_list = []
            if pk_bytes:
                ch_list.append((0x36, pk_bytes))
            ch_list.append((0x55, raw_pcm.tobytes()))
            ch_list.append((0xFA, fmt_bytes))
            player_map[sid] = ch_list

    for i in range(10):
        gc_hash = joaat(f"{audio_name}_gc_{i:02d}")
        gc_sid = gc_hash & 0x1FFFFFFF
        assert gc_sid not in player_map, f"GearCrack SID collision 0x{gc_sid:08x}"
        wp = os.path.join(FH6_BACKFIRE_DIR, f"gc_{i:02d}.wav")
        sr, samps_i16 = load_fh6_wav_oneshot(wp, target_rms=14200.0, trim_tail=False, hpf_hz=55.0, knee=0.62)
        player_map[gc_sid] = _pack_oneshot_chunks(samps_i16, sr, unk_hi=0x0000)
        rms_g, pk_g = compute_rms_and_peak(samps_i16)
        print(f"  [GearCrk] 0x{gc_sid:08x} gc_{i:02d}.wav     : sr={sr} nsamp={len(samps_i16):6d} pk={int(pk_g):5d} rms={int(rms_g):5d}")

    npc_map = {}
    for sid, (sr, samps_i16, gr_bytes, lp_bytes, _role) in npc_granular.items():
        fmt_bytes, pk_bytes = build_format_and_peak_chunks(samps_i16, sr, is_granular=True, unk_hi_word=0x0000)
        raw_pcm = array.array("h", samps_i16)
        if sys.byteorder != "little":
            raw_pcm.byteswap()
        ch_list = []
        if pk_bytes:
            ch_list.append((0x36, pk_bytes))
        ch_list.extend([
            (0x55, raw_pcm.tobytes()),
            (0x5A, gr_bytes),
            (0xD9, lp_bytes),
            (0xFA, fmt_bytes),
        ])
        npc_map[sid] = ch_list

    unenc_dir = os.path.join(STAGING_ROOT, "unencrypted_awc")
    p_unenc = os.path.join(unenc_dir, f"{audio_name}.awc")
    p_enc = os.path.join(dst_sfx_dir, f"{audio_name}.awc")
    n_unenc = os.path.join(unenc_dir, f"{audio_name}_npc.awc")
    n_enc = os.path.join(dst_sfx_dir, f"{audio_name}_npc.awc")

    p_usz, p_esz = pack_ps4_awc(player_map, p_unenc, p_enc)
    n_usz, n_esz = pack_ps4_awc(npc_map, n_unenc, n_enc)
    print(f"  [+] Player AWC: {p_enc} (unenc={p_usz:,} B, enc={p_esz:,} B)")
    print(f"  [+] NPC AWC   : {n_enc} (unenc={n_usz:,} B, enc={n_esz:,} B)")
    return p_unenc, n_unenc


# ---------------------------------------------------------------------------
# 6. Build / Patch DAT151 & DAT54 Metadata (Vans st77gtr Architecture)
# ---------------------------------------------------------------------------

def joaat(s):
    h = 0
    for c in s.lower().encode("ascii"):
        h = (h + c) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h = (h ^ (h >> 6)) & 0xFFFFFFFF
    h = (h + (h << 3)) & 0xFFFFFFFF
    h = (h ^ (h >> 11)) & 0xFFFFFFFF
    h = (h + (h << 15)) & 0xFFFFFFFF
    return h


def ror8_u32(h):
    return ((h >> 8) | ((h & 0xFF) << 24)) & 0xFFFFFFFF


def build_rekey_map(audio_name):
    audio_name = audio_name.lower()
    if audio_name == "lamavgineng":
        return {}
    pairs = [
        ("lamavgineng", audio_name),
        ("lamavgineng_scanner_params", f"{audio_name}_scanner_params"),
        ("lamavgineng_engine", f"{audio_name}_engine"),
        ("lamavgineng_granular_engine", f"{audio_name}_granular_engine"),
        ("lamavgineng_engine_accel", f"{audio_name}_engine_accel"),
        ("lamavgineng_engine_accel_npc", f"{audio_name}_engine_accel_npc"),
        ("lamavgineng_exhaust_accel", f"{audio_name}_exhaust_accel"),
        ("lamavgineng_dump_valve", f"{audio_name}_dump_valve"),
        ("dlc_lamavgineng/lamavgineng", f"dlc_{audio_name}/{audio_name}"),
        ("dlc_lamavgineng/lamavgineng_npc", f"dlc_{audio_name}/{audio_name}_npc"),
    ]
    hmap = {joaat(old): joaat(new) for old, new in pairs}

    # Also re-key all remaining internal sound item hashes in SRC_DAT54 so that
    # zero IndexHashes collide when multiple cars (e.g. 27rev + svj63) are loaded simultaneously
    b54_orig = open(SRC_DAT54, "rb").read()
    salt = (joaat(audio_name) ^ joaat("lamavgineng")) & 0xFFFFFFFF
    for h in parse_rel_index(b54_orig).keys():
        if h not in hmap:
            hmap[h] = (h ^ salt) & 0xFFFFFFFF
    return hmap


def rekey_and_serialize_rel(blob, audio_name):
    """
    Re-key a patched DAT151 or DAT54 .rel blob from 'lamavgineng' to audio_name:
      - Replaces all matching 32-bit hashes in DataBlock & IndexHashes
      - Updates NameTable container paths ('DLC_<NAME>\\<name>')
      - Sorts IndexHashes by ror8_u32(hash) as required by RAGE's binary search
    """
    audio_name = audio_name.lower()
    hmap = build_rekey_map(audio_name)
    if not hmap:
        return bytes(blob)

    rel_type, data_len = struct.unpack_from("<II", blob, 0)
    data_block = bytearray(blob[8 : 8 + data_len])
    pos = 8 + data_len

    _nt_len, nt_cnt = struct.unpack_from("<II", blob, pos)
    pos += 8 + 4 * nt_cnt
    names = []
    for _ in range(nt_cnt):
        end = blob.index(b"\x00", pos)
        s = blob[pos:end].decode("ascii")
        if s.lower() == "dlc_lamavgineng\\lamavgineng":
            s = f"DLC_{audio_name.upper()}\\{audio_name}"
        elif s.lower() == "dlc_lamavgineng\\lamavgineng_npc":
            s = f"DLC_{audio_name.upper()}\\{audio_name}_NPC"
        names.append(s)
        pos = end + 1

    idx_cnt = struct.unpack_from("<I", blob, pos)[0]
    pos += 4
    items = []
    for _ in range(idx_cnt):
        h, off, ln = struct.unpack_from("<III", blob, pos)
        pos += 12
        items.append((hmap.get(h, h), off, ln))

    tail_tables = blob[pos:]

    # Parse HashTableOffsets and PackTableOffsets (file-relative offsets)
    t_pos = pos
    ht_cnt = struct.unpack_from("<I", blob, t_pos)[0]
    t_pos += 4
    ht_offs = struct.unpack_from(f"<{ht_cnt}I", blob, t_pos) if ht_cnt else ()
    t_pos += 4 * ht_cnt
    pt_cnt = struct.unpack_from("<I", blob, t_pos)[0]
    t_pos += 4
    pt_offs = struct.unpack_from(f"<{pt_cnt}I", blob, t_pos) if pt_cnt else ()

    if rel_type == 151:
        # In DAT151, all hash fields inside DataBlock are 4-byte aligned
        for off in range(4, data_len, 4):
            val = struct.unpack_from("<I", data_block, off)[0]
            if val in hmap:
                struct.pack_into("<I", data_block, off, hmap[val])
    elif rel_type == 54:
        # In DAT54, HashTableOffsets and PackTableOffsets give the exact file-relative positions
        for f_off in ht_offs + pt_offs:
            db_off = f_off - 8
            val = struct.unpack_from("<I", data_block, db_off)[0]
            if val in hmap:
                struct.pack_into("<I", data_block, db_off, hmap[val])

    # Sort IndexHashes by ror8_u32(h)
    items.sort(key=lambda x: ror8_u32(x[0]))

    out = bytearray()
    out.extend(struct.pack("<II", rel_type, data_len))
    out.extend(data_block)

    if nt_cnt > 0:
        nt_len_new = 4 + 4 * nt_cnt + sum(len(s) + 1 for s in names)
        out.extend(struct.pack("<II", nt_len_new, nt_cnt))
        cur_nt_off = 0
        for s in names:
            out.extend(struct.pack("<I", cur_nt_off))
            cur_nt_off += len(s) + 1
        for s in names:
            out.extend(s.encode("ascii") + b"\x00")
    else:
        out.extend(struct.pack("<II", 4, 0))

    out.extend(struct.pack("<I", idx_cnt))
    for h, off, ln in items:
        out.extend(struct.pack("<III", h, off, ln))

    out.extend(tail_tables)
    return bytes(out)


def parse_rel_index(blob):
    _rel_type, data_len = struct.unpack_from("<II", blob, 0)
    pos = 8 + data_len
    _nt_len, nt_cnt = struct.unpack_from("<II", blob, pos)
    pos += 8 + 4 * nt_cnt
    for _ in range(nt_cnt):
        pos = blob.index(b"\x00", pos) + 1
    idx_cnt = struct.unpack_from("<I", blob, pos)[0]
    pos += 4
    items = {}
    for _ in range(idx_cnt):
        h, off, ln = struct.unpack_from("<III", blob, pos)
        pos += 12
        items[h] = (8 + off, ln)
    return items


REL_SOUND_HEADER_FIELDS = [
    (0, "F2", "<I"), (1, "MaxH", "<H"), (2, "Vol", "<h"), (3, "VolV", "<H"),
    (4, "Pitch", "<h"), (5, "PitchV", "<H"), (6, "Pan", "<H"), (7, "PanV", "<H"),
    (8, "PreD", "<h"), (9, "PreDV", "<H"), (10, "StO", "<i"), (11, "StOV", "<i"),
    (12, "Atk", "<H"), (13, "Rel", "<H"), (14, "Doppler", "<H"), (15, "Cat", "<I"),
    (16, "LPF", "<H"), (17, "LPFV", "<H"), (18, "HPF", "<H"), (19, "HPFV", "<H"),
    (20, "VC", "<I"), (21, "VCS", "<h"), (22, "VCP", "<B"), (23, "Spk", "<B"),
    (24, "EffRt", "<B"), (25, "PDV", "<I"), (26, "SOV", "<I"), (27, "SmRev", "<H"),
    (28, "MdRev", "<H"), (29, "LgRev", "<H"), (30, "U25", "<H"), (31, "U26", "<H"),
]

REVUELTO_BACKFIRE_STREAM_MAP = {
    0x1F6A1F6A: "bf_00.wav",
    0x0A1834C6: "bf_03.wav",
    0x1BB6D804: "bf_07.wav",
    0x0E897DA9: "bf_09.wav",
    0x1053013D: "bf_02.wav",
    0x1050E468: "bf_04.wav",
    0x0D5D9E81: "bf_05.wav",
    0x0B5ADA7B: "bf_01.wav",
    0x1A1537EF: "bf_11.wav",
}


def decode_sound_header(raw):
    stype = raw[0]
    flags = struct.unpack_from("<I", raw, 1)[0]
    p = 5
    fields = {}
    for bit, name, fmt in REL_SOUND_HEADER_FIELDS:
        if ((flags >> ((bit // 8) * 8)) & 0xFF) == 0xAA:
            continue
        if (flags >> bit) & 1:
            sz = struct.calcsize(fmt)
            fields[name] = struct.unpack_from(fmt, raw, p)[0]
            p += sz
    return stype, flags, fields, p


def encode_sound_header(stype, flags, fields):
    buf = bytearray(struct.pack("<BI", stype, flags))
    for bit, name, fmt in REL_SOUND_HEADER_FIELDS:
        if ((flags >> ((bit // 8) * 8)) & 0xFF) == 0xAA:
            continue
        if (flags >> bit) & 1:
            buf.extend(struct.pack(fmt, fields[name]))
    return bytes(buf)


def rebuild_dat54_container(blob, header_upgrades, new_items=None):
    """
    Update RelSoundHeader bitflags/fields on existing DAT54 items (shifting
    body-internal and downstream HashTableOffsets / PackTableOffsets) and optionally
    append new DAT54 items into DataBlock + IndexHashes + pointer tables.
    """
    rel_type, data_len = struct.unpack_from("<II", blob, 0)
    assert rel_type == 54
    data_unk = struct.unpack_from("<I", blob, 8)[0]

    nt_off = 8 + data_len
    nt_len, _nt_cnt = struct.unpack_from("<II", blob, nt_off)
    nt_bytes = bytes(blob[nt_off : nt_off + 4 + nt_len])

    idx_off = nt_off + 4 + nt_len
    idx_cnt = struct.unpack_from("<I", blob, idx_off)[0]
    pos = idx_off + 4
    orig_items = []
    for _ in range(idx_cnt):
        h, roff, ln = struct.unpack_from("<III", blob, pos)
        pos += 12
        orig_items.append((8 + roff, ln, h))
    orig_items.sort(key=lambda x: x[0])

    ht_cnt = struct.unpack_from("<I", blob, pos)[0]
    pos += 4
    ht_offs = list(struct.unpack_from(f"<{ht_cnt}I", blob, pos)) if ht_cnt else []
    pos += 4 * ht_cnt
    pt_cnt = struct.unpack_from("<I", blob, pos)[0]
    pos += 4
    pt_offs = list(struct.unpack_from(f"<{pt_cnt}I", blob, pos)) if pt_cnt else []

    rebuilt = []
    seen_hashes = set()
    for foff, ln, h in orig_items:
        raw = bytes(blob[foff : foff + ln])
        ht_rel = [x - foff for x in ht_offs if foff <= x < foff + ln]
        pt_rel = [x - foff for x in pt_offs if foff <= x < foff + ln]
        if h in header_upgrades:
            new_flags, patches = header_upgrades[h]
            stype, _old_flags, hdr_fields, old_hdr_len = decode_sound_header(raw)
            for rel_x in ht_rel + pt_rel:
                if rel_x < old_hdr_len:
                    raise RuntimeError(f"Item 0x{h:08x} has pointer offset {rel_x} inside header < {old_hdr_len}")
            merged = dict(hdr_fields)
            merged.update(patches)
            new_hdr = encode_sound_header(stype, new_flags, merged)
            delta = len(new_hdr) - old_hdr_len
            raw = new_hdr + raw[old_hdr_len:]
            ht_rel = [x + delta for x in ht_rel]
            pt_rel = [x + delta for x in pt_rel]
        seen_hashes.add(h)
        rebuilt.append((h, raw, ht_rel, pt_rel))

    if new_items:
        for h, raw, ht_rel, pt_rel in new_items:
            assert h not in seen_hashes, f"Duplicate DAT54 hash 0x{h:08x}"
            seen_hashes.add(h)
            rebuilt.append((h, bytes(raw), list(ht_rel), list(pt_rel)))

    new_db = bytearray(struct.pack("<I", data_unk))
    new_idx = []
    new_ht = []
    new_pt = []
    for h, raw, ht_rel, pt_rel in rebuilt:
        item_foff = 8 + len(new_db)
        new_idx.append((h, len(new_db), len(raw)))
        for rx in ht_rel:
            new_ht.append(item_foff + rx)
        for rx in pt_rel:
            new_pt.append(item_foff + rx)
        new_db.extend(raw)

    new_idx.sort(key=lambda x: ror8_u32(x[0]))
    out = bytearray()
    out.extend(struct.pack("<II", 54, len(new_db)))
    out.extend(new_db)
    out.extend(nt_bytes)
    out.extend(struct.pack("<I", len(new_idx)))
    for h, roff, ln in new_idx:
        out.extend(struct.pack("<III", h, roff, ln))
    out.extend(struct.pack("<I", len(new_ht)))
    if new_ht:
        out.extend(struct.pack(f"<{len(new_ht)}I", *new_ht))
    out.extend(struct.pack("<I", len(new_pt)))
    if new_pt:
        out.extend(struct.pack(f"<{len(new_pt)}I", *new_pt))
    return bytearray(out)


def _read_fh6_zip_role(zip_path, prefix):
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        acc_dat = [n for n in names if n.startswith(f"{prefix}_Acc_") and n.endswith("_Sweep_GS.dat")][0]
        dec_dat = [n for n in names if n.startswith(f"{prefix}_Dec_") and n.endswith("_Sweep_GS.dat")][0]
        idle_wav = [n for n in names if n.startswith("Idle_") and n.endswith(".wav")][0]

        def _parse_pts(dat_name):
            return [int(x.strip()) for x in z.read(dat_name).decode("ascii").splitlines() if x.strip()]

        sr_acc, pcm_acc = decode_ms_adpcm(z.read(acc_dat + "_ADPCM.wav"))
        sr_dec, pcm_dec = decode_ms_adpcm(z.read(dec_dat + "_ADPCM.wav"))
        sr_idl, pcm_idl = decode_ms_adpcm(z.read(idle_wav))
        assert sr_acc == 48000 and sr_dec == 48000 and sr_idl == 48000
        return (pcm_acc, _parse_pts(acc_dat)), (pcm_dec, _parse_pts(dec_dat)), pcm_idl


def extract_fh6_revuelto_granular_streams():
    """
    Option 1 Lamborghini Revuelto V12 blend from RevoluSound Team Pack 56:
      - Engine (mechanical shriek/intake): G_V12RST_FER_F140_Race_2_Eng.zip
      - Exhaust (raspy Lambo howl):       G_V12RST_LAM_L539_Race_1_Exh.zip
      - Seamless low-RPM braking decel coast-down right into warm 9.20 Hz idle.
    """
    print("\n[1/5] Extracting & mastering FH6 Option 1 (F140_Race_2_Eng + LAM_L539_Race_1_Exh) for Revuelto...")
    f140_zip = os.path.join(FH6_PACK_DIR, "G_V12RST_FER_F140_Race_2_Eng.zip")
    l539_zip = os.path.join(FH6_PACK_DIR, "G_V12RST_LAM_L539_Race_1_Exh.zip")

    (eng_acc_pcm, eng_acc_pts), (eng_dec_pcm, eng_dec_pts), eng_idle_48k = _read_fh6_zip_role(f140_zip, "Eng")
    (exh_acc_pcm, exh_acc_pts), (exh_dec_pcm, exh_dec_pts), exh_idle_48k = _read_fh6_zip_role(l539_zip, "Exh")

    F_MIN, F_MAX = 9.20, 71.00
    F_IDLE_MIN, F_IDLE_MAX = 9.20, 9.80

    eng_acc_th, eng_acc_th_pts = _thin_sweep_48k(eng_acc_pcm, eng_acc_pts, keep_first=32, drop_mod=4)
    exh_acc_th, exh_acc_th_pts = _thin_sweep_48k(exh_acc_pcm, exh_acc_pts, keep_first=32, drop_mod=4)
    eng_dec_th, eng_dec_th_pts = _thin_sweep_48k(eng_dec_pcm, eng_dec_pts, keep_first=32, drop_mod=4)
    exh_dec_th, exh_dec_th_pts = _thin_sweep_48k(exh_dec_pcm, exh_dec_pts, keep_first=32, drop_mod=4)

    eng_acc_ext, eng_acc_ext_pts = build_extended_accel_48k(eng_acc_th, eng_acc_th_pts, f_start_target=9.20, n_prefix_grains=48)
    exh_acc_ext, exh_acc_ext_pts = build_extended_accel_48k(exh_acc_th, exh_acc_th_pts, f_start_target=9.20, n_prefix_grains=48)
    eng_dec_ext, eng_dec_ext_pts = build_extended_decel_48k(eng_dec_th, eng_dec_th_pts, f_end_target=9.20, n_suffix_grains=38)
    exh_dec_ext, exh_dec_ext_pts = build_extended_decel_48k(exh_dec_th, exh_dec_th_pts, f_end_target=9.20, n_suffix_grains=38)

    eng_idle_loop_48k = _loop_crossfade_pcm(eng_idle_48k[: min(len(eng_idle_48k), 48000)], fade_samples=960)
    exh_idle_slice = exh_idle_48k[48000:96000] if len(exh_idle_48k) >= 96000 else exh_idle_48k[:48000]
    exh_idle_loop_48k = _loop_crossfade_pcm(exh_idle_slice, fade_samples=960)

    def _build_idle_grains(pcm_len):
        n_g = 21
        pts = [int(round(i * float(pcm_len - 1) / float(n_g - 1))) for i in range(n_g)]
        pts[-1] = min(pts[-1], pcm_len - 1)
        return [
            (pts[i], F_IDLE_MIN + (F_IDLE_MAX - F_IDLE_MIN) * (float(i) / float(n_g - 1)), 24000, 26000)
            for i in range(n_g)
        ]

    specs = [
        (0x1F7402AA, "engine_accel",  eng_acc_ext, eng_acc_ext_pts, 32000, 16000, True,  10, 8),
        (0x03AB9171, "exhaust_accel", exh_acc_ext, exh_acc_ext_pts, 32000, 16000, True,  10, 8),
        (0x18648298, "engine_decel",  eng_dec_ext, eng_dec_ext_pts, 32000, None,  False, 10, 0),
        (0x0E804842, "exhaust_decel", exh_dec_ext, exh_dec_ext_pts, 32000, None,  False, 10, 0),
    ]

    player_streams = {}
    npc_streams = {}
    for sid, role, pcm48, pts48, sr_p, sr_n, asc, nloops_p, nloops_n in specs:
        pcm_p, grains_p = convert_sweep_to_target_sr(pcm48, pts48, sr_p, ascending=asc, f_min_clamp=F_MIN, f_max_clamp=F_MAX)
        samps_p = master_granular_stream(pcm_p, sr_p, role, grains=grains_p, ascending=asc)
        gr_bytes_p = build_granular_chunk_bytes(grains_p, tail_float=1.0)
        lp_bytes_p = build_loops_chunk_bytes(len(grains_p), num_loops=nloops_p, ascending=asc)
        player_streams[sid] = (sr_p, samps_p, gr_bytes_p, lp_bytes_p, role)
        rms, pk = compute_rms_and_peak(samps_p)
        print(f"  [Player] 0x{sid:08x} {role:14s}: sr={sr_p} grains={len(grains_p):3d} hz={grains_p[0][1]:.2f}->{grains_p[-1][1]:.2f} pk={int(pk):5d} rms={int(rms):5d}")
        if sr_n:
            pcm_n, grains_n = convert_sweep_to_target_sr(pcm48, pts48, sr_n, ascending=asc, f_min_clamp=F_MIN, f_max_clamp=F_MAX)
            samps_n = master_granular_stream(pcm_n, sr_n, role, grains=grains_n, ascending=asc)
            npc_streams[sid] = (
                sr_n, samps_n, build_granular_chunk_bytes(grains_n, tail_float=1.0),
                build_loops_chunk_bytes(len(grains_n), num_loops=nloops_n, ascending=asc), role
            )

    for sid, role, pcm48_idle, sr_i in [
        (0x1D18B9A1, "engine_idle", eng_idle_loop_48k, 32000),
        (0x179DDCAA, "exhaust_idle", exh_idle_loop_48k, 32000),
    ]:
        pcm_i = resample_linear_aa(pcm48_idle, 48000, sr_i)
        samps_i = master_granular_stream(pcm_i, sr_i, role)
        if len(samps_i) & 1:
            samps_i.append(samps_i[-1])
        grains_i = _build_idle_grains(len(samps_i))
        pack_tup = (sr_i, samps_i, build_granular_chunk_bytes(grains_i, tail_float=1.0), build_loops_chunk_bytes(len(grains_i), num_loops=0), role)
        player_streams[sid] = pack_tup
        npc_streams[sid] = pack_tup
        rms, pk = compute_rms_and_peak(samps_i)
        print(f"  [Idle  ] 0x{sid:08x} {role:14s}: sr={sr_i} grains={len(grains_i):3d} hz={grains_i[0][1]:.2f}->{grains_i[-1][1]:.2f} pk={int(pk):5d} rms={int(rms):5d}")

    startup_samps = build_revuelto_startup_oneshot(eng_acc_pcm, eng_dec_pcm, eng_dec_pts, exh_acc_pcm, exh_dec_pcm, exh_dec_pts)
    return player_streams, npc_streams, startup_samps, (F_MIN, F_MAX), (F_IDLE_MIN, F_IDLE_MAX)


def build_revuelto_startup_oneshot(eng_acc_48k, eng_dec_48k, eng_dec_pts, exh_acc_48k, exh_dec_48k, exh_dec_pts):
    """
    Build calibrated 1.00s (32,000 samples @ 32kHz) fierce dual-layer F140+L539 Lamborghini Revuelto V12
    cold-start bark + flare + settle:
      - 0.00..0.18s: crisp starter crank
      - 0.12..0.62s: explosive combined V12 roar
      - 0.50..0.98s: smooth settle-down directly meeting warm idle level
      (finishes within GTA V's ~1.00s ignition window for seamless transition into idle).
    """
    st_streams = read_awc_streams(SRC_ST77_AWC)
    starter_arr = array.array("h", st_streams[0x1D64B840][0x55])
    if sys.byteorder != "little":
        starter_arr.byteswap()
    starter_f = [float(x) for x in starter_arr]

    flare_eng = [float(x) for x in master_granular_stream(resample_linear_aa(eng_acc_48k[:28000], 48000, 32000), 32000, "engine_accel")]
    flare_exh = [float(x) for x in master_granular_stream(resample_linear_aa(exh_acc_48k[:28000], 48000, 32000), 32000, "exhaust_accel")]

    dec_eng_fwd, _ = build_extended_decel_48k(eng_dec_48k, eng_dec_pts, f_end_target=10.0, n_suffix_grains=10)
    dec_exh_fwd, _ = build_extended_decel_48k(exh_dec_48k, exh_dec_pts, f_end_target=10.0, n_suffix_grains=10)
    s0 = int(len(dec_eng_fwd) * 0.45)
    s1 = int(len(dec_exh_fwd) * 0.45)
    settle_eng = [float(x) for x in master_granular_stream(resample_linear_aa(dec_eng_fwd[s0 : s0 + 32000], 48000, 32000), 32000, "engine_decel")]
    settle_exh = [float(x) for x in master_granular_stream(resample_linear_aa(dec_exh_fwd[s1 : s1 + 32000], 48000, 32000), 32000, "exhaust_decel")]

    total_len = 32000  # Exactly 1.00s @ 32000 Hz
    out = [0.0] * total_len

    # 0.00..0.18s: crisp starter crank
    starter_len = min(len(starter_f), 5800)
    for i in range(starter_len):
        w = 1.0 if i < 3600 else max(0.0, 1.0 - (i - 3600) / 2200.0)
        out[i] += starter_f[i] * w * 0.92

    # 0.12..0.62s: explosive V12 combustion bark flaring up
    flare_start = 3800
    flare_len = min(total_len - flare_start, len(flare_eng), len(flare_exh), 16000)
    for j in range(flare_len):
        if j < 1600:
            env = math.sin(0.5 * math.pi * j / 1600.0)
        elif j > flare_len - 4800:
            rem = flare_len - j
            env = 0.5 * (1.0 - math.cos(math.pi * rem / 4800.0)) * 1.12
        else:
            env = 1.12
        out[flare_start + j] += (flare_eng[j] * 0.48 + flare_exh[j] * 0.68) * env

    # 0.50..0.98s: smooth settle down into warm idle
    settle_start = 16000
    settle_len = min(total_len - settle_start, len(settle_eng), len(settle_exh))
    for j in range(settle_len):
        idx = settle_start + j
        if j < 3600:
            env = 0.5 * (1.0 - math.cos(math.pi * j / 3600.0)) * 0.94
        elif idx > total_len - 3200:
            p = float(idx - (total_len - 3200)) / 3200.0
            env = 0.94 - 0.14 * p  # smoothly ease from 0.94 down to idle level
        else:
            env = 0.94
        out[idx] += (settle_eng[j] * 0.44 + settle_exh[j] * 0.66) * env

    # Final 500-sample micro-taper to ensure 0-crossing finish
    for k in range(500):
        out[total_len - 500 + k] *= 0.5 * (1.0 + math.cos(math.pi * k / 500.0))

    _, limited = fit_drive_to_rms(out, 13800.0, ceiling=31800.0, knee=0.68)
    return to_int16_array(limited)


def _synthesize_procedural_oneshot(name="pop", duration_s=0.22, sr=32000):
    """Synthesize a procedural, punchy backfire/gearcrack burst if WAV sample is missing."""
    import random
    n_samps = int(sr * duration_s)
    rng = random.Random(hash(name) & 0xFFFFFFFF)
    is_gearcrack = "gc" in name.lower()
    decay_rate = 38.0 if is_gearcrack else 16.0
    thump_f = 145.0 if is_gearcrack else 80.0
    out = []
    for i in range(n_samps):
        t = i / float(sr)
        env = math.exp(-t * decay_rate)
        attack = min(1.0, t / 0.0015)
        thump = math.sin(2.0 * math.pi * thump_f * (1.0 - t * 0.7) * t) * 0.7
        noise = rng.uniform(-1.0, 1.0)
        out.append((thump + noise * 0.9) * env * attack * 18000.0)
    return out


def load_fh6_wav_oneshot(wav_path, target_rms=15200.0, trim_tail=True, hpf_hz=42.0, knee=0.60):
    global FH6_BACKFIRE_DIR
    resolved_path = wav_path
    if not os.path.isfile(resolved_path):
        fn = os.path.basename(wav_path)
        cand_dirs = [
            FH6_BACKFIRE_DIR,
            os.path.join(TOOLS_DIR, "fh6_v12_backfires"),
            os.path.join(STAGING_ROOT, "fh6_v12_backfires"),
            r"C:\Users\Cath\.gemini\antigravity\brain\097b9845-a8b6-4603-b8ea-4d417117f293\scratch\fh6_pops",
            os.path.join(ROOT, "tools", "fh6_v12_backfires"),
        ]
        for cd in cand_dirs:
            p = os.path.join(cd, fn)
            if os.path.isfile(p):
                resolved_path = p
                break

    if os.path.isfile(resolved_path):
        try:
            with wave.open(resolved_path, "rb") as w:
                sr = w.getframerate()
                raw = array.array("h", w.readframes(w.getnframes()))
            samps_f = resample_linear_aa([float(x) for x in raw], sr, 32000) if sr != 32000 else [float(x) for x in raw]
        except Exception as e:
            print(f"  [WARN] Failed to read {resolved_path} ({e}), synthesizing procedural fallback.")
            samps_f = _synthesize_procedural_oneshot(os.path.basename(wav_path), sr=32000)
            sr = 32000
    else:
        print(f"  [WARN] Missing oneshot '{os.path.basename(wav_path)}' - synthesizing procedural burst fallback.")
        samps_f = _synthesize_procedural_oneshot(os.path.basename(wav_path), sr=32000)
        sr = 32000
    if trim_tail:
        pk0 = max((abs(x) for x in samps_f), default=1.0)
        # Keep the full reverberant V12 pop decay tail; trim only trailing silence (< 0.8% peak)
        thresh = max(110.0, pk0 * 0.008)
        min_keep = min(len(samps_f) - 1, 6400)
        last_idx = len(samps_f) - 1
        while last_idx > min_keep and abs(samps_f[last_idx]) < thresh:
            last_idx -= 1
        samps_f = samps_f[: min(len(samps_f), last_idx + 640)]
    dc = sum(samps_f) / max(1, len(samps_f))
    samps_f = [x - dc for x in samps_f]
    if abs(samps_f[0]) > 250.0:
        fin = min(64, len(samps_f) // 8)
        for i in range(fin):
            samps_f[i] *= i / float(fin)
    fout = min(640, len(samps_f) // 4)
    for i in range(fout):
        samps_f[-1 - i] *= i / float(fout)
    sos = [
        biquad_hpf(hpf_hz, sr, 0.7071),
        biquad_peaking(135.0, sr, 0.85, +4.5),   # Chest-thumping gunshot body
        biquad_peaking(2200.0, sr, 0.85, +5.5),  # Loud metallic crack
        biquad_peaking(4800.0, sr, 0.90, +6.0),  # Piercing exhaust flame snap
        biquad_highshelf(7200.0, sr, +4.5, 0.75),
    ]
    shaped = filtfilt_sos(samps_f, sos)
    _, limited = fit_drive_to_rms(shaped, target_rms, ceiling=32000.0, knee=knee)
    out = to_int16_array(limited)
    if len(out) & 1:
        out.append(out[-1])
    return sr, out


def _pack_oneshot_chunks(samps_i16, sr, unk_hi=0x0000):
    fmt_bytes, pk_bytes = build_format_and_peak_chunks(samps_i16, sr, is_granular=False, unk_hi_word=unk_hi)
    raw_pcm = array.array("h", samps_i16)
    if sys.byteorder != "little":
        raw_pcm.byteswap()
    ch_list = []
    if pk_bytes:
        ch_list.append((0x36, pk_bytes))
    ch_list.extend([(0x55, raw_pcm.tobytes()), (0xFA, fmt_bytes)])
    return ch_list


def build_revuelto_awc_banks(player_granular, npc_granular, startup_samps, dst_sfx_dir, audio_name):
    return build_svj_awc_banks(
        player_granular,
        npc_granular,
        startup_samps,
        dst_sfx_dir,
        audio_name,
        backfire_override_map=REVUELTO_BACKFIRE_STREAM_MAP,
    )


def build_dat151(dst_path, audio_name="lamavgineng", profile="svj"):
    is_rev = profile == "revuelto"
    print(f"\n[3/5] Building DAT151 game audio metadata (name={audio_name}, profile={profile}, RAGE Environmental Acoustics)...")
    b = bytearray(open(SRC_DAT151, "rb").read())
    items = parse_rel_index(b)

    # 1. CarAudioSettings (0x08665670): SPORTS_CAR_REVS_ENABLED + cabin/exterior acoustic openness
    veh_off, _ = items[0x08665670]
    assert b[veh_off] == 3
    flags = struct.unpack_from("<I", b, veh_off + 0x04)[0]
    struct.pack_into("<I", b, veh_off + 0x04, flags | 0x00000001)
    struct.pack_into("<f", b, veh_off + 0xDC, 0.28 if is_rev else 0.24)   # Openness
    struct.pack_into("<f", b, veh_off + 0x118, 0.36 if is_rev else 0.32)  # InteriorViewEngineOpenness

    # 2. VehicleEngine (0x228e59b9): 360-deg dispersion, FXCompensation, custom int/ext gear shifts (zero stock leaks)
    ve_off, _ = items[0x228E59B9]
    assert b[ve_off] == 4
    master_v = 380 if is_rev else 350
    fx_comp = 1450 if is_rev else 1350
    npc_fx = 1750 if is_rev else 1650
    min_pitch, max_pitch = (-950, 1400) if is_rev else (-1000, 1400)
    struct.pack_into("<4i", b, ve_off + 0x04, master_v, 0, fx_comp, npc_fx)
    struct.pack_into("<2i", b, ve_off + 0x28, min_pitch, max_pitch)
    # Upshifts: Crisp dual-clutch cut snap (gc_00..gc_09)
    struct.pack_into("<I", b, ve_off + 0xB0, joaat(f"{audio_name}_gearchange_int"))
    struct.pack_into("<I", b, ve_off + 0xB4, joaat(f"{audio_name}_gearchange_ext"))
    # Downshifts: Dedicated throttle-blip rev rise + deep resonant exhaust pop
    struct.pack_into("<I", b, ve_off + 0xDC, joaat(f"{audio_name}_downshift_int"))
    struct.pack_into("<I", b, ve_off + 0xE0, joaat(f"{audio_name}_downshift_ext"))
    # Exhaust Backfire & Overrun pop (high-powered explosive gunshots & crackles):
    struct.pack_into("<I", b, ve_off + 0xD0, joaat(f"{audio_name}_downshift_ext"))
    struct.pack_into("<I", b, ve_off + 0x80, 0xAE5985A5)
    struct.pack_into("<I", b, ve_off + 0xE4, joaat(f"{audio_name}_downshift_ext"))

    # 3. GranularEngine (0x541a17f9): balanced cones + strong ExhaustProximityVolume for walls/alleys/buildings/tunnels
    ge_off, _ = items[0x541A17F9]
    assert b[ge_off] == 88
    struct.pack_into("<i", b, ge_off + 0x08, 1050 if is_rev else 1000)
    struct.pack_into("<5i", b, ge_off + 0x14, 50, 200, 0, 0, -100)
    # Proven supercar pitch & decel curve (+250/+350 volume boost specifically when letting go of accelerator!)
    struct.pack_into("<6i", b, ge_off + 0x38, 950, 1340, 250, 350, 200, 250)
    # GranularEngine floats:
    # 0.025: idle cut-off (keeps engine audio active down to 1-2 km/h, perfectly seamless when pressing drive + brake!)
    # 0.160: dynamic range (low value keeps volume loud & rumbling even off-throttle/coasting)
    # 0.150: load factor (prevents abrupt volume drop under low load)
    # 0.280: decel inertia (revs hang smoothly on throttle lift-off, creating authentic Forza overrun rumble)
    # 0.420: decel decay factor (prolongs visceral overrun rumble for ~1.8s cascade)
    # 0.400: pitch tracking factor
    # 0.150: wobble factor
    idle_grains = 50
    struct.pack_into("<i", b, ge_off + 0x58, idle_grains)
    struct.pack_into("<7f", b, ge_off + 0x5C, 0.025, 0.160, 0.150, 0.280, 0.420, 0.400, 0.150)
    struct.pack_into("<i", b, ge_off + 0xAC, 1650)    # ExhaustProximityVolume_PostSubmix (walls/alleys/buildings/tunnels)
    struct.pack_into("<i", b, ge_off + 0xBC, 250)     # UpgradedEngineVolumeBoost_PostSubmix
    struct.pack_into("<i", b, ge_off + 0xC8, 500)     # UpgradedExhaustVolumeBoost_PostSubmix
    struct.pack_into("<I", b, ge_off + 0xD8, joaat(f"{audio_name}_downshift_ext"))  # Wire ExhaustPops directly to downshift pops
    # Seamless idle/driving volume balance: running (420/720) meets idle (350/650) with 0dB cliff
    struct.pack_into("<4i", b, ge_off + 0xDC, 350, 650, 420, 720)
    # Subtle 10% rev limiter volume cut (keeps full roar alive when airborne/redlining, no quiet dips!)
    struct.pack_into("<I", b, ge_off + 0xB0, 8)   # RevLimiterGrainsToPlay
    struct.pack_into("<f", b, ge_off + 0xF0, 0.10)

    final_bytes = rekey_and_serialize_rel(b, audio_name)
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    open(dst_path, "wb").write(final_bytes)
    print(f"  [+] Wrote {dst_path} ({len(final_bytes):,} bytes)")


def build_dat54(dst_path, clock0, clock1, audio_name="lamavgineng", profile="svj"):
    is_rev = profile == "revuelto"
    print(f"\n[4/5] Building DAT54 sound graph metadata (name={audio_name}, profile={profile}, Zero-Fallback + Wall/Alley Reverb)...")
    b = bytearray(open(SRC_DAT54, "rb").read())

    # Update GranularSound bodies before header resizing
    items_pre = parse_rel_index(b)
    player_cset = [(0, 0, 0, 0, 1.0), (1, 0, 0, 0, 1.0), (0, 0, 0, 1, 1.0), (1, 0, 0, 1, 1.0), (0, 1, 0, 0, 1.0), (1, 1, 0, 0, 1.0)]
    npc_cset = [(0, 0, 0, 0, 0.5), (1, 0, 0, 0, 0.5), (0, 0, 0, 0, 1.0), (1, 0, 0, 0, 1.0), (0, 1, 0, 0, 1.0), (1, 1, 0, 0, 1.0)]
    # Boosted decel channel volumes (ch 2 & 3: 300/750) deliver deep, throaty Forza-style overrun rumble on throttle lift-off
    p_vols = (220, 580, 320, 780, 220, 580) if is_rev else (200, 540, 300, 750, 200, 540)
    n_vols = (180, 480, -280, 0, 150, 450) if is_rev else (150, 450, -300, 0, 150, 450)

    for h, cset_list, cvols in [(0x9BB91A7B, player_cset, p_vols), (0x98403247, npc_cset, n_vols)]:
        off, ln = items_pre[h]
        assert b[off] == 19 and ln == 165
        body_off = off + 24
        for k, (obuf, clk_idx, smi, sma, max_prop) in enumerate(cset_list):
            struct.pack_into("<BBBBf", b, body_off + 52 + k * 8, obuf, clk_idx, smi, sma, max_prop)
        struct.pack_into("<6h", b, body_off + 108, *cvols)
        struct.pack_into("<ffff", b, body_off + 125, clock0[0], clock0[1], clock1[0], clock1[1])

    # Environmental Acoustics & Backfire Graph Upgrades:
    # - 0x2CD488E0 (ExhaustSpatialRouter t=15) sends continuous exhaust into Small (walls/alleys), Medium (streets), Large (tunnels) reverb
    # - High-powered explosive gunshot backfire pops on vehicles_extras_loud (0x6bad2627) with VCS=350 + rich reverb
    CAT_ENGINES = 0xF7C35252
    CAT_STARTUP = 0xA7A1137D
    CAT_POPS = 0x6BAD2627
    header_upgrades = {
        0x9BB91A7B: (0x00208001, {"F2": 0x9555AAAA, "Cat": CAT_ENGINES, "VCS": 195}),
        0x98403247: (0x00208001, {"F2": 0x9555AAAA, "Cat": CAT_ENGINES, "VCS": 175}),
        0x2CD488E0: (0x38208000, {"Cat": CAT_ENGINES, "VCS": 210, "SmRev": 68, "MdRev": 48, "LgRev": 32}),
        0xA2F19336: (0x38208014, {"Vol": -120 if is_rev else -250, "Pitch": 150 if is_rev else 100, "Cat": CAT_STARTUP, "VCS": 220, "SmRev": 44, "MdRev": 32, "LgRev": 18}),
        0x3B3868D7: (0x38208014, {"Vol": -550, "Pitch": 50, "Cat": CAT_STARTUP, "VCS": 180, "SmRev": 28, "MdRev": 18, "LgRev": 10}),
        0xAE5985A5: (0x38208004, {"Vol": 2950, "Cat": CAT_POPS, "VCS": 350, "SmRev": 72, "MdRev": 54, "LgRev": 36}),
        0x7044B7C5: (0x00000005, {"F2": 0xAAAAAAAA, "Vol": 1450}),
        0x570E34BF: (0x38208004, {"Vol": 3000, "Cat": CAT_POPS, "VCS": 350, "SmRev": 72, "MdRev": 54, "LgRev": 36}),
        0x25EE0E66: (0x38208004, {"Vol": 2950, "Cat": CAT_POPS, "VCS": 350, "SmRev": 72, "MdRev": 54, "LgRev": 36}),
    }

    cont_src = joaat("dlc_lamavgineng/lamavgineng")
    gc_item_hashes = []
    new_items = []
    for i in range(10):
        gc_h = joaat(f"{audio_name}_gc_{i:02d}")
        gc_item_hashes.append(gc_h)
        raw_s = encode_sound_header(12, 0x00000000, {}) + struct.pack("<IIB", cont_src, gc_h, 0)
        new_items.append((gc_h, raw_s, [], [5]))

    rand_h = joaat(f"{audio_name}_gearcrack_rand")
    rand_hdr = encode_sound_header(14, 0x00000004, {"Vol": 950})
    rand_body = bytearray(struct.pack("<BBB", 0, 0, len(gc_item_hashes)))
    rand_ht = []
    for gh in gc_item_hashes:
        rand_ht.append(len(rand_hdr) + len(rand_body))
        rand_body.extend(struct.pack("<If", gh, 1.0))
    new_items.append((rand_h, rand_hdr + bytes(rand_body), rand_ht, []))

    wrap_ext_h = joaat(f"{audio_name}_gearchange_ext")
    wrap_ext_hdr = encode_sound_header(
        6, 0x38208004, {"Vol": 2450, "Cat": CAT_POPS, "VCS": 330, "SmRev": 60, "MdRev": 44, "LgRev": 26}
    )
    wrap_body = struct.pack("<IiIhB", rand_h, 0, 0, 0, 0)
    new_items.append((wrap_ext_h, wrap_ext_hdr + wrap_body, [len(wrap_ext_hdr), len(wrap_ext_hdr) + 8], []))

    wrap_int_h = joaat(f"{audio_name}_gearchange_int")
    wrap_int_hdr = encode_sound_header(
        6, 0x38218004, {"Vol": 1350, "Cat": CAT_POPS, "LPF": 2400, "VCS": 230, "SmRev": 34, "MdRev": 24, "LgRev": 14}
    )
    new_items.append((wrap_int_h, wrap_int_hdr + wrap_body, [len(wrap_int_hdr), len(wrap_int_hdr) + 8], []))

    # Dedicated Downshift Rev-Match & Overrun Sound Nodes:
    # Randomly selects from deep resonant pops (bf_00, bf_06, bf_08, bf_10, bf_04, bf_02, bf_11)
    ds_hashes = [
        0xC10570D0,  # bf_00: heavy V12 bang + crackle tail
        0x90238F0D,  # bf_06: deep thunderous V12 pop + embers
        0x7E56EB78,  # bf_08: punchy low-mid gunshot bang + tail
        0x6BBFC64A,  # bf_10: double-tap V12 gunshot
        0x9F462D52,  # bf_04: sharp crack + rapid overrun crackles
        0xE062556E,  # bf_02: crisp multi-pop
        0x7EA89200,  # bf_11: fast double crack
    ]

    ds_rand_h = joaat(f"{audio_name}_downshift_rand")
    ds_rand_hdr = encode_sound_header(14, 0x00000004, {"Vol": 1850, "Pitch": 120})
    ds_rand_body = bytearray(struct.pack("<BBB", 0, 0, len(ds_hashes)))
    ds_rand_ht = []
    for dh in ds_hashes:
        ds_rand_ht.append(len(ds_rand_hdr) + len(ds_rand_body))
        ds_rand_body.extend(struct.pack("<If", dh, 1.0))
    new_items.append((ds_rand_h, ds_rand_hdr + bytes(ds_rand_body), ds_rand_ht, []))

    ds_ext_h = joaat(f"{audio_name}_downshift_ext")
    ds_ext_hdr = encode_sound_header(
        6, 0x38208004, {"Vol": 3450, "Cat": CAT_POPS, "VCS": 350, "SmRev": 72, "MdRev": 54, "LgRev": 36}
    )
    ds_wrap_body = struct.pack("<IiIhB", ds_rand_h, 0, 0, 0, 0)
    new_items.append((ds_ext_h, ds_ext_hdr + ds_wrap_body, [len(ds_ext_hdr), len(ds_ext_hdr) + 8], []))

    ds_int_h = joaat(f"{audio_name}_downshift_int")
    ds_int_hdr = encode_sound_header(
        6, 0x38218004, {"Vol": 2150, "Cat": CAT_POPS, "LPF": 2800, "VCS": 250, "SmRev": 44, "MdRev": 32, "LgRev": 20}
    )
    new_items.append((ds_int_h, ds_int_hdr + ds_wrap_body, [len(ds_int_hdr), len(ds_int_hdr) + 8], []))

    b = rebuild_dat54_container(b, header_upgrades, new_items=new_items)
    items_post = parse_rel_index(b)

    # Zero FallBackSound + MinRepeatTime on ALL WrapperSound (t=6) nodes so RAGE never falls back to vanilla GTA V audio
    for _h, (off, ln) in items_post.items():
        if b[off] == 6:
            _st, _fl, _hdr, hl = decode_sound_header(b[off : off + ln])
            if hl + 15 <= ln:
                struct.pack_into("<Ih", b, off + hl + 8, 0, 0)

    # Set calibrated child track volumes post-rebuild so pops/limiter cut loudly over the V12 exhaust
    inner_vols = [
        (0x86D7504D, 2400),
        (0x645FFC48, 2450),
        (0x92BBBBE1, 1200),
        (0x308305B1, 350 if is_rev else 300),
    ]
    for h, new_vol in inner_vols:
        off, _ = items_post[h]
        struct.pack_into("<h", b, off + 0x05, new_vol)

    final_bytes = rekey_and_serialize_rel(b, audio_name)
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    open(dst_path, "wb").write(final_bytes)
    print(f"  [+] Wrote {dst_path} ({len(final_bytes):,} bytes, items={len(items_post)})")


# ---------------------------------------------------------------------------
# 7. Write XML Package Descriptors & Build Standalone PS4 DLC RPF
# ---------------------------------------------------------------------------

def write_dlc_xmls(pkg_root, audio_name):
    content_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>dlc_{audio_name}:/ps4/audio/{audio_name}_game.dat</filename>
      <fileType>AUDIO_GAMEDATA</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="false" />
    </Item>
    <Item>
      <filename>dlc_{audio_name}:/ps4/audio/{audio_name}_sounds.dat</filename>
      <fileType>AUDIO_SOUNDDATA</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="false" />
    </Item>
    <Item>
      <filename>dlc_{audio_name}:/ps4/audio/sfx/dlc_{audio_name}</filename>
      <fileType>AUDIO_WAVEPACK</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="false" />
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>{audio_name.upper()}_AUTOGEN</changeSetName>
      <mapChangeSetData />
      <filesToInvalidate />
      <filesToDisable />
      <filesToEnable>
        <Item>dlc_{audio_name}:/ps4/audio/{audio_name}_game.dat</Item>
        <Item>dlc_{audio_name}:/ps4/audio/{audio_name}_sounds.dat</Item>
        <Item>dlc_{audio_name}:/ps4/audio/sfx/dlc_{audio_name}</Item>
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
      <requiresLoadingScreen value="false" />
    </Item>
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
"""
    setup2_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>dlc_{audio_name}</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>07/07/2026 12:00:00</timeStamp>
  <nameHash>{audio_name}</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>{audio_name.upper()}_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <startupScript />
  <scriptCallstackSize value="0" />
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="15" />
</SSetupData>
"""
    open(os.path.join(pkg_root, "content.xml"), "w", encoding="utf-8").write(content_xml)
    open(os.path.join(pkg_root, "setup2.xml"), "w", encoding="utf-8").write(setup2_xml)


def pack_standalone_rpf(pkg_root, dst_rpf):
    print("\n[5/5] Packing standalone PS4/PS5 Audio DLC RPF...")
    neogen_dir = r"D:\ps5payload\NeoGen EARY ACCESS\Terminal Build\NeoGen"
    if neogen_dir not in sys.path:
        sys.path.insert(0, neogen_dir)
    if TOOLS_DIR not in sys.path:
        sys.path.insert(0, TOOLS_DIR)

    import NeoGen
    import convert_vremastered as cv

    rpf_dir, mode_ovr, wine_bin = NeoGen.load_rpf_builder_settings()
    mode = NeoGen.detect_mode(mode_ovr)

    os.makedirs(os.path.dirname(dst_rpf), exist_ok=True)
    tmp_rpf = dst_rpf + ".repack_tmp"
    if os.path.exists(tmp_rpf):
        os.remove(tmp_rpf)

    ok = NeoGen.build_single_rpf(pkg_root, tmp_rpf, rpf_dir, mode, wine_bin)
    if not ok or not os.path.exists(tmp_rpf):
        raise RuntimeError("Failed to build standalone dlc.rpf with NeoGen!")

    shutil.move(tmp_rpf, dst_rpf)
    cv.ensure_rpf_min_chunks(dst_rpf, rpf_dir, mode, wine_bin, NeoGen, min_chunk=8192)
    print(f"  [+] Built standalone PS4/PS5 DLC RPF: {dst_rpf} ({os.path.getsize(dst_rpf):,} bytes)")


def main():
    parser = argparse.ArgumentParser(description="Convert FH6 V12 sounds to GTA 5 PS4/PS5")
    parser.add_argument("--profile", choices=["svj", "revuelto"], default="svj", help="Audio conversion profile")
    parser.add_argument("--name", default="lamavgineng", help="Audio name hash (e.g. svj63 or revuelto)")
    parser.add_argument("--install-to", default="", help="Optional target vehicle build folder")
    args = parser.parse_args()

    audio_name = args.name.lower()
    profile = args.profile
    pkg_root = os.path.join(STAGING_ROOT, f"dlc_package_{audio_name}_{profile}")
    shutil.rmtree(pkg_root, ignore_errors=True)

    dst_audio_dir = os.path.join(pkg_root, "ps4", "audio")
    dst_sfx_dir = os.path.join(dst_audio_dir, "sfx", f"dlc_{audio_name}")
    os.makedirs(dst_sfx_dir, exist_ok=True)

    if profile == "revuelto":
        player_granular, npc_granular, startup_samps, clock0, clock1 = extract_fh6_revuelto_granular_streams()
        build_revuelto_awc_banks(player_granular, npc_granular, startup_samps, dst_sfx_dir, audio_name)
    else:
        player_granular, npc_granular, startup_samps, clock0, clock1 = extract_fh6_svj_granular_streams()
        build_svj_awc_banks(player_granular, npc_granular, startup_samps, dst_sfx_dir, audio_name)

    build_dat151(os.path.join(dst_audio_dir, f"{audio_name}_game.dat151.rel"), audio_name=audio_name, profile=profile)
    build_dat54(os.path.join(dst_audio_dir, f"{audio_name}_sounds.dat54.rel"), clock0, clock1, audio_name=audio_name, profile=profile)
    write_dlc_xmls(pkg_root, audio_name)

    if args.install_to:
        target_audio = os.path.join(args.install_to, "ps4", "audio")
        shutil.rmtree(target_audio, ignore_errors=True)
        os.makedirs(target_audio, exist_ok=True)
        shutil.copytree(dst_audio_dir, target_audio, dirs_exist_ok=True)
        print(f"\n[+] Installed converted {profile.upper()} audio files into: {target_audio}")
    else:
        pack_standalone_rpf(pkg_root, STANDALONE_DLC_RPF)

    print(f"\n[SUCCESS] Forza Horizon 6 {profile.upper()} ({audio_name}) -> GTA 5 PS4/PS5 sound conversion complete!")


if __name__ == "__main__":
    main()

"""
Audio Analysis Tool - Decodes Base64 WAV from audio.txt and diagnoses problems.
Run: python analyze_audio.py
"""

import base64
import struct
import os
import wave
import math

AUDIO_TXT  = r"d:\Assignment_for_Uni\iot-arduino-project\audio.txt"
OUT_WAV    = r"d:\Assignment_for_Uni\iot-arduino-project\decoded_audio.wav"
SHIFT_TEST = r"d:\Assignment_for_Uni\iot-arduino-project\shifted_audio.wav"

# ──────────────────────────────────────────────────────────────
# 1. Read and clean the base64 text
# ──────────────────────────────────────────────────────────────
print("=" * 60)
print("  IoT Arduino Audio Analyzer")
print("=" * 60)

with open(AUDIO_TXT, "r") as f:
    raw = f.read()

# Strip the BEGIN/END markers if present
lines = []
inside = False
has_markers = "-----BEGIN_WAV_BASE64-----" in raw
if has_markers:
    for line in raw.splitlines():
        if "BEGIN_WAV_BASE64" in line:
            inside = True
            continue
        if "END_WAV_BASE64" in line:
            break
        if inside:
            lines.append(line.strip())
    b64_clean = "".join(lines)
else:
    b64_clean = "".join(raw.split())

print(f"[INFO] Total base64 characters: {len(b64_clean)}")

# Decode
try:
    raw_bytes = base64.b64decode(b64_clean + "==")  # padding safe
except Exception as e:
    print(f"[ERROR] Base64 decode failed: {e}")
    exit(1)

print(f"[INFO] Decoded bytes: {len(raw_bytes)}")

# ──────────────────────────────────────────────────────────────
# 2. Save the raw decoded bytes directly as-is
# ──────────────────────────────────────────────────────────────
with open(OUT_WAV, "wb") as f:
    f.write(raw_bytes)
print(f"[OK]  Saved as-is WAV -> {OUT_WAV}")

# ──────────────────────────────────────────────────────────────
# 3. Parse WAV header to check what is claimed
# ──────────────────────────────────────────────────────────────
print("\n--- WAV Header Info ---")
riff   = raw_bytes[0:4]
fmt    = raw_bytes[12:16]
fmt_sz = struct.unpack_from("<I", raw_bytes, 16)[0]
audio_fmt     = struct.unpack_from("<H", raw_bytes, 20)[0]
channels      = struct.unpack_from("<H", raw_bytes, 22)[0]
sample_rate   = struct.unpack_from("<I", raw_bytes, 24)[0]
byte_rate     = struct.unpack_from("<I", raw_bytes, 28)[0]
block_align   = struct.unpack_from("<H", raw_bytes, 32)[0]
bits_per_smp  = struct.unpack_from("<H", raw_bytes, 34)[0]
data_id       = raw_bytes[36:40]
data_size     = struct.unpack_from("<I", raw_bytes, 40)[0]

print(f"  RIFF:          {riff}")
print(f"  fmt chunk:     {fmt}")
print(f"  Audio format:  {audio_fmt} (1=PCM)")
print(f"  Channels:      {channels}")
print(f"  Sample rate:   {sample_rate} Hz")
print(f"  Bits/sample:   {bits_per_smp}")
print(f"  Byte rate:     {byte_rate}")
print(f"  Block align:   {block_align}")
print(f"  Data chunk:    {data_id}")
print(f"  Data size:     {data_size} bytes")

duration_s = data_size / byte_rate if byte_rate > 0 else 0
print(f"  Duration:      {duration_s:.2f} seconds")

# ──────────────────────────────────────────────────────────────
# 4. Analyze the raw sample data
# ──────────────────────────────────────────────────────────────
print("\n--- Sample Analysis ---")
HEADER_SIZE = 44
audio_data = raw_bytes[HEADER_SIZE:]
n_samples = len(audio_data) // (bits_per_smp // 8)

if bits_per_smp == 16:
    samples = struct.unpack_from(f"<{n_samples}h", audio_data)
elif bits_per_smp == 32:
    samples = struct.unpack_from(f"<{n_samples}i", audio_data)
else:
    print(f"[WARN] Unexpected bits per sample: {bits_per_smp}")
    samples = []

if samples:
    max_val   = max(samples)
    min_val   = min(samples)
    peak      = max(abs(max_val), abs(min_val))
    max_range = (2 ** (bits_per_smp - 1)) - 1
    peak_pct  = (peak / max_range) * 100
    avg_abs   = sum(abs(s) for s in samples) / len(samples)
    rms       = math.sqrt(sum(s*s for s in samples) / len(samples))

    print(f"  Total samples: {n_samples}")
    print(f"  Max value:     {max_val} (range: ±{max_range})")
    print(f"  Min value:     {min_val}")
    print(f"  Peak:          {peak} ({peak_pct:.1f}% of full scale)")
    print(f"  Avg amplitude: {avg_abs:.0f}")
    print(f"  RMS:           {rms:.0f}")

    # Check if audio is clipped
    if peak_pct > 95:
        print(f"  ⚠️  WARNING: Audio is SEVERELY CLIPPED ({peak_pct:.1f}%) — amplifier too high!")
    elif peak_pct > 80:
        print(f"  ⚠️  WARNING: Audio is clipping ({peak_pct:.1f}%) — reduce amplifier")
    elif peak_pct < 1:
        print(f"  ⚠️  WARNING: Audio is near-silent ({peak_pct:.2f}%) — signal too weak or wrong shift")
    elif peak_pct < 5:
        print(f"  ℹ️  Audio is very quiet ({peak_pct:.1f}%) — may need more gain")
    else:
        print(f"  ✅ Audio level looks reasonable ({peak_pct:.1f}%)")

    # Check for DC offset (sign of wrong bit alignment)
    dc_offset = sum(samples) / len(samples)
    if abs(dc_offset) > max_range * 0.05:
        print(f"  ⚠️  HIGH DC OFFSET: {dc_offset:.0f} — possible wrong bit shift or I2S format mismatch")
    else:
        print(f"  ✅ DC offset OK: {dc_offset:.0f}")

    # Check if audio looks like silence (all samples near 0)
    near_zero = sum(1 for s in samples if abs(s) < max_range * 0.001)
    near_zero_pct = (near_zero / len(samples)) * 100
    if near_zero_pct > 80:
        print(f"  ⚠️  {near_zero_pct:.0f}% of samples near zero — mic may not be picking up sound")
    
    # Check for noise (high freq energy even in "silent" sections - first 0.5s)
    half_sec = int(sample_rate * 0.5)
    first_half_sec = samples[:half_sec]
    if first_half_sec:
        first_rms = math.sqrt(sum(s*s for s in first_half_sec) / len(first_half_sec))
        print(f"  First 0.5s RMS: {first_rms:.0f} (noise floor estimate)")

# ──────────────────────────────────────────────────────────────
# 5. Try different bit shifts and save the best sounding one
# ──────────────────────────────────────────────────────────────
print("\n--- Testing Bit Shift Variants ---")
print("Trying to re-interpret the raw data with different shifts...")
print("(This only makes sense if the original was recorded as 32-bit)")

# Read the original as 16-bit PCM and try treating it as 32-bit with different shifts
raw_as_32 = struct.unpack_from(f"<{len(audio_data)//4}i", audio_data)

for shift in [0, 4, 8, 10, 12, 14, 16]:
    shifted = []
    for s in raw_as_32:
        v = (s >> shift)
        v = max(-32768, min(32767, v))
        shifted.append(v)
    if shifted:
        rms_sh = math.sqrt(sum(x*x for x in shifted) / len(shifted))
        peak_sh = max(abs(x) for x in shifted)
        print(f"  shift>>{shift:2d}: peak={peak_sh:6d} ({peak_sh/32767*100:5.1f}%)  rms={rms_sh:.0f}")

print()
print("─" * 60)
print("DIAGNOSIS:")
if samples and peak_pct > 95:
    print("  ❌ Audio is CLIPPED → reduce multiplication factor (try x1 or x2)")
elif samples and peak_pct < 2:
    print("  ❌ Audio is TOO QUIET → likely wrong I2S format (MSB vs Philips)")
    print("     Try: gBuf[i] >> 8  instead of >> 16")
elif samples and abs(dc_offset) > max_range * 0.05:
    print("  ❌ HIGH DC OFFSET → I2S frame alignment is wrong")
    print("     Most likely cause: using MSB slot instead of PHILIPS slot")
else:
    print("  ✅ Audio data structure looks OK")
    print("     If you still hear noise, the issue is hardware (wiring/power supply)")
print("─" * 60)
print(f"\n✅ Decoded WAV saved to:\n   {OUT_WAV}")
print("   Open this file in any audio player (VLC, Windows Media, Audacity, etc.)")

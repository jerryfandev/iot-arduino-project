"""
Decode audio.txt -> normalized + filtered audio.wav
Filters applied:
  1. DC offset removal (high-pass at 80Hz)  — removes hum/rumble
  2. Low-pass filter at 8000Hz              — removes high-freq hiss  
  3. Normalize to 70% full scale            — makes voice loud & clear

Run: python decode_wav.py
Then upload normalized_audio.wav to gemini.google.com
"""
import base64, struct, math

AUDIO_TXT = r"d:\Assignment_for_Uni\iot-arduino-project\audio.txt"
OUT_WAV   = r"d:\Assignment_for_Uni\iot-arduino-project\normalized_audio.wav"

# ── 1. Read & decode base64 ──────────────────────────────────
with open(AUDIO_TXT, "r") as f:
    raw = f.read()

lines, inside = [], False
if "-----BEGIN_WAV_BASE64-----" in raw:
    for line in raw.splitlines():
        if "BEGIN_WAV_BASE64" in line: inside = True; continue
        if "END_WAV_BASE64"   in line: break
        if inside: lines.append(line.strip())
    b64 = "".join(lines)
else:
    b64 = "".join(raw.split())

data = base64.b64decode(b64 + "==")
print(f"[1/5] Decoded {len(data)} bytes")

# ── 2. Parse samples ─────────────────────────────────────────
HEADER      = 44
sample_rate = struct.unpack_from("<I", data, 24)[0]
n_samples   = struct.unpack_from("<I", data, 40)[0] // 2
samples     = list(struct.unpack_from(f"<{n_samples}h", data, HEADER))
print(f"[2/5] Duration: {n_samples/sample_rate:.1f}s | Rate: {sample_rate}Hz")

# ── 3. DC Offset / High-pass filter (removes hum below 80Hz) ─
# Simple single-pole IIR high-pass: y[n] = alpha*(y[n-1] + x[n] - x[n-1])
# Cutoff at ~80Hz: alpha = RC/(RC + 1/Fs), RC = 1/(2*pi*fc)
fc_hp   = 80.0
rc      = 1.0 / (2.0 * math.pi * fc_hp)
dt      = 1.0 / sample_rate
alpha   = rc / (rc + dt)

hp_out = [0.0] * n_samples
hp_out[0] = float(samples[0])
for i in range(1, n_samples):
    hp_out[i] = alpha * (hp_out[i-1] + samples[i] - samples[i-1])
print("[3/5] High-pass filter applied (removed DC offset & rumble < 80Hz)")

# ── 4. Low-pass filter (smooths hiss above 8000Hz) ───────────
# Simple single-pole IIR low-pass: y[n] = alpha*x[n] + (1-alpha)*y[n-1]
# Cutoff at 8000Hz
fc_lp   = 8000.0
rc_lp   = 1.0 / (2.0 * math.pi * fc_lp)
alpha_lp = dt / (rc_lp + dt)

lp_out = [0.0] * n_samples
lp_out[0] = hp_out[0]
for i in range(1, n_samples):
    lp_out[i] = alpha_lp * hp_out[i] + (1.0 - alpha_lp) * lp_out[i-1]
print("[4/5] Low-pass filter applied (removed hiss > 8000Hz)")

# ── 5. Normalize to 70% full scale ───────────────────────────
peak = max(abs(s) for s in lp_out) or 1.0
gain = 22000.0 / peak
normalized = [max(-32768, min(32767, int(s * gain))) for s in lp_out]
print(f"[5/5] Normalized: peak={int(peak)} | gain={gain:.2f}x")

# ── 6. Write WAV ─────────────────────────────────────────────
n   = len(normalized)
dsz = n * 2
with open(OUT_WAV, "wb") as f:
    f.write(b"RIFF"); f.write(struct.pack("<I", 36 + dsz))
    f.write(b"WAVE")
    f.write(b"fmt "); f.write(struct.pack("<I", 16))
    f.write(struct.pack("<HH", 1, 1))
    f.write(struct.pack("<I",  sample_rate))
    f.write(struct.pack("<I",  sample_rate * 2))
    f.write(struct.pack("<HH", 2, 16))
    f.write(b"data"); f.write(struct.pack("<I", dsz))
    f.write(struct.pack(f"<{n}h", *normalized))

print(f"\nSaved WAV -> {OUT_WAV}")

# ── Optional: Export MP3 ──────────────────────────────────────
OUT_MP3 = OUT_WAV.replace(".wav", ".mp3")
try:
    from pydub import AudioSegment
    audio = AudioSegment.from_wav(OUT_WAV)
    audio.export(OUT_MP3, format="mp3", bitrate="128k",
                 parameters=["-ar", str(sample_rate), "-ac", "1"])
    print(f"Saved MP3 -> {OUT_MP3}")
    print("       (MP3 is smaller and easier to share/play)")
except Exception as e:
    print(f"[SKIP] MP3 export skipped: {e}")
    print("       (Make sure ffmpeg is installed and restart your terminal)")

print("\nNEXT STEPS:")
print("  1. Open normalized_audio.wav or .mp3 to verify your voice is clear")
print("  2. Go to https://gemini.google.com")
print("  3. Click the paperclip icon -> Upload normalized_audio.wav")
print("  4. Ask: 'What did I say in this audio recording?'")
print()
print("  NOTE: Upload the .wav file to Gemini (better for AI recognition)")
print("        Use .mp3 only for listening/sharing with people")

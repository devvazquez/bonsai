#!/usr/bin/env python3
"""Generate synthetic "Hey Bonsai" wake-word samples with Piper TTS.

Output: 1000 ms / 16 kHz / mono / PCM16 WAVs ready for Edge Impulse upload,
plus a metadata CSV+JSON describing the synthesis and augmentation
parameters used for every sample.

Usage:
    python3 tools/gen_wakeword_synth.py --count 300
    python3 tools/gen_wakeword_synth.py --count 5 --out ./output/_smoketest
"""

from __future__ import annotations

import argparse
import csv
import json
import logging
import random
import sys
import traceback
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
VOICES_DIR = REPO / "voices"
NOISE_DIR = REPO / "noise_samples"
DEFAULT_OUT = REPO / "output" / "hey_bonsai_synthetic"

SR = 16000
CLIP_MS = 1000
CLIP_LEN = SR * CLIP_MS // 1000

# Per-language spellings that make Piper's phonemizer land on the intended
# pronunciation of "Hey Bonsai" rather than reading it with native rules.
PHRASES = {
    "en": ["Hey Bonsai", "Hey, Bonsai", "Hey Bonsai."],
    "es": ["Hey Bonsai", "Jey Bonsai", "Hey, Bonsái"],
    "ca": ["Hey Bonsai", "Hei Bonsai", "Hey, Bonsai"],
}

log = logging.getLogger("synth")


@dataclass
class VoiceSpec:
    name: str
    lang: str
    path: Path
    speaker_ids: list[int] = field(default_factory=lambda: [None])


def discover_voices(voices_dir: Path) -> list[VoiceSpec]:
    """Find every Piper .onnx voice (with its .json config) on disk."""
    voices: list[VoiceSpec] = []
    for onnx in sorted(voices_dir.glob("*.onnx")):
        cfg_path = onnx.with_suffix(".onnx.json")
        if not cfg_path.exists():
            log.warning("skipping %s: missing config json", onnx.name)
            continue
        try:
            cfg = json.loads(cfg_path.read_text())
        except Exception as exc:  # noqa: BLE001
            log.warning("skipping %s: unreadable config (%s)", onnx.name, exc)
            continue
        lang = (cfg.get("language", {}) or {}).get("family") or onnx.stem.split("_")[0]
        n_speakers = int(cfg.get("num_speakers", 1) or 1)
        # Cap multi-speaker voices so one model does not dominate the mix.
        speakers = list(range(min(n_speakers, 8))) if n_speakers > 1 else [None]
        voices.append(VoiceSpec(onnx.stem, lang, onnx, speakers))
    return voices


def load_noise_bank(noise_dir: Path) -> list[np.ndarray]:
    """Load ./noise_samples/*.wav as 16 kHz mono float arrays, if present."""
    bank: list[np.ndarray] = []
    if not noise_dir.is_dir():
        return bank
    import librosa

    for wav in sorted(noise_dir.rglob("*.wav")):
        try:
            y, _ = librosa.load(wav, sr=SR, mono=True)
            if y.size > SR // 4:
                bank.append(y.astype(np.float32))
        except Exception as exc:  # noqa: BLE001
            log.warning("noise clip %s unreadable: %s", wav.name, exc)
    return bank


def pink_noise(n: int, rng: random.Random) -> np.ndarray:
    """Voss-McCartney-ish 1/f noise via spectral shaping."""
    state = np.random.default_rng(rng.randrange(2**32))
    white = state.standard_normal(n)
    spec = np.fft.rfft(white)
    freqs = np.arange(spec.size)
    freqs[0] = 1
    spec /= np.sqrt(freqs)
    out = np.fft.irfft(spec, n).astype(np.float32)
    peak = np.max(np.abs(out)) or 1.0
    return out / peak


def make_noise(n: int, bank: list[np.ndarray], rng: random.Random) -> tuple[np.ndarray, str]:
    """Pick a noise bed of exactly n samples: recorded clip, white, or pink."""
    if bank and rng.random() < 0.6:
        clip = bank[rng.randrange(len(bank))]
        if clip.size < n:
            clip = np.tile(clip, int(np.ceil(n / clip.size)))
        start = rng.randrange(max(1, clip.size - n))
        return clip[start : start + n].copy(), "recorded"
    if rng.random() < 0.5:
        state = np.random.default_rng(rng.randrange(2**32))
        return state.standard_normal(n).astype(np.float32), "white"
    return pink_noise(n, rng), "pink"


def mix_at_snr(signal: np.ndarray, noise: np.ndarray, snr_db: float) -> np.ndarray:
    """Scale noise so signal/noise hits the requested SNR, then add it."""
    sig_pow = float(np.mean(signal**2))
    noise_pow = float(np.mean(noise**2))
    if sig_pow <= 0 or noise_pow <= 0:
        return signal
    target = sig_pow / (10 ** (snr_db / 10.0))
    return signal + noise * np.sqrt(target / noise_pow)


def center_to_length(y: np.ndarray, floor_noise: np.ndarray, rng: random.Random) -> np.ndarray:
    """Trim/pad to exactly CLIP_LEN, keeping the word centred.

    Padding uses a low-level noise bed, never digital silence, so the model
    cannot key on absolute-zero regions as a shortcut.
    """
    if y.size > CLIP_LEN:
        # Keep the highest-energy CLIP_LEN window (the spoken word).
        win = max(1, SR // 100)
        env = np.convolve(np.abs(y), np.ones(win) / win, mode="same")
        centre = int(np.argmax(np.convolve(env, np.ones(CLIP_LEN) / CLIP_LEN, mode="same")))
        start = int(np.clip(centre - CLIP_LEN // 2, 0, y.size - CLIP_LEN))
        return y[start : start + CLIP_LEN].copy()

    pad = CLIP_LEN - y.size
    # Jitter the onset so the word is not pixel-aligned in every sample.
    left = int(np.clip(pad // 2 + rng.randint(-pad // 4, pad // 4) if pad >= 4 else pad // 2, 0, pad))
    out = floor_noise[:CLIP_LEN].copy()
    out[left : left + y.size] += y
    return out


def normalize(y: np.ndarray, gain_db: float) -> np.ndarray:
    """Peak-normalise to -1 dBFS, apply gain, then guard against clipping."""
    peak = float(np.max(np.abs(y))) or 1.0
    y = y / peak * 10 ** (-1.0 / 20.0)
    y = y * 10 ** (gain_db / 20.0)
    peak = float(np.max(np.abs(y)))
    if peak > 0.999:
        y = y / peak * 0.999
    return y.astype(np.float32)


def synthesize(voice, phrase: str, syn_cfg) -> tuple[np.ndarray, int]:
    """Run Piper and concatenate its audio chunks into one float array."""
    chunks, rate = [], None
    for chunk in voice.synthesize(phrase, syn_config=syn_cfg):
        rate = chunk.sample_rate
        chunks.append(np.frombuffer(chunk.audio_int16_bytes, dtype=np.int16))
    if not chunks:
        raise RuntimeError("piper returned no audio")
    audio = np.concatenate(chunks).astype(np.float32) / 32768.0
    return audio, rate


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--count", type=int, default=300, help="samples to generate")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--voices", type=Path, default=VOICES_DIR)
    ap.add_argument("--noise", type=Path, default=NOISE_DIR)
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    rng = random.Random(args.seed)

    import librosa
    from piper import PiperVoice, SynthesisConfig

    voices = discover_voices(args.voices)
    if not voices:
        log.error("no Piper voices found in %s", args.voices)
        return 2
    log.info("voices: %s", ", ".join(f"{v.name}({len(v.speaker_ids)}spk)" for v in voices))

    noise_bank = load_noise_bank(args.noise)
    log.info("noise bank: %d recorded clip(s)", len(noise_bank))

    args.out.mkdir(parents=True, exist_ok=True)

    loaded: dict[str, object] = {}
    rows: list[dict] = []
    errors: list[dict] = []

    # Round-robin over (voice, speaker) so every voice gets equal share.
    combos = [(v, spk) for v in voices for spk in v.speaker_ids]
    rng.shuffle(combos)

    idx = 0
    attempt = 0
    max_attempts = args.count * 3
    while idx < args.count and attempt < max_attempts:
        vspec, speaker = combos[attempt % len(combos)]
        attempt += 1
        try:
            if vspec.name not in loaded:
                loaded[vspec.name] = PiperVoice.load(str(vspec.path))
            voice = loaded[vspec.name]

            phrase = rng.choice(PHRASES.get(vspec.lang, PHRASES["en"]))
            length_scale = round(rng.uniform(0.85, 1.20), 3)
            noise_scale = round(rng.uniform(0.55, 0.80), 3)
            noise_w = round(rng.uniform(0.60, 0.95), 3)

            audio, rate = synthesize(
                voice,
                phrase,
                SynthesisConfig(
                    speaker_id=speaker,
                    length_scale=length_scale,
                    noise_scale=noise_scale,
                    noise_w_scale=noise_w,
                    normalize_audio=True,
                ),
            )

            # --- post-processing: make it look like a real mic recording ---
            if rate != SR:
                audio = librosa.resample(audio, orig_sr=rate, target_sr=SR)
            audio = librosa.effects.trim(audio, top_db=35)[0]
            if audio.size < SR // 10:
                raise RuntimeError(f"clip too short after trim ({audio.size} samples)")

            semitones = 0.0
            if rng.random() < 0.5:
                semitones = round(rng.choice([-1, 1]) * rng.uniform(1.0, 2.0), 2)
                audio = librosa.effects.pitch_shift(y=audio, sr=SR, n_steps=semitones)

            floor, noise_kind = make_noise(CLIP_LEN, noise_bank, rng)
            floor = floor / (np.max(np.abs(floor)) or 1.0) * rng.uniform(0.002, 0.006)
            audio = center_to_length(audio, floor, rng)

            snr_db = round(rng.uniform(15.0, 25.0), 2)
            bed, _ = make_noise(CLIP_LEN, noise_bank, rng)
            audio = mix_at_snr(audio, bed, snr_db)

            gain_db = round(rng.uniform(-6.0, 3.0), 2)
            audio = normalize(audio, gain_db)

            fname = f"hey_bonsai_synth_{idx:04d}_{vspec.name}.wav"
            sf.write(args.out / fname, audio, SR, subtype="PCM_16")

            rows.append(
                {
                    "file": fname,
                    "index": idx,
                    "voice": vspec.name,
                    "lang": vspec.lang,
                    "speaker_id": speaker if speaker is not None else "",
                    "phrase": phrase,
                    "length_scale": length_scale,
                    "noise_scale": noise_scale,
                    "noise_w_scale": noise_w,
                    "pitch_shift_semitones": semitones,
                    "snr_db": snr_db,
                    "gain_db": gain_db,
                    "noise_kind": noise_kind,
                    "sample_rate": SR,
                    "duration_ms": CLIP_MS,
                }
            )
            idx += 1
            if idx % 25 == 0:
                log.info("generated %d/%d", idx, args.count)
        except Exception as exc:  # noqa: BLE001 - never abort the batch
            errors.append(
                {
                    "attempt": attempt,
                    "voice": vspec.name,
                    "speaker_id": speaker,
                    "error": f"{type(exc).__name__}: {exc}",
                }
            )
            log.warning("sample failed on %s: %s", vspec.name, exc)
            log.debug("%s", traceback.format_exc())

    if rows:
        with (args.out / "metadata.csv").open("w", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
    (args.out / "metadata.json").write_text(
        json.dumps({"samples": rows, "errors": errors}, indent=2)
    )

    by_voice: dict[str, int] = {}
    by_lang: dict[str, int] = {}
    for r in rows:
        by_voice[r["voice"]] = by_voice.get(r["voice"], 0) + 1
        by_lang[r["lang"]] = by_lang.get(r["lang"], 0) + 1

    print("\n=== SUMMARY ===")
    print(f"generated : {len(rows)}/{args.count}")
    print(f"errors    : {len(errors)}")
    print(f"output    : {args.out}")
    for v, c in sorted(by_voice.items()):
        print(f"  {v:28s} {c}")
    print(f"by language: {by_lang}")
    for e in errors[:10]:
        print(f"  ERROR {e['voice']}: {e['error']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

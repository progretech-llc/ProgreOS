"""Local Kokoro voices and script-aware Japanese/English speech segments."""
from functools import lru_cache
from pathlib import Path
import re
import threading

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "runtime/kokoro/kokoro-v1.0.onnx"
VOICES = ROOT / "runtime/kokoro/voices-v1.0.bin"
JAPANESE = re.compile(r"[\u3040-\u30ff\u3400-\u9fff]")
JAPANESE_RUN = re.compile(r"([0-9]*[\u3040-\u30ff\u3400-\u9fff][\u3040-\u30ff\u3400-\u9fff\u3000-\u303f\uff00-\uffef0-9\s]*)")
LOCK = threading.Lock()


@lru_cache(maxsize=1)
def available_voices():
    if not MODEL.is_file() or not VOICES.is_file():
        return ()
    try:
        import numpy as np
        with np.load(VOICES) as voices:
            return tuple(sorted(voices.files))
    except ImportError:
        return ()


def segments(text):
    # Keep English explanations in English; pass kanji/kana to Japanese G2P.
    for part in JAPANESE_RUN.split(text):
        if any(c.isalnum() for c in part):
            yield part.strip(), bool(JAPANESE.search(part))


@lru_cache(maxsize=1)
def _engine():
    from kokoro_onnx import Kokoro
    return Kokoro(str(MODEL), str(VOICES))


@lru_cache(maxsize=1)
def _japanese_g2p():
    from misaki.ja import JAG2P
    return JAG2P(version="pyopenjtalk")


def synthesize(text, voice, japanese, path):
    if voice not in available_voices():
        raise RuntimeError("Selected Kokoro voice is not installed.")
    import soundfile as sf
    # Reuse the CPU session and phonemizer; these objects are not re-entrant.
    with LOCK:
        if japanese:
            phonemes, _ = _japanese_g2p()(text)
            if not phonemes:
                raise RuntimeError("Japanese phonemizer produced no speech.")
            samples, rate = _engine().create(phonemes, voice=voice, is_phonemes=True)
        else:
            samples, rate = _engine().create(text, voice=voice, lang="en-gb" if voice.startswith("b") else "en-us")
    sf.write(str(path), samples, rate, subtype="PCM_16")

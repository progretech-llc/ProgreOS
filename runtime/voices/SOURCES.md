# Local voice provenance

`en_US-arctic-medium.onnx` and its config are downloaded by the existing
installer from rhasspy/piper-voices on Hugging Face, en/en_US/arctic/medium.

`en_US-ljspeech-medium.onnx` and its config were copied into this project
from project-avalon's existing local cache without changing the originals.
Upstream: https://huggingface.co/rhasspy/piper-voices/tree/main/en/en_US/ljspeech/medium
The inspected model card identifies a US English female voice, medium quality,
22,050 Hz, trained on the public-domain LJ Speech dataset:
https://keithito.com/LJ-Speech-Dataset/ .

Model binaries and configuration files are local runtime artifacts, excluded
from Git. These notes describe workstation installation, not bundled assets.

Kokoro model and voice bank were scoped copies of project-avalon's existing
local assets. The originals are unchanged. Upstream model:
https://huggingface.co/hexgrad/Kokoro-82M (Apache-2.0).
ONNX runtime: https://github.com/thewh1teagle/kokoro-onnx .
Japanese G2P: https://github.com/hexgrad/misaki and
https://github.com/r9y9/pyopenjtalk . Its Open JTalk dictionary is downloaded
once during setup; speech generation then works locally.
Multilingual STT: ggml-small.bin from
https://huggingface.co/ggerganov/whisper.cpp .

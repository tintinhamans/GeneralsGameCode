from pathlib import Path
import math
import struct
import sys
import wave

root = Path(sys.argv[1]).resolve()
root.mkdir(parents=True, exist_ok=True)
(root / "audio-manager-fixture.marker").write_bytes(b"generals-openal-audio-manager-test-fixture-v1\n")
for directory in ("Data/INI/Default", "Data/Audio/Sounds", "Data/Audio/Music", "Data/Audio/Speech"):
    (root / directory).mkdir(parents=True, exist_ok=True)

def audio(name, frames, frequency=440, amplitude=8000, channels=1):
    values = []
    for i in range(frames):
        sample = round(amplitude * math.sin(2 * math.pi * frequency * i / 48000))
        for channel in range(channels):
            values.append(sample)
    target = root / "Data/Audio/Sounds" / (name + ".wav")
    with wave.open(str(target), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(2)
        output.setframerate(48000)
        output.writeframes(struct.pack("<" + "h" * len(values), *values))
    return target

for name, frames, hz in (("attack", 1600, 200), ("main", 3200, 400), ("decay", 1600, 800),
                         ("long", 48000, 440), ("tiny", 64, 1000), ("odd", 14239, 500)):
    audio(name, frames, hz)
for directory in ("Music", "Speech"):
    (root / "Data/Audio" / directory / "long.wav").write_bytes((root / "Data/Audio/Sounds/long.wav").read_bytes())

for directory in ("Sounds", "Music", "Speech"):
    (root / "Data/Audio" / directory / "corrupt.wav").write_bytes(b"not an audio container")

common = """  Volume = 100
  VolumeShift = 0
  MinVolume = 0
  PitchShift = 0 0
  Delay = 0 0
  Limit = 0
  LoopCount = 1
  Priority = NORMAL
  Type = UI EVERYONE
  Control = NONE
  MinRange = 10
  MaxRange = 100
  LowPassCutoff = 100
"""
def block(kind, name, extra="", base=False):
    fields = ""
    if base:
        fields = common
    return kind + " " + name + "\n" + fields + extra + "End\n\n"

(root / "Data/INI/AudioSettings.ini").write_text("; Settings are initialized explicitly; never read user preferences.\n")
(root / "Data/INI/Default/Music.ini").write_text(block("MusicTrack", "DefaultMusicTrack", "  Filename = long.wav\n", True))
mp3 = Path(__file__).resolve().parent.parent / "ffmpeg/fixtures/gapless.mp3"
(root / "Data/Audio/Music/second.mp3").write_bytes(mp3.read_bytes())
(root / "Data/INI/Music.ini").write_text(block("MusicTrack", "MusicOne", "  Filename = long.wav\n") + block("MusicTrack", "MusicTwo", "  Filename = second.mp3\n"))
(root / "Data/INI/Default/SoundEffects.ini").write_text(block("AudioEvent", "DefaultSoundEffect", "  Sounds = main\n", True))
events = block("AudioEvent", "Ui", "  Sounds = main\n")
events += block("AudioEvent", "Sequence", "  Attack = attack\n  Sounds = main\n  Decay = decay\n")
events += block("AudioEvent", "FiniteLoop", "  Sounds = main\n  Control = LOOP\n  LoopCount = 3\n")
events += block("AudioEvent", "Permanent", "  Sounds = main\n  Control = LOOP\n  LoopCount = 0\n")
events += block("AudioEvent", "StopLoop", "  Sounds = main\n  Decay = decay\n  Control = LOOP\n  LoopCount = 0\n")
events += block("AudioEvent", "AllParts", "  Sounds = attack main decay\n  Control = ALL\n")
events += block("AudioEvent", "Limited", "  Sounds = long\n  Limit = 1\n")
events += block("AudioEvent", "Low", "  Sounds = long\n  Priority = LOW\n")
events += block("AudioEvent", "High", "  Sounds = long\n  Priority = HIGH\n")
events += block("AudioEvent", "Delayed", "  Sounds = main\n  Delay = 100 100\n")
events += block("AudioEvent", "World", "  Sounds = long\n  Type = WORLD GLOBAL EVERYONE\n")
for suffix, filename in (("Missing", "missing"), ("Corrupt", "corrupt")):
    events += block("AudioEvent", "Bad" + suffix, "  Sounds = " + filename + "\n  Priority = HIGH\n")
events += block("AudioEvent", "Interrupt", "  Sounds = long\n  Limit = 1\n  Control = INTERRUPT\n")
(root / "Data/INI/SoundEffects.ini").write_text(events)
(root / "Data/INI/Default/Speech.ini").write_text(block("DialogEvent", "DefaultDialog", "  Filename = long.wav\n", True))
(root / "Data/INI/Speech.ini").write_text(block("DialogEvent", "SpeechOne", "  Filename = long.wav\n"))
for name in ("Default/Voice", "Voice", "MiscAudio"):
    (root / "Data/INI" / (name + ".ini")).write_text("; Deliberately empty fixture.\n")

payload = (root / "Data/Audio/Sounds/main.wav").read_bytes()
name = b"Data/Audio/Sounds/archive.wav\0"
header_size = 16 + 8 + len(name)
header = b"BIGF" + struct.pack("<I", header_size + len(payload)) + struct.pack(">II", 1, header_size)
(root / "fixture.big").write_bytes(header + struct.pack(">II", header_size, len(payload)) + name + payload)
print(root)

for kind, ini, prefix in (("MusicTrack", "Music", "Music"), ("DialogEvent", "Speech", "Speech")):
    path = root / "Data/INI" / (ini + ".ini")
    extra = ""
    for suffix, filename in (("Missing", "missing.wav"), ("Corrupt", "corrupt.wav")):
        extra += block(kind, "Bad" + prefix + suffix, "  Filename = " + filename + "\n")
    for priority in ("LOW", "HIGH"):
        extra += block(kind, prefix + priority, "  Filename = long.wav\n  Priority = " + priority + "\n")
    path.write_text(path.read_text() + extra)

#!/usr/bin/env python3
"""Generate the public-domain melody Twinkle Twinkle Little Star as local assets."""

import json
import math
import struct
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SONG_DIR = ROOT / "assets" / "songs" / "twinkle-star"
INDEX_PATH = ROOT / "assets" / "songs" / "index.json"
SAMPLE_RATE = 44100
BPM = 100
BEAT_MS = 600
INTRO_MS = 3000
TAIL_MS = 2000
MELODY_BEATS = 48
DURATION_MS = INTRO_MS + MELODY_BEATS * BEAT_MS + TAIL_MS


MELODY = [
    ("C4", 1), ("C4", 1), ("G4", 1), ("G4", 1), ("A4", 1), ("A4", 1), ("G4", 2),
    ("F4", 1), ("F4", 1), ("E4", 1), ("E4", 1), ("D4", 1), ("D4", 1), ("C4", 2),
    ("G4", 1), ("G4", 1), ("F4", 1), ("F4", 1), ("E4", 1), ("E4", 1), ("D4", 2),
    ("G4", 1), ("G4", 1), ("F4", 1), ("F4", 1), ("E4", 1), ("E4", 1), ("D4", 2),
    ("C4", 1), ("C4", 1), ("G4", 1), ("G4", 1), ("A4", 1), ("A4", 1), ("G4", 2),
    ("F4", 1), ("F4", 1), ("E4", 1), ("E4", 1), ("D4", 1), ("D4", 1), ("C4", 2),
]

FREQUENCIES = {
    "C4": 261.6256,
    "D4": 293.6648,
    "E4": 329.6276,
    "F4": 349.2282,
    "G4": 391.9954,
    "A4": 440.0000,
}


def melody_events():
    """Return (index, start_ms, duration_ms, frequency) for the six phrases."""
    events = []
    time_ms = INTRO_MS
    for index, (name, beats) in enumerate(MELODY):
        duration_ms = beats * BEAT_MS
        events.append((index, time_ms, duration_ms, FREQUENCIES[name]))
        time_ms += duration_ms
    return events


def envelope(position: float, length: float) -> float:
    attack = min(1.0, position / 0.025)
    release = min(1.0, max(0.0, length - position) / 0.09)
    return attack * release


def render_note(samples, start_ms: int, duration_ms: int, frequency: float, amplitude: float):
    start = int(start_ms * SAMPLE_RATE / 1000)
    count = int(duration_ms * SAMPLE_RATE / 1000)
    length = duration_ms / 1000.0
    end = min(len(samples), start + count)
    for sample in range(start, end):
        position = (sample - start) / SAMPLE_RATE
        env = envelope(position, length)
        tone = (math.sin(2 * math.pi * frequency * position)
                + 0.18 * math.sin(4 * math.pi * frequency * position)
                + 0.04 * math.sin(6 * math.pi * frequency * position))
        samples[sample] += amplitude * env * tone


def synthesize_audio():
    """Render a soft, deterministic piano-like cue with an audible quarter-note pulse."""
    sample_count = int(DURATION_MS * SAMPLE_RATE / 1000)
    samples = [0.0] * sample_count
    for _, start_ms, duration_ms, frequency in melody_events():
        render_note(samples, start_ms, duration_ms, frequency, 0.22)

    # A quiet I-IV-V accompaniment and short metronome accents make the beat audible.
    chords = [(130.8128, 164.8138, 196.0000), (174.6141, 220.0000, 261.6256),
              (196.0000, 246.9417, 293.6648)]
    beat_count = MELODY_BEATS
    for beat in range(beat_count):
        start_ms = INTRO_MS + beat * BEAT_MS
        root, third, fifth = chords[(beat // 4) % len(chords)]
        render_note(samples, start_ms, 430, root, 0.026)
        render_note(samples, start_ms, 300, third, 0.018)
        if beat % 2 == 1:
            render_note(samples, start_ms, 220, fifth, 0.014)

    for sample in range(sample_count):
        time_ms = sample * 1000.0 / SAMPLE_RATE
        beat_position = time_ms / BEAT_MS
        pulse = 0.0
        if 0 <= beat_position < beat_count + INTRO_MS / BEAT_MS:
            beat_phase = (beat_position - math.floor(beat_position)) * BEAT_MS / 1000.0
            pulse = 0.014 * math.exp(-beat_phase * 36.0) * math.sin(2 * math.pi * 1450.0 * beat_phase)
        fade_in = min(1.0, max(0.0, (time_ms - INTRO_MS + 1000) / 1000.0))
        fade_out = min(1.0, max(0.0, (DURATION_MS - time_ms) / 900.0))
        samples[sample] = max(-0.9, min(0.9, (samples[sample] + pulse) * fade_in * fade_out))

    SONG_DIR.mkdir(parents=True, exist_ok=True)
    with wave.open(str(SONG_DIR / "twinkle-star.wav"), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for value in samples:
            pcm = int(value * 32767)
            frames.extend(struct.pack("<hh", pcm, pcm))
        output.writeframes(frames)


def add_tap(notes, available, note_id, time_ms, chain_id=-1):
    possible = [lane for lane in range(4) if time_ms >= available[lane] + 200]
    if not possible:
        raise RuntimeError(f"no free lane for tap at {time_ms} ms")
    lane = possible[(note_id + time_ms // 300) % len(possible)]
    note = {"id": note_id, "type": "chain" if chain_id >= 0 else "tap",
            "lane": lane, "timeMs": time_ms}
    if chain_id >= 0:
        note["chainId"] = chain_id
    notes.append(note)
    available[lane] = time_ms
    return note_id + 1


def add_hold(notes, available, note_id, time_ms, duration_ms):
    possible = [lane for lane in range(4) if time_ms >= available[lane] + 200]
    if not possible:
        raise RuntimeError(f"no free lane for hold at {time_ms} ms")
    lane = possible[(note_id + time_ms // 300) % len(possible)]
    notes.append({"id": note_id, "type": "hold", "lane": lane, "timeMs": time_ms,
                  "endMs": time_ms + duration_ms})
    available[lane] = time_ms + duration_ms
    return note_id + 1


def add_chord(notes, available, note_id, time_ms, members, chord_id):
    possible = [lane for lane in range(4) if time_ms >= available[lane] + 200]
    if len(possible) < members:
        raise RuntimeError(f"no free lanes for chord at {time_ms} ms")
    start = (note_id + time_ms // 300) % len(possible)
    selected = (possible[start:] + possible[:start])[:members]
    for lane in selected:
        notes.append({"id": note_id, "type": "tap", "lane": lane, "timeMs": time_ms,
                      "chordId": chord_id})
        available[lane] = time_ms
        note_id += 1
    return note_id


def make_chart(difficulty: str):
    settings = {
        "Easy": {"approach": 2400, "extra_offsets": (), "chord_members": 2},
        "Normal": {"approach": 2000, "extra_offsets": (300, 1500), "chord_members": 2},
        "Hard": {"approach": 1700, "extra_offsets": (300, 900, 1500), "chord_members": 3},
    }[difficulty]
    notes = []
    available = [-200] * 4
    note_id = 1
    chain_id = 1
    chord_id = 1
    scheduled = []
    # Keep the melody recognizable. Special forms are distributed across phrases.
    for index, start_ms, duration_ms, _ in melody_events():
        if index in (6, 34):
            scheduled.append((start_ms, "hold", min(duration_ms, 900)))
        elif index in (8, 9):
            scheduled.append((start_ms, "chain", chain_id))
        elif index == 28 or (index in (14, 35) and difficulty != "Easy"):
            scheduled.append((start_ms, "chord", settings["chord_members"]))
        else:
            scheduled.append((start_ms, "tap", 0))

    # Higher difficulties add simple off-beat accompaniment, still respecting four lanes.
    for phrase in range(6):
        phrase_start = INTRO_MS + phrase * 8 * BEAT_MS
        for offset in settings["extra_offsets"]:
            scheduled.append((phrase_start + offset, "tap", 0))

    for start_ms, kind, detail in sorted(scheduled):
        if kind == "hold":
            note_id = add_hold(notes, available, note_id, start_ms, detail)
        elif kind == "chain":
            note_id = add_tap(notes, available, note_id, start_ms, detail)
        elif kind == "chord":
            note_id = add_chord(notes, available, note_id, start_ms, detail, chord_id)
            chord_id += 1
        else:
            note_id = add_tap(notes, available, note_id, start_ms)

    notes.sort(key=lambda note: (note["timeMs"], note["lane"], note["id"]))
    return {
        "schemaVersion": 1,
        "chartId": f"twinkle-star-{difficulty.lower()}",
        "chartVersion": 1,
        "songId": "twinkle-star",
        "difficulty": difficulty,
        "audioDurationMs": DURATION_MS,
        "approachMs": settings["approach"],
        "notes": notes,
    }


def update_index():
    existing = {"schemaVersion": 1, "songs": []}
    if INDEX_PATH.exists():
        existing = json.loads(INDEX_PATH.read_text(encoding="utf-8"))
    songs = [song for song in existing.get("songs", []) if song.get("id") != "twinkle-star"]
    songs.append({
        "id": "twinkle-star",
        "title": "小星星",
        "artist": "NoteCollision 公版旋律合成",
        "audioPath": "twinkle-star/twinkle-star.wav",
        "charts": {
            "Easy": "twinkle-star/easy.json",
            "Normal": "twinkle-star/normal.json",
            "Hard": "twinkle-star/hard.json",
        },
    })
    # Keep the original demo song first, and add this new song as the second item.
    songs.sort(key=lambda song: (0 if song.get("id") == "neon-pulse" else 1,
                                 0 if song.get("id") == "twinkle-star" else 1))
    INDEX_PATH.write_text(json.dumps({"schemaVersion": 1, "songs": songs},
                                     ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    synthesize_audio()
    for difficulty in ("Easy", "Normal", "Hard"):
        chart = make_chart(difficulty)
        (SONG_DIR / f"{difficulty.lower()}.json").write_text(
            json.dumps(chart, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    update_index()


if __name__ == "__main__":
    main()

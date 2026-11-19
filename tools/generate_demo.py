import json
import math
import struct
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SONG_DIR = ROOT / "assets" / "songs" / "neon-pulse"
SAMPLE_RATE = 44100
DURATION_SECONDS = 36
BPM = 120
BEAT_SECONDS = 60.0 / BPM


def envelope(position: float, length: float) -> float:
    attack = min(1.0, position / 0.012)
    release = min(1.0, max(0.0, length - position) / 0.035)
    return attack * release


def note_tone(frequency: float, position: float, length: float) -> float:
    env = envelope(position, length)
    return env * (math.sin(2 * math.pi * frequency * position)
                  + 0.22 * math.sin(4 * math.pi * frequency * position)) * 0.15


def synthesize_audio() -> None:
    sample_count = SAMPLE_RATE * DURATION_SECONDS
    samples = bytearray()
    progression = [(110.00, 164.81, 196.00), (87.31, 130.81, 164.81),
                   (98.00, 146.83, 174.61), (73.42, 110.00, 146.83)]
    melody = [0, 7, 12, 7, 3, 7, 10, 7]

    for sample in range(sample_count):
        time_s = sample / SAMPLE_RATE
        beat_position = time_s / BEAT_SECONDS
        bar = int(beat_position // 4)
        beat_in_bar = beat_position % 4
        root, third, fifth = progression[(bar // 2) % len(progression)]
        phase = time_s % BEAT_SECONDS
        sound = 0.035 * math.sin(2 * math.pi * root * 0.5 * time_s)
        sound += 0.018 * math.sin(2 * math.pi * third * time_s)

        arpeggio_step = int(beat_position * 2) % 8
        frequency = (root, third, fifth, third, root * 2, fifth, third * 2, fifth)[arpeggio_step]
        local = (beat_position * 2) % 1 * BEAT_SECONDS / 2
        sound += note_tone(frequency, local, BEAT_SECONDS / 2) * 0.44
        melody_step = int(beat_position * 2) % len(melody)
        melody_frequency = 440.0 * (2 ** (melody[melody_step] / 12))
        if (bar % 2 == 1) and int(beat_position * 2) % 4 == 1:
            sound += note_tone(melody_frequency, local, BEAT_SECONDS / 2) * 0.15

        if phase < 0.045 and int(beat_in_bar) in (0, 2):
            decay = math.exp(-phase * 30)
            sound += 0.34 * decay * math.sin(2 * math.pi * (58 - 18 * phase) * phase)
        if abs(beat_in_bar - round(beat_in_bar)) < 0.035 and round(beat_in_bar) in (1, 3):
            decay = math.exp(-phase * 55)
            noise = math.sin(sample * 12.9898) * math.sin(sample * 0.017)
            sound += 0.075 * decay * noise
        if abs((beat_position * 2) % 1) < 0.018 and int(beat_position * 2) % 2 == 1:
            sound += 0.017 * math.exp(-local * 35) * math.sin(2 * math.pi * 9300 * local)

        fade = min(1.0, (DURATION_SECONDS - time_s) / 0.8)
        sound = max(-0.92, min(0.92, sound * fade))
        value = int(sound * 32767)
        samples.extend(struct.pack("<hh", value, value))

    SONG_DIR.mkdir(parents=True, exist_ok=True)
    with wave.open(str(SONG_DIR / "neon-pulse.wav"), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(samples)


def add_note(notes: list, available: list, note_id: int, kind: str, time_ms: int,
             duration_ms: int = 0, group_id: int = -1) -> int:
    possible = [lane for lane in range(4) if time_ms >= available[lane] + 200]
    if not possible:
        raise RuntimeError(f"No free lane at {time_ms} ms ({kind})")
    lane = possible[(note_id + (time_ms // 250)) % len(possible)]
    item = {"id": note_id, "type": kind, "lane": lane, "timeMs": time_ms}
    if kind == "hold":
        item["endMs"] = time_ms + duration_ms
        available[lane] = item["endMs"]
    else:
        available[lane] = time_ms
    if kind == "chain":
        item["chainId"] = group_id
    notes.append(item)
    return note_id + 1


def add_chord(notes: list, available: list, note_id: int, time_ms: int,
              lanes_needed: int, chord_id: int) -> int:
    possible = [lane for lane in range(4) if time_ms >= available[lane] + 200]
    if len(possible) < lanes_needed:
        raise RuntimeError(f"No {lanes_needed}-lane chord available at {time_ms} ms")
    start = (note_id + time_ms // 250) % len(possible)
    selected = (possible[start:] + possible[:start])[:lanes_needed]
    for lane in selected:
        notes.append({"id": note_id, "type": "tap", "lane": lane,
                      "timeMs": time_ms, "chordId": chord_id})
        available[lane] = time_ms
        note_id += 1
    return note_id


def make_chart(difficulty: str) -> dict:
    settings = {
        "Easy": {"approach": 2200, "steps": (0, 500, 1000, 1500), "chord": 2},
        "Normal": {"approach": 1800, "steps": (0, 250, 750, 1250, 1750), "chord": 2},
        "Hard": {"approach": 1400, "steps": tuple(range(0, 2000, 250)), "chord": 3},
    }[difficulty]
    offset_ms = 3000
    notes = []
    available = [-200, -200, -200, -200]
    note_id = 1
    chain_id = 1
    chord_id = 1

    for bar in range(16):
        bar_start = offset_ms + bar * 2000
        steps = settings["steps"]
        chord_slot = len(steps) - 1 if bar % 4 == 1 else -1
        hold_slot = len(steps) // 2 if bar % 4 == 0 else -1
        chain_slot = 0 if bar % 5 == 2 else -1
        chain_second_slot = 1 if chain_slot == 0 else -1
        for slot, local_ms in enumerate(steps):
            if slot == chord_slot:
                note_id = add_chord(notes, available, note_id, bar_start + local_ms,
                                    settings["chord"], chord_id)
                chord_id += 1
            elif slot == hold_slot:
                note_id = add_note(notes, available, note_id, "hold", bar_start + local_ms, 500)
            elif slot in (chain_slot, chain_second_slot) and chain_slot >= 0:
                note_id = add_note(notes, available, note_id, "chain", bar_start + local_ms,
                                   group_id=chain_id)
                if slot == chain_second_slot:
                    chain_id += 1
            else:
                note_id = add_note(notes, available, note_id, "tap", bar_start + local_ms)

    notes.sort(key=lambda note: (note["timeMs"], note["lane"], note["id"]))
    return {
        "schemaVersion": 1,
        "chartId": f"neon-pulse-{difficulty.lower()}",
        "chartVersion": 1,
        "songId": "neon-pulse",
        "difficulty": difficulty,
        "audioDurationMs": DURATION_SECONDS * 1000,
        "approachMs": settings["approach"],
        "notes": notes,
    }


def main() -> None:
    synthesize_audio()
    for difficulty in ("Easy", "Normal", "Hard"):
        chart = make_chart(difficulty)
        path = SONG_DIR / f"{difficulty.lower()}.json"
        path.write_text(json.dumps(chart, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    demo_song = {
        "id": "neon-pulse",
        "title": "霓虹脉冲",
        "artist": "NoteCollision 原创合成",
        "audioPath": "neon-pulse/neon-pulse.wav",
        "charts": {
            "Easy": "neon-pulse/easy.json",
            "Normal": "neon-pulse/normal.json",
            "Hard": "neon-pulse/hard.json",
        },
    }
    index_path = ROOT / "assets" / "songs" / "index.json"
    existing_songs = []
    if index_path.exists():
        existing = json.loads(index_path.read_text(encoding="utf-8"))
        existing_songs = [song for song in existing.get("songs", [])
                         if song.get("id") != "neon-pulse"]
    index = {"schemaVersion": 1, "songs": [demo_song] + existing_songs}
    index_path.write_text(
        json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

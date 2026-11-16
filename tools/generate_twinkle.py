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
    """将小星星旋律定义换算成毫秒事件。

    参数：无参数，读取 MELODY、FREQUENCIES、INTRO_MS 和 BEAT_MS 常量。
    返回：返回若干 (旋律下标, 起始毫秒, 持续毫秒, Hz 频率) 元组组成的列表。
    说明：从 3000ms 前奏之后开始累加，100 BPM 对应每拍 600ms；旋律总长 48
    拍。结果供音频与谱面共享，保证两者使用相同节奏。
    """
    events = []
    time_ms = INTRO_MS
    for index, (name, beats) in enumerate(MELODY):
        duration_ms = beats * BEAT_MS
        events.append((index, time_ms, duration_ms, FREQUENCIES[name]))
        time_ms += duration_ms
    return events


def envelope(position: float, length: float) -> float:
    """计算钢琴音色的柔和起音和释音包络。

    参数：position/length 为音符内的秒数和总秒数，调用者保证非负。
    返回：返回两个包络增益的乘积。
    说明：起音 25ms、释音 90ms，释音剩余量下限为零；短音符可同时处在起音和释音区间。不修改外部状态。
    """
    attack = min(1.0, position / 0.025)
    release = min(1.0, max(0.0, length - position) / 0.09)
    return attack * release


def render_note(samples, start_ms: int, duration_ms: int, frequency: float, amplitude: float):
    """把一个旋律音符叠加到浮点混音缓冲区。

    参数：samples 为原地修改的浮点列表；start_ms/duration_ms 为非负毫秒；frequency 为
    Hz；amplitude 为振幅系数。
    返回：返回 None；已有样本与本音符相加。
    说明：毫秒换算采样点，尾端限制在列表长度内；基频混合 0.18 倍二次谐波和 0.04
    倍三次谐波，再乘包络及振幅。此处不最终限幅、不写 WAV，起点有效性由调用者保证。
    """
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
    """生成小星星旋律、伴奏与节拍组成的 WAV。

    参数：无参数，读取模块路径、100 BPM 和采样率等常量。
    返回：返回 None，写入 twinkle-star.wav；文件系统错误向上传播。
    说明：先渲染旋律再叠加 I/IV/V 伴奏及节拍，做淡入淡出与限幅。输出 33800ms、44.1kHz、双声道、16bit
    小端 PCM；创建目录并覆盖同名文件，结果无需随机种子。
    """
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
    """为短音符或连锁节点选择一条空闲轨道。

    参数：notes/available 原地修改；note_id 为新 ID；time_ms 为毫秒；chain_id
    非负表示连锁组，默认 -1 表示普通短音符。
    返回：返回下一个 ID；没有满足间距的轨道时抛 RuntimeError。
    说明：要求距轨道上次占用至少 200ms，通过 ID 和时间确定性选轨。写入可选 chainId
    后更新占用时刻，不检查整个连锁分组是否完整。
    """
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
    """选择空闲轨道并加入长按音符。

    参数：notes/available 原地修改；note_id 为新 ID；time_ms/duration_ms
    是头部时间与正持续毫秒数。
    返回：返回下一个 ID；无轨道可用抛 RuntimeError。
    说明：选轨前要求至少 200ms 间距；写入
    endMs，并把该轨占用延长至尾部。完整的范围和重叠合法性由运行时谱面校验器再次检查。
    """
    possible = [lane for lane in range(4) if time_ms >= available[lane] + 200]
    if not possible:
        raise RuntimeError(f"no free lane for hold at {time_ms} ms")
    lane = possible[(note_id + time_ms // 300) % len(possible)]
    notes.append({"id": note_id, "type": "hold", "lane": lane, "timeMs": time_ms,
                  "endMs": time_ms + duration_ms})
    available[lane] = time_ms + duration_ms
    return note_id + 1


def add_chord(notes, available, note_id, time_ms, members, chord_id):
    """为同时按键创建多个不同轨道的短音符。

    参数：notes/available 原地修改；note_id 为首个 ID；time_ms 为毫秒；members
    为合法成员数量；chord_id 为组号。
    返回：返回该组之后的 ID；满足间距的轨道不足抛 RuntimeError。
    说明：筛选至少 200ms 空闲间隔的轨道并确定性旋转选择，每个成员保留独立 ID、共用 chordId
    与头部时刻；更新被选轨道的占用时间。
    """
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
    """把旋律与难度附加节奏编排成小星星谱面。

    参数：difficulty 必须为 Easy、Normal、Hard。
    返回：返回 JSON 可序列化字典；非法难度抛 KeyError，排轨失败抛 RuntimeError。
    说明：保留旋律头部，指定位置安排长按、连锁及和弦，较高难度再加入切分伴奏。按时间顺序分配轨道，最终按时间/轨道/ID
    排序；三档接近时间为 2400/2000/1700ms，不在此写文件。
    """
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
    """将小星星条目合入现有曲库索引。

    参数：无参数，读取 INDEX_PATH 和固定歌曲元数据。
    返回：返回 None；读取、JSON 解析和写入异常向上传播。
    说明：移除旧的小星星条目再加入新条目，保留其他歌曲并使霓虹脉冲排前、小星星随后。以 UTF-8 缩进 JSON
    覆盖索引；这是离线生成工具，不使用应用存储的原子写入接口。
    """
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
    """生成小星星音频、三档谱面及索引。

    参数：无参数，输出路径由脚本所在项目确定。
    返回：返回 None；任何生成或文件异常向上传播。
    说明：先渲染 WAV，再逐难度保存 JSON，最后调用 update_index
    保留其他歌曲。多文件生成没有整体回滚，出错后应修复原因再完整运行脚本。
    """
    synthesize_audio()
    for difficulty in ("Easy", "Normal", "Hard"):
        chart = make_chart(difficulty)
        (SONG_DIR / f"{difficulty.lower()}.json").write_text(
            json.dumps(chart, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    update_index()


if __name__ == "__main__":
    main()

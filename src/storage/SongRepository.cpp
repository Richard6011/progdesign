#include "storage/SongRepository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace nc {
namespace {
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
bool integer(const QJsonValue &value, qint64 *number, qint64 minimum = 0,
             qint64 maximum = 2147483647) {
    if (!value.isDouble())
        return false;
    const double d = value.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < minimum || d > maximum)
        return false;
    *number = static_cast<qint64>(d);
    return true;
}
bool readJson(const QString &path, QJsonObject *object, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, "无法读取资源：" + path + "；" + file.errorString());
    if (file.size() > 16 * 1024 * 1024)
        return fail(error, "JSON 资源超过 16MiB 上限：" + path);
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, "JSON 格式错误：" + path + "；" + parse.errorString());
    *object = doc.object();
    return true;
}
QString noteName(NoteType type) {
    return type == NoteType::Hold ? "hold" : type == NoteType::ChainStep ? "chain" : "tap";
}
bool validateWavDuration(const QString &audioPath, qint64 expectedMs, QString *error) {
    if (QFileInfo(audioPath).suffix().compare("wav", Qt::CaseInsensitive) != 0)
        return true;
    QFile file(audioPath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, "音频不可读取：" + audioPath);
    const auto riff = file.read(12);
    if (riff.size() != 12 || riff.first(4) != "RIFF" || riff.mid(8, 4) != "WAVE")
        return fail(error, "WAV 文件头无效：" + audioPath);
    quint32 byteRate = 0;
    quint32 dataSize = 0;
    bool dataFound = false;
    while (!file.atEnd()) {
        const auto chunk = file.read(8);
        if (chunk.size() != 8)
            return fail(error, "WAV 数据块头被截断");
        const quint32 size =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(chunk.constData() + 4));
        const qint64 after = file.pos() + static_cast<qint64>(size);
        if (after > file.size())
            return fail(error, "WAV 数据块被截断");
        if (chunk.first(4) == "fmt ") {
            if (size < 16)
                return fail(error, "WAV fmt 数据块无效");
            const auto format = file.read(16);
            const auto encoding =
                qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(format.constData()));
            byteRate =
                qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(format.constData() + 8));
            if ((encoding != 1 && encoding != 3 && encoding != 0xfffe) || byteRate == 0)
                return fail(error, "WAV 编码或字节率无效");
        } else if (chunk.first(4) == "data") {
            dataFound = true;
            dataSize = size;
        }
        if (!file.seek(after + (size & 1)))
            return fail(error, "WAV 数据块定位失败");
    }
    if (!byteRate || !dataFound || dataSize == 0)
        return fail(error, "WAV 缺少有效音频数据");
    const qint64 actualMs = static_cast<qint64>(dataSize) * 1000 / byteRate;
    if (std::abs(actualMs - expectedMs) > 1)
        return fail(error, "谱面声明音频时长与实际 WAV 时长不一致");
    return true;
}
QByteArray canonical(const Chart &chart, bool includeIdentity = true) {
    QJsonObject root{{"audioDurationMs", chart.audioDurationMs}, {"approachMs", chart.approachMs}};
    if (includeIdentity) {
        root["schemaVersion"] = 1;
        root["chartId"] = chart.id;
        root["chartVersion"] = chart.version;
        root["songId"] = chart.songId;
        root["difficulty"] = chart.difficulty;
    }
    QJsonArray notes;
    for (const auto &note : chart.notes) {
        QJsonObject object{{"type", noteName(note.type)}, {"lane", note.lane},
                           {"timeMs", note.hitTimeMs},    {"endMs", note.endTimeMs},
                           {"chainId", note.chainId},     {"chordId", note.chordId}};
        if (includeIdentity)
            object["id"] = note.id;
        notes.append(object);
    }
    root["notes"] = notes;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
bool validate(Chart *chart, QString *error) {
    if (chart->notes.empty() || chart->notes.size() > 20000)
        return fail(error, "谱面音符数量必须在 1—20000 之间");
    std::sort(chart->notes.begin(), chart->notes.end(), [](const auto &a, const auto &b) {
        if (a.hitTimeMs != b.hitTimeMs)
            return a.hitTimeMs < b.hitTimeMs;
        if (a.lane != b.lane)
            return a.lane < b.lane;
        return a.id < b.id;
    });
    if (chart->notes.front().hitTimeMs < chart->approachMs)
        return fail(error, "首个音符早于接近时间 approachMs");
    QSet<int> ids;
    QMap<int, std::vector<const NoteDefinition *>> chains, chords;
    qint64 laneAvailable[4] = {-200, -200, -200, -200};
    for (const auto &note : chart->notes) {
        if (ids.contains(note.id))
            return fail(error, "音符 id 重复：" + QString::number(note.id));
        ids.insert(note.id);
        if (note.endTimeMs + 140 > chart->audioDurationMs)
            return fail(error, "音符或长按尾部加漏击窗口超过音频时长");
        if (note.hitTimeMs < laneAvailable[note.lane] + 200)
            return fail(error,
                        "同轨音符头部间隔不足 200ms，或占用了长按：" + QString::number(note.id));
        laneAvailable[note.lane] = note.endTimeMs;
        if (note.type == NoteType::Hold && note.endTimeMs <= note.hitTimeMs)
            return fail(error, "长按 endMs 必须晚于 timeMs");
        if (note.type == NoteType::ChainStep) {
            if (note.chainId < 0 || note.chordId >= 0)
                return fail(error, "连锁节点必须有 chainId，且不能属于同时键组");
            chains[note.chainId].push_back(&note);
        } else if (note.chainId >= 0)
            return fail(error, "只有连锁节点可包含 chainId");
        if (note.chordId >= 0) {
            if (note.type != NoteType::Tap)
                return fail(error, "同时键组只能包含单点音符");
            chords[note.chordId].push_back(&note);
        }
    }
    for (const auto &group : chains) {
        if (group.size() < 2)
            return fail(error, "连锁组至少需要两个节点");
        for (size_t i = 1; i < group.size(); ++i)
            if (group[i]->hitTimeMs - group[i - 1]->hitTimeMs < 200)
                return fail(error, "连锁节点必须按至少 200ms 的间隔递增");
    }
    for (const auto &group : chords) {
        if (group.size() < 2 || group.size() > 3)
            return fail(error, "同时键组必须为双押或三押");
        QSet<int> lanes;
        for (const auto *note : group) {
            if (note->hitTimeMs != group.front()->hitTimeMs || lanes.contains(note->lane))
                return fail(error, "同时键组必须同一时间且轨道互不重复");
            lanes.insert(note->lane);
        }
    }
    chart->chartHash = QString::fromLatin1(
        QCryptographicHash::hash(canonical(*chart), QCryptographicHash::Sha256).toHex());
    return true;
}
} // namespace

SongRepository::SongRepository(QString resourceRoot)
    : root_(QDir::cleanPath(QFileInfo(resourceRoot).absoluteFilePath())) {}

bool SongRepository::resolveResource(const QString &path, QString *resolved, QString *error) const {
    const QFileInfo rootInfo(root_);
    const QString base = rootInfo.canonicalFilePath();
    QFileInfo info(QDir::isAbsolutePath(path) ? path : QDir(root_).filePath(path));
    const QString real = info.canonicalFilePath();
    if (base.isEmpty() || real.isEmpty() || !info.isFile() || !info.isReadable())
        return fail(error, "资源不存在或不可读取：" + path);
    const QString relative = QDir(base).relativeFilePath(real);
    if (relative == ".." || relative.startsWith("../") || QDir::isAbsolutePath(relative))
        return fail(error, "资源路径越过资源根目录：" + path);
    *resolved = real;
    return true;
}

bool SongRepository::loadIndex(QString *error) {
    QString indexPath;
    if (!resolveResource("index.json", &indexPath, error))
        return false;
    QJsonObject root;
    if (!readJson(indexPath, &root, error))
        return false;
    qint64 version;
    if (!integer(root["schemaVersion"], &version) || version != 1 || !root["songs"].isArray())
        return fail(error, "曲目索引版本或 songs 字段不受支持");
    std::vector<SongInfo> loaded;
    QSet<QString> ids;
    const auto array = root["songs"].toArray();
    if (array.isEmpty())
        return fail(error, "曲目索引不能为空");
    for (const auto &value : array) {
        if (!value.isObject())
            return fail(error, "曲目索引成员必须为对象");
        const auto object = value.toObject();
        SongInfo song;
        for (const auto &key : {"id", "title", "artist", "audioPath"})
            if (!object[key].isString() || object[key].toString().trimmed().isEmpty())
                return fail(error, "曲目缺少有效字段：" + QString::fromLatin1(key));
        song.id = object["id"].toString();
        song.title = object["title"].toString();
        song.artist = object["artist"].toString();
        if (ids.contains(song.id))
            return fail(error, "歌曲 id 重复：" + song.id);
        ids.insert(song.id);
        if (!resolveResource(object["audioPath"].toString(), &song.audioPath, error))
            return false;
        if (!object["charts"].isObject())
            return fail(error, "歌曲缺少 charts 难度映射");
        const auto paths = object["charts"].toObject();
        std::vector<Chart> charts;
        for (const auto &difficulty : {QString("Easy"), QString("Normal"), QString("Hard")}) {
            QString path;
            if (!paths[difficulty].isString() ||
                !resolveResource(paths[difficulty].toString(), &path, error))
                return fail(error, "歌曲缺少有效的 " + difficulty + " 谱面");
            song.difficultyCharts[difficulty] = path;
            Chart chart;
            if (!loadChart(song, difficulty, &chart, error))
                return false;
            for (const auto &previous : charts)
                if (canonical(previous, false) == canonical(chart, false))
                    return fail(error, "三档难度不能复用完全相同的谱面内容和参数");
            charts.push_back(chart);
        }
        loaded.push_back(std::move(song));
    }
    songs_ = std::move(loaded);
    if (error)
        error->clear();
    return true;
}

bool SongRepository::loadChart(const SongInfo &song, const QString &difficulty, Chart *chart,
                               QString *error) const {
    if (!chart)
        return fail(error, "谱面输出参数不能为空");
    if (difficulty != "Easy" && difficulty != "Normal" && difficulty != "Hard")
        return fail(error, "难度必须为 Easy、Normal 或 Hard");
    if (!song.difficultyCharts.contains(difficulty))
        return fail(error, "歌曲不包含该难度：" + difficulty);
    QString path, audio;
    if (!resolveResource(song.difficultyCharts[difficulty], &path, error) ||
        !resolveResource(song.audioPath, &audio, error))
        return false;
    QJsonObject root;
    if (!readJson(path, &root, error))
        return false;
    Chart loaded;
    qint64 number;
    if (!integer(root["schemaVersion"], &number) || number != 1)
        return fail(error, "不支持的谱面 schemaVersion");
    for (const auto &key : {"chartId", "songId", "difficulty"})
        if (!root[key].isString() || root[key].toString().trimmed().isEmpty())
            return fail(error, "谱面标识字段无效：" + QString::fromLatin1(key));
    loaded.id = root["chartId"].toString();
    loaded.songId = root["songId"].toString();
    loaded.difficulty = root["difficulty"].toString();
    if (loaded.songId != song.id || loaded.difficulty != difficulty)
        return fail(error, "索引、歌曲与谱面的标识或难度不一致");
    if (!integer(root["chartVersion"], &number, 1))
        return fail(error, "chartVersion 必须为正整数");
    loaded.version = static_cast<int>(number);
    if (!integer(root["audioDurationMs"], &loaded.audioDurationMs, 1) ||
        !integer(root["approachMs"], &loaded.approachMs, 1))
        return fail(error, "音频时长与接近时间必须为正整数毫秒");
    if (!validateWavDuration(audio, loaded.audioDurationMs, error))
        return false;
    if (!root["notes"].isArray())
        return fail(error, "notes 必须为数组");
    const auto notes = root["notes"].toArray();
    if (notes.isEmpty() || notes.size() > 20000)
        return fail(error, "音符数量必须在 1—20000 之间");
    for (const auto &value : notes) {
        if (!value.isObject())
            return fail(error, "音符必须为对象");
        const auto object = value.toObject();
        NoteDefinition note;
        if (!integer(object["id"], &number))
            return fail(error, "音符 id 必须为非负整数");
        note.id = static_cast<int>(number);
        if (!integer(object["lane"], &number, 0, 3))
            return fail(error, "lane 必须在 0—3 之间");
        note.lane = static_cast<int>(number);
        if (!integer(object["timeMs"], &note.hitTimeMs))
            return fail(error, "timeMs 必须为非负整数");
        if (!object["type"].isString())
            return fail(error, "音符 type 必须为字符串");
        const auto type = object["type"].toString();
        if (type == "tap")
            note.type = NoteType::Tap;
        else if (type == "hold")
            note.type = NoteType::Hold;
        else if (type == "chain")
            note.type = NoteType::ChainStep;
        else
            return fail(error, "不支持的音符类型：" + type);
        note.endTimeMs = note.hitTimeMs;
        if (note.type == NoteType::Hold) {
            if (!integer(object["endMs"], &note.endTimeMs))
                return fail(error, "长按必须提供整数 endMs");
        } else if (object.contains("endMs")) {
            if (!integer(object["endMs"], &number) || number != note.hitTimeMs)
                return fail(error, "非长按 endMs 只能等于 timeMs");
        }
        if (object.contains("chainId")) {
            if (!integer(object["chainId"], &number, -1))
                return fail(error, "chainId 类型无效");
            note.chainId = static_cast<int>(number);
        }
        if (object.contains("chordId")) {
            if (!integer(object["chordId"], &number, -1))
                return fail(error, "chordId 类型无效");
            note.chordId = static_cast<int>(number);
        }
        loaded.notes.push_back(note);
    }
    if (!validate(&loaded, error))
        return false;
    *chart = std::move(loaded);
    if (error)
        error->clear();
    return true;
}

QString SongRepository::audioHash(const SongInfo &song, QString *error) const {
    QString path;
    if (!resolveResource(song.audioPath, &path, error))
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        fail(error, "读取音频以计算 SHA-256 失败");
        return {};
    }
    if (error)
        error->clear();
    return QString::fromLatin1(hash.result().toHex());
}
} // namespace nc

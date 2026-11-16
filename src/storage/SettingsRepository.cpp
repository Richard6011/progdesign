#include "storage/SettingsRepository.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace nc {
namespace {
constexpr int CurrentSettingsSchema = 2;

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
bool integer(const QJsonValue &value, int *number, int minimum, int maximum) {
    if (!value.isDouble())
        return false;
    const auto d = value.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < minimum || d > maximum)
        return false;
    *number = static_cast<int>(d);
    return true;
}
bool valid(const AppSettings &settings) {
    return settings.schemaVersion == CurrentSettingsSchema && settings.musicVolume >= 0 &&
           settings.musicVolume <= 100 && settings.effectVolume >= 0 &&
           settings.effectVolume <= 100 && settings.inputOffsetMs >= -200 &&
           settings.inputOffsetMs <= 200 && !settings.playerName.trimmed().isEmpty() &&
           settings.playerName.trimmed().size() <= 16;
}
} // namespace

SettingsRepository::SettingsRepository(QString dataDir)
    : dataDir_(dataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                 : QDir::cleanPath(dataDir)) {}

QString SettingsRepository::filePath() const {
    return QDir(dataDir_).filePath("settings.json");
}

AppSettings SettingsRepository::load(QString *warning) {
    AppSettings settings;
    if (!QFile::exists(filePath())) {
        QString error;
        if (!save(settings, &error) && warning)
            *warning = "使用默认设置；" + error;
        else if (warning)
            warning->clear();
        return settings;
    }
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (warning)
            *warning = "设置读取失败，使用默认设置：" + file.errorString();
        return settings;
    }
    if (file.size() > 64 * 1024) {
        if (warning)
            *warning = "设置文件超过大小上限，保留原文件并使用默认设置";
        return settings;
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (warning)
            *warning = "设置文件损坏，保留原文件并使用默认设置";
        return settings;
    }
    const auto object = doc.object();
    int schema = 0;
    if (!integer(object["schemaVersion"], &schema, 1, CurrentSettingsSchema)) {
        if (warning)
            *warning = "设置版本未知，保留原文件并使用默认设置";
        return AppSettings{};
    }
    if (schema < CurrentSettingsSchema) {
        file.close();
        QString resetError;
        AppSettings fresh;
        if (!save(fresh, &resetError)) {
            if (warning)
                *warning = "旧版设置重置失败，保留原文件并使用默认设置：" + resetError;
            return fresh;
        }
        if (warning)
            *warning = "旧版设置已重置为默认值";
        return fresh;
    }
    settings.schemaVersion = schema;
    if (!integer(object["musicVolume"], &settings.musicVolume, 0, 100) ||
        !integer(object["effectVolume"], &settings.effectVolume, 0, 100) ||
        !integer(object["inputOffsetMs"], &settings.inputOffsetMs, -200, 200) ||
        !object["playerName"].isString()) {
        if (warning)
            *warning = "设置字段或版本无效，保留原文件并使用默认设置";
        return AppSettings{};
    }
    settings.playerName = object["playerName"].toString().trimmed();
    if (!valid(settings)) {
        if (warning)
            *warning = "昵称无效，保留原设置文件并使用默认设置";
        return AppSettings{};
    }
    if (warning)
        warning->clear();
    return settings;
}

bool SettingsRepository::save(const AppSettings &settings, QString *error) {
    if (!valid(settings))
        return fail(error, "设置无效：音量应为 0—100，偏移应为 -200—200ms，昵称应为 1—16 字符");
    if (dataDir_.isEmpty() || !QDir().mkpath(dataDir_))
        return fail(error, "无法创建用户数据目录：" + dataDir_);
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, "无法保存设置：" + file.errorString());
    QJsonObject object{{"schemaVersion", CurrentSettingsSchema},
                       {"musicVolume", settings.musicVolume},
                       {"effectVolume", settings.effectVolume},
                       {"inputOffsetMs", settings.inputOffsetMs},
                       {"playerName", settings.playerName.trimmed()}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "保存设置失败：" + file.errorString());
    if (error)
        error->clear();
    return true;
}
} // namespace nc

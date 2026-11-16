#include "ProtocolCodec.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>
#include <cmath>

namespace nc {
namespace {
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

bool integer(const QJsonValue &value, double minimum, double maximum) {
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= minimum &&
           value.toDouble() <= maximum && std::floor(value.toDouble()) == value.toDouble();
}
} // namespace

bool ProtocolCodec::validateEnvelope(const QJsonObject &message, QString *error) {
    if (!integer(message.value("protocolVersion"), 1, 65535) ||
        message.value("protocolVersion").toInt() != ProtocolVersion)
        return fail(error, QStringLiteral("协议版本不匹配"));
    if (!message.value("type").isString() || message.value("type").toString().isEmpty() ||
        message.value("type").toString().size() > 40)
        return fail(error, QStringLiteral("协议消息类型无效"));
    if (!message.value("roomId").isString() || !message.value("matchId").isString() ||
        message.value("roomId").toString().size() > 64 ||
        message.value("matchId").toString().size() > 64)
        return fail(error, QStringLiteral("协议房间或对局标识无效"));
    if (!integer(message.value("seq"), 1, 9007199254740991.0))
        return fail(error, QStringLiteral("协议序号必须为正整数"));
    if (!message.value("payload").isObject())
        return fail(error, QStringLiteral("协议正文必须是对象"));
    if (message.contains("configRevision") &&
        !integer(message.value("configRevision"), 0, 2147483647))
        return fail(error, QStringLiteral("配置版本必须为非负整数"));
    return true;
}

QByteArray ProtocolCodec::encode(const QJsonObject &message, QString *error) {
    if (!validateEnvelope(message, error))
        return {};
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > MaximumFrameBytes) {
        fail(error, QStringLiteral("协议帧超出长度限制"));
        return {};
    }
    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame.append(body);
    return frame;
}

bool ProtocolCodec::feed(const QByteArray &bytes, QList<QJsonObject> &messages, QString *error) {
    // 分段复制输入，缓冲上限为单帧长度；一批输入可以包含任意数量的完整帧。
    qsizetype consumed = 0;
    while (consumed < bytes.size() || m_buffer.size() >= 4) {
        if (m_buffer.size() < 4) {
            const auto count = qMin<qsizetype>(4 - m_buffer.size(), bytes.size() - consumed);
            m_buffer.append(bytes.constData() + consumed, count);
            consumed += count;
            if (m_buffer.size() < 4)
                break;
        }
        const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());
        if (length == 0 || length > MaximumFrameBytes) {
            reset();
            return fail(error, QStringLiteral("协议帧长度为零或超过 64 KiB"));
        }
        const qsizetype count =
            qMin<qsizetype>(4 + length - m_buffer.size(), bytes.size() - consumed);
        m_buffer.append(bytes.constData() + consumed, count);
        consumed += count;
        if (m_buffer.size() < 4 + length)
            break;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(m_buffer.mid(4), &parseError);
        reset();
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return fail(error, QStringLiteral("协议正文不是有效 JSON 对象"));
        if (!validateEnvelope(document.object(), error))
            return false;
        messages.append(document.object());
    }
    return true;
}

void ProtocolCodec::reset() {
    m_buffer.clear();
}

} // namespace nc

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace nc {

// TCP 消息使用四字节大端正文长度和 UTF-8 JSON，不依赖一次 readyRead 对应一条消息。
class ProtocolCodec {
  public:
    static constexpr int ProtocolVersion = 1;
    static constexpr quint32 MaximumFrameBytes = 64 * 1024;

    // 返回完整帧；半包留到下次 feed。非法帧清空缓冲并返回 false。
    bool feed(const QByteArray &bytes, QList<QJsonObject> &messages, QString *error = nullptr);
    // 校验信封并编码。失败返回空数组，通过 error 给出原因。
    static QByteArray encode(const QJsonObject &message, QString *error = nullptr);
    static bool validateEnvelope(const QJsonObject &message, QString *error = nullptr);
    void reset();

  private:
    QByteArray m_buffer; // 仅保存当前尚未完整到达的帧，避免网络输入无限增长。
};

} // namespace nc

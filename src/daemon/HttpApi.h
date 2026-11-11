// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QTcpServer>

namespace opendial {

class Engine;

// Optional local HTTP API (disabled by default). Binds to 127.0.0.1 only,
// requires a bearer token stored 0600 in $XDG_RUNTIME_DIR/opendial/http-token,
// rejects non-local Host headers (DNS rebinding) and any Origin header
// (browser cross-site requests).
class HttpApi : public QObject {
    Q_OBJECT
public:
    explicit HttpApi(Engine *engine, QObject *parent = nullptr);
    bool start(quint16 port, QString *error);
    void stop();
    bool isRunning() const { return m_server.isListening(); }
    QString tokenPath() const;
private:
    void onConnection();
    QByteArray handle(const QByteArray &method, const QByteArray &path, const QHash<QByteArray, QByteArray> &headers, const QByteArray &body);
    Engine *m_engine;
    QTcpServer m_server;
    QByteArray m_token;
};

} // namespace opendial

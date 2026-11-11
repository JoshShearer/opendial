// SPDX-License-Identifier: MIT
#include "HttpApi.h"
#include "Config.h"
#include "Engine.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTimer>

namespace opendial {

namespace {
QByteArray response(int code, const QByteArray &body, const QByteArray &type = "application/json")
{
    static const QHash<int, QByteArray> reasons{{200, "OK"}, {400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"},
                                                {404, "Not Found"}, {405, "Method Not Allowed"}, {413, "Payload Too Large"}};
    return "HTTP/1.1 " + QByteArray::number(code) + ' ' + reasons.value(code, "Error") + "\r\nContent-Type: " + type
           + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
}
QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
QByteArray err(int code, const QString &m) { return response(code, json(QJsonObject{{QStringLiteral("error"), m}})); }

bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char d = 0;
    for (int i = 0; i < a.size(); ++i)
        d |= uchar(a[i]) ^ uchar(b[i]);
    return d == 0;
}
} // namespace

HttpApi::HttpApi(Engine *engine, QObject *parent) : QObject(parent), m_engine(engine)
{
    connect(&m_server, &QTcpServer::newConnection, this, &HttpApi::onConnection);
}

QString HttpApi::tokenPath() const { return runtimeDir() + QStringLiteral("/http-token"); }

bool HttpApi::start(quint16 port, QString *error)
{
    if (m_server.isListening())
        return true;
    QByteArray raw(32, Qt::Uninitialized);
    for (int i = 0; i < raw.size(); ++i)
        raw[i] = char(QRandomGenerator::system()->bounded(256));
    m_token = raw.toHex();
    QDir().mkpath(runtimeDir());
    QFile::setPermissions(runtimeDir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile f(tokenPath());
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) || f.write(m_token + '\n') < 0 || !f.commit()) {
        *error = QStringLiteral("cannot write token file");
        return false;
    }
    QFile::setPermissions(tokenPath(), QFile::ReadOwner | QFile::WriteOwner);
    if (!m_server.listen(QHostAddress::LocalHost, port)) {
        *error = m_server.errorString();
        return false;
    }
    return true;
}

void HttpApi::stop()
{
    m_server.close();
    QFile::remove(tokenPath());
}

void HttpApi::onConnection()
{
    while (auto *sock = m_server.nextPendingConnection()) {
        auto buf = std::make_shared<QByteArray>();
        QTimer::singleShot(5000, sock, [sock] { sock->abort(); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        connect(sock, &QTcpSocket::readyRead, sock, [this, sock, buf] {
            buf->append(sock->readAll());
            if (buf->size() > 256 * 1024) {
                sock->write(err(413, QStringLiteral("too large")));
                sock->disconnectFromHost();
                return;
            }
            const int hdrEnd = buf->indexOf("\r\n\r\n");
            if (hdrEnd < 0)
                return;
            const QList<QByteArray> lines = buf->left(hdrEnd).split('\n');
            const QList<QByteArray> req = lines.value(0).trimmed().split(' ');
            if (req.size() < 3) {
                sock->write(err(400, QStringLiteral("bad request line")));
                sock->disconnectFromHost();
                return;
            }
            QHash<QByteArray, QByteArray> headers;
            for (int i = 1; i < lines.size(); ++i) {
                const int c = lines[i].indexOf(':');
                if (c > 0)
                    headers.insert(lines[i].left(c).trimmed().toLower(), lines[i].mid(c + 1).trimmed());
            }
            const int len = headers.value("content-length", "0").toInt();
            if (len < 0 || len > 128 * 1024) {
                sock->write(err(413, QStringLiteral("too large")));
                sock->disconnectFromHost();
                return;
            }
            if (buf->size() < hdrEnd + 4 + len)
                return;
            sock->write(handle(req[0], req[1], headers, buf->mid(hdrEnd + 4, len)));
            sock->disconnectFromHost();
        });
    }
}

QByteArray HttpApi::handle(const QByteArray &method, const QByteArray &path, const QHash<QByteArray, QByteArray> &h, const QByteArray &body)
{
    const QByteArray host = h.value("host");
    const QByteArray hostName = host.left(host.lastIndexOf(':') > 0 ? host.lastIndexOf(':') : host.size());
    if (hostName != "127.0.0.1" && hostName != "localhost")
        return err(403, QStringLiteral("host not allowed"));
    if (h.contains("origin"))
        return err(403, QStringLiteral("browser requests are not allowed"));
    const QByteArray auth = h.value("authorization");
    if (!auth.startsWith("Bearer ") || !constantTimeEquals(auth.mid(7).trimmed(), m_token))
        return err(401, QStringLiteral("missing or invalid token"));

    QJsonObject in;
    if (!body.isEmpty()) {
        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isObject())
            return err(400, QStringLiteral("expected JSON object"));
        in = doc.object();
    }
    if (path == "/v1/state" && method == "GET")
        return response(200, json(m_engine->stateJson()));
    if (path == "/v1/tools" && method == "GET")
        return response(200, QJsonDocument(m_engine->menuTools()).toJson(QJsonDocument::Compact));
    if (path == "/v1/tool" && method == "POST")
        return m_engine->setActiveTool(in.value(QStringLiteral("id")).toString()) ? response(200, "{\"ok\":true}") : err(400, QStringLiteral("unknown tool"));
    if (path == "/v1/menu" && method == "POST") {
        m_engine->menuOp(in.value(QStringLiteral("op")).toString(QStringLiteral("toggle")));
        return response(200, "{\"ok\":true}");
    }
    if (path == "/v1/simulate" && method == "POST") {
        QString e;
        return m_engine->simulate(in, &e) ? response(200, "{\"ok\":true}") : err(400, e);
    }
    if (path == "/v1/profile/lock" && method == "POST")
        return m_engine->lockProfile(in.value(QStringLiteral("id")).toString()) ? response(200, "{\"ok\":true}") : err(400, QStringLiteral("unknown profile"));
    if (path == "/v1/profile/unlock" && method == "POST") {
        m_engine->unlockProfile();
        return response(200, "{\"ok\":true}");
    }
    return err(404, QStringLiteral("not found"));
}

} // namespace opendial

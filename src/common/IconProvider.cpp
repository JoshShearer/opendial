// SPDX-License-Identifier: MIT
#include "IconProvider.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrlQuery>

static void opendialInitIcons() { Q_INIT_RESOURCE(icons); }

namespace opendial {

void initIconResources()
{
    static bool done = false;
    if (!done) {
        opendialInitIcons();
        done = true;
    }
}

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) { initIconResources(); }

QStringList IconProvider::builtinNames()
{
    initIconResources();
    QStringList out;
    for (const auto &f : QDir(QStringLiteral(":/opendial/icons")).entryList({QStringLiteral("*.svg")}))
        if (f != QLatin1String("app.svg"))
            out << f.chopped(4);
    return out;
}

QImage IconProvider::render(const QString &name, const QColor &color, const QSize &size)
{
    QByteArray svg;
    if (name.startsWith(QLatin1Char('/'))) {
        const QFileInfo fi(name);
        const QString suffix = fi.suffix().toLower();
        if (!fi.isFile() || fi.size() > 1024 * 1024 || (suffix != QLatin1String("svg") && suffix != QLatin1String("png")))
            return render(QStringLiteral("custom"), color, size);
        if (suffix == QLatin1String("png")) {
            QImageReader r(name);
            r.setScaledSize(size);
            return r.read();
        }
        QFile f(name);
        if (f.open(QIODevice::ReadOnly))
            svg = f.readAll();
    } else {
        QFile f(QStringLiteral(":/opendial/icons/%1.svg").arg(name.isEmpty() ? QStringLiteral("custom") : name));
        if (!f.open(QIODevice::ReadOnly)) {
            QFile fallback(QStringLiteral(":/opendial/icons/custom.svg"));
            if (!fallback.open(QIODevice::ReadOnly))
                return {};
            svg = fallback.readAll();
        } else {
            svg = f.readAll();
        }
    }
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QSvgRenderer r(svg);
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p);
    return img;
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    QString name = id;
    QColor color(Qt::white);
    const int q = id.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        name = id.left(q);
        const QUrlQuery query(id.mid(q + 1));
        // accept "#rrggbb", "%23rrggbb" or bare "rrggbb" (QML passes the latter)
        QString cs = query.queryItemValue(QStringLiteral("color"), QUrl::FullyDecoded);
        cs.remove(QStringLiteral("%23"));
        cs.remove(QLatin1Char('#'));
        const QColor c(QLatin1Char('#') + cs);
        if (c.isValid())
            color = c;
    }
    const QSize s = requested.isValid() && !requested.isEmpty() ? requested.boundedTo(QSize(512, 512)) : QSize(48, 48);
    if (size)
        *size = s;
    return render(name, color, s);
}

} // namespace opendial

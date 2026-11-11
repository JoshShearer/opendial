// SPDX-License-Identifier: MIT
#pragma once

#include <QQuickImageProvider>

namespace opendial {

// image://opendial/<name>?color=#rrggbb  -> tinted built-in icon
// image://opendial//abs/path/icon.svg     -> user icon (svg/png only)
class IconProvider : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    static QStringList builtinNames();
    static QImage render(const QString &name, const QColor &color, const QSize &size);
};

void initIconResources();

} // namespace opendial

#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>

#include "backend.h"

// The selected read's chromatogram around the cursor: the four channels, the
// calls above them, their qualities below, and the trimmed ends shaded.
class TraceView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *session READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY appearanceChanged)
    Q_PROPERTY(QString trimmedLabel MEMBER m_trimmedLabel NOTIFY appearanceChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY appearanceChanged)

public:
    explicit TraceView(QQuickItem *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *backend);
    qreal zoom() const { return m_zoom; }
    void paint(QPainter *painter) override;

    // The call nearest x, or -1 when no read is shown.
    Q_INVOKABLE int baseAt(qreal x) const;
    Q_INVOKABLE void pan(qreal pixels);
    Q_INVOKABLE void zoomBy(qreal factor);

signals:
    void backendChanged();
    void appearanceChanged();

private:
    const Read *read() const;
    // The sample in the middle of the view.
    qreal center() const;
    qreal xOf(qreal sample) const { return width() / 2 + (sample - center()) * m_zoom; }

    QPointer<Backend> m_backend;
    QColor m_accent = QColor("#FFD60A");
    QString m_trimmedLabel = "Trimmed";
    qreal m_zoom = 2.0;
    qreal m_pan = 0;
};

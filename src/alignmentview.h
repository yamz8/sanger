#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>

#include "backend.h"

// The reads stacked against the reference, one column per base, with the
// consensus underneath and the cursor column marked. Scrolls itself to keep
// the cursor in view; QML asks it what lies under the pointer.
class AlignmentView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *session READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY appearanceChanged)
    Q_PROPERTY(QString referenceLabel MEMBER m_referenceLabel NOTIFY appearanceChanged)
    Q_PROPERTY(QString consensusLabel MEMBER m_consensusLabel NOTIFY appearanceChanged)
    Q_PROPERTY(QString noMatchLabel MEMBER m_noMatchLabel NOTIFY appearanceChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentHeightChanged)

public:
    explicit AlignmentView(QQuickItem *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *backend);
    int contentHeight() const;
    void paint(QPainter *painter) override;

    // -1 over the names.
    Q_INVOKABLE int columnAt(qreal x) const;
    // The read in the row under y, or -1 for the reference, consensus, and ruler.
    Q_INVOKABLE int readAt(qreal y) const;
    Q_INVOKABLE bool isOverview(qreal y) const;
    Q_INVOKABLE int overviewColumnAt(qreal x) const;
    Q_INVOKABLE void scrollBy(int columns);
    Q_INVOKABLE void scrollRows(int rows);

signals:
    void backendChanged();
    void appearanceChanged();
    void contentHeightChanged();

private:
    struct Row {
        enum Kind { Reference, Read, Consensus } kind;
        int read = -1;
        qreal y = 0;
    };
    QVector<Row> visibleRows() const;
    int visibleColumns() const;
    qreal cellWidth() const;
    void followCursor();
    void clampScroll();

    QPointer<Backend> m_backend;
    QColor m_accent = QColor("#FFD60A");
    QString m_referenceLabel = "Reference", m_consensusLabel = "Consensus", m_noMatchLabel = "no match";
    QFont m_font;
    int m_first = 0;
    int m_firstRead = 0;
};

#include "alignmentview.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "basecolors.h"

namespace {
constexpr qreal Gutter = 176;
constexpr qreal RightPad = 8;
constexpr qreal OverviewY = 4, OverviewHeight = 8;
constexpr qreal RulerY = 16;
constexpr qreal RowsY = 34;
constexpr qreal RowHeight = 20;
constexpr qreal BottomPad = 6;
}

AlignmentView::AlignmentView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    m_font = QFont("monospace");
    m_font.setStyleHint(QFont::TypeWriter);
    m_font.setPixelSize(13);
    setAntialiasing(true);
    connect(this, &AlignmentView::appearanceChanged, this, [this] { update(); });
    connect(this, &QQuickItem::widthChanged, this, [this] { clampScroll(); followCursor(); });
    connect(this, &QQuickItem::heightChanged, this, [this] { followCursor(); });
}

void AlignmentView::setBackend(Backend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &Backend::changed, this, [this] {
            emit contentHeightChanged();
            clampScroll();
            followCursor();
        });
        connect(m_backend, &Backend::cursorChanged, this, &AlignmentView::followCursor);
    }
    emit backendChanged();
    emit contentHeightChanged();
    update();
}

int AlignmentView::contentHeight() const {
    if (!m_backend || m_backend->readCount() == 0)
        return 0;
    const int rows = m_backend->readCount() + (m_backend->hasReference() ? 2 : 0);
    return int(RowsY + rows * RowHeight + BottomPad);
}

qreal AlignmentView::cellWidth() const { return std::ceil(QFontMetricsF(m_font).horizontalAdvance('W')) + 3; }

int AlignmentView::visibleColumns() const { return std::max(1, int((width() - Gutter - RightPad) / cellWidth())); }

// The reference stays at the top and the consensus at the bottom; the reads
// between them scroll when there are more than fit.
QVector<AlignmentView::Row> AlignmentView::visibleRows() const {
    QVector<Row> rows;
    if (!m_backend)
        return rows;
    const bool reference = m_backend->hasReference();
    const int reads = m_backend->readCount();
    qreal y = RowsY;
    if (reference) {
        rows << Row{Row::Reference, -1, y};
        y += RowHeight;
    }
    const qreal bottom = reference ? std::min(y + reads * RowHeight, height() - BottomPad - RowHeight)
                                   : height() - BottomPad;
    for (int r = m_firstRead; r < reads && y + RowHeight <= bottom + 0.5; ++r, y += RowHeight)
        rows << Row{Row::Read, r, y};
    if (reference)
        rows << Row{Row::Consensus, -1, std::max(y, bottom)};
    return rows;
}

void AlignmentView::clampScroll() {
    if (!m_backend)
        return;
    m_first = std::clamp(m_first, 0, std::max(0, m_backend->columnCount() - visibleColumns()));
    m_firstRead = std::clamp(m_firstRead, 0, std::max(0, m_backend->readCount() - 1));
}

void AlignmentView::followCursor() {
    if (!m_backend)
        return;
    const int visible = visibleColumns();
    const int cursor = m_backend->cursorColumn();
    const int margin = std::min(4, visible / 4);
    if (cursor < m_first + margin || cursor > m_first + visible - 1 - margin)
        m_first = cursor - visible / 2;

    const int selected = m_backend->selectedRead();
    if (selected >= 0) {
        if (selected < m_firstRead)
            m_firstRead = selected;
        int shown = 0;
        for (const Row &row : visibleRows())
            shown += row.kind == Row::Read;
        if (shown > 0 && selected >= m_firstRead + shown)
            m_firstRead = selected - shown + 1;
    }
    clampScroll();
    update();
}

int AlignmentView::columnAt(qreal x) const {
    if (!m_backend || x < Gutter)
        return -1;
    const int column = m_first + int((x - Gutter) / cellWidth());
    return column < m_backend->columnCount() ? column : -1;
}

int AlignmentView::readAt(qreal y) const {
    for (const Row &row : visibleRows())
        if (row.kind == Row::Read && y >= row.y && y < row.y + RowHeight)
            return row.read;
    return -1;
}

bool AlignmentView::isOverview(qreal y) const { return y < RulerY; }

int AlignmentView::overviewColumnAt(qreal x) const {
    if (!m_backend || m_backend->columnCount() == 0)
        return -1;
    const qreal span = width() - Gutter - RightPad;
    const qreal fraction = std::clamp((x - Gutter) / span, 0.0, 1.0);
    return std::min(m_backend->columnCount() - 1, int(fraction * m_backend->columnCount()));
}

void AlignmentView::scrollBy(int columns) {
    m_first += columns;
    clampScroll();
    update();
}

void AlignmentView::scrollRows(int rows) {
    m_firstRead += rows;
    clampScroll();
    update();
}

void AlignmentView::paint(QPainter *painter) {
    if (!m_backend || m_backend->readCount() == 0)
        return;
    const Assembly &assembly = m_backend->assembly();
    const Layout &layout = assembly.layout();
    const bool hasReference = assembly.hasReference();
    const qreal cell = cellWidth();
    const int visible = visibleColumns();
    const int last = std::min(layout.size(), m_first + visible + 1);
    const int cursor = m_backend->cursorColumn();
    const int selected = m_backend->selectedRead();
    const QVector<Row> rows = visibleRows();
    const qreal rowsBottom = rows.isEmpty() ? RowsY : rows.last().y + RowHeight;
    const QFontMetricsF metrics(m_font);
    QFont small = m_font;
    small.setPixelSize(10);
    QFont label = QFont();
    label.setPixelSize(12);

    // Overview: the whole alignment in one strip, mismatches as ticks, the
    // visible window outlined.
    const qreal span = width() - Gutter - RightPad;
    auto overviewX = [&](int column) { return Gutter + span * column / std::max(1, layout.size()); };
    painter->fillRect(QRectF(Gutter, OverviewY, span, OverviewHeight), Palette::Row);
    if (layout.firstCovered >= 0)
        painter->fillRect(QRectF(overviewX(layout.firstCovered), OverviewY,
                                 std::max(1.0, overviewX(layout.lastCovered + 1) - overviewX(layout.firstCovered)),
                                 OverviewHeight),
                          Palette::Faint);
    for (int column : layout.mismatches)
        painter->fillRect(QRectF(overviewX(column), OverviewY, 1.5, OverviewHeight), Palette::Problem);
    painter->setPen(QPen(m_accent, 1));
    painter->drawRect(QRectF(overviewX(m_first), OverviewY - 0.5,
                             std::max(2.0, overviewX(std::min(layout.size(), m_first + visible)) - overviewX(m_first)),
                             OverviewHeight + 1));

    // Selected row and cursor column under everything else.
    for (const Row &row : rows)
        if (row.kind == Row::Read && row.read == selected)
            painter->fillRect(QRectF(0, row.y, width(), RowHeight), Palette::Row);
    if (cursor >= m_first && cursor < last) {
        QColor band = m_accent;
        band.setAlpha(46);
        painter->fillRect(QRectF(Gutter + (cursor - m_first) * cell, RulerY, cell, rowsBottom - RulerY), band);
    }

    // Ruler: every tenth reference position (every tenth base without one).
    painter->setFont(small);
    for (int c = m_first; c < last; ++c) {
        const auto &column = layout.columns[c];
        if (column.insert != 0 || (column.ref + 1) % 10 != 0)
            continue;
        const qreal x = Gutter + (c - m_first) * cell + cell / 2;
        painter->setPen(Palette::Faint);
        painter->drawLine(QPointF(x, RulerY + 11), QPointF(x, RulerY + 15));
        painter->setPen(Palette::Dim);
        const QString number = QLocale().toString(column.ref + 1);
        painter->drawText(QRectF(x - 40, RulerY - 2, 80, 13), Qt::AlignCenter, number);
    }

    // Names.
    painter->setFont(label);
    for (const Row &row : rows) {
        QString name;
        QColor color = Palette::Dim;
        bool struck = false;
        if (row.kind == Row::Reference) {
            name = m_referenceLabel;
        } else if (row.kind == Row::Consensus) {
            name = m_consensusLabel;
            color = Palette::Text;
        } else {
            const Read &read = assembly.reads()[row.read];
            const QString arrow = !hasReference ? QString() : read.reverse ? QStringLiteral("← ") : QStringLiteral("→ ");
            name = arrow + read.original.name;
            color = row.read == selected ? Palette::Text : QColor("#a8a8b2");
            struck = !read.included;
            if (hasReference && !read.aligned())
                color = Palette::Dim;
        }
        QFont font = label;
        font.setStrikeOut(struck);
        painter->setFont(font);
        painter->setPen(color);
        const QString elided = QFontMetricsF(font).elidedText(name, Qt::ElideMiddle, Gutter - 20);
        painter->drawText(QRectF(10, row.y, Gutter - 16, RowHeight), Qt::AlignVCenter | Qt::AlignLeft, elided);
    }

    if (hasReference && !rows.isEmpty() && rows.last().kind == Row::Consensus) {
        painter->setPen(Palette::Faint);
        painter->drawLine(QPointF(0, rows.last().y), QPointF(width(), rows.last().y));
    }

    // Bases.
    painter->setFont(m_font);
    const qreal baseline = (RowHeight + metrics.ascent() - metrics.descent()) / 2;
    for (const Row &row : rows) {
        if (row.kind == Row::Read && hasReference && !assembly.reads()[row.read].aligned()) {
            QFont italic = label;
            italic.setItalic(true);
            painter->setFont(italic);
            painter->setPen(Palette::Dim);
            painter->drawText(QRectF(Gutter + 4, row.y, span, RowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                              m_noMatchLabel);
            painter->setFont(m_font);
            continue;
        }
        for (int c = m_first; c < last; ++c) {
            const qreal x = Gutter + (c - m_first) * cell;
            const QRectF box(x, row.y + 1, cell, RowHeight - 2);
            char symbol = ' ';
            QColor color = Palette::Text;
            if (row.kind == Row::Reference) {
                symbol = layout.reference[c];
                color = symbol == '-' ? Palette::Faint : Palette::Text;
            } else if (row.kind == Row::Consensus) {
                symbol = layout.consensus[c];
                if (symbol != ' ' && disagrees(symbol, layout.reference[c])) {
                    color = m_accent;
                    QColor fill = m_accent;
                    fill.setAlpha(40);
                    painter->fillRect(box, fill);
                }
            } else {
                const Read &read = assembly.reads()[row.read];
                symbol = layout.rows[row.read][c];
                if (symbol == ' ')
                    continue;
                const int base = layout.bases[row.read][c];
                if (hasReference) {
                    if (disagrees(symbol, layout.reference[c])) {
                        QColor fill = symbol == '-' ? Palette::Problem : baseColor(symbol);
                        fill.setAlpha(read.included ? 80 : 30);
                        painter->fillRect(box, fill);
                        color = Palette::Text;
                    } else {
                        color = QColor("#8c8c96");
                    }
                } else {
                    color = baseColor(symbol);
                    if (!read.trim.contains(base))
                        color.setAlphaF(0.25);
                }
                if (base >= 0 && read.trace.hasQuality && read.trace.quality[base] < 20)
                    color.setAlphaF(color.alphaF() * 0.55);
                if (!read.included)
                    color.setAlphaF(color.alphaF() * 0.4);
            }
            if (symbol == ' ')
                continue;
            painter->setPen(color);
            painter->drawText(QPointF(x + (cell - metrics.horizontalAdvance(QChar(symbol))) / 2, row.y + baseline),
                              QString(QChar(symbol)));
        }
    }

    if (cursor >= m_first && cursor < last) {
        painter->setPen(QPen(m_accent, 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(Gutter + (cursor - m_first) * cell, RowsY, cell, rowsBottom - RowsY));
    }
}

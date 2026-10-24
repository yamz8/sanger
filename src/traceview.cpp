#include "traceview.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "basecolors.h"

namespace {
constexpr qreal NumbersHeight = 14;
constexpr qreal LettersHeight = 22;
constexpr qreal QualityHeight = 48;
constexpr qreal Gap = 6;
constexpr int QualityTop = 60; // The quality drawn at full bar height.
}

TraceView::TraceView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &TraceView::appearanceChanged, this, [this] { update(); });
}

void TraceView::setBackend(Backend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &Backend::changed, this, [this] { update(); });
        // Moving the cursor brings the view back to it.
        connect(m_backend, &Backend::cursorChanged, this, [this] {
            m_pan = 0;
            update();
        });
    }
    emit backendChanged();
    update();
}

const Read *TraceView::read() const {
    if (!m_backend)
        return nullptr;
    const int selected = m_backend->selectedRead();
    const auto &reads = m_backend->assembly().reads();
    return selected >= 0 && selected < reads.size() ? &reads[selected] : nullptr;
}

qreal TraceView::center() const {
    const Read *r = read();
    if (!r)
        return 0;
    const auto &peaks = r->trace.peaks;
    const Backend::Spot spot = m_backend->cursorSpot();
    qreal sample;
    if (spot.left >= 0 && spot.right >= 0)
        sample = (peaks[spot.left] + peaks[spot.right]) / 2.0;
    else if (spot.left >= 0 || spot.right >= 0)
        sample = peaks[std::max(spot.left, spot.right)];
    else
        sample = peaks[std::clamp(r->trim.start, 0, int(peaks.size()) - 1)];
    return sample + m_pan;
}

int TraceView::baseAt(qreal x) const {
    const Read *r = read();
    if (!r || r->trace.peaks.isEmpty())
        return -1;
    const qreal sample = center() + (x - width() / 2) / m_zoom;
    const auto &peaks = r->trace.peaks;
    const auto it = std::lower_bound(peaks.begin(), peaks.end(), sample);
    int index = int(it - peaks.begin());
    if (index > 0 && (index == peaks.size() || sample - peaks[index - 1] < peaks[index] - sample))
        --index;
    return index;
}

void TraceView::pan(qreal pixels) {
    m_pan += pixels / m_zoom;
    update();
}

void TraceView::zoomBy(qreal factor) {
    m_zoom = std::clamp(m_zoom * factor, 0.5, 10.0);
    emit appearanceChanged();
}

void TraceView::paint(QPainter *painter) {
    const Read *r = read();
    if (!r)
        return;
    const Trace &trace = r->trace;
    const qreal w = width(), h = height();
    const qreal lettersTop = NumbersHeight;
    const qreal traceTop = lettersTop + LettersHeight + 4;
    const qreal qualityTop = h - QualityHeight;
    const qreal traceBottom = qualityTop - Gap;
    const qreal traceHeight = std::max(10.0, traceBottom - traceTop);
    const qreal middle = center();
    const qreal firstSample = std::max(0.0, middle - w / 2 / m_zoom - 2);
    const qreal lastSample = std::min(qreal(trace.sampleCount() - 1), middle + w / 2 / m_zoom + 2);
    const auto &peaks = trace.peaks;
    const int firstBase = int(std::lower_bound(peaks.begin(), peaks.end(), int(firstSample)) - peaks.begin());
    const int lastBase = int(std::upper_bound(peaks.begin(), peaks.end(), int(lastSample)) - peaks.begin());

    auto boundary = [&](int base) {
        // The sample halfway between a call and the one before it.
        if (base <= 0)
            return 0.0;
        if (base >= peaks.size())
            return qreal(trace.sampleCount());
        return (peaks[base - 1] + peaks[base]) / 2.0;
    };

    // Trimmed ends, shaded and labelled.
    QFont label;
    label.setPixelSize(11);
    painter->setFont(label);
    const qreal trimStartX = xOf(boundary(r->trim.start));
    const qreal trimEndX = xOf(boundary(r->trim.end));
    if (trimStartX > 0) {
        painter->fillRect(QRectF(0, 0, std::min(w, trimStartX), h), Palette::Shade);
        painter->setPen(Palette::Dim);
        painter->drawText(QRectF(0, traceTop, std::min(w, trimStartX) - 8, 16), Qt::AlignRight | Qt::AlignTop, m_trimmedLabel);
    }
    if (trimEndX < w) {
        painter->fillRect(QRectF(std::max(0.0, trimEndX), 0, w - std::max(0.0, trimEndX), h), Palette::Shade);
        painter->setPen(Palette::Dim);
        painter->drawText(QRectF(std::max(0.0, trimEndX) + 8, traceTop, w, 16), Qt::AlignLeft | Qt::AlignTop, m_trimmedLabel);
    }

    // Quality: one bar per call, with guides at Q20 and Q30.
    if (trace.hasQuality) {
        painter->setPen(QPen(Palette::Faint, 1, Qt::DashLine));
        for (int q : {20, 30}) {
            const qreal y = h - QualityHeight * q / QualityTop;
            painter->drawLine(QPointF(0, y), QPointF(w, y));
        }
        painter->setPen(Qt::NoPen);
        for (int i = firstBase; i < lastBase; ++i) {
            const qreal x = xOf(peaks[i]);
            const qreal spacing = (xOf(boundary(i + 1)) - xOf(boundary(i)));
            const qreal barWidth = std::clamp(spacing * 0.7, 1.5, 14.0);
            const int q = trace.quality[i];
            QColor color = q >= 20 ? m_accent : Palette::Problem;
            color.setAlphaF(q >= 30 ? 0.8 : q >= 20 ? 0.45 : 0.6);
            const qreal barHeight = QualityHeight * std::min(q, QualityTop) / QualityTop;
            painter->fillRect(QRectF(x - barWidth / 2, h - barHeight, barWidth, barHeight), color);
        }
        // Guide labels on a backing, since bars run underneath them.
        painter->setFont(label);
        for (int q : {20, 30}) {
            const QString text = QStringLiteral("Q%1").arg(q);
            const qreal y = h - QualityHeight * q / QualityTop;
            const QRectF box(w - (q == 30 ? 34 : 66), y - 7, 28, 14);
            painter->fillRect(box, QColor("#141418"));
            painter->setPen(Palette::Dim);
            painter->drawText(box, Qt::AlignCenter, text);
        }
    }

    // The four channels, scaled so most called peaks reach near the top.
    const int step = std::max(1, int(1 / m_zoom));
    painter->setBrush(Qt::NoBrush);
    for (int c = 0; c < 4; ++c) {
        const auto &channel = trace.channels[c];
        QPainterPath path;
        bool started = false;
        for (int s = int(firstSample); s <= int(lastSample); s += step) {
            const qreal value = std::min(1.12, qreal(channel[s]) / trace.scale);
            const QPointF point(xOf(s), traceBottom - std::max(0.0, value) * traceHeight * 0.85);
            if (!started) {
                path.moveTo(point);
                started = true;
            } else {
                path.lineTo(point);
            }
        }
        painter->setPen(QPen(channelColor(c), 1.4));
        painter->drawPath(path);
    }

    // Which calls disagree with the reference, to underline them.
    const Layout &layout = m_backend->assembly().layout();
    const int selected = m_backend->selectedRead();
    QVector<bool> disagreeing(trace.length(), false);
    if (m_backend->hasReference() && selected < layout.bases.size()) {
        const QVector<int> &bases = layout.bases[selected];
        for (int c = 0; c < bases.size(); ++c)
            if (bases[c] >= 0 && disagrees(layout.rows[selected][c], layout.reference[c]))
                disagreeing[bases[c]] = true;
    }

    // Calls, numbered every ten along the read as sequenced.
    const Backend::Spot spot = m_backend->cursorSpot();
    QFont letters("monospace");
    letters.setStyleHint(QFont::TypeWriter);
    letters.setPixelSize(14);
    letters.setBold(true);
    QFont numbers;
    numbers.setPixelSize(10);
    const QFontMetricsF metrics(letters);
    for (int i = firstBase; i < lastBase; ++i) {
        const qreal x = xOf(peaks[i]);
        const char base = trace.bases[i];
        const QRectF box(x - 8, lettersTop, 16, LettersHeight);
        QColor color = baseColor(base);
        if (trace.hasQuality && trace.quality[i] < 20)
            color.setAlphaF(0.55);
        if (spot.left == i && spot.right == i) {
            QPainterPath rounded;
            rounded.addRoundedRect(box.adjusted(-1, 1, 1, -1), 3, 3);
            painter->fillPath(rounded, m_accent);
            color = QColor(m_accent.lightnessF() > 0.55 ? "#111114" : "#ffffff");
        }
        painter->setFont(letters);
        painter->setPen(color);
        painter->drawText(box, Qt::AlignCenter, QString(QChar(base)));
        if (disagreeing[i]) {
            painter->setPen(QPen(Palette::Problem, 2));
            painter->drawLine(QPointF(x - 5, lettersTop + LettersHeight + 1), QPointF(x + 5, lettersTop + LettersHeight + 1));
        }
        const int number = r->reverse ? trace.length() - i : i + 1;
        if (number % 10 == 0) {
            painter->setFont(numbers);
            painter->setPen(Palette::Dim);
            painter->drawText(QRectF(x - 30, 0, 60, NumbersHeight), Qt::AlignCenter, QLocale().toString(number));
        }
    }

    // The cursor: solid on a call, dashed between two calls across a gap.
    if (spot.left >= 0 || spot.right >= 0) {
        const bool onCall = spot.left == spot.right;
        const qreal sample = onCall ? peaks[spot.left]
                             : spot.left >= 0 && spot.right >= 0 ? (peaks[spot.left] + peaks[spot.right]) / 2.0
                                                                   : peaks[std::max(spot.left, spot.right)];
        const qreal x = xOf(sample);
        painter->setPen(QPen(m_accent, 1.5, onCall ? Qt::SolidLine : Qt::DashLine));
        painter->drawLine(QPointF(x, traceTop), QPointF(x, h));
    }
}

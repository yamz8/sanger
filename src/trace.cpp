#include "trace.h"
#include "sequence.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPair>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Trace", text); }

quint32 u32(const QByteArray &data, qsizetype at) {
    const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
    return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3];
}
quint16 u16(const QByteArray &data, qsizetype at) {
    const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
    return quint16(p[0] << 8 | p[1]);
}
bool fits(const QByteArray &data, qint64 at, qint64 size) {
    return at >= 0 && size >= 0 && at + size <= data.size();
}

// Finishes a parsed trace: every call needs a peak inside the samples and a
// quality, and the drawing scale comes from the called peaks' heights.
bool finish(Trace &trace, QString *error) {
    if (trace.bases.isEmpty()) {
        *error = tr("The trace has no base calls.");
        return false;
    }
    if (trace.peaks.size() != trace.bases.size()) {
        *error = tr("The trace's peak positions don't match its base calls.");
        return false;
    }
    const int samples = trace.sampleCount();
    for (const auto &channel : trace.channels)
        if (channel.size() != samples || samples == 0) {
            *error = tr("The trace's four channels are missing or uneven.");
            return false;
        }
    for (int &peak : trace.peaks)
        peak = std::clamp(peak, 0, samples - 1);
    if (trace.quality.size() != trace.bases.size()) {
        trace.quality = QVector<int>(trace.bases.size(), 0);
        trace.hasQuality = false;
    }
    for (char &base : trace.bases) {
        base = char(std::toupper(uchar(base)));
        if (!Sequence::isBase(base))
            base = 'N';
    }

    QVector<int> heights;
    heights.reserve(trace.peaks.size());
    for (int peak : trace.peaks) {
        int height = 0;
        for (const auto &channel : trace.channels)
            height = std::max(height, channel[peak]);
        heights << height;
    }
    std::sort(heights.begin(), heights.end());
    trace.scale = std::max(1, heights.isEmpty() ? 1 : heights[heights.size() * 9 / 10]);
    return true;
}

// ABIF, Applied Biosystems' format: a directory of tagged, numbered entries,
// all big-endian. A payload of four bytes or fewer sits in the entry itself.
Trace parseAbif(const QByteArray &data, QString *error) {
    Trace trace;
    trace.format = "AB1";
    if (data.size() < 34) {
        *error = tr("The AB1 file is cut short.");
        return {};
    }
    const quint32 count = u32(data, 18), directory = u32(data, 26);
    if (!fits(data, directory, qint64(count) * 28)) {
        *error = tr("The AB1 file's directory is damaged.");
        return {};
    }
    QHash<QPair<QByteArray, int>, QByteArray> payloads;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype at = directory + qsizetype(i) * 28;
        const QByteArray name = data.mid(at, 4);
        const int number = int(u32(data, at + 4));
        const quint32 size = u32(data, at + 16);
        if (size <= 4)
            payloads.insert({name, number}, data.mid(at + 20, size));
        else if (fits(data, u32(data, at + 20), size))
            payloads.insert({name, number}, data.mid(u32(data, at + 20), size));
    }
    auto shorts = [](const QByteArray &bytes, bool isSigned) {
        QVector<int> values(bytes.size() / 2);
        for (int i = 0; i < values.size(); ++i)
            values[i] = isSigned ? int(qint16(u16(bytes, i * 2))) : int(u16(bytes, i * 2));
        return values;
    };

    // The edited calls (2) when there are any, else what the basecaller made
    // (1). Calls, peaks, and qualities come as a set from one or the other: an
    // editor can write new calls without new peaks, and mixing the sets puts
    // calls on the wrong peaks.
    int set = 1;
    if (payloads.contains({"PBAS", 2}) && payloads.contains({"PLOC", 2}) &&
        payloads.value({"PLOC", 2}).size() == 2 * payloads.value({"PBAS", 2}).size())
        set = 2;
    trace.bases = payloads.value({"PBAS", set});
    trace.peaks = shorts(payloads.value({"PLOC", set}), false);
    const QByteArray quality = payloads.value({"PCON", set});
    // Old basecallers (the 310's, for one) write the block but leave it zero.
    if (quality.size() == trace.bases.size() && quality.count('\0') < quality.size()) {
        trace.hasQuality = true;
        for (char q : quality)
            trace.quality << std::clamp(int(uchar(q)), 0, 99);
    }

    // DATA 9 to 12 are the analyzed channels, in the dye order FWO_ names
    // (often "GATC"); DATA 1 to 4 are the raw ones, used only without them.
    QByteArray order = payloads.value({"FWO_", 1}).toUpper();
    if (order.size() != 4)
        order = "GATC";
    const int first = payloads.contains({"DATA", 9}) ? 9 : 1;
    for (int i = 0; i < 4; ++i) {
        const int channel = QByteArray("ACGT").indexOf(order[i]);
        if (channel < 0) {
            *error = tr("The AB1 file names an unknown dye order.");
            return {};
        }
        trace.channels[channel] = shorts(payloads.value({"DATA", first + i}), true);
    }

    const QByteArray sample = payloads.value({"SMPL", 1});
    if (!sample.isEmpty())
        trace.sample = QString::fromLatin1(sample.mid(1, uchar(sample[0]))).trimmed();
    if (!finish(trace, error))
        return {};
    return trace;
}

// SCF, the Staden package's format: a 128-byte header, the samples, and the
// bases. Version 3 stores each channel delta-encoded twice; older versions
// interleave the four channels sample by sample.
Trace parseScf(const QByteArray &data, QString *error) {
    Trace trace;
    trace.format = "SCF";
    if (data.size() < 128) {
        *error = tr("The SCF file is cut short.");
        return {};
    }
    const quint32 samples = u32(data, 4), samplesOffset = u32(data, 8);
    const quint32 bases = u32(data, 12), basesOffset = u32(data, 24);
    const quint32 commentsSize = u32(data, 28), commentsOffset = u32(data, 32);
    const double version = QString::fromLatin1(data.mid(36, 4)).toDouble();
    const quint32 sampleSize = u32(data, 40) == 1 ? 1 : 2;
    if (!fits(data, samplesOffset, qint64(samples) * 4 * sampleSize) ||
        !fits(data, basesOffset, qint64(bases) * 12)) {
        *error = tr("The SCF file is cut short.");
        return {};
    }

    auto sampleAt = [&](qsizetype at) { return sampleSize == 1 ? int(uchar(data[at])) : int(u16(data, at)); };
    for (int c = 0; c < 4; ++c)
        trace.channels[c].resize(samples);
    if (version >= 3) {
        for (int c = 0; c < 4; ++c) {
            auto &channel = trace.channels[c];
            const qsizetype start = samplesOffset + qsizetype(c) * samples * sampleSize;
            for (quint32 i = 0; i < samples; ++i)
                channel[i] = sampleAt(start + i * sampleSize);
            // Undo the two rounds of differencing, wrapping like the writer did.
            const int mask = sampleSize == 1 ? 0xff : 0xffff;
            for (int round = 0; round < 2; ++round) {
                int previous = 0;
                for (int &value : channel) {
                    value = (value + previous) & mask;
                    previous = value;
                }
            }
        }
        for (quint32 i = 0; i < bases; ++i) {
            trace.peaks << int(u32(data, basesOffset + i * 4));
            const char base = data[basesOffset + bases * 8 + i];
            trace.bases += base;
            const int column = QByteArray("ACGT").indexOf(char(std::toupper(uchar(base))));
            int q = 0;
            for (int c = 0; c < 4; ++c) {
                const int p = uchar(data[basesOffset + bases * (4 + c) + i]);
                if (c == column || column < 0)
                    q = std::max(q, p);
            }
            trace.quality << q;
        }
    } else {
        for (quint32 i = 0; i < samples; ++i)
            for (int c = 0; c < 4; ++c)
                trace.channels[c][i] = sampleAt(samplesOffset + (qsizetype(i) * 4 + c) * sampleSize);
        for (quint32 i = 0; i < bases; ++i) {
            const qsizetype at = basesOffset + qsizetype(i) * 12;
            trace.peaks << int(u32(data, at));
            const char base = data[at + 8];
            trace.bases += base;
            const int column = QByteArray("ACGT").indexOf(char(std::toupper(uchar(base))));
            int q = 0;
            for (int c = 0; c < 4; ++c)
                if (c == column || column < 0)
                    q = std::max(q, int(uchar(data[at + 4 + c])));
            trace.quality << q;
        }
    }
    trace.hasQuality = std::any_of(trace.quality.begin(), trace.quality.end(), [](int q) { return q > 0; });

    if (fits(data, commentsOffset, commentsSize))
        for (const auto &line : data.mid(commentsOffset, commentsSize).split('\n'))
            if (line.startsWith("NAME="))
                trace.sample = QString::fromLatin1(line.mid(5)).trimmed();
    if (!finish(trace, error))
        return {};
    return trace;
}
}

double Trace::meanQuality(int start, int end) const {
    start = std::max(0, start);
    end = std::min(int(quality.size()), end);
    if (end <= start)
        return 0;
    double sum = 0;
    for (int i = start; i < end; ++i)
        sum += quality[i];
    return sum / (end - start);
}

// The read as the other strand would show it: calls complemented and reversed,
// each channel swapped for its complement's and played backwards.
Trace Trace::reverseComplemented() const {
    Trace rc = *this;
    rc.bases = Sequence::reverseComplement(bases);
    std::reverse(rc.quality.begin(), rc.quality.end());
    const int last = sampleCount() - 1;
    rc.peaks.clear();
    for (auto it = peaks.crbegin(); it != peaks.crend(); ++it)
        rc.peaks << last - *it;
    for (int c = 0; c < 4; ++c) {
        rc.channels[c] = channels[3 - c];
        std::reverse(rc.channels[c].begin(), rc.channels[c].end());
    }
    return rc;
}

bool TraceFile::looksLikeTrace(const QByteArray &data) {
    return data.startsWith("ABIF") || data.startsWith(".scf");
}

Trace TraceFile::parse(const QByteArray &data, QString *error) {
    if (data.startsWith("ABIF"))
        return parseAbif(data, error);
    if (data.startsWith(".scf"))
        return parseScf(data, error);
    *error = tr("Not an AB1 or SCF trace.");
    return {};
}

Trace TraceFile::read(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    Trace trace = parse(file.readAll(), error);
    if (trace.bases.isEmpty())
        return {};
    trace.path = path;
    trace.name = QFileInfo(path).completeBaseName();
    return trace;
}

TrimRange mottTrim(const QVector<int> &quality, double errorLimit) {
    TrimRange best;
    double sum = 0, bestSum = 0;
    int start = 0;
    for (int i = 0; i < quality.size(); ++i) {
        sum += errorLimit - std::pow(10.0, -quality[i] / 10.0);
        if (sum < 0) {
            sum = 0;
            start = i + 1;
        } else if (sum > bestSum) {
            bestSum = sum;
            best = {start, i + 1};
        }
    }
    return best;
}

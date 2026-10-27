#pragma once

// Made-up traces, written as real AB1 and SCF files.

#include <QByteArray>
#include <QFile>
#include <QVector>
#include <cmath>

#include "trace.h"

namespace Fixtures {

constexpr int Spacing = 12;

// A clean chromatogram: one Gaussian peak per call in its base's channel,
// lower and with a little crosstalk where the quality is low.
inline Trace synthetic(const QByteArray &bases, const QVector<int> &quality, const QString &name = "read") {
    Trace trace;
    trace.name = name;
    trace.bases = bases;
    trace.quality = quality;
    trace.hasQuality = true;
    const int samples = bases.size() * Spacing + Spacing;
    for (auto &channel : trace.channels)
        channel = QVector<int>(samples, 0);
    for (int i = 0; i < bases.size(); ++i) {
        const int peak = Spacing / 2 + i * Spacing;
        trace.peaks << peak;
        const int channel = QByteArray("ACGT").indexOf(bases[i]);
        const double height = 300 + 18 * quality[i];
        for (int s = std::max(0, peak - Spacing); s < std::min(samples, peak + Spacing); ++s) {
            const double shape = std::exp(-std::pow(s - peak, 2) / 8.0);
            for (int c = 0; c < 4; ++c) {
                const double amount = c == channel || channel < 0 ? height : height * (quality[i] < 20 ? 0.45 : 0.06);
                trace.channels[c][s] += int(amount * shape * (c == channel ? 1 : 0.5 + 0.13 * ((i + c) % 4)));
            }
        }
    }
    return trace;
}

inline void putU32(QByteArray &data, quint32 value) {
    data += char(value >> 24);
    data += char(value >> 16);
    data += char(value >> 8);
    data += char(value);
}
inline void putU16(QByteArray &data, quint16 value) {
    data += char(value >> 8);
    data += char(value);
}

// ABIF with the tags Sanger reads, channels in the common "GATC" dye order.
// With editedCalls, the trace's calls go in as set 1 and editedCalls as PBAS 2
// alone, as an editor that changes calls without moving peaks would write.
inline QByteArray abif(const Trace &trace, const QByteArray &sample = "SAMPLE-1", const QByteArray &editedCalls = {}) {
    struct Entry {
        QByteArray name;
        int number;
        quint16 type, elementSize;
        quint32 count;
        QByteArray payload;
    };
    auto shorts = [](const QVector<int> &values) {
        QByteArray bytes;
        for (int v : values)
            putU16(bytes, quint16(qint16(v)));
        return bytes;
    };
    QByteArray quality;
    for (int q : trace.quality)
        quality += char(q);
    const int set = editedCalls.isEmpty() ? 2 : 1;
    QVector<Entry> entries{
        {"PBAS", set, 2, 1, quint32(trace.bases.size()), trace.bases},
        {"PLOC", set, 4, 2, quint32(trace.peaks.size()), shorts(trace.peaks)},
        {"PCON", set, 2, 1, quint32(quality.size()), quality},
        {"FWO_", 1, 2, 1, 4, "GATC"},
        {"SMPL", 1, 18, 1, quint32(sample.size() + 1), char(sample.size()) + sample},
    };
    if (!editedCalls.isEmpty())
        entries << Entry{"PBAS", 2, 2, 1, quint32(editedCalls.size()), editedCalls};
    const QByteArray order = "GATC";
    for (int i = 0; i < 4; ++i) {
        const auto &channel = trace.channels[QByteArray("ACGT").indexOf(order[i])];
        entries << Entry{"DATA", 9 + i, 4, 2, quint32(channel.size()), shorts(channel)};
    }

    QByteArray body;
    QByteArray directory;
    const int dataStart = 128;
    for (const Entry &entry : entries) {
        directory += entry.name;
        putU32(directory, quint32(entry.number));
        putU16(directory, entry.type);
        putU16(directory, entry.elementSize);
        putU32(directory, entry.count);
        putU32(directory, quint32(entry.payload.size()));
        if (entry.payload.size() <= 4) {
            QByteArray inline_ = entry.payload;
            inline_.resize(4, '\0');
            directory += inline_;
        } else {
            putU32(directory, quint32(dataStart + body.size()));
            body += entry.payload;
        }
        putU32(directory, 0);
    }
    QByteArray file = "ABIF";
    putU16(file, 101);
    file += "tdir";
    putU32(file, 1);
    putU16(file, 1023);
    putU16(file, 28);
    putU32(file, quint32(entries.size()));
    putU32(file, quint32(directory.size()));
    putU32(file, quint32(dataStart + body.size()));
    putU32(file, 0);
    file.resize(dataStart, '\0');
    return file + body + directory;
}

// SCF version 3 (channels delta-encoded twice) or version 2 (interleaved).
inline QByteArray scf(const Trace &trace, int version = 3) {
    const quint32 samples = trace.sampleCount(), bases = trace.length();
    QByteArray sampleData;
    if (version >= 3) {
        for (int c = 0; c < 4; ++c) {
            QVector<int> values = trace.channels[c];
            for (int round = 0; round < 2; ++round)
                for (int i = values.size() - 1; i > 0; --i)
                    values[i] = (values[i] - values[i - 1]) & 0xffff;
            for (int v : values)
                putU16(sampleData, quint16(v));
        }
    } else {
        for (quint32 i = 0; i < samples; ++i)
            for (int c = 0; c < 4; ++c)
                putU16(sampleData, quint16(trace.channels[c][i]));
    }
    auto probability = [&](int i, int c) {
        return char(QByteArray("ACGT").indexOf(trace.bases[i]) == c ? trace.quality[i] : 0);
    };
    QByteArray baseData;
    if (version >= 3) {
        for (int p : trace.peaks)
            putU32(baseData, quint32(p));
        for (int c = 0; c < 4; ++c)
            for (quint32 i = 0; i < bases; ++i)
                baseData += probability(i, c);
        baseData += trace.bases;
        baseData += QByteArray(bases * 3, '\0');
    } else {
        for (quint32 i = 0; i < bases; ++i) {
            putU32(baseData, quint32(trace.peaks[i]));
            for (int c = 0; c < 4; ++c)
                baseData += probability(i, c);
            baseData += trace.bases[i];
            baseData += QByteArray(3, '\0');
        }
    }
    const QByteArray comments = "NAME=" + trace.name.toLatin1() + "\n";
    QByteArray file = ".scf";
    putU32(file, samples);
    putU32(file, 128);
    putU32(file, bases);
    putU32(file, 0);
    putU32(file, 0);
    putU32(file, quint32(128 + sampleData.size()));
    putU32(file, quint32(comments.size()));
    putU32(file, quint32(128 + sampleData.size() + baseData.size()));
    file += version >= 3 ? "3.00" : "2.00";
    putU32(file, 2);
    putU32(file, 0);
    putU32(file, 0);
    putU32(file, 0);
    file.resize(128, '\0');
    return file + sampleData + baseData + comments;
}

inline bool write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

}

#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <array>

// A Sanger read as the sequencer called it: four fluorescence channels, the
// base calls, the sample where each call peaks, and each call's Phred quality.
struct Trace {
    QString name;    // The file's base name, which is what people know the read by.
    QString sample;  // The sample name the instrument recorded, when it has one.
    QString path;
    QString format;  // "AB1" or "SCF"
    std::array<QVector<int>, 4> channels; // A, C, G, T
    QByteArray bases;
    QVector<int> peaks;
    QVector<int> quality;
    bool hasQuality = false;
    // The signal drawn at full height: most peaks, but not the odd spike.
    int scale = 1;

    int length() const { return bases.size(); }
    int sampleCount() const { return channels[0].size(); }
    double meanQuality(int start, int end) const;
    Trace reverseComplemented() const;
};

namespace TraceFile {
// Reads an ABIF (.ab1) or SCF file; on failure returns an empty trace and
// says why in error.
Trace read(const QString &path, QString *error);
Trace parse(const QByteArray &data, QString *error);
bool looksLikeTrace(const QByteArray &data);
}

// A half-open range of base indices, [start, end).
struct TrimRange {
    int start = 0;
    int end = 0;
    int length() const { return end - start; }
    bool contains(int index) const { return index >= start && index < end; }
    bool operator==(const TrimRange &other) const = default;
};

// Mott's trimming, as phred and most trace tools do it: the stretch of reads
// whose error probabilities fall furthest below the limit.
TrimRange mottTrim(const QVector<int> &quality, double errorLimit);

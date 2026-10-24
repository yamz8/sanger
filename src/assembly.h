#pragma once

#include "sequence.h"
#include "trace.h"

// One loaded trace and what has been decided about it.
struct Read {
    Trace original;
    Trace trace; // In the reference's orientation, or as loaded without one.
    bool reverse = false;
    bool included = true;
    // A trim set by hand, in the original orientation's coordinates so it
    // survives a new reference flipping the read; -1 when trimmed by quality.
    int manualStart = -1, manualEnd = -1;
    TrimRange trim; // Effective, in the trace's orientation.
    PairwiseAlignment alignment;
    // Agreement with the reference over the trimmed, aligned stretch.
    double identity = 0;
    int differences = 0;

    bool aligned() const { return alignment.found; }
    bool manuallyTrimmed() const { return manualStart >= 0; }
};

// What undo restores: the choices the user made, not what follows from them.
struct ReadChoices {
    bool included = true;
    int manualStart = -1, manualEnd = -1;
    bool operator==(const ReadChoices &other) const = default;
};

// Every read laid against the reference in shared columns. A column is a
// reference base, or a base some read has inserted after one.
struct Layout {
    struct Column {
        int ref = -1;    // Reference index, or the base index without a reference.
        int insert = 0;  // 0 for the reference base, k for the k-th insertion after it.
    };
    QVector<Column> columns;
    QVector<int> columnOfRef;
    QByteArray reference;       // Per column: base, or '-' for an insertion.
    QVector<QByteArray> rows;   // Per read: base, '-' for a gap, ' ' where it doesn't reach.
    QVector<QVector<int>> bases; // Per read: the trace's base index in each column, or -1.
    QByteArray consensus;       // Per column: base, '-', 'N', or ' ' where no read reaches.
    QVector<int> mismatches;    // Columns where an included read disagrees with the reference.
    int firstCovered = -1, lastCovered = -1;

    int size() const { return columns.size(); }
};

// Whether a read's symbol in a column says something other than the
// reference's: a different base, a gap, an insertion, or an N.
bool disagrees(char symbol, char reference);

class Assembly {
public:
    static constexpr double DefaultErrorLimit = 0.05;

    bool hasReference() const { return !m_reference.sequence.isEmpty(); }
    const Sequence::Record &reference() const { return m_reference; }
    void setReference(const Sequence::Record &reference);

    const QVector<Read> &reads() const { return m_reads; }
    int addTrace(const Trace &trace);

    double errorLimit() const { return m_errorLimit; }
    void setErrorLimit(double limit);
    void setIncluded(int read, bool included);
    // start and end are in the trace's current orientation.
    void setManualTrim(int read, int start, int end);
    void clearManualTrim(int read);

    QVector<ReadChoices> choices() const;
    void restore(double errorLimit, const QVector<ReadChoices> &choices);

    const Layout &layout() const { return m_layout; }
    // The consensus over the covered stretch, gaps dropped, uncovered
    // reference bases between reads as N.
    QByteArray consensus() const;
    int includedAlignedReads() const;
    QByteArray consensusFasta() const;
    // Without a reference, each read's trimmed sequence as it was loaded.
    QByteArray readsFasta() const;

private:
    void orient(Read &read) const;
    void retrim(Read &read) const;
    void rebuild();
    void rebuildWithoutReference();

    Sequence::Record m_reference;
    QVector<Read> m_reads;
    double m_errorLimit = DefaultErrorLimit;
    Layout m_layout;
};

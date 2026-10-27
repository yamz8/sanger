#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <limits>

namespace Sequence {
// IUPAC nucleotide codes, so references with ambiguities compare sensibly.
bool isBase(char base);
char complement(char base);
QByteArray reverseComplement(const QByteArray &sequence);
// True when the two codes can stand for the same nucleotide.
bool compatible(char a, char b);

struct Record {
    QString name;
    QByteArray sequence;
};
// FASTA, or a bare sequence with no header; numbers and spaces are ignored,
// as GenBank-style listings carry them. Fails on anything that isn't a base.
QVector<Record> parseFasta(const QByteArray &text, QString *error);
QByteArray formatFasta(const QString &header, const QByteArray &sequence, int width = 70);
}

// A read placed on the reference, base by base.
struct PairwiseAlignment {
    bool found = false;
    int score = 0;
    int matches = 0;
    int columns = 0;
    // For each read base: the reference index it sits on, Insertion when it
    // sits between reference bases, or Outside when it hangs off either end.
    QVector<int> refOfBase;
    static constexpr int Insertion = -1;
    static constexpr int Outside = -2;

    double identity() const { return columns ? double(matches) / columns : 0; }
};

// Where reads start on a long reference, from the 12-mers they share. Built
// once per reference, since building it costs far more than one lookup.
class ReferenceIndex {
public:
    static constexpr int NoStart = std::numeric_limits<int>::min();

    explicit ReferenceIndex(const QByteArray &reference);
    // The reference offset the read most likely starts at, or NoStart.
    int likelyStart(const QByteArray &read) const;
    // Whether aligning a read this long to a reference this long needs the
    // index to narrow the reference first.
    static bool needed(int readLength, int referenceLength);

private:
    QHash<quint32, QVector<int>> m_positions;
};

// Affine gaps (Gotoh), free end gaps so a read may overhang either end of the
// reference, and a read's ends clipped for a fixed cost where they stop
// matching inside it. Long references are cut down to the window around the
// read's best k-mer diagonal first, from the index when one is given.
PairwiseAlignment alignToReference(const QByteArray &read, const QByteArray &reference,
                                   const ReferenceIndex *index = nullptr);

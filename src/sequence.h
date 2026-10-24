#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

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

// Affine gaps (Gotoh), free end gaps so a read may overhang either end of the
// reference, and a read's ends clipped for a fixed cost where they stop
// matching inside it. Long references are cut down to the window around the
// read's best k-mer diagonal first.
PairwiseAlignment alignToReference(const QByteArray &read, const QByteArray &reference);

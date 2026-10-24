#include "sequence.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

namespace {
// A=1, C=2, G=4, T=8, and the IUPAC codes as their unions.
int mask(char base) {
    switch (base) {
    case 'A': return 1;
    case 'C': return 2;
    case 'G': return 4;
    case 'T': case 'U': return 8;
    case 'R': return 1 | 4;
    case 'Y': return 2 | 8;
    case 'S': return 2 | 4;
    case 'W': return 1 | 8;
    case 'K': return 4 | 8;
    case 'M': return 1 | 2;
    case 'B': return 2 | 4 | 8;
    case 'D': return 1 | 4 | 8;
    case 'H': return 1 | 2 | 8;
    case 'V': return 1 | 2 | 4;
    case 'N': return 15;
    default: return 0;
    }
}

constexpr int Match = 2, Ambiguous = 0, Mismatch = -3, GapOpen = -6, GapExtend = -2;
// What it costs to leave a read's end unaligned inside the reference. More than
// a stray mismatch, so a real difference near the end stays in, and far less
// than the gaps and mismatches the junk before a primer would need.
constexpr int Clip = -10;
constexpr int Negative = -1'000'000'000 / 2;
// The cells one full alignment may use before the reference is windowed.
constexpr qint64 CellLimit = 24'000'000;
constexpr int NoStart = std::numeric_limits<int>::min();

int score(char a, char b) {
    const int ma = mask(a), mb = mask(b);
    if (ma == 15 || mb == 15)
        return Ambiguous;
    if (ma == mb && (ma & (ma - 1)) == 0)
        return Match;
    return (ma & mb) ? Ambiguous : Mismatch;
}

// The reference offset where the read most likely starts, from the diagonal
// the most shared 12-mers fall on, or NoStart when they share none.
int likelyStart(const QByteArray &read, const QByteArray &reference) {
    constexpr int K = 12;
    auto code = [](char c) { return c == 'A' ? 0 : c == 'C' ? 1 : c == 'G' ? 2 : c == 'T' ? 3 : -1; };
    auto kmers = [&](const QByteArray &sequence, auto visit) {
        quint32 word = 0;
        int valid = 0;
        for (int i = 0; i < sequence.size(); ++i) {
            const int c = code(sequence[i]);
            if (c < 0) {
                valid = 0;
                continue;
            }
            word = ((word << 2) | quint32(c)) & ((1u << (2 * K)) - 1);
            if (++valid >= K)
                visit(word, i - K + 1);
        }
    };
    QHash<quint32, QVector<int>> index;
    kmers(reference, [&](quint32 word, int at) { index[word] << at; });
    QHash<int, int> votes;
    kmers(read, [&](quint32 word, int at) {
        const auto hits = index.constFind(word);
        // A k-mer found all over the reference says nothing about where.
        if (hits != index.cend() && hits->size() <= 8)
            for (int position : *hits)
                ++votes[(position - at) / 32];
    });
    int best = -1, bestVotes = 0;
    for (auto it = votes.cbegin(); it != votes.cend(); ++it)
        if (it.value() > bestVotes) {
            bestVotes = it.value();
            best = it.key();
        }
    return bestVotes >= 3 ? best * 32 : NoStart;
}
}

bool Sequence::isBase(char base) { return mask(base) != 0; }

char Sequence::complement(char base) {
    static const QByteArray from = "ACGTURYKMBVDHSWNacgturykmbvdhswn-";
    static const QByteArray to = "TGCAAYRMKVBHDSWNtgcaayrmkvbhdswn-";
    const int at = from.indexOf(base);
    return at < 0 ? 'N' : to[at];
}

QByteArray Sequence::reverseComplement(const QByteArray &sequence) {
    QByteArray result(sequence.size(), 'N');
    for (int i = 0; i < sequence.size(); ++i)
        result[sequence.size() - 1 - i] = complement(sequence[i]);
    return result;
}

bool Sequence::compatible(char a, char b) { return (mask(a) & mask(b)) != 0; }

QVector<Sequence::Record> Sequence::parseFasta(const QByteArray &text, QString *error) {
    QVector<Record> records;
    int lineNumber = 0;
    for (const QByteArray &raw : text.split('\n')) {
        ++lineNumber;
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(';'))
            continue;
        if (line.startsWith('>')) {
            records << Record{QString::fromUtf8(line.mid(1)).section(QRegularExpression("\\s+"), 0, 0), {}};
            continue;
        }
        if (records.isEmpty())
            records << Record{};
        for (char c : line) {
            if (std::isspace(uchar(c)) || std::isdigit(uchar(c)) || c == '-' || c == '.' || c == '*')
                continue;
            const char base = char(std::toupper(uchar(c)));
            if (!isBase(base)) {
                *error = QCoreApplication::translate("Sequence", "Line %1 has \"%2\", which is not a base.")
                             .arg(lineNumber)
                             .arg(QChar(c));
                return {};
            }
            records.last().sequence += base == 'U' ? 'T' : base;
        }
    }
    records.erase(std::remove_if(records.begin(), records.end(), [](const Record &r) { return r.sequence.isEmpty(); }),
                  records.end());
    if (records.isEmpty())
        *error = QCoreApplication::translate("Sequence", "There is no sequence in it.");
    return records;
}

QByteArray Sequence::formatFasta(const QString &header, const QByteArray &sequence, int width) {
    QByteArray text = ">" + header.toUtf8() + "\n";
    for (int i = 0; i < sequence.size(); i += width)
        text += sequence.mid(i, width) + "\n";
    return text;
}

PairwiseAlignment alignToReference(const QByteArray &read, const QByteArray &reference) {
    PairwiseAlignment result;
    result.refOfBase = QVector<int>(read.size(), PairwiseAlignment::Outside);
    if (read.isEmpty() || reference.isEmpty())
        return result;

    int offset = 0;
    QByteArray target = reference;
    if (qint64(read.size()) * reference.size() > CellLimit) {
        const int start = likelyStart(read, reference);
        if (start == NoStart)
            return result;
        const int margin = 200 + read.size() / 5;
        offset = std::clamp(start - margin, 0, int(reference.size()));
        const int end = std::clamp(start + int(read.size()) + margin, 0, int(reference.size()));
        target = reference.mid(offset, end - offset);
        if (target.isEmpty() || qint64(read.size()) * target.size() > CellLimit)
            return result;
    }

    // Three states per cell: M (read base on reference base), X (read base on
    // a gap) and Y (reference base on a gap). Each cell keeps two bits per
    // state saying which state it came from: 0 for a start (free at the top
    // or left edge, Clip anywhere else), 1 for M, 2 for X, 3 for Y.
    const int n = read.size(), m = target.size();
    std::vector<uchar> from(size_t(n + 1) * (m + 1), 0);
    std::vector<int> mPrev(m + 1, 0), xPrev(m + 1, Negative), yPrev(m + 1, Negative);
    std::vector<int> mCur(m + 1), xCur(m + 1), yCur(m + 1);
    int best = Negative, bestI = 0, bestJ = 0;
    auto consider = [&](int value, int i, int j) {
        if (value > best) {
            best = value;
            bestI = i;
            bestJ = j;
        }
    };

    for (int i = 1; i <= n; ++i) {
        mCur[0] = 0;
        xCur[0] = Negative;
        yCur[0] = Negative;
        for (int j = 1; j <= m; ++j) {
            uchar bits = 0;

            int diagonal = 0, diagonalFrom = 0;
            if (i > 1 && j > 1) {
                diagonal = mPrev[j - 1], diagonalFrom = 1;
                if (xPrev[j - 1] > diagonal)
                    diagonal = xPrev[j - 1], diagonalFrom = 2;
                if (yPrev[j - 1] > diagonal)
                    diagonal = yPrev[j - 1], diagonalFrom = 3;
                if (Clip > diagonal)
                    diagonal = Clip, diagonalFrom = 0;
            }
            mCur[j] = diagonal + score(read[i - 1], target[j - 1]);
            bits |= uchar(diagonalFrom);

            int up = mPrev[j] + GapOpen, upFrom = 1;
            if (xPrev[j] + GapExtend > up)
                up = xPrev[j] + GapExtend, upFrom = 2;
            if (yPrev[j] + GapOpen > up)
                up = yPrev[j] + GapOpen, upFrom = 3;
            if (i == 1)
                upFrom = 0;
            xCur[j] = up;
            bits |= uchar(upFrom << 2);

            int left = mCur[j - 1] + GapOpen, leftFrom = 1;
            if (xCur[j - 1] + GapOpen > left)
                left = xCur[j - 1] + GapOpen, leftFrom = 2;
            if (yCur[j - 1] + GapExtend > left)
                left = yCur[j - 1] + GapExtend, leftFrom = 3;
            if (j == 1)
                leftFrom = 0, left = Negative;
            yCur[j] = left;
            bits |= uchar(leftFrom << 4);

            from[size_t(i) * (m + 1) + j] = bits;
            consider(mCur[j] + (i == n || j == m ? 0 : Clip), i, j);
        }
        std::swap(mPrev, mCur);
        std::swap(xPrev, xCur);
        std::swap(yPrev, yCur);
    }
    if (best <= 0)
        return result;

    // Walk back from the best end to the edge the alignment started on.
    int i = bestI, j = bestJ, state = 1;
    while (i > 0 && j > 0 && state != 0) {
        const uchar bits = from[size_t(i) * (m + 1) + j];
        ++result.columns;
        if (state == 1) {
            result.refOfBase[i - 1] = offset + j - 1;
            if (read[i - 1] == target[j - 1] && score(read[i - 1], target[j - 1]) == Match)
                ++result.matches;
            state = bits & 3;
            --i, --j;
        } else if (state == 2) {
            result.refOfBase[i - 1] = PairwiseAlignment::Insertion;
            state = (bits >> 2) & 3;
            --i;
        } else {
            state = (bits >> 4) & 3;
            --j;
        }
    }
    result.found = true;
    result.score = best;
    return result;
}

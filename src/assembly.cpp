#include "assembly.h"

#include <QHash>
#include <algorithm>
#include <array>

namespace {
// Below this alignment score a read is taken not to come from the reference:
// a chance overlap at either end of the reference scores well under it.
constexpr int MinimumScore = 40;
constexpr double MinimumIdentity = 0.5;
// The weight of a call when the trace carries no qualities at all.
constexpr int UnknownQuality = 20;

int symbolIndex(char symbol) {
    switch (symbol) {
    case 'A': return 0;
    case 'C': return 1;
    case 'G': return 2;
    case 'T': return 3;
    case '-': return 4;
    default: return 5;
    }
}
}

bool disagrees(char symbol, char reference) {
    if (symbol == '-' || reference == '-')
        return symbol != reference;
    return symbol == 'N' || !Sequence::compatible(symbol, reference);
}

void Assembly::setReference(const Sequence::Record &reference) {
    m_reference = reference;
    for (Read &read : m_reads) {
        orient(read);
        retrim(read);
    }
    rebuild();
}

int Assembly::addTrace(const Trace &trace) {
    Read read;
    read.original = trace;
    orient(read);
    retrim(read);
    m_reads << read;
    rebuild();
    return m_reads.size() - 1;
}

void Assembly::setErrorLimit(double limit) {
    m_errorLimit = std::clamp(limit, 0.0001, 0.5);
    for (Read &read : m_reads)
        retrim(read);
    rebuild();
}

void Assembly::setIncluded(int index, bool included) {
    if (index < 0 || index >= m_reads.size())
        return;
    m_reads[index].included = included;
    rebuild();
}

void Assembly::setManualTrim(int index, int start, int end) {
    if (index < 0 || index >= m_reads.size())
        return;
    Read &read = m_reads[index];
    const int length = read.trace.length();
    start = std::clamp(start, 0, length);
    end = std::clamp(end, start, length);
    read.manualStart = read.reverse ? length - end : start;
    read.manualEnd = read.reverse ? length - start : end;
    retrim(read);
    rebuild();
}

void Assembly::clearManualTrim(int index) {
    if (index < 0 || index >= m_reads.size())
        return;
    m_reads[index].manualStart = m_reads[index].manualEnd = -1;
    retrim(m_reads[index]);
    rebuild();
}

QVector<ReadChoices> Assembly::choices() const {
    QVector<ReadChoices> result;
    for (const Read &read : m_reads)
        result << ReadChoices{read.included, read.manualStart, read.manualEnd};
    return result;
}

void Assembly::restore(double errorLimit, const QVector<ReadChoices> &choices) {
    m_errorLimit = errorLimit;
    for (int i = 0; i < m_reads.size() && i < choices.size(); ++i) {
        m_reads[i].included = choices[i].included;
        m_reads[i].manualStart = choices[i].manualStart;
        m_reads[i].manualEnd = choices[i].manualEnd;
        retrim(m_reads[i]);
    }
    rebuild();
}

// Sequencing reads either strand, so try the read both ways round and keep
// whichever lies on the reference better.
void Assembly::orient(Read &read) const {
    read.reverse = false;
    read.trace = read.original;
    read.alignment = {};
    if (!hasReference())
        return;
    const PairwiseAlignment forward = alignToReference(read.original.bases, m_reference.sequence);
    const Trace flipped = read.original.reverseComplemented();
    const PairwiseAlignment backward = alignToReference(flipped.bases, m_reference.sequence);
    const bool useBackward = backward.found && (!forward.found || backward.score > forward.score);
    const PairwiseAlignment &best = useBackward ? backward : forward;
    if (useBackward) {
        read.reverse = true;
        read.trace = flipped;
    }
    if (best.found && best.score >= MinimumScore && best.identity() >= MinimumIdentity)
        read.alignment = best;
    else
        read.alignment.refOfBase = QVector<int>(read.trace.length(), PairwiseAlignment::Outside);
}

void Assembly::retrim(Read &read) const {
    const int length = read.trace.length();
    if (read.manuallyTrimmed()) {
        const int start = std::clamp(read.manualStart, 0, length);
        const int end = std::clamp(read.manualEnd, start, length);
        read.trim = read.reverse ? TrimRange{length - end, length - start} : TrimRange{start, end};
    } else if (read.trace.hasQuality) {
        read.trim = mottTrim(read.trace.quality, m_errorLimit);
    } else {
        read.trim = {0, length};
    }
}

void Assembly::rebuildWithoutReference() {
    Layout layout;
    int width = 0;
    for (const Read &read : m_reads)
        width = std::max(width, read.trace.length());
    for (int i = 0; i < width; ++i) {
        layout.columns << Layout::Column{i, 0};
        layout.columnOfRef << i;
    }
    for (const Read &read : m_reads) {
        QByteArray row = read.trace.bases;
        row.resize(width, ' ');
        QVector<int> bases(width, -1);
        for (int i = 0; i < read.trace.length(); ++i)
            bases[i] = i;
        layout.rows << row;
        layout.bases << bases;
    }
    if (width > 0) {
        layout.firstCovered = 0;
        layout.lastCovered = width - 1;
    }
    m_layout = layout;
}

void Assembly::rebuild() {
    if (!hasReference()) {
        rebuildWithoutReference();
        return;
    }
    const QByteArray &reference = m_reference.sequence;
    const int length = reference.size();

    // Where each read's trimmed bases land: one base (or none) per reference
    // position, plus the runs it inserts after a position. Insertions are
    // kept only between two placed bases, never hanging off an end.
    struct Placement {
        int first = -1, last = -1;
        QVector<int> baseOfRef;
        QHash<int, QVector<int>> inserts;
    };
    QVector<Placement> placements(m_reads.size());
    QVector<int> widest(length, 0);
    for (int r = 0; r < m_reads.size(); ++r) {
        const Read &read = m_reads[r];
        Placement &place = placements[r];
        if (!read.aligned())
            continue;
        place.baseOfRef = QVector<int>(length, -1);
        QVector<int> pending;
        int previous = -1;
        for (int b = read.trim.start; b < read.trim.end; ++b) {
            const int at = read.alignment.refOfBase.value(b, PairwiseAlignment::Outside);
            if (at >= 0) {
                if (previous >= 0 && !pending.isEmpty())
                    place.inserts.insert(previous, pending);
                pending.clear();
                place.baseOfRef[at] = b;
                if (place.first < 0)
                    place.first = at;
                place.last = previous = at;
            } else if (at == PairwiseAlignment::Insertion && previous >= 0) {
                pending << b;
            }
        }
        if (read.included)
            for (auto it = place.inserts.cbegin(); it != place.inserts.cend(); ++it)
                widest[it.key()] = std::max(widest[it.key()], int(it.value().size()));
    }

    Layout layout;
    layout.columnOfRef.resize(length);
    for (int p = 0; p < length; ++p) {
        layout.columnOfRef[p] = layout.columns.size();
        layout.columns << Layout::Column{p, 0};
        layout.reference += reference[p];
        for (int k = 1; k <= widest[p]; ++k) {
            layout.columns << Layout::Column{p, k};
            layout.reference += '-';
        }
    }
    const int width = layout.columns.size();

    for (int r = 0; r < m_reads.size(); ++r) {
        const Read &read = m_reads[r];
        const Placement &place = placements[r];
        QByteArray row(width, ' ');
        QVector<int> bases(width, -1);
        for (int p = place.first; p >= 0 && p <= place.last; ++p) {
            const int column = layout.columnOfRef[p];
            const int base = place.baseOfRef[p];
            row[column] = base >= 0 ? read.trace.bases[base] : '-';
            bases[column] = base;
            if (p == place.last)
                break;
            const QVector<int> inserted = place.inserts.value(p);
            for (int k = 1; k <= widest[p]; ++k) {
                const int extra = k <= inserted.size() ? inserted[k - 1] : -1;
                row[column + k] = extra >= 0 ? read.trace.bases[extra] : '-';
                bases[column + k] = extra;
            }
        }
        layout.rows << row;
        layout.bases << bases;
    }

    // Each included read votes for its symbol with its call's quality. A gap
    // votes with the weaker of the calls on either side of it, which is how
    // sure the read is that nothing sits between them.
    QVector<std::array<int, 6>> votes(width, std::array<int, 6>{});
    for (int r = 0; r < m_reads.size(); ++r) {
        const Read &read = m_reads[r];
        if (!read.included || !read.aligned())
            continue;
        const QByteArray &row = layout.rows[r];
        const QVector<int> &bases = layout.bases[r];
        auto weight = [&](int base) {
            return read.trace.hasQuality ? std::max(1, read.trace.quality[base]) : UnknownQuality;
        };
        QVector<int> before(width, 0);
        int last = 0;
        for (int c = 0; c < width; ++c) {
            before[c] = last;
            if (bases[c] >= 0)
                last = weight(bases[c]);
        }
        int next = 0;
        for (int c = width - 1; c >= 0; --c) {
            if (row[c] == ' ')
                continue;
            if (bases[c] >= 0) {
                votes[c][symbolIndex(row[c])] += symbolIndex(row[c]) == 5 ? 0 : weight(bases[c]);
                // An N still says the read reaches here.
                votes[c][5] += symbolIndex(row[c]) == 5 ? 1 : 0;
                next = weight(bases[c]);
            } else {
                votes[c][4] += std::max(1, std::min(before[c], next));
            }
        }
    }

    static const char symbols[] = "ACGT-N";
    layout.consensus = QByteArray(width, ' ');
    for (int c = 0; c < width; ++c) {
        const auto &v = votes[c];
        int best = 0, runnerUp = 0, winner = -1;
        for (int s = 0; s < 5; ++s) {
            if (v[s] > best) {
                runnerUp = best;
                best = v[s];
                winner = s;
            } else if (v[s] > runnerUp) {
                runnerUp = v[s];
            }
        }
        if (winner < 0 && v[5] == 0)
            continue;
        layout.consensus[c] = winner < 0 || best == runnerUp ? 'N' : symbols[winner];
        if (layout.firstCovered < 0)
            layout.firstCovered = c;
        layout.lastCovered = c;
    }

    // A mismatch is any column where an included read disagrees.
    for (int c = std::max(0, layout.firstCovered); c <= layout.lastCovered; ++c) {
        const char ref = layout.reference[c];
        for (int r = 0; r < m_reads.size(); ++r) {
            const char symbol = layout.rows[r][c];
            if (!m_reads[r].included || symbol == ' ')
                continue;
            if (disagrees(symbol, ref)) {
                layout.mismatches << c;
                break;
            }
        }
    }

    for (int r = 0; r < m_reads.size(); ++r) {
        Read &read = m_reads[r];
        int covered = 0, matches = 0;
        for (int c = 0; c < width; ++c) {
            const char symbol = layout.rows[r][c];
            if (symbol == ' ')
                continue;
            ++covered;
            if (symbol != '-' && symbol != 'N' && Sequence::compatible(symbol, layout.reference[c]))
                ++matches;
        }
        read.identity = covered ? double(matches) / covered : 0;
        read.differences = covered - matches;
    }
    m_layout = layout;
}

QByteArray Assembly::consensus() const {
    QByteArray sequence;
    if (!hasReference() || m_layout.firstCovered < 0)
        return sequence;
    for (int c = m_layout.firstCovered; c <= m_layout.lastCovered; ++c) {
        const char symbol = m_layout.consensus[c];
        if (symbol == ' ' && m_layout.columns[c].insert == 0)
            sequence += 'N';
        else if (symbol != ' ' && symbol != '-')
            sequence += symbol;
    }
    return sequence;
}

int Assembly::includedAlignedReads() const {
    return int(std::count_if(m_reads.begin(), m_reads.end(),
                             [](const Read &read) { return read.included && read.aligned(); }));
}

QByteArray Assembly::consensusFasta() const {
    const QByteArray sequence = consensus();
    if (sequence.isEmpty())
        return {};
    const QString name = m_reference.name.isEmpty() ? QStringLiteral("sanger") : m_reference.name;
    const auto &first = m_layout.columns[m_layout.firstCovered], &last = m_layout.columns[m_layout.lastCovered];
    return Sequence::formatFasta(QString("%1_consensus reads=%2 reference=%3-%4 length=%5")
                                     .arg(name)
                                     .arg(includedAlignedReads())
                                     .arg(first.ref + 1)
                                     .arg(last.ref + 1)
                                     .arg(sequence.size()),
                                 sequence);
}

QByteArray Assembly::readsFasta() const {
    QByteArray text;
    for (const Read &read : m_reads) {
        if (!read.included || read.trim.length() <= 0)
            continue;
        QByteArray sequence = read.trace.bases.mid(read.trim.start, read.trim.length());
        if (read.reverse)
            sequence = Sequence::reverseComplement(sequence);
        text += Sequence::formatFasta(read.original.name, sequence);
    }
    return text;
}

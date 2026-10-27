#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

#include "filepicker.h"
#include "portalfilepicker.h"

namespace {
// The error limits stricter and looser trims step through.
const QVector<double> ErrorLimits{0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2};
constexpr int UndoDepth = 200;
}

Backend::Backend(QObject *parent) : Backend(new PortalFilePicker, parent) {}

Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker) {
    if (!m_picker->parent())
        m_picker->setParent(this);
    connect(m_picker, &FilePicker::openSelected, this, &Backend::loadUrls);
    connect(m_picker, &FilePicker::saveSelected, this, [this](const QUrl &url) { saveFasta(url.toLocalFile()); });
    connect(m_picker, &FilePicker::failed, this, [this](const QString &message) { say(message, true); });
}

QVariantList Backend::reads() const {
    QVariantList list;
    for (const Read &read : m_assembly.reads()) {
        list << QVariantMap{
            {"name", read.original.name},
            {"sample", read.original.sample},
            {"format", read.original.format},
            {"length", read.trace.length()},
            {"trimStart", read.sequencedTrim().start},
            {"trimEnd", read.sequencedTrim().end},
            {"trimmedLength", read.trim.length()},
            {"meanQuality", read.trace.hasQuality ? read.trace.meanQuality(read.trim.start, read.trim.end) : -1.0},
            {"reverse", read.reverse},
            {"aligned", read.aligned()},
            {"identity", read.identity},
            {"differences", read.differences},
            {"included", read.included},
            {"manual", read.manuallyTrimmed()},
        };
    }
    return list;
}

bool Backend::canExport() const {
    if (hasReference())
        return m_assembly.includedAlignedReads() > 0 && m_assembly.layout().firstCovered >= 0;
    return std::any_of(m_assembly.reads().begin(), m_assembly.reads().end(),
                       [](const Read &read) { return read.included && read.trim.length() > 0; });
}

int Backend::mismatchNumber() const {
    const auto &mismatches = m_assembly.layout().mismatches;
    const auto it = std::lower_bound(mismatches.begin(), mismatches.end(), m_cursor);
    return it != mismatches.end() && *it == m_cursor ? int(it - mismatches.begin()) + 1 : 0;
}

int Backend::cursorReference() const {
    const auto &columns = m_assembly.layout().columns;
    return hasReference() && m_cursor < columns.size() ? columns[m_cursor].ref + 1 : 0;
}

int Backend::cursorInsert() const {
    const auto &columns = m_assembly.layout().columns;
    return hasReference() && m_cursor < columns.size() ? columns[m_cursor].insert : 0;
}

int Backend::cursorTraceBase() const {
    const auto &bases = m_assembly.layout().bases;
    if (m_selected < 0 || m_selected >= bases.size() || m_cursor >= bases[m_selected].size())
        return -1;
    return bases[m_selected][m_cursor];
}

// One-based and counted along the read as it was sequenced, as other tools
// and the sequencing report number it.
int Backend::cursorBase() const {
    const int base = cursorTraceBase();
    if (base < 0)
        return 0;
    const Read &read = m_assembly.reads()[m_selected];
    return (read.reverse ? read.trace.length() - 1 - base : base) + 1;
}

int Backend::cursorQuality() const {
    const int base = cursorTraceBase();
    if (base < 0 || !m_assembly.reads()[m_selected].trace.hasQuality)
        return -1;
    return m_assembly.reads()[m_selected].trace.quality[base];
}

Backend::Spot Backend::cursorSpot() const {
    const Layout &layout = m_assembly.layout();
    if (m_selected < 0 || m_selected >= layout.rows.size() || m_cursor >= layout.size())
        return {};
    const QVector<int> &bases = layout.bases[m_selected];
    if (bases[m_cursor] >= 0)
        return {bases[m_cursor], bases[m_cursor]};
    if (layout.rows[m_selected][m_cursor] == ' ')
        return {};
    Spot spot;
    for (int c = m_cursor - 1; c >= 0 && spot.left < 0; --c)
        spot.left = bases[c];
    for (int c = m_cursor + 1; c < bases.size() && spot.right < 0; ++c)
        spot.right = bases[c];
    return spot;
}

void Backend::openFiles() { m_picker->openFiles(); }

void Backend::openReference() { m_picker->openReference(); }

void Backend::exportFasta() {
    if (!canExport()) {
        say(readCount() == 0 ? tr("Open some traces first.")
            : hasReference() ? tr("No included read reaches the reference, so there is no consensus.")
                             : tr("Every read is trimmed away or left out."),
            true);
        return;
    }
    const QString folder = m_lastFolder.isEmpty() ? QDir::homePath() : m_lastFolder;
    // A FASTA name can hold anything, but the suggestion must stay one file
    // in the folder.
    QString reference = referenceName();
    static const QRegularExpression unsafe(R"([/\\:*?"<>|\x00-\x1f])");
    reference.replace(unsafe, "_");
    const QString name = hasReference() ? reference + "_consensus.fasta" : QStringLiteral("trimmed_reads.fasta");
    m_picker->saveFasta(QUrl::fromLocalFile(QDir(folder).filePath(name)));
}

bool Backend::saveFasta(const QString &path) {
    if (path.isEmpty() || !canExport())
        return false;
    const QByteArray text = hasReference() ? m_assembly.consensusFasta() : m_assembly.readsFasta();
    // Written beside the target and renamed into place, so a failure never
    // leaves half a file where a good one was.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) {
        say(tr("Could not export %1: %2").arg(QFileInfo(path).fileName(), file.errorString()), true);
        return false;
    }
    m_dirty = false;
    emit changed();
    say(hasReference() ? tr("Exported the consensus to %1").arg(QFileInfo(path).fileName())
                       : tr("Exported the trimmed reads to %1").arg(QFileInfo(path).fileName()));
    return true;
}

void Backend::loadUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &url : urls)
        paths << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    loadPaths(paths);
}

// Traces are told apart from references by their contents, not their names,
// so any mix of files can be opened or dropped at once.
void Backend::loadPaths(const QStringList &paths) {
    const Layout::Column before = m_cursor < columnCount() ? m_assembly.layout().columns[m_cursor] : Layout::Column{};
    QStringList problems, notes;
    int traces = 0;
    bool newReference = false;
    for (const QString &path : paths) {
        const QString file = QFileInfo(path).fileName();
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            problems << tr("%1: %2").arg(file, input.errorString());
            continue;
        }
        const QByteArray data = input.readAll();
        QString error;
        if (TraceFile::looksLikeTrace(data)) {
            Trace trace = TraceFile::parse(data, &error);
            if (trace.bases.isEmpty()) {
                problems << tr("%1: %2").arg(file, error);
                continue;
            }
            trace.path = path;
            trace.name = QFileInfo(path).completeBaseName();
            m_assembly.addTrace(trace);
            ++traces;
        } else {
            const auto records = Sequence::parseFasta(data, &error);
            if (records.isEmpty()) {
                problems << tr("%1 is neither a trace nor a FASTA reference. %2").arg(file, error);
                continue;
            }
            Sequence::Record reference = records.first();
            if (reference.name.isEmpty())
                reference.name = QFileInfo(path).completeBaseName();
            if (records.size() > 1)
                notes << tr("%1 holds %n sequences; using the first, %2.", "", records.size()).arg(file, reference.name);
            m_assembly.setReference(reference);
            newReference = true;
        }
        m_lastFolder = QFileInfo(path).absolutePath();
    }

    if (traces == 0 && !newReference) {
        if (!problems.isEmpty())
            say(problems.join("\n"), true);
        return;
    }
    // Undo steps name reads by position, which loading changes.
    m_undo.clear();
    m_redo.clear();
    m_dirty = true;
    if (m_selected < 0 && readCount() > 0)
        m_selected = 0;
    emit changed();
    // A new reference has its own coordinates, so the old position means nothing there.
    if (newReference)
        cursorToStart();
    else
        keepCursorOn(before);

    QStringList summary;
    if (traces > 0)
        summary << tr("Opened %n traces.", "", traces);
    if (newReference)
        summary << tr("Reference: %1 (%Ln bases).", "", referenceLength()).arg(referenceName());
    if (hasReference()) {
        int unaligned = 0;
        for (const Read &read : m_assembly.reads())
            unaligned += !read.aligned();
        if (unaligned > 0)
            notes << tr("%n reads don't match the reference.", "", unaligned);
    }
    say((problems + summary + notes).join(" "), !problems.isEmpty());
}

void Backend::select(int read) {
    read = std::clamp(read, readCount() > 0 ? 0 : -1, readCount() - 1);
    if (read == m_selected)
        return;
    m_selected = read;
    emit cursorChanged();
}

void Backend::selectRelative(int delta) {
    if (readCount() > 0)
        select(std::clamp(m_selected + delta, 0, readCount() - 1));
}

void Backend::setCursor(int column) {
    column = std::clamp(column, 0, std::max(0, columnCount() - 1));
    if (column == m_cursor)
        return;
    m_cursor = column;
    emit cursorChanged();
}

void Backend::moveCursor(int delta) { setCursor(m_cursor + delta); }

void Backend::cursorToStart() {
    const Layout &layout = m_assembly.layout();
    m_cursor = -1;
    setCursor(layout.firstCovered >= 0 ? layout.firstCovered : 0);
}

void Backend::cursorToEnd() {
    const Layout &layout = m_assembly.layout();
    setCursor(layout.lastCovered >= 0 ? layout.lastCovered : columnCount() - 1);
}

void Backend::cursorToBase(int base) {
    const Layout &layout = m_assembly.layout();
    if (m_selected < 0 || m_selected >= layout.bases.size())
        return;
    const int column = layout.bases[m_selected].indexOf(base);
    if (column >= 0)
        setCursor(column);
    else if (base >= 0)
        say(tr("That base is outside the alignment. Loosen the trim to bring it in."));
}

void Backend::jumpTo(int column) {
    setCursor(column);
    selectDisagreeingRead();
}

// At a mismatch, show the chromatogram of a read that disagrees, keeping the
// selected one if it already does.
void Backend::selectDisagreeingRead() {
    const Layout &layout = m_assembly.layout();
    if (!hasReference() || m_cursor >= layout.size())
        return;
    auto disagreeing = [&](int r) {
        return r >= 0 && r < layout.rows.size() && m_assembly.reads()[r].included &&
               layout.rows[r][m_cursor] != ' ' && disagrees(layout.rows[r][m_cursor], layout.reference[m_cursor]);
    };
    if (disagreeing(m_selected))
        return;
    for (int r = 0; r < layout.rows.size(); ++r)
        if (disagreeing(r)) {
            select(r);
            return;
        }
}

void Backend::nextMismatch() {
    const auto &mismatches = m_assembly.layout().mismatches;
    if (mismatches.isEmpty()) {
        say(hasReference() ? tr("The reads agree with the reference everywhere.")
                           : tr("Open a reference to find mismatches."));
        return;
    }
    const auto it = std::upper_bound(mismatches.begin(), mismatches.end(), m_cursor);
    if (it == mismatches.end())
        say(tr("Back to the first mismatch."));
    jumpTo(it == mismatches.end() ? mismatches.first() : *it);
}

void Backend::previousMismatch() {
    const auto &mismatches = m_assembly.layout().mismatches;
    if (mismatches.isEmpty()) {
        nextMismatch();
        return;
    }
    const auto it = std::lower_bound(mismatches.begin(), mismatches.end(), m_cursor);
    if (it == mismatches.begin())
        say(tr("Round to the last mismatch."));
    jumpTo(it == mismatches.begin() ? mismatches.last() : *(it - 1));
}

void Backend::primaryAction() {
    if (readCount() == 0)
        openFiles();
    else
        nextMismatch();
}

Backend::Snapshot Backend::snapshot() const { return {m_assembly.errorLimit(), m_assembly.choices()}; }

void Backend::remember() {
    m_undo << snapshot();
    if (m_undo.size() > UndoDepth)
        m_undo.removeFirst();
    m_redo.clear();
}

void Backend::afterEdit(const QString &message) {
    m_dirty = true;
    emit changed();
    emit cursorChanged();
    say(message);
}

// Rebuilding the layout can add or drop insertion columns, so the cursor
// follows its reference position rather than its column number.
void Backend::keepCursorOn(const Layout::Column &column) {
    const Layout &layout = m_assembly.layout();
    if (!hasReference() || column.ref < 0 || column.ref >= layout.columnOfRef.size()) {
        m_cursor = std::clamp(m_cursor, 0, std::max(0, columnCount() - 1));
        emit cursorChanged();
        return;
    }
    int c = layout.columnOfRef[column.ref];
    while (c + 1 < layout.size() && layout.columns[c + 1].ref == column.ref &&
           layout.columns[c + 1].insert <= column.insert)
        ++c;
    m_cursor = c;
    emit cursorChanged();
}

void Backend::trimStartAtCursor() {
    const Spot spot = cursorSpot();
    if (spot.right < 0) {
        say(tr("Put the cursor on the selected read first."), true);
        return;
    }
    const Read &read = m_assembly.reads()[m_selected];
    const Layout::Column column = m_assembly.layout().columns[m_cursor];
    remember();
    m_assembly.setManualTrim(m_selected, spot.right, read.trim.end);
    keepCursorOn(column);
    afterEdit(tr("Trimmed the start of %1.").arg(read.original.name));
}

void Backend::trimEndAtCursor() {
    const Spot spot = cursorSpot();
    if (spot.left < 0) {
        say(tr("Put the cursor on the selected read first."), true);
        return;
    }
    const Read &read = m_assembly.reads()[m_selected];
    const Layout::Column column = m_assembly.layout().columns[m_cursor];
    remember();
    m_assembly.setManualTrim(m_selected, read.trim.start, spot.left + 1);
    keepCursorOn(column);
    afterEdit(tr("Trimmed the end of %1.").arg(read.original.name));
}

void Backend::resetTrim() {
    if (m_selected < 0 || !m_assembly.reads()[m_selected].manuallyTrimmed())
        return;
    const Layout::Column column = m_assembly.layout().columns.value(m_cursor);
    remember();
    m_assembly.clearManualTrim(m_selected);
    keepCursorOn(column);
    afterEdit(tr("%1 is trimmed by quality again.").arg(m_assembly.reads()[m_selected].original.name));
}

void Backend::toggleIncluded() {
    if (m_selected < 0)
        return;
    const Read &read = m_assembly.reads()[m_selected];
    const Layout::Column column = m_assembly.layout().columns.value(m_cursor);
    const bool include = !read.included;
    remember();
    m_assembly.setIncluded(m_selected, include);
    keepCursorOn(column);
    afterEdit(include ? tr("%1 is back in the consensus.").arg(read.original.name)
                      : tr("%1 is left out of the consensus.").arg(read.original.name));
}

void Backend::stricterTrim() {
    const double current = errorLimit();
    auto it = std::find_if(ErrorLimits.rbegin(), ErrorLimits.rend(), [&](double limit) { return limit < current - 1e-9; });
    if (it == ErrorLimits.rend()) {
        say(tr("The trim is as strict as it goes."));
        return;
    }
    const Layout::Column column = m_assembly.layout().columns.value(m_cursor);
    remember();
    m_assembly.setErrorLimit(*it);
    keepCursorOn(column);
    afterEdit(tr("Trimming where the error rate passes %1.").arg(QLocale().toString(*it, 'g', 3)));
}

void Backend::looserTrim() {
    const double current = errorLimit();
    auto it = std::find_if(ErrorLimits.begin(), ErrorLimits.end(), [&](double limit) { return limit > current + 1e-9; });
    if (it == ErrorLimits.end()) {
        say(tr("The trim is as loose as it goes."));
        return;
    }
    const Layout::Column column = m_assembly.layout().columns.value(m_cursor);
    remember();
    m_assembly.setErrorLimit(*it);
    keepCursorOn(column);
    afterEdit(tr("Trimming where the error rate passes %1.").arg(QLocale().toString(*it, 'g', 3)));
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    const Layout::Column column = m_assembly.layout().columns.value(m_cursor);
    m_redo << snapshot();
    const Snapshot previous = m_undo.takeLast();
    m_assembly.restore(previous.errorLimit, previous.reads);
    keepCursorOn(column);
    afterEdit(tr("Undone."));
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    const Layout::Column column = m_assembly.layout().columns.value(m_cursor);
    m_undo << snapshot();
    const Snapshot next = m_redo.takeLast();
    m_assembly.restore(next.errorLimit, next.reads);
    keepCursorOn(column);
    afterEdit(tr("Redone."));
}

void Backend::say(const QString &message, bool error) {
    m_message = message;
    m_messageIsError = error;
    emit messageChanged();
}

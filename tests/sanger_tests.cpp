#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "assembly.h"
#include "backend.h"
#include "filepicker.h"
#include "fixtures.h"
#include "theme.h"
#include "translator.h"

namespace {
QByteArray randomSequence(int length, quint32 seed) {
    QRandomGenerator random(seed);
    QByteArray sequence;
    for (int i = 0; i < length; ++i)
        sequence += "ACGT"[random.bounded(4)];
    return sequence;
}

QVector<int> qualities(int length, int value) { return QVector<int>(length, value); }

char other(char base) { return base == 'A' ? 'C' : 'A'; }

Trace traceOf(const QByteArray &bases, int quality = 40, const QString &name = "read") {
    return Fixtures::synthetic(bases, qualities(bases.size(), quality), name);
}

// Answers dialogs at once: opening yields nothing, saving yields the
// suggested file.
class FakePicker : public FilePicker {
public:
    QUrl lastSuggestion;
    void openFiles() override {}
    void openReference() override {}
    void saveFasta(const QUrl &suggestedUrl) override {
        lastSuggestion = suggestedUrl;
        emit saveSelected(suggestedUrl);
    }
};
}

class SangerTests : public QObject {
    Q_OBJECT

private slots:
    void abifRoundTrip()
    {
        const QByteArray bases = "ACGTTGCAACGTNACG";
        QVector<int> quality;
        for (int i = 0; i < bases.size(); ++i)
            quality << 10 + i * 2;
        const Trace written = Fixtures::synthetic(bases, quality);
        QString error;
        const Trace read = TraceFile::parse(Fixtures::abif(written), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read.format, QString("AB1"));
        QCOMPARE(read.bases, bases);
        QCOMPARE(read.peaks, written.peaks);
        QCOMPARE(read.quality, quality);
        QVERIFY(read.hasQuality);
        QCOMPARE(read.sample, QString("SAMPLE-1"));
        for (int c = 0; c < 4; ++c)
            QCOMPARE(read.channels[c], written.channels[c]);
    }

    void abifWithZeroedQualitiesIsNotTrimmed()
    {
        Trace written = traceOf(randomSequence(80, 1));
        written.quality = qualities(80, 0);
        QString error;
        const Trace read = TraceFile::parse(Fixtures::abif(written), &error);
        QVERIFY(!read.hasQuality);
        Assembly assembly;
        assembly.addTrace(read);
        QCOMPARE(assembly.reads()[0].trim, (TrimRange{0, 80}));
    }

    void scfRoundTrip_data()
    {
        QTest::addColumn<int>("version");
        QTest::newRow("version 3") << 3;
        QTest::newRow("version 2") << 2;
    }

    void scfRoundTrip()
    {
        QFETCH(int, version);
        const QByteArray bases = "GATTACAGATTACA";
        QVector<int> quality;
        for (int i = 0; i < bases.size(); ++i)
            quality << 5 + i * 3;
        const Trace written = Fixtures::synthetic(bases, quality, "clone7");
        QString error;
        const Trace read = TraceFile::parse(Fixtures::scf(written, version), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read.format, QString("SCF"));
        QCOMPARE(read.bases, bases);
        QCOMPARE(read.peaks, written.peaks);
        QCOMPARE(read.quality, quality);
        QCOMPARE(read.sample, QString("clone7"));
        for (int c = 0; c < 4; ++c)
            QCOMPARE(read.channels[c], written.channels[c]);
    }

    void damagedFilesSayWhy()
    {
        const QByteArray file = Fixtures::abif(traceOf("ACGTACGT"));
        QString error;
        QVERIFY(TraceFile::parse(file.left(40), &error).bases.isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(TraceFile::parse(Fixtures::scf(traceOf("ACGT")).left(140), &error).bases.isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(TraceFile::parse("hello", &error).bases.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void reverseComplementFlipsTheTrace()
    {
        QVector<int> quality{10, 20, 30, 40};
        const Trace trace = Fixtures::synthetic("AACG", quality);
        const Trace rc = trace.reverseComplemented();
        QCOMPARE(rc.bases, QByteArray("CGTT"));
        QCOMPARE(rc.quality, (QVector<int>{40, 30, 20, 10}));
        QCOMPARE(rc.peaks.first(), trace.sampleCount() - 1 - trace.peaks.last());
        // The C call's peak, last in the original, is now a G at the front.
        QCOMPARE(rc.channels[2][rc.peaks[0]], trace.channels[1][trace.peaks[3]]);
        QCOMPARE(rc.reverseComplemented().channels, trace.channels);
    }

    void mottTrimKeepsTheGoodStretch()
    {
        QVector<int> quality{4, 6, 3};
        quality += qualities(30, 40);
        quality += QVector<int>{8, 30, 2, 2, 5, 3};
        const TrimRange range = mottTrim(quality, 0.05);
        QCOMPARE(range.start, 3);
        QVERIFY(range.end >= 33 && range.end <= 35);
        QCOMPARE(mottTrim(qualities(10, 2), 0.05).length(), 0);
        // A looser limit keeps at least as much.
        QVERIFY(mottTrim(quality, 0.2).length() >= range.length());
    }

    void fastaParsing()
    {
        QString error;
        auto records = Sequence::parseFasta(">one first record\nacgt\nNNRY\n>two\n  1 GGCC uu\n", &error);
        QCOMPARE(records.size(), 2);
        QCOMPARE(records[0].name, QString("one"));
        QCOMPARE(records[0].sequence, QByteArray("ACGTNNRY"));
        QCOMPARE(records[1].sequence, QByteArray("GGCCTT"));

        records = Sequence::parseFasta("ACGT\nACGT\n", &error);
        QCOMPARE(records.size(), 1);
        QVERIFY(records[0].name.isEmpty());

        QVERIFY(Sequence::parseFasta(">x\nACGZ\n", &error).isEmpty());
        QVERIFY(error.contains("Z"));
        QCOMPARE(Sequence::formatFasta("x", "ACGTACGT", 3), QByteArray(">x\nACG\nTAC\nGT\n"));
    }

    void alignmentFindsSubstitutionInsertionAndDeletion()
    {
        const QByteArray reference = randomSequence(600, 7);
        QByteArray read = reference.mid(100, 400);
        read[50] = other(read[50]);  // reference 150
        read.remove(150, 1);         // reference 250 missing from the read
        read.insert(250, 'G');       // after reference 350
        Assembly assembly;
        assembly.setReference({"ref", reference});
        assembly.addTrace(traceOf(read));
        const Read &placed = assembly.reads()[0];
        QVERIFY(placed.aligned());
        QVERIFY(!placed.reverse);

        const Layout &layout = assembly.layout();
        QCOMPARE(layout.mismatches.size(), 3);
        QCOMPARE(layout.columns[layout.mismatches[0]].ref, 150);
        QCOMPARE(layout.columns[layout.mismatches[1]].ref, 250);
        QCOMPARE(layout.rows[0][layout.mismatches[1]], '-');
        QCOMPARE(layout.columns[layout.mismatches[2]].insert, 1);
        QCOMPARE(layout.reference[layout.mismatches[2]], '-');
        // The consensus follows the only read, so it differs from the reference the same three ways.
        QCOMPARE(assembly.consensus(), read);
    }

    void reverseReadsAreFlipped()
    {
        const QByteArray reference = randomSequence(700, 11);
        assemblyCheck(Sequence::reverseComplement(reference.mid(200, 350)), reference, true);
        assemblyCheck(reference.mid(200, 350), reference, false);
    }

    void junkAtTheEndsIsClipped()
    {
        // Bases before the primer and after the read fades match nothing, and
        // must not be forced into the alignment as gaps and mismatches.
        const QByteArray reference = randomSequence(900, 13);
        QByteArray read = reference.mid(300, 400);
        read[2] = other(read[2]); // A real difference near the start stays in.
        const QByteArray junked = randomSequence(25, 77) + read + randomSequence(30, 78);
        QVector<int> quality = qualities(junked.size(), 40);
        for (int i = 0; i < 25; ++i)
            quality[i] = 6;
        for (int i = junked.size() - 30; i < junked.size(); ++i)
            quality[i] = 6;
        Assembly assembly;
        assembly.setReference({"ref", reference});
        assembly.addTrace(Fixtures::synthetic(junked, quality));
        const Read &placed = assembly.reads()[0];
        QVERIFY(placed.aligned());
        QCOMPARE(placed.alignment.refOfBase.count(PairwiseAlignment::Insertion), 0);
        QCOMPARE(placed.alignment.refOfBase[25 + 2], 302);
        QCOMPARE(placed.alignment.refOfBase[25 + 399], 699);
        QCOMPARE(assembly.layout().mismatches.size(), 1);
        QCOMPARE(assembly.layout().columns[assembly.layout().mismatches[0]].ref, 302);
    }

    void abifCallsAndPeaksComeFromOneSet()
    {
        // Edited calls (PBAS 2) without edited peaks can't be placed, so the
        // basecaller's own calls and peaks are used together.
        const QByteArray bases = randomSequence(60, 91);
        const Trace written = traceOf(bases);
        QString error;
        const Trace read = TraceFile::parse(Fixtures::abif(written, "S", bases + "ACG"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read.bases, bases);
        QCOMPARE(read.peaks, written.peaks);
        QVERIFY(read.hasQuality);
    }

    void readsOverhangingALongReferenceAlign()
    {
        const QByteArray reference = randomSequence(80'000, 17);
        const QByteArray read = randomSequence(50, 18) + reference.left(600);
        Assembly assembly;
        assembly.setReference({"genome", reference});
        assembly.addTrace(traceOf(read));
        assembly.addTrace(traceOf(reference.mid(79'300, 600) + randomSequence(50, 19)));
        QVERIFY(assembly.reads()[0].aligned());
        QCOMPARE(assembly.reads()[0].alignment.refOfBase[50], 0);
        QVERIFY(assembly.reads()[1].aligned());
        QCOMPARE(assembly.reads()[1].alignment.refOfBase[599], 79'899);

        // The index lookup agrees with the overhang to within a bucket.
        const ReferenceIndex index(reference);
        const int start = index.likelyStart(read);
        QVERIFY2(start <= -50 && start > -50 - 32, qPrintable(QString::number(start)));
    }

    void leftOutReadsKeepTheirInsertions()
    {
        const QByteArray reference = randomSequence(500, 23);
        QByteArray inserted = reference.mid(50, 300);
        inserted.insert(150, "TT");
        Assembly assembly;
        assembly.setReference({"ref", reference});
        assembly.addTrace(traceOf(inserted, 40, "inserted"));
        assembly.addTrace(traceOf(reference.mid(40, 320), 40, "plain"));
        const int differences = assembly.reads()[0].differences;
        QCOMPARE(differences, 2);
        assembly.setIncluded(0, false);
        QCOMPARE(assembly.reads()[0].differences, differences);
        QCOMPARE(assembly.layout().rows[0].count('T'), inserted.count('T'));
        QCOMPARE(assembly.layout().mismatches.size(), 0);
        QCOMPARE(assembly.consensus(), reference.mid(40, 320));
    }

    void unrelatedReadsDoNotAlign()
    {
        Assembly assembly;
        assembly.setReference({"ref", randomSequence(800, 3)});
        assembly.addTrace(traceOf(randomSequence(300, 99)));
        QVERIFY(!assembly.reads()[0].aligned());
        QCOMPARE(assembly.consensus(), QByteArray());
        QVERIFY(assembly.consensusFasta().isEmpty());
    }

    void longReferencesAreWindowed()
    {
        const QByteArray reference = randomSequence(80'000, 5);
        Assembly assembly;
        assembly.setReference({"genome", reference});
        assembly.addTrace(traceOf(reference.mid(41'000, 700)));
        const Read &read = assembly.reads()[0];
        QVERIFY(read.aligned());
        QCOMPARE(read.alignment.refOfBase.first(), 41'000);
        QCOMPARE(read.alignment.refOfBase.last(), 41'699);
        QCOMPARE(assembly.layout().mismatches.size(), 0);
    }

    void consensusWeighsQuality()
    {
        const QByteArray reference = randomSequence(500, 21);
        QByteArray wrong = reference.mid(50, 300);
        wrong[100] = other(wrong[100]);
        QVector<int> quality = qualities(300, 40);
        quality[100] = 8;
        Assembly assembly;
        assembly.setReference({"ref", reference});
        assembly.addTrace(Fixtures::synthetic(wrong, quality, "weak"));
        assembly.addTrace(traceOf(reference.mid(40, 320), 40, "strong"));
        QCOMPARE(assembly.consensus(), reference.mid(40, 320));
        // The weak read still disagrees there, so it is a mismatch to look at.
        QCOMPARE(assembly.layout().mismatches.size(), 1);
        QCOMPARE(assembly.layout().columns[assembly.layout().mismatches[0]].ref, 150);

        // Two equally sure reads that disagree leave the call open.
        Assembly tie;
        tie.setReference({"ref", reference});
        tie.addTrace(Fixtures::synthetic(wrong, qualities(300, 30), "a"));
        tie.addTrace(Fixtures::synthetic(reference.mid(50, 300), qualities(300, 30), "b"));
        QCOMPARE(tie.consensus()[100], 'N');

        // Leaving the weak read out removes its mismatch.
        assembly.setIncluded(0, false);
        QCOMPARE(assembly.layout().mismatches.size(), 0);
    }

    void consensusFillsUncoveredStretchesWithN()
    {
        const QByteArray reference = randomSequence(600, 8);
        Assembly assembly;
        assembly.setReference({"ref", reference});
        assembly.addTrace(traceOf(reference.mid(50, 200)));
        assembly.addTrace(traceOf(reference.mid(350, 200)));
        const QByteArray consensus = assembly.consensus();
        QCOMPARE(consensus.size(), 500);
        QCOMPARE(consensus.mid(200, 100), QByteArray(100, 'N'));
        QVERIFY(assembly.consensusFasta().startsWith(">ref_consensus reads=2 reference=51-550 length=500\n"));
    }

    void manualTrimSurvivesFlipping()
    {
        const QByteArray reference = randomSequence(500, 31);
        const QByteArray read = Sequence::reverseComplement(reference.mid(100, 200));
        Assembly assembly;
        assembly.addTrace(traceOf(read));
        assembly.setManualTrim(0, 20, 180);
        QCOMPARE(assembly.reads()[0].trim, (TrimRange{20, 180}));
        // Flipped to match the reference, the same bases stay trimmed.
        assembly.setReference({"ref", reference});
        QVERIFY(assembly.reads()[0].reverse);
        QCOMPARE(assembly.reads()[0].trim, (TrimRange{20, 180}));
        QCOMPARE(assembly.consensus(), reference.mid(120, 160));
    }

    void backendLoadsAndJumpsBetweenMismatches()
    {
        QTemporaryDir dir;
        const QByteArray reference = randomSequence(800, 41);
        QByteArray read = reference.mid(100, 500);
        read[100] = other(read[100]);
        read[300] = other(read[300]);
        QVERIFY(Fixtures::write(dir.filePath("ref.fa"), ">plasmid\n" + reference + "\n"));
        QVERIFY(Fixtures::write(dir.filePath("one.ab1"), Fixtures::abif(traceOf(read))));
        QVERIFY(Fixtures::write(dir.filePath("two.scf"),
                                Fixtures::scf(traceOf(Sequence::reverseComplement(reference.mid(150, 500))))));

        Backend backend(new FakePicker);
        backend.loadPaths({dir.filePath("one.ab1"), dir.filePath("two.scf"), dir.filePath("ref.fa")});
        QVERIFY2(!backend.messageIsError(), qPrintable(backend.message()));
        QCOMPARE(backend.readCount(), 2);
        QVERIFY(backend.hasReference());
        QCOMPARE(backend.referenceName(), QString("plasmid"));
        QCOMPARE(backend.mismatchCount(), 2);
        QVERIFY(backend.reads()[1].toMap()["reverse"].toBool());
        QVERIFY(backend.dirty());

        backend.nextMismatch();
        QCOMPARE(backend.cursorReference(), 201);
        QCOMPARE(backend.mismatchNumber(), 1);
        QCOMPARE(backend.selectedRead(), 0);
        QCOMPARE(backend.cursorBase(), 101);
        backend.nextMismatch();
        QCOMPARE(backend.cursorReference(), 401);
        backend.nextMismatch();
        QCOMPARE(backend.cursorReference(), 201);
        backend.previousMismatch();
        QCOMPARE(backend.cursorReference(), 401);

        // The reverse read is numbered along the read as sequenced.
        backend.select(1);
        QCOMPARE(backend.cursorBase(), 500 - (400 - 150));
    }

    void backendTrimsAtTheCursorWithUndo()
    {
        QTemporaryDir dir;
        const QByteArray reference = randomSequence(600, 51);
        QVERIFY(Fixtures::write(dir.filePath("ref.fa"), ">r\n" + reference + "\n"));
        QVERIFY(Fixtures::write(dir.filePath("a.ab1"), Fixtures::abif(traceOf(reference.mid(100, 400)))));
        Backend backend(new FakePicker);
        backend.loadPaths({dir.filePath("ref.fa"), dir.filePath("a.ab1")});
        QCOMPARE(backend.consensusLength(), 400);

        backend.moveCursor(50);
        backend.trimStartAtCursor();
        QCOMPARE(backend.consensusLength(), 350);
        QVERIFY(backend.reads()[0].toMap()["manual"].toBool());
        QCOMPARE(backend.cursorReference(), 151);
        backend.undo();
        QCOMPARE(backend.consensusLength(), 400);
        QVERIFY(backend.canRedo());
        backend.redo();
        QCOMPARE(backend.consensusLength(), 350);
        backend.resetTrim();
        QCOMPARE(backend.consensusLength(), 400);

        backend.toggleIncluded();
        QVERIFY(!backend.canExport());
        backend.undo();
        QVERIFY(backend.canExport());

        const double limit = backend.errorLimit();
        backend.stricterTrim();
        QVERIFY(backend.errorLimit() < limit);
        backend.looserTrim();
        backend.looserTrim();
        QVERIFY(backend.errorLimit() > limit);
    }

    void backendExportsTheConsensus()
    {
        QTemporaryDir dir;
        const QByteArray reference = randomSequence(500, 61);
        QVERIFY(Fixtures::write(dir.filePath("ref.fa"), ">insert\n" + reference + "\n"));
        QVERIFY(Fixtures::write(dir.filePath("a.ab1"), Fixtures::abif(traceOf(reference.mid(20, 450)))));
        QVERIFY(Fixtures::write(dir.filePath("insert_consensus.fasta"), "old contents"));
        auto *picker = new FakePicker;
        Backend backend(picker);
        backend.loadPaths({dir.filePath("ref.fa"), dir.filePath("a.ab1")});
        backend.exportFasta();
        QCOMPARE(picker->lastSuggestion.toLocalFile(), dir.filePath("insert_consensus.fasta"));
        QFile written(dir.filePath("insert_consensus.fasta"));
        QVERIFY(written.open(QIODevice::ReadOnly));
        const auto records = [&] {
            QString error;
            return Sequence::parseFasta(written.readAll(), &error);
        }();
        QCOMPARE(records.size(), 1);
        QCOMPARE(records[0].name, QString("insert_consensus"));
        QCOMPARE(records[0].sequence, reference.mid(20, 450));
        QVERIFY(!backend.dirty());
        QVERIFY(!backend.messageIsError());
        // Nothing but the export is left beside it.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 3);
    }

    void backendWithoutReferenceExportsTrimmedReads()
    {
        QTemporaryDir dir;
        QVector<int> quality = qualities(200, 40);
        for (int i = 0; i < 10; ++i)
            quality[i] = quality[199 - i] = 3;
        const QByteArray bases = randomSequence(200, 71);
        QVERIFY(Fixtures::write(dir.filePath("solo.ab1"), Fixtures::abif(Fixtures::synthetic(bases, quality))));
        Backend backend(new FakePicker);
        backend.loadPaths({dir.filePath("solo.ab1")});
        QVERIFY(!backend.hasReference());
        QVERIFY(backend.canExport());
        backend.nextMismatch();
        QVERIFY(backend.message().contains("reference"));
        QVERIFY(backend.saveFasta(dir.filePath("out.fasta")));
        QFile written(dir.filePath("out.fasta"));
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), Sequence::formatFasta("solo", bases.mid(10, 180)));
    }

    void reverseReadsAreNumberedAsSequenced()
    {
        // The trim, the cursor, and the command line all count along the read
        // as it came off the sequencer.
        QTemporaryDir dir;
        const QByteArray reference = randomSequence(700, 81);
        const QByteArray sequenced = Sequence::reverseComplement(reference.mid(100, 400));
        QVector<int> quality = qualities(400, 40);
        for (int i = 0; i < 20; ++i)
            quality[i] = 3; // A poor start, as sequenced.
        QVERIFY(Fixtures::write(dir.filePath("ref.fa"), ">r\n" + reference + "\n"));
        QVERIFY(Fixtures::write(dir.filePath("rev.ab1"), Fixtures::abif(Fixtures::synthetic(sequenced, quality))));
        Backend backend(new FakePicker);
        backend.loadPaths({dir.filePath("ref.fa"), dir.filePath("rev.ab1")});
        const QVariantMap read = backend.reads()[0].toMap();
        QVERIFY(read["reverse"].toBool());
        QCOMPARE(read["trimStart"].toInt(), 21);
        QCOMPARE(read["trimEnd"].toInt(), 400);
        // The first base kept, as sequenced, sits at the right end of the alignment.
        backend.cursorToEnd();
        QCOMPARE(backend.cursorBase(), 21);
    }

    void exportSuggestionStaysInTheFolder()
    {
        QTemporaryDir dir;
        const QByteArray reference = randomSequence(400, 83);
        QVERIFY(Fixtures::write(dir.filePath("ref.fa"), ">HIV/pol:region\n" + reference + "\n"));
        QVERIFY(Fixtures::write(dir.filePath("a.ab1"), Fixtures::abif(traceOf(reference.mid(50, 300)))));
        auto *picker = new FakePicker;
        Backend backend(picker);
        backend.loadPaths({dir.filePath("ref.fa"), dir.filePath("a.ab1")});
        backend.exportFasta();
        QCOMPARE(picker->lastSuggestion.toLocalFile(), dir.filePath("HIV_pol_region_consensus.fasta"));
        QVERIFY(QFile::exists(dir.filePath("HIV_pol_region_consensus.fasta")));
    }

    void aNewReferenceStartsTheCursorOver()
    {
        QTemporaryDir dir;
        const QByteArray first = randomSequence(6000, 85), second = randomSequence(900, 86);
        QVERIFY(Fixtures::write(dir.filePath("one.fa"), ">one\n" + first + "\n"));
        QVERIFY(Fixtures::write(dir.filePath("two.fa"), ">two\n" + second + "\n"));
        QVERIFY(Fixtures::write(dir.filePath("a.ab1"), Fixtures::abif(traceOf(first.mid(4000, 500)))));
        QVERIFY(Fixtures::write(dir.filePath("b.ab1"), Fixtures::abif(traceOf(second.mid(300, 400)))));
        Backend backend(new FakePicker);
        backend.loadPaths({dir.filePath("one.fa"), dir.filePath("a.ab1"), dir.filePath("b.ab1")});
        QCOMPARE(backend.cursorReference(), 4001);
        backend.loadPaths({dir.filePath("two.fa")});
        QCOMPARE(backend.cursorReference(), 301);
    }

    void backendSaysWhatItCouldNotOpen()
    {
        QTemporaryDir dir;
        QVERIFY(Fixtures::write(dir.filePath("notes.txt"), "Hello, plate 4!\n"));
        QVERIFY(Fixtures::write(dir.filePath("broken.ab1"), "ABIF and then nothing"));
        Backend backend(new FakePicker);
        backend.loadPaths({dir.filePath("notes.txt"), dir.filePath("broken.ab1"), dir.filePath("missing.scf")});
        QVERIFY(backend.messageIsError());
        QVERIFY(backend.message().contains("notes.txt"));
        QVERIFY(backend.message().contains("broken.ab1"));
        QVERIFY(backend.message().contains("missing.scf"));
        QCOMPARE(backend.readCount(), 0);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        QVERIFY(Fixtures::write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(Fixtures::write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(Fixtures::write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        QVERIFY(Fixtures::write(dir.path() + "/language", "pt-BR\n"));
        QVERIFY(Fixtures::write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})"));
        QVERIFY(Fixtures::write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        QVERIFY(Fixtures::write(dir.path() + "/language", "ru\n"));
        QVERIFY(Fixtures::write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})"));
        QVERIFY(Fixtures::write(dir.path() + "/ru.json",
                                R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})"));

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    void bundledCatalogsAreValid()
    {
        // Every catalog parses, keeps each placeholder its English has, and
        // gives counted strings as plural forms.
        QDir catalogs(QFINDTESTDATA("../src/i18n"));
        QVERIFY(catalogs.exists());
        for (const QString &name : catalogs.entryList({"*.json"})) {
            QFile file(catalogs.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonObject catalog = QJsonDocument::fromJson(file.readAll(), &error).object();
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                const QString key = it.key();
                QStringList forms;
                if (it.value().isObject())
                    for (const auto &form : it.value().toObject())
                        forms << form.toString();
                else
                    forms << it.value().toString();
                QVERIFY2(!it.value().isObject() || key.contains("%n") || key.contains("%Ln"), qPrintable(name + ": " + key));
                for (const QString &form : forms)
                    for (const char *placeholder : {"%1", "%2", "%3"})
                        QVERIFY2(key.contains(placeholder) == form.contains(placeholder),
                                 qPrintable(name + ": " + key + " -> " + form));
            }
        }
    }

private:
    static void assemblyCheck(const QByteArray &read, const QByteArray &reference, bool reverse)
    {
        Assembly assembly;
        assembly.setReference({"ref", reference});
        assembly.addTrace(traceOf(read));
        QVERIFY(assembly.reads()[0].aligned());
        QCOMPARE(assembly.reads()[0].reverse, reverse);
        QCOMPARE(assembly.reads()[0].identity, 1.0);
        QCOMPARE(assembly.layout().mismatches.size(), 0);
        QCOMPARE(assembly.consensus(), reference.mid(200, 350));
    }
};

QTEST_MAIN(SangerTests)
#include "sanger_tests.moc"

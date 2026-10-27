// Commands that need no display, following Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

#include "assembly.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description, bool reference, bool traces = true) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        if (reference)
            parser.addPositionalArgument("reference", "FASTA reference", "<reference>");
        if (traces) {
            parser.addPositionalArgument("traces", "AB1 or SCF traces", "<trace>...");
            parser.addOption({"error-limit", "Trim where the error rate passes this (default: 0.05)", "rate", "0.05"});
        }
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    void output() { parser.addOption({{"o", "output"}, "Write FASTA here instead of to standard output", "file"}); }
    bool isJson() const { return parser.isSet("json"); }
    QStringList arguments() const { return parser.positionalArguments().mid(1); }

    // Loads the reference (when the command takes one) and every trace.
    bool load(Assembly &assembly, bool reference) const {
        bool ok = false;
        const double limit = parser.value("error-limit").toDouble(&ok);
        if (!ok || limit < Assembly::MinimumErrorLimit || limit > Assembly::MaximumErrorLimit) {
            print(stderr, QString("The error limit must be a rate from %1 to %2, such as 0.05.")
                              .arg(Assembly::MinimumErrorLimit)
                              .arg(Assembly::MaximumErrorLimit));
            return false;
        }
        assembly.setErrorLimit(limit);
        QStringList paths = arguments();
        if (reference) {
            if (paths.isEmpty()) {
                print(stderr, "Name a FASTA reference, then the traces.");
                return false;
            }
            const QString path = paths.takeFirst();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                print(stderr, path + ": " + file.errorString());
                return false;
            }
            QString error;
            const auto records = Sequence::parseFasta(file.readAll(), &error);
            if (records.isEmpty()) {
                print(stderr, path + ": " + error);
                return false;
            }
            Sequence::Record record = records.first();
            if (record.name.isEmpty())
                record.name = QFileInfo(path).completeBaseName();
            assembly.setReference(record);
        }
        if (paths.isEmpty()) {
            print(stderr, "Name at least one AB1 or SCF trace.");
            return false;
        }
        for (const QString &path : paths) {
            QString error;
            const Trace trace = TraceFile::read(path, &error);
            if (trace.bases.isEmpty()) {
                print(stderr, path + ": " + error);
                return false;
            }
            assembly.addTrace(trace);
        }
        return true;
    }
};

// Where a read lies on the reference, one-based and inclusive.
QPair<int, int> referenceSpan(const Assembly &assembly, int read) {
    const Layout &layout = assembly.layout();
    int first = -1, last = -1;
    for (int c = 0; c < layout.size(); ++c)
        if (layout.rows[read][c] != ' ') {
            if (first < 0)
                first = layout.columns[c].ref + 1;
            last = layout.columns[c].ref + 1;
        }
    return {first, last};
}

QJsonObject readJson(const Assembly &assembly, int index) {
    const Read &read = assembly.reads()[index];
    QJsonObject object{
        {"name", read.original.name},
        {"file", QFileInfo(read.original.path).absoluteFilePath()},
        {"format", read.original.format},
        {"sample", read.original.sample},
        {"length", read.original.length()},
        {"trimStart", read.sequencedTrim().start},
        {"trimEnd", read.sequencedTrim().end},
        {"trimmedLength", read.trim.length()},
        {"meanQuality", read.trace.hasQuality ? QJsonValue(read.trace.meanQuality(read.trim.start, read.trim.end))
                                              : QJsonValue()},
    };
    if (assembly.hasReference()) {
        const auto span = referenceSpan(assembly, index);
        object["strand"] = read.reverse ? "reverse" : "forward";
        object["aligned"] = read.aligned();
        if (read.aligned()) {
            object["identity"] = read.identity;
            object["differences"] = read.differences;
            object["referenceStart"] = span.first;
            object["referenceEnd"] = span.second;
        }
    }
    return object;
}

QString percent(double value) { return QString::number(value * 100, 'f', 1) + "%"; }

int info(const QStringList &arguments) {
    Command command("info", "Describe traces: length, quality, and the trimmed stretch.", false);
    command.json();
    command.parser.process(arguments);
    Assembly assembly;
    if (!command.load(assembly, false))
        return 1;
    QJsonArray list;
    for (int i = 0; i < assembly.reads().size(); ++i) {
        const QJsonObject object = readJson(assembly, i);
        list << object;
        if (!command.isJson())
            // One arg() call for every field, so a %3 in a file name stays as it is.
            print(stdout, QString("%1  %2  %3 bases, trimmed to %4-%5 (%6 bases)%7")
                              .arg(object["name"].toString(), object["format"].toString(),
                                   QString::number(object["length"].toInt()),
                                   QString::number(object["trimStart"].toInt()),
                                   QString::number(object["trimEnd"].toInt()),
                                   QString::number(object["trimmedLength"].toInt()),
                                   object["meanQuality"].isNull()
                                       ? QString(", no qualities")
                                       : QString(", mean Q%1").arg(object["meanQuality"].toDouble(), 0, 'f', 1)));
    }
    if (command.isJson())
        print({{"errorLimit", assembly.errorLimit()}, {"traces", list}});
    return 0;
}

bool writeOutput(const Command &command, const QByteArray &text) {
    const QString path = command.parser.value("output");
    if (path.isEmpty()) {
        fputs(text.constData(), stdout);
        return true;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit()) {
        print(stderr, path + ": " + file.errorString());
        return false;
    }
    return true;
}

int trim(const QStringList &arguments) {
    Command command("trim", "Trim traces by quality and print their sequences as FASTA.", false);
    command.output();
    command.json();
    command.parser.process(arguments);
    Assembly assembly;
    if (!command.load(assembly, false))
        return 1;
    const QByteArray fasta = assembly.readsFasta();
    if (command.isJson()) {
        QJsonArray list;
        for (int i = 0; i < assembly.reads().size(); ++i) {
            QJsonObject object = readJson(assembly, i);
            const Read &read = assembly.reads()[i];
            object["sequence"] = QString::fromLatin1(read.trace.bases.mid(read.trim.start, read.trim.length()));
            list << object;
        }
        if (command.parser.isSet("output") && !writeOutput(command, fasta))
            return 1;
        print({{"errorLimit", assembly.errorLimit()}, {"traces", list}});
        return 0;
    }
    return writeOutput(command, fasta) ? 0 : 1;
}

int align(const QStringList &arguments) {
    Command command("align", "Align traces to a reference and list every mismatch.", true);
    command.json();
    command.parser.process(arguments);
    Assembly assembly;
    if (!command.load(assembly, true))
        return 1;
    const Layout &layout = assembly.layout();
    const bool json = command.isJson();

    QJsonArray reads;
    for (int i = 0; i < assembly.reads().size(); ++i) {
        const QJsonObject object = readJson(assembly, i);
        reads << object;
        if (json)
            continue;
        if (!assembly.reads()[i].aligned())
            print(stdout, object["name"].toString() + "  does not match the reference");
        else
            print(stdout, QString("%1  %2  reference %3-%4  %5 identity, %6 differences")
                              .arg(object["name"].toString(), object["strand"].toString(),
                                   QString::number(object["referenceStart"].toInt()),
                                   QString::number(object["referenceEnd"].toInt()),
                                   percent(object["identity"].toDouble()),
                                   QString::number(object["differences"].toInt())));
    }

    QJsonArray mismatches;
    if (!json && !layout.mismatches.isEmpty())
        print(stdout, "\nposition  reference  consensus  reads");
    for (int column : layout.mismatches) {
        const auto &place = layout.columns[column];
        QJsonArray calls;
        QStringList text;
        for (int r = 0; r < assembly.reads().size(); ++r) {
            const char symbol = layout.rows[r][column];
            if (symbol == ' ' || !assembly.reads()[r].included)
                continue;
            const Read &read = assembly.reads()[r];
            const int base = layout.bases[r][column];
            QJsonObject call{{"read", read.original.name}, {"call", QString(QChar(symbol))}};
            if (base >= 0) {
                call["position"] = (read.reverse ? read.trace.length() - 1 - base : base) + 1;
                if (read.trace.hasQuality)
                    call["quality"] = read.trace.quality[base];
            }
            calls << call;
            text << QString("%1=%2%3").arg(read.original.name, QString(QChar(symbol)),
                                           base >= 0 && read.trace.hasQuality
                                               ? QString("(Q%1)").arg(read.trace.quality[base])
                                               : QString());
        }
        mismatches << QJsonObject{{"position", place.ref + 1},
                                  {"insert", place.insert},
                                  {"reference", QString(QChar(layout.reference[column]))},
                                  {"consensus", QString(QChar(layout.consensus[column]))},
                                  {"calls", calls}};
        if (!json)
            print(stdout, QString("%1  %2  %3  %4")
                              .arg(place.insert ? QString("%1+%2").arg(place.ref + 1).arg(place.insert)
                                                : QString::number(place.ref + 1),
                                   -8)
                              .arg(QString(QChar(layout.reference[column])), -9)
                              .arg(QString(QChar(layout.consensus[column])), -9)
                              .arg(text.join("  ")));
    }
    if (json)
        print({{"reference", QJsonObject{{"name", assembly.reference().name},
                                          {"length", assembly.reference().sequence.size()}}},
               {"errorLimit", assembly.errorLimit()},
               {"reads", reads},
               {"mismatches", mismatches}});
    else
        print(stdout, QString("\n%1 mismatches").arg(layout.mismatches.size()));
    return 0;
}

int consensus(const QStringList &arguments) {
    Command command("consensus", "Align traces to a reference and print their consensus as FASTA.", true);
    command.output();
    command.json();
    command.parser.process(arguments);
    Assembly assembly;
    if (!command.load(assembly, true))
        return 1;
    const QByteArray fasta = assembly.consensusFasta();
    if (fasta.isEmpty())
        return fail("No read matches the reference, so there is no consensus.");
    if (command.isJson()) {
        if (command.parser.isSet("output") && !writeOutput(command, fasta))
            return 1;
        const Layout &layout = assembly.layout();
        print({{"reference", assembly.reference().name},
               {"referenceStart", layout.columns[layout.firstCovered].ref + 1},
               {"referenceEnd", layout.columns[layout.lastCovered].ref + 1},
               {"reads", assembly.includedAlignedReads()},
               {"mismatches", layout.mismatches.size()},
               {"length", assembly.consensus().size()},
               {"sequence", QString::fromLatin1(assembly.consensus())},
               {"output", command.parser.isSet("output")
                              ? QJsonValue(QFileInfo(command.parser.value("output")).absoluteFilePath())
                              : QJsonValue()}});
        return 0;
    }
    return writeOutput(command, fasta) ? 0 : 1;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.", false,
                    false);
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/sanger and link into ~/.claude/skills",
                                         "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.arguments().isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.arguments().first() != "install")
        return fail("Use sanger skill to print the skill, or sanger skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/sanger");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/sanger");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/sanger", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: sanger <command> [options]\n\n" + cliSummary());
    return 0;
}
} // namespace

QString cliSummary() {
    return "Viewer:\n"
           "  open [files...]                        Open traces and a FASTA reference in the viewer\n\n"
           "Commands that need no display (sanger help <command> for options):\n"
           "  info <trace>...                        Length, quality, and trimmed stretch of each trace\n"
           "  trim <trace>...                        Quality-trimmed sequences as FASTA\n"
           "  align <reference> <trace>...           Place traces on a reference and list mismatches\n"
           "  consensus <reference> <trace>...       The consensus of the traces as FASTA\n"
           "  skill [install]                        Print the skill for coding agents, or install it";
}

bool isCliCommand(const QString &word) {
    return QStringList{"info", "trim", "align", "consensus", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "info"        ? info(arguments)
           : command == "trim"      ? trim(arguments)
           : command == "align"     ? align(arguments)
           : command == "consensus" ? consensus(arguments)
           : command == "skill"     ? skill(arguments)
                                    : help(arguments);
}

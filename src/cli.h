#pragma once
#include <QStringList>
// Headless commands for scripts and agents: sanger info, trim, align, consensus.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);

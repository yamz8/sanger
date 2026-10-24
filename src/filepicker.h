#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

// Open and save dialogs, behind an interface so tests can answer them.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openFiles() = 0;
    virtual void openReference() = 0;
    virtual void saveFasta(const QUrl &suggestedUrl) = 0;

signals:
    void openSelected(const QList<QUrl> &urls);
    void saveSelected(const QUrl &url);
    void failed(const QString &message);
};

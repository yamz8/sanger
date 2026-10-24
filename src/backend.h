#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>

#include "assembly.h"

class FilePicker;

// The open session: the reads, the reference, the cursor in the alignment,
// and the trim choices that undo walks back through.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int readCount READ readCount NOTIFY changed)
    Q_PROPERTY(QVariantList reads READ reads NOTIFY changed)
    Q_PROPERTY(bool hasReference READ hasReference NOTIFY changed)
    Q_PROPERTY(QString referenceName READ referenceName NOTIFY changed)
    Q_PROPERTY(int referenceLength READ referenceLength NOTIFY changed)
    Q_PROPERTY(double errorLimit READ errorLimit NOTIFY changed)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY changed)
    Q_PROPERTY(int mismatchCount READ mismatchCount NOTIFY changed)
    Q_PROPERTY(int consensusLength READ consensusLength NOTIFY changed)
    Q_PROPERTY(bool canExport READ canExport NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(int selectedRead READ selectedRead WRITE select NOTIFY cursorChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn WRITE setCursor NOTIFY cursorChanged)
    Q_PROPERTY(int mismatchNumber READ mismatchNumber NOTIFY cursorChanged)
    Q_PROPERTY(int cursorReference READ cursorReference NOTIFY cursorChanged)
    Q_PROPERTY(int cursorInsert READ cursorInsert NOTIFY cursorChanged)
    Q_PROPERTY(int cursorBase READ cursorBase NOTIFY cursorChanged)
    Q_PROPERTY(int cursorQuality READ cursorQuality NOTIFY cursorChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    Backend(FilePicker *picker, QObject *parent = nullptr);

    const Assembly &assembly() const { return m_assembly; }

    int readCount() const { return m_assembly.reads().size(); }
    QVariantList reads() const;
    bool hasReference() const { return m_assembly.hasReference(); }
    QString referenceName() const { return m_assembly.reference().name; }
    int referenceLength() const { return m_assembly.reference().sequence.size(); }
    double errorLimit() const { return m_assembly.errorLimit(); }
    int columnCount() const { return m_assembly.layout().size(); }
    int mismatchCount() const { return m_assembly.layout().mismatches.size(); }
    int consensusLength() const { return m_assembly.consensus().size(); }
    bool canExport() const;
    bool dirty() const { return m_dirty && canExport(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    int selectedRead() const { return m_selected; }
    int cursorColumn() const { return m_cursor; }
    int mismatchNumber() const;
    int cursorReference() const;
    int cursorInsert() const;
    int cursorBase() const;
    int cursorQuality() const;
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }

    // Where the cursor sits in the selected read's trace: one base, or the
    // two bases either side of a gap. Both are -1 when the read isn't there.
    struct Spot {
        int left = -1, right = -1;
    };
    Spot cursorSpot() const;

    Q_INVOKABLE void openFiles();
    Q_INVOKABLE void openReference();
    Q_INVOKABLE void exportFasta();
    Q_INVOKABLE void loadUrls(const QList<QUrl> &urls);
    void loadPaths(const QStringList &paths);
    bool saveFasta(const QString &path);

    Q_INVOKABLE void select(int read);
    Q_INVOKABLE void selectRelative(int delta);
    Q_INVOKABLE void setCursor(int column);
    Q_INVOKABLE void moveCursor(int delta);
    Q_INVOKABLE void cursorToStart();
    Q_INVOKABLE void cursorToEnd();
    Q_INVOKABLE void cursorToBase(int base);
    Q_INVOKABLE void nextMismatch();
    Q_INVOKABLE void previousMismatch();
    // Space: open files when there are none, else on to the next mismatch.
    Q_INVOKABLE void primaryAction();

    Q_INVOKABLE void trimStartAtCursor();
    Q_INVOKABLE void trimEndAtCursor();
    Q_INVOKABLE void resetTrim();
    Q_INVOKABLE void toggleIncluded();
    Q_INVOKABLE void stricterTrim();
    Q_INVOKABLE void looserTrim();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void changed();
    void cursorChanged();
    void messageChanged();

private:
    struct Snapshot {
        double errorLimit;
        QVector<ReadChoices> reads;
    };
    Snapshot snapshot() const;
    // Records the state before a change, so undo can return to it.
    void remember();
    void afterEdit(const QString &message);
    void keepCursorOn(const Layout::Column &column);
    void selectDisagreeingRead();
    void jumpTo(int column);
    void say(const QString &message, bool error = false);
    int cursorTraceBase() const;

    Assembly m_assembly;
    FilePicker *m_picker;
    QString m_lastFolder;
    int m_selected = -1;
    int m_cursor = 0;
    bool m_dirty = false;
    QVector<Snapshot> m_undo, m_redo;
    QString m_message;
    bool m_messageIsError = false;
};

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

// Owns one native AnyDoc conversion. The converter is deliberately an external
// FastMD-owned executable: this keeps Rust and document parsers out of the Qt
// startup path and preserves the application's no-Node policy.
class ImportManager final : public QObject {
    Q_OBJECT
public:
    // What the converted Markdown is for. The conversion itself is identical;
    // only the destination and the wording of a failure differ, so both
    // workflows share one process, one timeout and one result parser.
    enum class Purpose {
        ImportDocument, // File > Import as Markdown: becomes a new document.
        InsertContent,  // Insert > Content from File: goes into the caret.
    };
    Q_ENUM(Purpose)

    explicit ImportManager(QObject* parent = nullptr);
    ~ImportManager() override;

    static QString converterPath();
    // The one list of formats the bundled converter handles (lowercase, no
    // leading dot). Both file filters and the Insert routing decision are
    // derived from it -- see dev-docs/BUNDLED_TOOLS.md before changing it.
    static const QStringList& supportedExtensions();
    static bool handlesExtension(const QString& suffix);
    // "Documents supported by AnyDoc (*.doc *.docx ...)" -- one filter entry,
    // for dialogs that offer other entries alongside it.
    static QString documentFilterEntry();
    static QString importFileFilter();
    // Markdown destination beside the source document, never overwriting an
    // existing file: Report.docx -> Report.md, or Report (2).md if taken.
    static QString destinationFor(const QString& inputPath);
    // Private, unique scratch destination for a conversion whose Markdown is
    // only read back and discarded. Pair with removeTemporary().
    static QString temporaryDestinationFor(const QString& inputPath);
    // Deletes a temporaryDestinationFor() result and the folder made for it.
    static void removeTemporary(const QString& markdownPath);

    bool isRunning() const;
    bool start(Purpose purpose, const QString& inputPath, const QString& markdownPath);

signals:
    // droppedImages counts embedded images AnyDoc could only render as alt
    // text; see the wrapper's embedded_asset_count().
    void converted(ImportManager::Purpose purpose, const QString& markdownPath, int droppedImages);
    void conversionFailed(ImportManager::Purpose purpose, const QString& title, const QString& message);

private:
    void finishProcess();
    // Terminates the converter and reaps it without blocking indefinitely.
    void killProcess();
    static QString errorMessage(const QString& code, const QString& detail);
    static QString failureTitle(Purpose purpose);

    QProcess* m_process = nullptr;
    QTimer* m_timeout = nullptr;
    bool m_timedOut = false;
    Purpose m_purpose = Purpose::ImportDocument;
    QString m_markdownPath;
};

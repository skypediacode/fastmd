#pragma once
#include <QSyntaxHighlighter>
#include <QTextBlockUserData>
#include <QString>
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/State>

// Syntax highlighting for standalone (non-Markdown) text files, keyed off
// the document's file name via KSyntaxHighlighting's own extension/mimetype
// database. Falls back to no highlighting when no definition matches.
class SourceHighlighter : public QSyntaxHighlighter, public KSyntaxHighlighting::AbstractHighlighter {
    Q_OBJECT
    Q_INTERFACES(KSyntaxHighlighting::AbstractHighlighter)
public:
    explicit SourceHighlighter(QTextDocument* parent = nullptr);

    void setDarkMode(bool dark);
    void setEnabled(bool enabled);
    // Applies an explicitly resolved definition (language policy now lives in
    // DocumentModel/SyntaxDefinitionResolver, driven by MainWindow's
    // centralized language operation -- this highlighter just projects it).
    void setDefinition(const KSyntaxHighlighting::Definition& definition) override;

protected:
    void highlightBlock(const QString& text) override;
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format) override;
    void applyFolding(int offset, int length, KSyntaxHighlighting::FoldingRegion region) override;

private:
    struct BlockState : QTextBlockUserData {
        KSyntaxHighlighting::State state;
    };

    bool m_enabled = true;
    bool m_dark = false;

    void applyTheme();
};

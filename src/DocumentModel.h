#pragma once
#include <QString>

enum class EditorMode {
    PlainText,
    Markdown,
};

class DocumentModel {
public:
    explicit DocumentModel(int untitledIndex);

    static EditorMode modeForPath(const QString& path);

    const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& path) {
        if (m_filePath != path) {
            m_filePath = path;
            m_mode = modeForPath(path);
        }
    }

    bool isDirty() const { return m_dirty; }
    void setDirty(bool d) { m_dirty = d; }

    EditorMode editorMode() const { return m_mode; }
    void setEditorMode(EditorMode mode) { m_mode = mode; }

    bool markdownPreviewVisible() const { return m_markdownPreviewVisible; }
    void setMarkdownPreviewVisible(bool visible) { m_markdownPreviewVisible = visible; }

    const QString& lineEnding() const { return m_lineEnding; }
    void setLineEnding(const QString& lineEnding) { m_lineEnding = lineEnding; }

    bool isUntitled() const { return m_filePath.isEmpty(); }
    QString displayName() const;

private:
    QString m_filePath;
    bool    m_dirty        = false;
    int     m_untitledIndex;
    EditorMode m_mode      = EditorMode::Markdown;
    bool    m_markdownPreviewVisible = true;
    QString m_lineEnding   = QStringLiteral("\r\n");
};

#pragma once
#include <QString>

class DocumentModel {
public:
    explicit DocumentModel(int untitledIndex);

    const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    bool isDirty() const { return m_dirty; }
    void setDirty(bool d) { m_dirty = d; }

    bool isUntitled() const { return m_filePath.isEmpty(); }
    QString displayName() const;

private:
    QString m_filePath;
    bool    m_dirty        = false;
    int     m_untitledIndex;
};

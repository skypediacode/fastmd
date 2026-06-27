#pragma once
#include <QTextBrowser>
#include "ExportManager.h"

class PreviewWidget : public QTextBrowser {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setBodyHtml(const QString& html);
    void setBasePath(const QString& path);
    void setDarkMode(bool dark);

public slots:
    void zoomIn(int range = 1);
    void zoomOut(int range = 1);

signals:
    void zoomRequested(int delta);

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    QString m_bodyHtml;
    QString m_basePath;
    bool    m_dark = false;
    int     m_baseFontSize = FastMdDefaults::PreviewFontSize;

    void refresh();
};

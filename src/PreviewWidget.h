#pragma once
#include <QTextBrowser>
#include <QToolButton>
#include "ExportManager.h"

class QPropertyAnimation;

class OutlineEdgeButton : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(int expandWidth READ expandWidth WRITE setExpandWidth)
public:
    explicit OutlineEdgeButton(QWidget* parent = nullptr);

    void setPopupOpen(bool open);
    bool isPopupOpen() const { return m_popupOpen; }

    int expandWidth() const { return m_expandWidth; }
    void setExpandWidth(int w);

    void updatePosition();

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;

private:
    bool m_popupOpen = false;
    int m_expandWidth = 32;
    QPropertyAnimation* m_anim;

    void updateState();
};

class PreviewWidget : public QTextBrowser {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setBodyHtml(const QString& html);
    void setBasePath(const QString& path);
    void setDarkMode(bool dark);
    bool isRefreshing() const { return m_refreshing; }
    void setBaseFontSize(int size) { m_baseFontSize = size; refresh(); }

    OutlineEdgeButton* outlineButton() const { return m_outlineBtn; }

public slots:
    void zoomIn(int range = 1);
    void zoomOut(int range = 1);

signals:
    void zoomRequested(int delta);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QString m_bodyHtml;
    QString m_basePath;
    bool    m_dark = false;
    int     m_baseFontSize = FastMdDefaults::PreviewFontSize;
    bool    m_refreshing = false;
    OutlineEdgeButton* m_outlineBtn = nullptr;

    void refresh();
};

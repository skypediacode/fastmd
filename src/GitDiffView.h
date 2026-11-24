#pragma once

#include "GitRepository.h"

#include <QFont>
#include <QPlainTextEdit>
#include <QVector>
#include <QWidget>

class QLabel;
class QStackedWidget;
class ThemedSplitter;

// One pane of the side-by-side diff: a read-only QPlainTextEdit with a
// line-number gutter. Modelled on MarkdownEditor's LineNumberArea, but the
// numbers come from a supplied map rather than the block index, because a gap
// row has no line number on its side.
class GitDiffPane final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit GitDiffPane(QWidget* parent = nullptr);

    void setContent(const QStringList& lines, const QVector<int>& lineNumbers);
    void setDarkMode(bool dark);
    void setSharedGutterWidth(int width);
    void setWordWrapEnabled(bool enabled) { setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap); }
    // Applied relative to the font captured at construction, not the pane's
    // current font, so repeated calls with the same delta are idempotent —
    // needed because the shared diff zoom is set absolutely, not stepped.
    void setZoomDelta(int delta);

    int  gutterWidth() const;
    void paintGutter(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

signals:
    // Forwarded to the diff view so either pane changes the one shared font
    // scale without coupling the panes' independent scroll positions.
    void zoomRequested(int delta);

private:
    void updateGutterGeometry();

    QWidget*     m_gutter = nullptr;
    QVector<int> m_lineNumbers;
    int          m_sharedGutterWidth = 0;
    bool         m_dark = false;
    QFont        m_baseFont;
};

// Side-by-side diff. Both panes always hold the same number of lines -- gap
// rows are blank lines -- so the two sides stay aligned with nothing more
// than mirrored scrollbars. Word wrap defaults off (View > Word Wrap is a
// per-tab preference, same as an ordinary editor tab -- see setWordWrapEnabled).
// When it is on, a row's wrapped height can differ between the two panes
// (their text lengths differ), so vertical sync switches from copying the
// raw scrollbar value to copying its fraction of the range -- exactly
// equivalent when wrapping is off (both panes then have identical ranges),
// and the closest available approximation to row alignment when it is on.
class GitDiffView final : public QWidget {
    Q_OBJECT
public:
    explicit GitDiffView(QWidget* parent = nullptr);

    void setRows(const QVector<GitDiffRow>& rows);
    void showPlaceholder(const QString& text);
    void setDarkMode(bool dark);
    void setWordWrapEnabled(bool enabled);
    bool wordWrapEnabled() const { return m_wordWrap; }

    // The shared diff zoom level, common to every open GitDiffView. Applying
    // it here never emits zoomDeltaChanged() — that signal exists only to
    // report a delta a *user* just made (via Ctrl+wheel) up to whoever
    // broadcasts it to every other open diff tab and persists it.
    int  zoomDelta() const { return m_zoomDelta; }
    void setZoomDelta(int delta);

    QByteArray saveSplitterState() const;
    void       restoreSplitterState(const QByteArray& state);

signals:
    void zoomDeltaChanged(int delta);

private:
    void syncScroll(QScrollBar* source, QScrollBar* target);
    void applyRowTints(const QVector<GitDiffRow>& rows);
    void adjustZoom(int delta);
    void refreshGutterWidths();

    QStackedWidget* m_stack = nullptr;
    QLabel*         m_placeholder = nullptr;
    ThemedSplitter* m_splitter = nullptr;
    GitDiffPane*    m_left = nullptr;
    GitDiffPane*    m_right = nullptr;
    bool            m_syncing = false;
    bool            m_dark = false;
    bool            m_wordWrap = false;
    int             m_zoomDelta = 0;
    QVector<GitDiffRow> m_rows;
};

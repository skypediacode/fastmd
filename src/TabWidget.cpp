#include "TabWidget.h"
#include "DocumentModel.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"

#include <QSplitter>
#include <QScrollBar>
#include <QToolButton>
#include <QTimer>
#include <QHBoxLayout>
#include <QStyle>
#include <QFont>
#include <QPainter>
#include <QSettings>
#include <QFileInfo>
#include <QEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>

int TabWidget::s_untitled = 0;



class TabWidget::CloseTabButton : public QToolButton {
public:
    explicit CloseTabButton(QWidget* parent = nullptr) : QToolButton(parent) {
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(true);
        setFixedSize(16, 16);
        setAttribute(Qt::WA_Hover);
    }

    void setDarkMode(bool dark) {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPointF c = QRectF(rect()).center();
        const qreal d = 4.0; // half-arm length — always 8px total, regardless of widget size

        if (underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 40));
            const qreal r = d + 7.0;
            p.drawRoundedRect(c.x()-r, c.y()-r, r*2, r*2, 3, 3);
        }

        const QColor glyph = m_dark ? QColor(0xd7, 0xd7, 0xd7) : palette().color(QPalette::Text);
        p.setPen(QPen(glyph, 1.3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + QPointF(-d, -d), c + QPointF(d,  d));
        p.drawLine(c + QPointF( d, -d), c + QPointF(-d, d));
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*)      override { update(); }

private:
    bool m_dark = false;
};

// ---------------------------------------------------------------------------
TabWidget::TabWidget(QWidget* parent)
    : QTabWidget(parent)
{
    auto* bar = new TabBar(this);
    setTabBar(bar);

    setTabsClosable(false);
    setMovable(true);
    setDocumentMode(true);

    connect(bar,  &TabBar::newTabRequested,       this, [this]{ addNewTab(); });
    connect(bar,  &TabBar::middleClickCloseRequested, this, &TabWidget::onCloseRequested);
    connect(this, &QTabWidget::currentChanged,    this, &TabWidget::onCurrentChanged);
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::onCloseRequested);

    tabBar()->installEventFilter(this);
}

// ---------------------------------------------------------------------------
int TabWidget::addNewTab(const QString& filePath, bool activate)
{
    auto* page    = new TabPage;
    page->model   = new DocumentModel(++s_untitled);
    page->editor  = new MarkdownEditor;
    page->preview = new PreviewWidget;
    page->splitter= new QSplitter(Qt::Horizontal);

    if (!filePath.isEmpty())
        page->model->setFilePath(filePath);
    page->editor->setDocumentPath(filePath);

    page->splitter->addWidget(page->editor);
    page->splitter->addWidget(page->preview);
    page->splitter->setStretchFactor(0, 50);
    page->splitter->setStretchFactor(1, 50);
    page->splitter->setSizes({ 500, 500 });
    page->splitter->setHandleWidth(1);

    QSettings s;
    QByteArray state = s.value(QStringLiteral("splitterState")).toByteArray();
    if (!state.isEmpty()) {
        page->splitter->restoreState(state);
    }
    const bool markdownPreviewVisible = s.value(QStringLiteral("previewVisible"), true).toBool();
    page->model->setMarkdownPreviewVisible(markdownPreviewVisible);
    page->preview->setVisible(page->model->editorMode() == EditorMode::Markdown && markdownPreviewVisible);
    page->editor->setMarkdownMode(page->model->editorMode() == EditorMode::Markdown);
    page->editor->setDarkMode(m_darkMode);
    page->preview->setDarkMode(m_darkMode);

    int fontSize = s.value(QStringLiteral("editorFontSize"), FastMdDefaults::EditorFontSize).toInt();
    QFont f = page->editor->font();
    f.setPixelSize(fontSize);
    page->editor->setFont(f);
    page->preview->setBaseFontSize(fontSize);

    connect(page->splitter, &QSplitter::splitterMoved, this, [page](int, int) {
        QSettings s;
        s.setValue(QStringLiteral("splitterState"), page->splitter->saveState());
    });

    // sync scrolling: editor → preview
    connect(page->editor->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [page](int value) {
        auto* eb = page->editor->verticalScrollBar();
        auto* pb = page->preview->verticalScrollBar();
        if (eb->maximum() > 0)
            pb->setValue(static_cast<int>(
                static_cast<double>(value) / eb->maximum() * pb->maximum()));
    });

    m_pages.append(page);
    int idx = addTab(page->splitter, page->model->displayName());
    tabBar()->setTabButton(idx, QTabBar::RightSide, makeCloseButton(page));
    if (activate) {
        setCurrentIndex(idx);
        page->editor->setFocus();
    }
    return idx;
}

QWidget* TabWidget::makeCloseButton(TabPage* page)
{
    auto* button = new CloseTabButton(tabBar());
    button->setDarkMode(m_darkMode);

    connect(button, &QToolButton::clicked, this, [this, page]() {
        int idx = m_pages.indexOf(page);
        if (idx >= 0)
            onCloseRequested(idx);
    });

    return button;
}

// ---------------------------------------------------------------------------
void TabWidget::closeTabAt(int index)
{
    if (index < 0 || index >= m_pages.size()) return;

    TabPage* page = m_pages.takeAt(index);
    removeTab(index);

    delete page->model;
    // splitter owns editor and preview as children
    delete page->splitter;
    delete page;
}

// ---------------------------------------------------------------------------
TabPage* TabWidget::currentPage() const
{
    int i = currentIndex();
    return (i >= 0 && i < m_pages.size()) ? m_pages.at(i) : nullptr;
}

TabPage* TabWidget::pageAt(int i) const
{
    return (i >= 0 && i < m_pages.size()) ? m_pages.at(i) : nullptr;
}

MarkdownEditor* TabWidget::currentEditor()  const { auto* p = currentPage(); return p ? p->editor  : nullptr; }
PreviewWidget*  TabWidget::currentPreview() const { auto* p = currentPage(); return p ? p->preview : nullptr; }
DocumentModel*  TabWidget::currentModel()   const { auto* p = currentPage(); return p ? p->model   : nullptr; }

// ---------------------------------------------------------------------------
void TabWidget::markDirty(int index, bool dirty)
{
    if (TabPage* p = pageAt(index))
        p->model->setDirty(dirty);
    refreshTitle(index);
}

void TabWidget::refreshTitle(int index)
{
    if (TabPage* p = pageAt(index)) {
        QString name = p->model->displayName();
        if (p->model->isDirty()) name.prepend('*');
        setTabText(index, name);
    }
}

// ---------------------------------------------------------------------------
void TabWidget::setDarkMode(bool dark)
{
    m_darkMode = dark;
    for (auto* p : m_pages) {
        p->editor->setDarkMode(dark);
        p->preview->setDarkMode(dark);
    }
    for (QObject* child : tabBar()->children()) {
        if (auto* button = dynamic_cast<CloseTabButton*>(child))
            button->setDarkMode(dark);
    }
}

// ---------------------------------------------------------------------------
void TabWidget::onCurrentChanged(int index)
{
    if (TabPage* p = pageAt(index))
        emit editorActivated(p->editor, p->preview);
    QTimer::singleShot(0, this, &TabWidget::updateTabBarLayout);
}

void TabWidget::onCloseRequested(int index)
{
    emit tabClosed(index);
}

void TabWidget::scheduleTabBarLayoutUpdate()
{
    if (m_layoutPending)
        return;
    m_layoutPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_layoutPending = false;
        updateTabBarLayout();
    });
}

void TabWidget::updateTabBarLayout()
{
    auto* bar = tabBar();
    if (!bar)
        return;

    int w = 0;
    if (auto* corner = cornerWidget(Qt::TopLeftCorner)) {
        if (!corner->isHidden())
            w = corner->x() + corner->width();
    }
    if (bar->x() == w && bar->width() == width() - w)
        return;

    m_layoutGuard = true;
    bar->move(w, bar->y());
    bar->resize(width() - w, bar->height());
    m_layoutGuard = false;
}

bool TabWidget::eventFilter(QObject* obj, QEvent* event)
{
    const bool cornerWidgetEvent = (obj == cornerWidget(Qt::TopLeftCorner));

    if ((obj == tabBar() || cornerWidgetEvent) &&
        (event->type() == QEvent::Move ||
         event->type() == QEvent::Resize ||
         event->type() == QEvent::Show ||
         event->type() == QEvent::Hide ||
         event->type() == QEvent::StyleChange ||
         event->type() == QEvent::FontChange) &&
        !m_layoutGuard)
        scheduleTabBarLayoutUpdate();
    return false;
}

bool TabWidget::event(QEvent* event)
{
    bool result = QTabWidget::event(event);
    if (event->type() == QEvent::LayoutRequest)
        scheduleTabBarLayoutUpdate();
    else if (event->type() == QEvent::StyleChange)
        scheduleTabBarLayoutUpdate();
    return result;
}

void TabWidget::resizeEvent(QResizeEvent* event)
{
    QTabWidget::resizeEvent(event);
    scheduleTabBarLayoutUpdate();
}

void TabWidget::showEvent(QShowEvent* event)
{
    QTabWidget::showEvent(event);
    scheduleTabBarLayoutUpdate();
}

void TabWidget::tabInserted(int index)
{
    QTabWidget::tabInserted(index);
    scheduleTabBarLayoutUpdate();
}

void TabWidget::tabRemoved(int index)
{
    QTabWidget::tabRemoved(index);
    scheduleTabBarLayoutUpdate();
}

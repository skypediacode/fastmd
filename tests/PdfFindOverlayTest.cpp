// FindOverlay on a PDF tab, against a real WebView2 and the runtime's PDF viewer.
// Needs a WebView2 runtime with the Find API: without one the search cases fail.

#include "ApplicationTheme.h"
#include "FindController.h"
#include "FindOverlay.h"
#include "PreviewWidget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

namespace {

// Mirrors FindOverlay's placement. PDF: the viewer's 40px toolbar plus the
// Markdown overlay's 10px gap, and its 16px scrollbar track plus a 6px gap.
constexpr int kExpectedTopOffset = 50;
constexpr int kExpectedRightInset = 22;
constexpr int kMarkdownTopOffset = 10;
constexpr int kMarkdownRightInset = 16;
constexpr int kMarkdownWidth = 420;
constexpr int kPdfToolbarHeight = 40;
constexpr int kPdfScrollbarWidth = 16;
constexpr int kSearchTimeoutMs = 20000;

QString writePdf(const QString& path, int tokenCount)
{
    QPdfWriter writer(path);
    QPainter painter(&writer);
    painter.setFont(QFont(QStringLiteral("Arial"), 14));
    const int step = painter.fontMetrics().height() * 2;
    for (int i = 0; i < tokenCount; ++i)
        painter.drawText(0, step * (i + 1), QStringLiteral("line %1 holds a token").arg(i + 1));
    return path;
}

} // namespace

class PdfFindOverlayTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void showsOnlyControlsThePdfEngineHonours();
    void opensBelowToolbarAtViewerRightEdge();
    void followsViewerThroughLayoutChanges();
    void searchesThePdfAndWalksMatches();
    void restartsSearchOnAnotherPdf();
    void cornersFollowTheRoundedBorder();
    void editorModeKeepsItsPlacement();

private:
    void expectAnchoredToViewer(PreviewWidget* viewer) const;
    QLineEdit* findEdit() const;

    QTemporaryDir m_dir;
    QString m_fourTokens;
    QString m_twoTokens;
    QWidget* m_host = nullptr;
    QWidget* m_sidebar = nullptr;
    PreviewWidget* m_viewer = nullptr;
    FindController* m_controller = nullptr;
    FindOverlay* m_overlay = nullptr;
};

void PdfFindOverlayTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_fourTokens = writePdf(m_dir.filePath(QStringLiteral("four.pdf")), 4);
    m_twoTokens = writePdf(m_dir.filePath(QStringLiteral("two.pdf")), 2);
}

void PdfFindOverlayTest::cleanupTestCase()
{
    PreviewWidget::shutdownSharedEnvironment();
}

void PdfFindOverlayTest::init()
{
    m_host = new QWidget;
    m_host->resize(1100, 700);
    auto* layout = new QHBoxLayout(m_host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_sidebar = new QWidget(m_host);
    m_sidebar->setFixedWidth(200);
    m_viewer = new PreviewWidget(m_host);
    layout->addWidget(m_sidebar);
    layout->addWidget(m_viewer, 1);

    m_controller = new FindController(m_host);
    m_overlay = new FindOverlay(m_controller, m_host);
    m_controller->setTarget(nullptr, m_viewer);
    m_overlay->setPdfMode(true);
    m_overlay->setAnchorWidget(m_viewer);

    m_viewer->showReadOnlyFile(m_fourTokens);
    m_host->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_host));
}

void PdfFindOverlayTest::cleanup()
{
    delete m_host;
    m_host = nullptr;
    qApp->setStyleSheet(QString());
}

QLineEdit* PdfFindOverlayTest::findEdit() const
{
    return m_overlay->findChild<QLineEdit*>(QStringLiteral("findOverlayEdit"));
}

void PdfFindOverlayTest::expectAnchoredToViewer(PreviewWidget* viewer) const
{
    const QPoint topLeft = viewer->mapToGlobal(QPoint(0, 0));
    QTRY_COMPARE(m_overlay->geometry().top(), topLeft.y() + kExpectedTopOffset);
    QCOMPARE(m_overlay->geometry().x() + m_overlay->width(),
             topLeft.x() + viewer->width() - kExpectedRightInset);
    QVERIFY(m_overlay->geometry().top() >= topLeft.y() + kPdfToolbarHeight);
    QVERIFY(m_overlay->geometry().x() >= topLeft.x());
    QVERIFY(m_overlay->geometry().x() + m_overlay->width()
            < topLeft.x() + viewer->width() - kPdfScrollbarWidth);
}

void PdfFindOverlayTest::cornersFollowTheRoundedBorder()
{
    const QStringList themes{QStringLiteral("light"), QStringLiteral("dark")};
    for (const bool pdfMode : {true, false}) {
        m_overlay->setPdfMode(pdfMode);
        for (const QString& theme : themes) {
            ApplicationTheme::apply(theme);
            m_overlay->setDarkMode(theme == QStringLiteral("dark"));
            const QImage image = m_overlay->grab().toImage();
            const int right = image.width() - 1;
            const int bottom = image.height() - 1;
            for (const QPoint& corner : {QPoint(0, 0), QPoint(right, 0),
                                         QPoint(0, bottom), QPoint(right, bottom)})
                QCOMPARE(image.pixelColor(corner).alpha(), 0);
            QCOMPARE(image.pixelColor(right / 2, 0).alpha(), 255);
            QCOMPARE(image.pixelColor(0, bottom / 2).alpha(), 255);
        }
    }
}

void PdfFindOverlayTest::editorModeKeepsItsPlacement()
{
    m_overlay->setPdfMode(false);
    m_overlay->openFind();
    QTRY_VERIFY(m_overlay->isVisible());

    const QPoint topLeft = m_viewer->mapToGlobal(QPoint(0, 0));
    QTRY_COMPARE(m_overlay->geometry().top(), topLeft.y() + kMarkdownTopOffset);
    QCOMPARE(m_overlay->width(), kMarkdownWidth);
    QCOMPARE(m_overlay->geometry().x() + m_overlay->width(),
             topLeft.x() + m_viewer->width() - kMarkdownRightInset);
}

void PdfFindOverlayTest::showsOnlyControlsThePdfEngineHonours()
{
    m_overlay->openReplace();
    QTRY_VERIFY(m_overlay->isVisible());

    const auto visibleCount = [this](const char* objectName) {
        int count = 0;
        const auto buttons = m_overlay->findChildren<QToolButton*>(QLatin1String(objectName));
        for (const QToolButton* button : buttons)
            count += button->isVisible() ? 1 : 0;
        return count;
    };
    QCOMPARE(visibleCount("findOverlayChevron"), 0);
    QCOMPARE(visibleCount("findOverlayOption"), 0);
    // Previous, Next and Close.
    QCOMPARE(visibleCount("findOverlayAction"), 3);
    QVERIFY(findEdit()->isVisible());
    QVERIFY(m_overlay->findChild<QWidget*>(QStringLiteral("findOverlayCount"))->isVisible());
}

void PdfFindOverlayTest::opensBelowToolbarAtViewerRightEdge()
{
    m_overlay->openFind();
    QTRY_VERIFY(m_overlay->isVisible());
    expectAnchoredToViewer(m_viewer);
}

void PdfFindOverlayTest::followsViewerThroughLayoutChanges()
{
    m_overlay->openFind();
    QTRY_VERIFY(m_overlay->isVisible());
    expectAnchoredToViewer(m_viewer);

    m_host->resize(900, 620);
    expectAnchoredToViewer(m_viewer);

    m_sidebar->hide();
    QTRY_COMPARE(m_viewer->width(), m_host->width());
    expectAnchoredToViewer(m_viewer);

    m_sidebar->show();
    QTRY_COMPARE(m_viewer->width(), m_host->width() - m_sidebar->width());
    expectAnchoredToViewer(m_viewer);

    m_host->move(m_host->pos() + QPoint(40, 30));
    expectAnchoredToViewer(m_viewer);
}

void PdfFindOverlayTest::searchesThePdfAndWalksMatches()
{
    m_overlay->openFind();
    QTRY_VERIFY(m_overlay->isVisible());
    findEdit()->setText(QStringLiteral("token"));
    QTRY_COMPARE(m_controller->term(), QStringLiteral("token"));

    // Searching straight after opening: the PDF has not finished loading yet.
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->matchCount(), 4, kSearchTimeoutMs);
    QTRY_COMPARE(m_controller->currentIndex(), 0);
    QVERIFY(m_controller->findNext());
    QTRY_COMPARE(m_controller->currentIndex(), 1);
    QVERIFY(m_controller->findPrevious());
    QTRY_COMPARE(m_controller->currentIndex(), 0);

    findEdit()->setText(QStringLiteral("absent"));
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->matchCount(), 0, kSearchTimeoutMs);

    // A restart that lands on an unchanged count raises no change event.
    findEdit()->setText(QStringLiteral("token"));
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->matchCount(), 4, kSearchTimeoutMs);
    m_controller->refresh();
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->matchCount(), 4, kSearchTimeoutMs);
}

void PdfFindOverlayTest::restartsSearchOnAnotherPdf()
{
    m_overlay->openFind();
    QTRY_VERIFY(m_overlay->isVisible());
    findEdit()->setText(QStringLiteral("token"));
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->matchCount(), 4, kSearchTimeoutMs);

    auto* other = new PreviewWidget(m_host);
    m_host->layout()->addWidget(other);
    other->showReadOnlyFile(m_twoTokens);
    m_viewer->hide();
    other->show();

    m_controller->setTarget(nullptr, other);
    m_overlay->setAnchorWidget(other);
    expectAnchoredToViewer(other);
    QTRY_COMPARE_WITH_TIMEOUT(m_controller->matchCount(), 2, kSearchTimeoutMs);
}

QTEST_MAIN(PdfFindOverlayTest)
#include "PdfFindOverlayTest.moc"

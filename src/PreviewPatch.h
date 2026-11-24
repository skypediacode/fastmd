#pragma once
#include <QList>
#include <QString>
#include <QStringView>
#include <memory>

// Changed-block transport for the live preview.
//
// The page holds one top-level element child of #content per block (see
// resources/preview/preview.js). Once the page has a body whose blocks C++
// knows, a later conversion is diffed against it here -- on the conversion
// worker -- and only the changed blocks, plus the line shift every kept block
// needs, cross to WebView2. The rules mirror applyBlocks() in preview.js:
// blocks equal apart from source-line numbers that fit one edit (lines before
// T stay, lines at or after T move by one delta) are kept and renumbered.
//
// Pure Qt Core: no widgets, no WebView2, safe on any thread.

// A prepared preview body and the [begin, end) span of each top-level element.
struct PreviewBlocks {
    QString body;
    QList<qsizetype> begin;
    QList<qsizetype> end;

    qsizetype count() const { return begin.size(); }
    QStringView block(qsizetype i) const
    {
        return QStringView(body).sliced(begin.at(i), end.at(i) - begin.at(i));
    }
};
using PreviewBlocksPtr = std::shared_ptr<const PreviewBlocks>;

namespace PreviewPatch {

// Splits `body` into its top-level elements, or returns null when the browser
// could parse it into different ones: unbalanced or misnested tags, text at the
// top level, a block element inside <p>, anything a table would foster out.
// Null only costs the optimization -- the body is then sent whole. `failedAt`,
// when given, receives the offset where the split gave up.
PreviewBlocksPtr split(const QString& body, qsizetype* failedAt = nullptr);

// Replace `removeCount` old blocks at `oldIndex` with `insertCount` new blocks
// starting at `newIndex`.
struct Op {
    qsizetype oldIndex = 0;
    qsizetype removeCount = 0;
    qsizetype newIndex = 0;
    qsizetype insertCount = 0;
};

struct Patch {
    qsizetype oldCount = 0;
    qsizetype newCount = 0;
    // Every source line >= shiftLine in a moved block moves by shiftDelta.
    int shiftLine = 0;
    int shiftDelta = 0;
    // Inclusive ranges of old block indices that are kept but renumbered.
    QList<std::pair<qsizetype, qsizetype>> moved;
    // Ascending by oldIndex, never overlapping each other or `moved`.
    QList<Op> ops;

    bool isEmpty() const { return ops.isEmpty() && moved.isEmpty(); }
    qsizetype insertedBlocks() const;
    qsizetype keptBlocks() const;
    qsizetype movedBlocks() const;
};

Patch diff(const PreviewBlocks& before, const PreviewBlocks& after);

// Appends `s` as a quoted JSON string. Lone surrogates become U+FFFD, the same
// as the UTF-8 round trip QJsonDocument would have put them through.
void appendJsonString(QString& out, QStringView s);

// The fields of a message after its "seq" (and "base"), closing brace
// included, so the UI thread only has to prepend the sequence numbers.
// `blockCount` < 0 means the body has no known split.
QString contentFields(const QString& preparedBody, qsizetype blockCount);
QString patchFields(const Patch& patch, const PreviewBlocks& after);

} // namespace PreviewPatch

// One conversion's result as the preview consumes it. Built on the conversion
// worker by buildPreviewBodyUpdate(); applied by PreviewWidget::applyBodyUpdate().
struct PreviewBodyUpdate {
    QString html;               // raw converted body
    QString prepared;           // PreviewWidget::prepareBodyForBrowser(html)
    PreviewBlocksPtr blocks;    // split of `prepared`, null when unsplittable
    PreviewBlocksPtr base;      // what `patch` was computed against
    bool hasPatch = false;
    bool patchEmpty = false;    // nothing on the page would change
    QString patchFields;
    QString fullFields;         // set when no patch could be computed
    PreviewPatch::Patch patch;  // kept for diagnostics and tests
};

// `base` is the body the preview last posted (PreviewWidget::postedBlocks()).
PreviewBodyUpdate buildPreviewBodyUpdate(QString html, QString prepared, PreviewBlocksPtr base);

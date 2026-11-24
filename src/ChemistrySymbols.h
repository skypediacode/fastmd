#pragma once
#include <QString>
#include <QVector>

// One clickable entry in the Math Toolbar's Chemistry popup. `display` is a
// Unicode chemistry-notation rendering (subscripts, superscripts, arrows)
// shown on the button; `insertLatex` is the exact mhchem source inserted
// into the document, optionally wrapping the editable field with
// "«»" stop markers (same convention as MathSymbols.h) so MarkdownEditor
// can select it for typing over, or substitute selected text into it.
struct ChemistryItem {
    QString id;
    QString display;
    QString insertLatex;
    QString tooltip;
    bool compact = true; // true = icon-style button, false = full-width row
};

struct ChemistrySection {
    QString title;
    QVector<ChemistryItem> items;
};

const QVector<ChemistrySection>& chemistrySections();

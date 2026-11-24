#pragma once
#include <QString>
#include <QVector>

struct MathSymbol {
    QString id;
    QString group;
    QString insertLatex;
    QString iconLatex;
    QString iconPath;
    QString compactIconFormula; // Optional formula for long expressions
    double scaleOverride;       // Optional scale multiplier (default 1.0)
    QString tooltip;
};

struct MathGroupConfig {
    double aspectRatio = 1.0; // Aspect ratio of SVG icons (1.0 for 1:1, 1.5 for 3:2)
    int cellWidth = 40;       // Width of each symbol cell
    int cellHeight = 40;      // Height of each symbol cell
    int columns = 8;          // Number of columns (1 for single-column list layout)
    int popupWidth = 0;       // Explicit popup width (0 = auto calculated)
    int iconInset = 3;        // Icon inset / margin inside cell
    int separatorMargin = 12; // Horizontal inset for popup section separators
    int secondaryCellWidth = 0; // Optional width for rows after the first separator
    int secondaryColumns = 0; // Optional column count for rows after the first separator
};

enum class MathGroupInteraction {
    PopupMenu,
    PickerDialog
};

struct MathGroup {
    QString id;
    QString title;
    QString representativeIconId;
    QVector<MathSymbol> symbols;
    bool listLayout = false;
    MathGroupConfig config;
    MathGroupInteraction interaction = MathGroupInteraction::PopupMenu;
};

const QVector<MathSymbol>& mathQuickAccessSymbols();
const QVector<MathGroup>& mathPopupGroups();
const MathGroup* getMathGroup(const QString& id);
const MathSymbol* getMathSymbol(const QString& id);

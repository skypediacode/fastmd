#pragma once

#include <QString>

#include <algorithm>

// VS Code's "Preserve Case": carry the casing shape of the text that was
// matched over to the replacement. Shared by the document Find/Replace
// (FindController) and Replace in Files (WorkspaceTextSearch).
inline QString applyPreservedCase(const QString& matched, const QString& replacement)
{
    if (matched.isEmpty() || replacement.isEmpty())
        return replacement;

    bool hasLetter = false;
    bool allUpper = true;
    bool allLower = true;
    for (const QChar c : matched) {
        if (!c.isLetter())
            continue;
        hasLetter = true;
        if (c.isUpper())
            allLower = false;
        else
            allUpper = false;
    }
    if (!hasLetter)
        return replacement;

    const auto letterCount = std::count_if(matched.cbegin(), matched.cend(),
                                           [](QChar c) { return c.isLetter(); });
    if (allUpper && letterCount > 1)
        return replacement.toUpper();
    if (allLower)
        return replacement.toLower();

    if (matched.at(0).isUpper()) {
        QString out = replacement.toLower();
        out[0] = out.at(0).toUpper();
        return out;
    }
    return replacement;
}

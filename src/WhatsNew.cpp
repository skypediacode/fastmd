#include "WhatsNew.h"

#include <QRegularExpression>
#include <QStringList>

namespace WhatsNew {

VersionTransition classifyVersionTransition(bool welcomeAlreadyShown,
                                            const QString& previousVersion,
                                            const QString& currentVersion)
{
    if (!welcomeAlreadyShown)
        return VersionTransition::FreshInstall;
    if (previousVersion.isEmpty() || previousVersion != currentVersion)
        return VersionTransition::Upgrade;
    return VersionTransition::SameVersion;
}

QString extractChangelogSection(const QString& changelog, const QString& version)
{
    if (version.isEmpty() || changelog.isEmpty())
        return QString();

    // A level-2 heading in general, used to find where the matched section
    // ends; and the same shape anchored to the exact running version, used
    // to find where it starts. Built fresh per call rather than function-
    // local statics -- this runs once per app launch, not on any hot path
    // (Rule 13 is about per-line/per-item construction, not this).
    static const QRegularExpression headingRe(
        QStringLiteral("^##[ \t]+\\[[^\\]]*\\].*$"), QRegularExpression::MultilineOption);
    const QRegularExpression versionHeadingRe(
        QStringLiteral("^##[ \t]+\\[") + QRegularExpression::escape(version) + QStringLiteral("\\].*$"),
        QRegularExpression::MultilineOption);

    const QRegularExpressionMatch startMatch = versionHeadingRe.match(changelog);
    if (!startMatch.hasMatch())
        return QString();

    const int sectionStart = startMatch.capturedStart();
    const QRegularExpressionMatch nextMatch = headingRe.match(changelog, startMatch.capturedEnd());
    const int sectionEnd = nextMatch.hasMatch() ? nextMatch.capturedStart() : changelog.size();

    QString section = changelog.mid(sectionStart, sectionEnd - sectionStart);
    while (section.endsWith(QLatin1Char('\n')))
        section.chop(1);
    return section;
}

QString extractChangelogSections(const QString& changelog, const QString& version)
{
    if (version.isEmpty() || changelog.isEmpty())
        return QString();

    static const QRegularExpression headingRe(
        QStringLiteral("^##[ \\t]+\\[([^\\]]*)\\].*$"), QRegularExpression::MultilineOption);

    QRegularExpressionMatchIterator it = headingRe.globalMatch(changelog);
    QRegularExpressionMatch currentMatch;
    QStringList groupedVersions;
    while (it.hasNext()) {
        const QRegularExpressionMatch candidate = it.next();
        const QStringList candidateVersions = candidate.captured(1).split(
            QRegularExpression(QStringLiteral("\\s*&\\s*")), Qt::SkipEmptyParts);
        for (const QString& candidateVersion : candidateVersions) {
            if (candidateVersion.trimmed() == version) {
                currentMatch = candidate;
                groupedVersions = candidateVersions;
                break;
            }
        }
        if (currentMatch.hasMatch())
            break;
    }

    if (!currentMatch.hasMatch())
        return QString();

    const int firstStart = currentMatch.capturedStart();
    const QRegularExpressionMatch nextMatch = headingRe.match(changelog, currentMatch.capturedEnd());
    const int firstEnd = nextMatch.hasMatch() ? nextMatch.capturedStart() : changelog.size();
    QString result = changelog.mid(firstStart, firstEnd - firstStart);

    // A grouped heading already contains the adjacent release(s). Appending
    // the following heading would incorrectly include a third, older release.
    if (groupedVersions.size() < 2 && nextMatch.hasMatch()) {
        const QRegularExpressionMatch afterNext = headingRe.match(changelog, nextMatch.capturedEnd());
        const int secondEnd = afterNext.hasMatch() ? afterNext.capturedStart() : changelog.size();
        result += changelog.mid(nextMatch.capturedStart(), secondEnd - nextMatch.capturedStart());
    }

    while (result.endsWith(QLatin1Char('\n')))
        result.chop(1);
    return result;
}

QString formatWhatsNewTitle(const QString& section, const QString& version)
{
    if (section.isEmpty() || version.isEmpty())
        return section;

    const int firstNewline = section.indexOf(QLatin1Char('\n'));
    const QString headingLine = firstNewline >= 0 ? section.left(firstNewline) : section;

    // The same heading shape extractChangelogSection() anchors on; relying on
    // it here (rather than a broad find/replace over the whole section) keeps
    // this rewrite scoped to exactly the leading heading line.
    static const QRegularExpression headingRe(
        QStringLiteral("^##[ \t]+\\[[^\\]]*\\].*$"));
    if (!headingRe.match(headingLine).hasMatch())
        return section;

    const QString title = QStringLiteral("# What's New in FastMD %1").arg(version);
    return firstNewline >= 0 ? title + section.mid(firstNewline) : title;
}

} // namespace WhatsNew

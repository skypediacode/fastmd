// Focused regression tests for the automatic What's New feature's pure
// logic: version-transition classification and CHANGELOG.md section
// extraction. Deliberately links only Qt6::Core + src/WhatsNew.cpp.

#include "WhatsNew.h"

#include <QString>

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

const QString kSampleChangelog = QStringLiteral(
    "# FastMD Changelog\n"
    "\n"
    "All notable changes to this project will be documented in this file.\n"
    "\n"
    "## [3.2.1] - 2026-08-31\n"
    "\n"
    "### Added\n"
    "- A\n"
    "\n"
    "### Fixed\n"
    "- B\n"
    "\n"
    "## [3.2.0] - 2026-08-31\n"
    "\n"
    "### Added\n"
    "- C\n");

void testFreshInstall()
{
    // No previous version, welcome never shown -> fresh install.
    const auto t = WhatsNew::classifyVersionTransition(false, QString(), QStringLiteral("3.2.1"));
    CHECK(t == WhatsNew::VersionTransition::FreshInstall);
}

void testNormalRelaunch()
{
    const auto t = WhatsNew::classifyVersionTransition(
        true, QStringLiteral("3.2.1"), QStringLiteral("3.2.1"));
    CHECK(t == WhatsNew::VersionTransition::SameVersion);
}

void testUpgrade()
{
    const auto t = WhatsNew::classifyVersionTransition(
        true, QStringLiteral("3.2.1"), QStringLiteral("3.2.2"));
    CHECK(t == WhatsNew::VersionTransition::Upgrade);
}

void testRelaunchAfterUpgrade()
{
    const auto t = WhatsNew::classifyVersionTransition(
        true, QStringLiteral("3.2.2"), QStringLiteral("3.2.2"));
    CHECK(t == WhatsNew::VersionTransition::SameVersion);
}

void testMultiVersionJump()
{
    const auto t = WhatsNew::classifyVersionTransition(
        true, QStringLiteral("3.1.0"), QStringLiteral("3.2.1"));
    CHECK(t == WhatsNew::VersionTransition::Upgrade);
}

void testUnknownPreviousVersionIsTreatedAsUpgrade()
{
    // An existing install upgrading from a build that predates this feature
    // (welcome already shown, but no lastRunVersion was ever recorded) must
    // still show What's New once for the version it just landed on, not be
    // silently treated as a fresh install.
    const auto t = WhatsNew::classifyVersionTransition(
        true, QString(), QStringLiteral("3.2.2"));
    CHECK(t == WhatsNew::VersionTransition::Upgrade);
}

void testChangelogExtraction()
{
    const QString section = WhatsNew::extractChangelogSection(kSampleChangelog, QStringLiteral("3.2.1"));
    CHECK(section.contains(QStringLiteral("## [3.2.1]")));
    CHECK(section.contains(QStringLiteral("- A")));
    CHECK(section.contains(QStringLiteral("- B")));
    CHECK(!section.contains(QStringLiteral("- C")));
    CHECK(!section.contains(QStringLiteral("[3.2.0]")));
}

void testChangelogExtractionMissingVersion()
{
    const QString section = WhatsNew::extractChangelogSection(kSampleChangelog, QStringLiteral("9.9.9"));
    CHECK(section.isEmpty());
}

void testChangelogExtractionDoesNotPrefixMatch()
{
    // "3.2" must not accidentally match the "[3.2.0]"/"[3.2.1]" headings.
    const QString section = WhatsNew::extractChangelogSection(kSampleChangelog, QStringLiteral("3.2"));
    CHECK(section.isEmpty());
}

void testChangelogExtractionLastSectionRunsToEnd()
{
    const QString section = WhatsNew::extractChangelogSection(kSampleChangelog, QStringLiteral("3.2.0"));
    CHECK(section.contains(QStringLiteral("## [3.2.0]")));
    CHECK(section.contains(QStringLiteral("- C")));
    CHECK(!section.contains(QStringLiteral("[3.2.1]")));
}

void testWhatsNewExtractionIncludesSecondLatestSection()
{
    const QString section = WhatsNew::extractChangelogSections(kSampleChangelog, QStringLiteral("3.2.1"));
    CHECK(section.contains(QStringLiteral("- A")));
    CHECK(section.contains(QStringLiteral("- B")));
    CHECK(section.contains(QStringLiteral("## [3.2.0]")));
    CHECK(section.contains(QStringLiteral("- C")));
}

void testWhatsNewExtractionKeepsGroupedVersionsTogether()
{
    const QString grouped = QStringLiteral(
        "# FastMD Changelog\n\n"
        "## [3.3.1 & 3.3.0] - 2026-09-01\n\n"
        "### Fixed\n- Current and previous\n\n"
        "## [3.2.0] - 2026-08-31\n\n"
        "- Older\n");
    const QString section = WhatsNew::extractChangelogSections(grouped, QStringLiteral("3.3.1"));
    CHECK(section.contains(QStringLiteral("[3.3.1 & 3.3.0]")));
    CHECK(section.contains(QStringLiteral("- Current and previous")));
    CHECK(!section.contains(QStringLiteral("- Older")));
}

void testWhatsNewTitleRewrite()
{
    const QString section = QStringLiteral(
        "## [3.2.1] - 2026-08-31\n"
        "\n"
        "### Added\n"
        "- Feature A\n"
        "\n"
        "### Fixed\n"
        "- Fix B");
    const QString formatted = WhatsNew::formatWhatsNewTitle(section, QStringLiteral("3.2.1"));
    const QString expected = QStringLiteral(
        "# What's New in FastMD 3.2.1\n"
        "\n"
        "### Added\n"
        "- Feature A\n"
        "\n"
        "### Fixed\n"
        "- Fix B");
    CHECK(formatted == expected);
    CHECK(!formatted.contains(QStringLiteral("2026-08-31")));
    CHECK(!formatted.contains(QStringLiteral("[3.2.1]")));
}

void testWhatsNewTitleRewriteDoesNotHardcodeVersion()
{
    const QString section = QStringLiteral("## [4.0.0] - 2027-01-01\n\n### Added\n- X");
    const QString formatted = WhatsNew::formatWhatsNewTitle(section, QStringLiteral("4.0.0"));
    CHECK(formatted.startsWith(QStringLiteral("# What's New in FastMD 4.0.0")));
}

void testWhatsNewTitleRewriteLeavesMalformedSectionUntouched()
{
    // Defensive: a section that does not start with the expected heading
    // shape (should not happen via extractChangelogSection(), but the
    // contract is "fail gracefully") is returned unchanged rather than
    // corrupted.
    const QString section = QStringLiteral("Not a heading\nmore text");
    const QString formatted = WhatsNew::formatWhatsNewTitle(section, QStringLiteral("3.2.1"));
    CHECK(formatted == section);
}

void testReleaseNotesExtractionUnaffectedByTitleRewrite()
{
    // Help > Release Notes renders the full, untouched changelog -- confirm
    // extraction alone (what that path uses) still yields the original
    // "## [version] - date" heading, never the What's New rewrite.
    const QString section = WhatsNew::extractChangelogSection(kSampleChangelog, QStringLiteral("3.2.1"));
    CHECK(section.startsWith(QStringLiteral("## [3.2.1] - 2026-08-31")));
}

} // namespace

int main()
{
    testFreshInstall();
    testNormalRelaunch();
    testUpgrade();
    testRelaunchAfterUpgrade();
    testMultiVersionJump();
    testUnknownPreviousVersionIsTreatedAsUpgrade();
    testChangelogExtraction();
    testChangelogExtractionMissingVersion();
    testChangelogExtractionDoesNotPrefixMatch();
    testChangelogExtractionLastSectionRunsToEnd();
    testWhatsNewExtractionIncludesSecondLatestSection();
    testWhatsNewExtractionKeepsGroupedVersionsTogether();
    testWhatsNewTitleRewrite();
    testWhatsNewTitleRewriteDoesNotHardcodeVersion();
    testWhatsNewTitleRewriteLeavesMalformedSectionUntouched();
    testReleaseNotesExtractionUnaffectedByTitleRewrite();

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}

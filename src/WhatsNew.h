#pragma once

#include <QString>

// Pure logic behind the automatic "What's New" tab: deciding whether the
// current launch is a fresh install, an upgrade, or a repeat launch of the
// same version, and extracting the single CHANGELOG.md section that matches
// the running version. No Qt Widgets, no file/QSettings I/O -- see
// tests/WhatsNewTest.cpp.
namespace WhatsNew {

enum class VersionTransition {
    FreshInstall, // Welcome.md has never been shown on this install.
    Upgrade,      // Welcome.md was already shown, and the previously
                  // recorded version differs from (or is unknown, predating
                  // this feature) the currently running version.
    SameVersion,  // Welcome.md was already shown, and the previously
                  // recorded version matches the currently running version.
};

// `welcomeAlreadyShown` is the persisted app/welcomeShown flag: it is the
// existing, reliable signal for "this install has launched FastMD before",
// which is what makes a fresh install distinguishable from an upgrade even
// though both can see an empty/missing `previousVersion`.
VersionTransition classifyVersionTransition(bool welcomeAlreadyShown,
                                            const QString& previousVersion,
                                            const QString& currentVersion);

// Extracts the complete section for exactly `version` from a
// CHANGELOG.md-shaped document. This remains the single-section primitive
// used by tests and callers that need one release only.
QString extractChangelogSection(const QString& changelog, const QString& version);

// Extracts the current release section and the next distinct release section
// for the automatic What's New view. If the current heading groups versions
// with '&' (for example `[3.3.1 & 3.3.0]`), that grouped section is returned
// as-is and is not followed by an older section. Returns empty when the
// current version is not present exactly in a heading's bracketed version
// list.
QString extractChangelogSections(const QString& changelog, const QString& version);

// Presentation-only rewrite for the automatic What's New tab: replaces the
// section's leading `## [version] - date` changelog heading with a
// user-facing `# What's New in FastMD <version>` title, leaving every other
// line (including the rest of that heading's own content) untouched. Must
// only be applied to the already-extracted current-version section, never to
// the full CHANGELOG.md shown by Help > Release Notes. If `section` does not
// start with the expected heading shape, it is returned unchanged -- the
// same "fail gracefully, show nothing broken" contract as extraction itself.
QString formatWhatsNewTitle(const QString& section, const QString& version);

} // namespace WhatsNew

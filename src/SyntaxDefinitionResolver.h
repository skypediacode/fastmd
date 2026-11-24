#pragma once
#include "DocumentModel.h"
#include <KSyntaxHighlighting/Definition>
#include <QString>
#include <QStringList>
#include <QVector>

// Single focused place that decides which KSyntaxHighlighting::Definition an
// editor should use, given a document's file path and its FastMD editor
// mode. Markdown mode always resolves to FastMD's own Markdown definition;
// Text mode resolves generically off the file name via the shared
// KSyntaxHighlighting::Repository (extension and exact-filename patterns,
// e.g. Dockerfile/Makefile/CMakeLists.txt, are already part of that
// database, so no separate extension map is maintained here). An invalid
// (default-constructed) Definition means "Plain Text" -- render with no
// highlighting.
namespace SyntaxDefinitionResolver {

KSyntaxHighlighting::Definition resolve(const QString& filePath, EditorMode mode);

bool isEditableTextFile(const QString& filePath);
// Bounded probe for UI affordances such as context menus. Full validation is
// still performed by isEditableTextFile() when the file is actually opened.
bool isLikelyEditableTextFile(const QString& filePath);

// Canonical name FastMD stores for its own Markdown definition -- the only
// Markdown definition ever selected or displayed.
QString fastMdMarkdownDefinitionName();

// Automatic-detection entry point for the language-selection feature:
// resolves purely off the file name (no EditorMode input), applying the two
// explicit normalization rules -- Markdown extensions always select
// "FastMD Markdown" (never the stock duplicate), and ".json" always selects
// "JSON" rather than the repository's higher-priority "JSONC" match. All
// other extensions/exact filenames fall through to the shared Repository's
// own filename-pattern matching. An invalid/empty return means Plain Text.
KSyntaxHighlighting::Definition autoDetect(const QString& filePath);

// Looks up a previously-stored, stable (untranslated) definition name.
// Returns an invalid Definition if the name is empty, unknown, or no longer
// exists in the repository (e.g. after a dependency upgrade removed it).
KSyntaxHighlighting::Definition definitionForStoredName(const QString& name);

// All valid, non-hidden definitions in the shared repository, excluding the
// stock/duplicate Markdown definition (FastMD's own "FastMD Markdown" is
// exposed separately as the single canonical Markdown choice).
QVector<KSyntaxHighlighting::Definition> availableDefinitions();

bool isMarkdownDefinition(const KSyntaxHighlighting::Definition& definition);
bool isMarkdownDefinitionName(const QString& name);

// Display name shown in the status button/popup: "Markdown" for FastMD's
// Markdown definition, "Plain Text" for an invalid/empty definition,
// otherwise the definition's own translated name.
QString displayName(const KSyntaxHighlighting::Definition& definition);
QString displayNameForStoredName(const QString& name);

// Deterministic default extension (with leading dot, e.g. ".cpp") to suggest
// for a stored definition name when saving an untitled document. Empty/Plain
// Text -> ".txt", Markdown -> ".md"; a handful of common languages have an
// explicit conventional suffix; everything else falls back to the first safe
// simple "*.ext" wildcard the definition advertises (excluding exact
// filenames, multi-wildcard patterns, and compound suffixes such as
// "*.fastq.gz"), preferring a lowercase pattern; ".txt" if none qualifies.
QString preferredSaveSuffix(const QString& definitionName);

// True if fileName is one of the definition's own patterns verbatim -- an
// exact filename match (e.g. "Dockerfile") or a case-insensitive match of a
// safe simple "*.ext" wildcard. Used so Save As doesn't append a suffix onto
// a filename that's already a recognized exact match for the language.
bool fileNameMatchesDefinition(const QString& fileName, const QString& definitionName);

// Broad, Windows-association-safe extension list (lowercase, leading dot,
// deduplicated, sorted) drawn from every visible, valid definition in the
// shared repository's safe simple "*.ext" patterns. Excludes hidden/invalid
// definitions, exact filenames, partial wildcards, and compound suffixes.
QStringList associableExtensions();

// Space-separated "*.ext" glob list (safe simple patterns only) advertised by
// a stored definition name, for building a Save As dialog filter. Empty for
// Plain Text/Markdown (callers use dedicated "*.txt"/"*.md ..." filters for
// those) or an unknown/invalid name.
QString saveFilterPatterns(const QString& definitionName);

} // namespace SyntaxDefinitionResolver

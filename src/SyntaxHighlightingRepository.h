#pragma once
#include <KSyntaxHighlighting/Repository>

// Process-wide KSyntaxHighlighting::Repository. Lazily constructed on first
// use (first Markdown editor shown), never per-tab/per-document. Loads
// FastMD's own Markdown definition/themes from disk and upstream per-language
// definitions from KSyntaxHighlighting's compiled-in resource index.
KSyntaxHighlighting::Repository& syntaxRepository();

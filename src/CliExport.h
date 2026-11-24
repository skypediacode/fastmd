#pragma once

#include "CliExportArguments.h"

namespace CliExport {

// Converts one Markdown file to PDF in this process and returns the process
// exit code (see ExitCode). Call right after QApplication is constructed and
// only when isRequested() is true: it must run before the single-instance
// mutex, launch counting, theme priming and MainWindow, and it never shows a
// window or writes settings.
int run(const QStringList& arguments);

}

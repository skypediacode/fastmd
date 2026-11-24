#pragma once
// Single source of truth for the file extensions the Workspace panel treats
// as writing files (see WorkspaceFileOps::isWritingFile()). The "Edit with
// FastMD" shell context-menu command (src/shellext/) is registered for all
// files, Notepad++-style, so it does not consume this list.
#define FASTMD_WRITING_EXTENSIONS(X) \
    X("md") X("markdown") X("mdown") X("mkd") X("txt") X("text")

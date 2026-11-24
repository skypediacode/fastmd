#pragma once

#include <QtCore/qnamespace.h>

// Item-data roles carried by every row in the Source Control panel's two trees
// (the file list and the graph). They live in one header because the panel is
// split across three translation units and each one needs the same numbering --
// two independently maintained copies silently disagreeing would be a very
// quiet bug.
namespace GitItemRoles {

constexpr int Kind    = Qt::UserRole + 1;
constexpr int Path    = Qt::UserRole + 2;
constexpr int Staged  = Qt::UserRole + 3;
constexpr int State   = Qt::UserRole + 4;
constexpr int Group   = Qt::UserRole + 5;
constexpr int Hash    = Qt::UserRole + 6;
constexpr int Subject = Qt::UserRole + 7;
constexpr int Lane    = Qt::UserRole + 8;
constexpr int Before  = Qt::UserRole + 9;
constexpr int After   = Qt::UserRole + 10;
constexpr int Loaded  = Qt::UserRole + 11;
constexpr int Date    = Qt::UserRole + 12;
constexpr int Author  = Qt::UserRole + 13;
constexpr int Parent  = Qt::UserRole + 14;
constexpr int Count   = Qt::UserRole + 15;
constexpr int Expanded = Qt::UserRole + 16;

}

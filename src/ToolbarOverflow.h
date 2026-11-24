#pragma once

class QToolBar;
class QToolButton;

namespace ToolbarOverflow {

QToolButton* extensionButton(QToolBar* toolbar);
void installMenu(QToolBar* toolbar, QToolButton* button);
void updateExtensionButton(QToolButton* button, bool dark, int iconSize);

}

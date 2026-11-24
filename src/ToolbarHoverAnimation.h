#pragma once

class QWidget;

namespace ToolbarHoverAnimation {

// radius must match the border-radius QSS gives the toolbar's buttons, so the
// hover fill has the same shape as their :pressed and :checked fills. The
// one-argument form uses ThemeCard::ControlRadius.
void install(QWidget* toolbar);
void install(QWidget* toolbar, int radius);
void setHovered(QWidget* button, bool hovered);

}

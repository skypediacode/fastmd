#pragma once

namespace QtGpuProbe {

// Must run after the QApplication constructor has loaded the Windows platform
// plugin and before the first top-level window is created. Returns whether the
// probe was found and disabled.
bool suppress();

}

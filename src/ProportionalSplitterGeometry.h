#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

// A splitter handle sits after its first pane. Keep positions in one caller-
// supplied reference domain so nested splitters with different chrome above
// them can retain a common screen Y while remaining otherwise independent.
namespace ProportionalSplitterGeometry {

inline int availableHeight(int height, int handleWidth)
{
    return std::max(0, height - handleWidth);
}

inline std::optional<double> ratioForPosition(int position, int available)
{
    if (available <= 0)
        return std::nullopt;
    return std::clamp(static_cast<double>(position) / available, 0.0, 1.0);
}

inline int positionForRatio(double ratio, int available)
{
    return std::lround(std::clamp(ratio, 0.0, 1.0) * std::max(0, available));
}

inline int firstPaneSizeForReferencePosition(int referenceTop, int referencePosition,
                                             int localTop)
{
    return referenceTop + referencePosition - localTop;
}

} // namespace ProportionalSplitterGeometry

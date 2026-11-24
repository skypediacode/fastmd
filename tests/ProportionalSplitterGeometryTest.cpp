#include "ProportionalSplitterGeometry.h"

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

int outlineHandleY(int editorTop, int editorHeight, int editorHandle,
                   int workspaceSplitterTop, double ratio)
{
    const int referencePosition = ProportionalSplitterGeometry::positionForRatio(
        ratio, ProportionalSplitterGeometry::availableHeight(editorHeight, editorHandle));
    return workspaceSplitterTop + ProportionalSplitterGeometry::firstPaneSizeForReferencePosition(
        editorTop, referencePosition, workspaceSplitterTop);
}

void testAlignedHandlesRemainAlignedAcrossResizes()
{
    constexpr int editorTop = 100;
    constexpr int workspaceSplitterTop = 212; // Workspace chrome is independent.
    constexpr int handle = 4;
    constexpr double ratio = 0.61;
    for (const int height : { 620, 980, 1440 }) {
        const int terminalY = editorTop + ProportionalSplitterGeometry::positionForRatio(
            ratio, ProportionalSplitterGeometry::availableHeight(height, handle));
        CHECK(outlineHandleY(editorTop, height, handle, workspaceSplitterTop, ratio) == terminalY);
    }
}

void testIndependentPositionsRemainIndependent()
{
    constexpr int editorTop = 100;
    constexpr int workspaceSplitterTop = 212;
    constexpr int handle = 4;
    constexpr double outlineRatio = 0.42;
    constexpr double terminalRatio = 0.73;
    for (const int height : { 620, 980, 1440 }) {
        const int outlineY = outlineHandleY(editorTop, height, handle, workspaceSplitterTop,
                                            outlineRatio);
        const int terminalY = editorTop + ProportionalSplitterGeometry::positionForRatio(
            terminalRatio, ProportionalSplitterGeometry::availableHeight(height, handle));
        CHECK(outlineY != terminalY);
    }
}

} // namespace

int main()
{
    testAlignedHandlesRemainAlignedAcrossResizes();
    testIndependentPositionsRemainIndependent();
    return g_failures == 0 ? 0 : 1;
}

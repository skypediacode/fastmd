#include "QtGpuProbe.h"

#include <QApplication>
#include <QWidget>

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

// A Qt upgrade that stops importing Direct3DCreate9 by name (delay-load,
// GetProcAddress, a renamed plugin) silently brings the ~100 ms driver load
// back into startup; fail here instead.
void testProbeIsFoundAndDisabled()
{
    CHECK(QtGpuProbe::suppress());
    CHECK(QtGpuProbe::suppress());

    QWidget top;
    top.setGeometry(-4000, -4000, 200, 100);
    CHECK(top.winId() != 0);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testProbeIsFoundAndDisabled();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

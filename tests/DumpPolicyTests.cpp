#include "core/DumpPolicy.h"

int main() {
    using MCP::DumpPolicy::ShouldTryPackedOriginalFallback;

    if (!ShouldTryPackedOriginalFallback(true, false, false, false)) {
        return 1;
    }
    if (ShouldTryPackedOriginalFallback(true, false, false, true)) {
        return 2;
    }
    if (ShouldTryPackedOriginalFallback(false, false, false, false) ||
        ShouldTryPackedOriginalFallback(true, true, false, false) ||
        ShouldTryPackedOriginalFallback(true, false, true, false)) {
        return 3;
    }

    return 0;
}

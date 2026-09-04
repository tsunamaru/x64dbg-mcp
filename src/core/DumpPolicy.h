#pragma once

namespace MCP {
namespace DumpPolicy {

inline bool ShouldTryPackedOriginalFallback(bool isPackedImage,
                                            bool autoDetectOEP,
                                            bool hasResolvedOEP,
                                            bool dumpFullImage) {
    return isPackedImage && !autoDetectOEP && !hasResolvedOEP &&
           !dumpFullImage;
}

} // namespace DumpPolicy
} // namespace MCP

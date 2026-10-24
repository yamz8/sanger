#pragma once

#include <QColor>

// The four bases in the colors chromatograms always use, with G's black made
// light so it shows on a dark window.
inline QColor baseColor(char base) {
    switch (base) {
    case 'A': return QColor("#5fd068");
    case 'C': return QColor("#5aa9ff");
    case 'G': return QColor("#d4d4d8");
    case 'T': return QColor("#ff5c5c");
    default: return QColor("#8a8a94");
    }
}

inline QColor channelColor(int channel) { return baseColor("ACGT"[channel]); }

namespace Palette {
inline const QColor Text("#e4e4e8");
inline const QColor Dim("#6e6e78");
inline const QColor Faint("#3a3a42");
inline const QColor Row("#1d1d23");
inline const QColor Shade(0, 0, 0, 110);
inline const QColor Problem("#ff6b6b");
}

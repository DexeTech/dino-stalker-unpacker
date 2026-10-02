#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// A "P2IG" image block.
//
//   0x00 "P2IG", u32 0x61, u32 0, u32 type (0x1000x; bit 3: pixels are swizzled)
//   0x10 char name[8], 8 bytes (hash?)
//   0x20 u16 log2 width, u16 log2 height, u32 pixel format (GS PSM)
//   0x40 u32 palette offset, u32 palette size, u32 pixel offset, u32 pixel size
//
// Offsets are relative to the block start, and the block ends at pixel offset +
// pixel size. Pixel formats (low byte, GS PSM):
//   0x00 32-bit RGBA, 0x01 24-bit RGB, 0x02 16-bit RGBA5551,
//   0x13 8-bit indexed, 0x14 4-bit indexed.
// Bits 8+ give the palette format: 0 = 32-bit RGBA, 2 = 16-bit RGBA5551.
// Pixels are stored linearly, unless type bit 3 is set: then they are in the order
// the GS keeps them in memory (the enemy textures), so the game can upload them as
// 32-bit data with a fast transfer. 8-bit palettes use the GS CSM1 layout, so entries
// 8-15 and 16-23 of every 32 are swapped. Alpha 0x80 is fully opaque.
struct P2Image {
    QString name;
    qint64 offset = 0;
    qint64 size = 0;
    int width = 0;
    int height = 0;
    quint32 type = 0;
    quint32 format = 0;
    quint32 paletteOffset = 0, paletteSize = 0, pixelOffset = 0, pixelSize = 0;

    static bool parse(const QByteArray &data, qint64 offset, P2Image &image, QString *error = nullptr);
    QImage decode(const QByteArray &data, QString *error = nullptr) const;
    QString formatName() const;
    bool swizzled() const { return type & 0x8; }
};

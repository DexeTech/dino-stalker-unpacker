#pragma once

#include <QByteArray>

// Decompresses the DATA/PACK/*.PAK files.
//
// Layout: u32 decompressed size, then LZSS. Each flag byte covers 8 items, LSB
// first; a set bit is a literal byte, a clear bit a 2-byte back-reference:
//   length   = (b0 & 0x1F) + 3
//   distance = (b0 >> 5) | (b1 << 3)     (bytes back from the current output)
// References before the start of the output read zeros.
namespace Lzss {
bool looksCompressed(const QByteArray &data);
QByteArray decompress(const QByteArray &data, QString *error = nullptr);
}

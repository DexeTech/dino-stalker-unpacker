#include "P2Image.h"
#include "Binary.h"

#include <QVector>

namespace {

int scaleAlpha(int alpha) {
    return alpha >= 0x80 ? 255 : alpha * 2;
}

QRgb rgba5551(quint16 v) {
    const int r = (v & 0x1F) << 3, g = ((v >> 5) & 0x1F) << 3, b = ((v >> 10) & 0x1F) << 3;
    return qRgba(r | r >> 5, g | g >> 5, b | b >> 5, (v & 0x8000) ? 255 : 0);
}

// GS CSM1 palette order: swap entries 8-15 with 16-23 in each block of 32.
int csm1(int i) {
    const int block = i & 0x18;
    if (block == 0x08) return (i & ~0x18) | 0x10;
    if (block == 0x10) return (i & ~0x18) | 0x08;
    return i;
}

int bitsPerPixel(quint32 psm) {
    switch (psm & 0xFF) {
    case 0x00: return 32;
    case 0x01: return 24;
    case 0x02: return 16;
    case 0x13: return 8;
    case 0x14: return 4;
    default: return 0;
    }
}

// Where pixel (x, y) of a swizzled image is stored. An 8-bit texture is uploaded as
// 32-bit data of half the width and height, so its pixels sit in GS PSMCT32 order
// read back as PSMT8. Returns a byte index.
qint64 swizzled8(int x, int y, int width) {
    const int block = (y & ~0xF) * width + (x & ~0xF) * 2;
    const int swap = (((y + 2) >> 2) & 1) * 4;
    const int row = (((y & ~3) >> 1) + (y & 1)) & 7;
    const int column = row * width * 2 + ((x + swap) & 7) * 4;
    const int byte = ((y >> 1) & 1) + ((x >> 2) & 2);
    return qint64(block) + column + byte;
}

// The same for a 4-bit texture (PSMCT32 read back as PSMT4). Returns a nibble index.
qint64 swizzled4(int x, int y, int width, int height) {
    const int pagesWide = (width + 127) / 128, pagesHigh = (height + 127) / 128;
    const int pageNumber = ((y & ~0x7F) / 128) * pagesWide + (x & ~0x7F) / 128;
    const qint64 page = qint64((pageNumber / pagesHigh) * 32) * height * 2 + (pageNumber % pagesHigh) * 64 * 4;
    const int localX = x & 0x7F, localY = y & 0x7F;
    const int block = ((localX & ~0x1F) >> 1) * height + (localY & ~0xF) * 2;
    const int swap = (((y + 2) >> 2) & 1) * 4;
    const int row = (((y & ~3) >> 1) + (y & 1)) & 7;
    const int column = row * height * 2 + ((x + swap) & 7) * 4;
    const int byte = (x >> 3) & 3;
    return (page + block + column + byte) * 2 + ((y >> 1) & 1);
}

} // namespace

bool P2Image::parse(const QByteArray &data, qint64 offset, P2Image &image, QString *error) {
    Reader r(data);
    if (!r.matches(offset, "P2IG") || !r.has(offset, 0x50)) {
        if (error) *error = "Not a P2IG block.";
        return false;
    }
    image.offset = offset;
    image.type = r.u32(offset + 0x0C);
    image.name = r.name(offset + 0x10, 8);
    const int log2w = r.u16(offset + 0x20), log2h = r.u16(offset + 0x22);
    image.format = r.u32(offset + 0x24);
    image.paletteOffset = r.u32(offset + 0x40);
    image.paletteSize = r.u32(offset + 0x44);
    image.pixelOffset = r.u32(offset + 0x48);
    image.pixelSize = r.u32(offset + 0x4C);
    if (log2w > 12 || log2h > 12) {
        if (error) *error = "Image dimensions are out of range.";
        return false;
    }
    image.width = 1 << log2w;
    image.height = 1 << log2h;
    image.size = qint64(image.pixelOffset) + image.pixelSize;
    const int bpp = bitsPerPixel(image.format);
    if (bpp == 0) {
        if (error) *error = QString("Unknown pixel format 0x%1.").arg(image.format, 0, 16);
        return false;
    }
    if (qint64(image.width) * image.height * bpp / 8 > image.pixelSize || !r.has(offset, image.size)
        || (image.paletteSize && !r.has(offset + image.paletteOffset, image.paletteSize))) {
        if (error) *error = "Image data is larger than its block.";
        return false;
    }
    return true;
}

QString P2Image::formatName() const {
    switch (format & 0xFF) {
    case 0x00: return "32-bit";
    case 0x01: return "24-bit";
    case 0x02: return "16-bit";
    case 0x13: return "8-bit indexed";
    case 0x14: return "4-bit indexed";
    default: return "unknown";
    }
}

QImage P2Image::decode(const QByteArray &data, QString *error) const {
    Reader r(data);
    const quint32 psm = format & 0xFF;
    const bool palette16 = ((format >> 8) & 0xFF) == 2;
    QVector<QRgb> palette;
    if (psm == 0x13 || psm == 0x14) {
        const int entries = palette16 ? int(paletteSize / 2) : int(paletteSize / 4);
        const int needed = psm == 0x13 ? 256 : 16;
        if (entries < needed) {
            if (error) *error = "Palette is too small.";
            return QImage();
        }
        QVector<QRgb> raw(entries);
        for (int i = 0; i < entries; ++i) {
            const qint64 p = offset + paletteOffset + (palette16 ? i * 2 : i * 4);
            raw[i] = palette16 ? rgba5551(r.u16(p))
                               : qRgba(r.u8(p), r.u8(p + 1), r.u8(p + 2), scaleAlpha(r.u8(p + 3)));
        }
        palette.resize(needed);
        for (int i = 0; i < needed; ++i) {
            palette[i] = raw[psm == 0x13 ? csm1(i) : i];
        }
    }

    const bool unswizzle = swizzled() && (psm == 0x13 || psm == 0x14);
    if (unswizzle && (width < 16 || height < 4 || (psm == 0x14 && (width < 32 || height < 16)))) {
        if (error) *error = "Swizzled image is too small to unswizzle.";
        return QImage();
    }
    QImage image(width, height, QImage::Format_ARGB32);
    const qint64 base = offset + pixelOffset;
    const uchar *px = reinterpret_cast<const uchar *>(data.constData()) + base;
    const qint64 pixels = qint64(width) * height;
    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            qint64 i = qint64(y) * width + x;
            if (unswizzle) {
                i = psm == 0x13 ? swizzled8(x, y, width) : swizzled4(x, y, width, height);
                if (i < 0 || i >= pixels) {
                    if (error) *error = "Swizzled pixel is outside the image.";
                    return QImage();
                }
            }
            switch (psm) {
            case 0x14: line[x] = palette[(px[i >> 1] >> ((i & 1) * 4)) & 0xF]; break;
            case 0x13: line[x] = palette[px[i]]; break;
            case 0x00: line[x] = qRgba(px[i * 4], px[i * 4 + 1], px[i * 4 + 2], scaleAlpha(px[i * 4 + 3])); break;
            case 0x01: line[x] = qRgb(px[i * 3], px[i * 3 + 1], px[i * 3 + 2]); break;
            case 0x02: line[x] = rgba5551(quint16(px[i * 2] | px[i * 2 + 1] << 8)); break;
            }
        }
    }
    return image;
}

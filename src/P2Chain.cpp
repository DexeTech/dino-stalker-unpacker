#include "P2Chain.h"
#include "Binary.h"

namespace {

// Tries to parse a block at offset; returns its size, or 0 if there is none.
qint64 tryBlock(const QByteArray &data, qint64 offset, P2Chain &chain, P2Block::Type &type) {
    Reader r(data);
    if (r.matches(offset, "P2IG")) {
        P2Image image;
        if (P2Image::parse(data, offset, image)) {
            type = P2Block::Image;
            chain.images.append(image);
            return image.size;
        }
    } else if (r.matches(offset, "P2OD")) {
        P2Model model;
        if (P2Model::parse(data, offset, model)) {
            type = P2Block::Model;
            chain.models.append(model);
            return model.size;
        }
    } else if (r.matches(offset, "P2MT")) {
        P2Motion motion;
        if (P2Motion::parse(data, offset, motion)) {
            type = P2Block::Motion;
            chain.motions.append(motion);
            return motion.size;
        }
    }
    return 0;
}

int countOf(const P2Chain &chain, P2Block::Type type) {
    switch (type) {
    case P2Block::Image: return chain.images.size() - 1;
    case P2Block::Model: return chain.models.size() - 1;
    case P2Block::Motion: return chain.motions.size() - 1;
    default: return 0;
    }
}

} // namespace

bool P2Chain::startsWithBlock(const QByteArray &data) {
    Reader r(data);
    return r.matches(0, "P2IG") || r.matches(0, "P2OD") || r.matches(0, "P2MT");
}

P2Chain P2Chain::walk(const QByteArray &data) {
    P2Chain chain;
    qint64 pos = 0;
    qint64 unknownStart = -1;
    auto closeUnknown = [&](qint64 end) {
        if (unknownStart >= 0 && end > unknownStart) {
            // Zero padding between blocks is not worth reporting.
            bool zero = true;
            for (qint64 i = unknownStart; i < end && zero; ++i) {
                zero = data[int(i)] == 0;
            }
            if (!zero) {
                chain.blocks.append({P2Block::Unknown, unknownStart, end - unknownStart, 0});
            }
        }
        unknownStart = -1;
    };
    while (pos < data.size()) {
        P2Block::Type type = P2Block::Unknown;
        const qint64 size = tryBlock(data, pos, chain, type);
        if (size > 0) {
            closeUnknown(pos);
            chain.blocks.append({type, pos, size, countOf(chain, type)});
            pos += size;
            continue;
        }
        if (unknownStart < 0) {
            unknownStart = pos;
        }
        // Blocks start on 16-byte boundaries.
        pos = (pos / 16 + 1) * 16;
        const qint64 next = data.indexOf("P2", pos);
        if (next < 0) {
            pos = data.size();
        } else {
            pos = (next + 15) / 16 * 16;
            if (next % 16 != 0) {
                continue;
            }
            pos = next;
        }
    }
    closeUnknown(data.size());
    return chain;
}

qint64 P2Chain::unknownBytes() const {
    qint64 total = 0;
    for (const P2Block &block : blocks) {
        if (block.type == P2Block::Unknown) total += block.size;
    }
    return total;
}

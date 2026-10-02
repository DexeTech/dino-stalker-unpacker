#pragma once

#include "P2Image.h"
#include "P2Model.h"
#include "P2Motion.h"

#include <QByteArray>
#include <QVector>

// Most game files are back-to-back P2IG/P2OD/P2MT blocks with no index. Packs and
// level files also contain other data between or after the blocks; the walker skips
// it by searching for the next valid block on a 16-byte boundary.
struct P2Block {
    enum Type { Image, Model, Motion, Unknown };
    Type type = Unknown;
    qint64 offset = 0;
    qint64 size = 0;
    int index = 0;   // index among blocks of the same type
};

struct P2Chain {
    QVector<P2Block> blocks;
    QVector<P2Image> images;
    QVector<P2Model> models;
    QVector<P2Motion> motions;

    static bool startsWithBlock(const QByteArray &data);
    static P2Chain walk(const QByteArray &data);
    qint64 unknownBytes() const;
};

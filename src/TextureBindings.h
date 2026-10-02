#pragma once

#include <QString>
#include <QVector>

// Which images a file's models use, for the files where the models do not say.
// Model texture indices are local; the game's set-up code binds them to the file's
// images. This table holds those bindings for the European release (see the .cpp).
struct TextureBinding {
    const char *file;     // file name, e.g. "ST2_BIN.BIN"
    qint64 fileSize;      // the table is only used when the size matches exactly
    int model;            // index among the file's P2OD blocks
    int local;            // the model's texture index; -1: index k uses image + k
    int image;            // index among the file's P2IG blocks
};

bool hasTextureBindings(const QString &fileName, qint64 fileSize);
QVector<TextureBinding> textureBindings(const QString &fileName, qint64 fileSize);

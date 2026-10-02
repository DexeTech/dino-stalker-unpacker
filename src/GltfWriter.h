#pragma once

#include "P2Model.h"
#include "P2Motion.h"

#include <QImage>
#include <QString>
#include <QVector>

// Writes a P2OD model as a self-contained glTF 2.0 file: buffers and PNG textures are
// embedded as data URIs.
//
// Every part becomes a node with its rest transform. Models that are skinned or have
// motions get a skin (one joint per part, each vertex bound 100% to its part, as the
// game does); static models get one mesh per part node instead. A root node turns the
// game's -Y-up axes into glTF's +Y-up (180 degrees about X) and applies the scale.
struct GltfOptions {
    double framesPerSecond = 60.0;
    double scale = 1.0;
};

struct GltfMotion {
    const P2Motion *motion = nullptr;
    QString name;
};

class GltfWriter {
public:
    // textures[i] is the image for the model's texture index i (null if unknown).
    static bool write(const QString &fileName, const QString &modelName, const P2Model &model,
                      const QVector<const QImage *> &textures, const QVector<GltfMotion> &motions,
                      const GltfOptions &options, QString *error = nullptr);
};

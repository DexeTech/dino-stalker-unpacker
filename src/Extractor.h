#pragma once

#include "GltfWriter.h"
#include "P2Chain.h"

#include <QHash>
#include <map>
#include <QString>
#include <QStringList>

struct ExtractOptions {
    QString outputDir;           // empty: next to each input
    bool animations = true;
    bool joinMotions = true;     // also export the motions the game plays back to back as one
    bool movies = true;
    bool saveDiscFiles = true;   // when reading a disc image
    GltfOptions gltf;
};

class Extractor {
public:
    explicit Extractor(const ExtractOptions &options) : options(options) {}

    // Accepts a disc image, a folder (searched recursively) or a single file.
    // Returns the number of failures.
    int process(const QString &path);

private:
    // Motion files (".../PMT/*PMT.BIN" and friends), parsed lazily by path.
    struct MotionFile {
        QString name;
        QByteArray data;
        QVector<P2Motion> motions;
        bool loaded = false;
    };

    int processDisc(const QString &isoPath);
    int processLooseFile(const QString &filePath, const QString &outputRoot);
    // name: path inside the disc or file name; outDir: folder for this file's output.
    bool processData(const QByteArray &data, const QString &name, const QString &outDir, bool fromDisc);
    bool exportChain(const QByteArray &data, const QString &name, const QString &outDir);
    // joinedNames: the names of the joined animations among them.
    QVector<GltfMotion> motionsFor(const QString &name, const P2Chain &chain, const P2Model &model,
                                   QStringList *joinedNames);
    // Appends the chains among motions[indices] (see MotionChains.h) joined into one each,
    // named "<prefix>_54+55+56" after the motions' indices.
    void addJoinedMotions(QVector<GltfMotion> &result, const QVector<P2Motion> &motions,
                          const QVector<int> &indices, const QString &prefix, QStringList *names);
    void registerMotionFile(const QString &key, const QString &name, const QByteArray &data);
    MotionFile *motionFile(const QString &key);

    ExtractOptions options;
    std::map<QString, MotionFile> motionFiles;   // key: path; std::map keeps entries in place
    QVector<P2Motion> joinedMotions;          // the joined motions of the model being written
    QString currentSourceDir;                 // for loose files: the folder of the input
    int failures = 0;
    bool quietUnsupported = false;   // folders: count unsupported files instead of warning
    int skippedUnsupported = 0;
};

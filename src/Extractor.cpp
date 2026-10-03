#include "Extractor.h"
#include "Iso9660.h"
#include "Lzss.h"
#include "Movie.h"
#include "EnemySizes.h"
#include "MotionChains.h"
#include "SoundBank.h"
#include "TextureBindings.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <iostream>

namespace {

void info(const QString &text) { std::cout << "[INFO] " << text.toStdString() << std::endl; }
void warn(const QString &text) { std::cout << "[WARNING] " << text.toStdString() << std::endl; }
void fail(const QString &text) { std::cout << "[ERROR] " << text.toStdString() << std::endl; }

QString baseName(const QString &name) {
    return QFileInfo(name).completeBaseName();
}

// Characters that are unsafe in file names on Windows.
QString safe(QString text) {
    static const QString bad = "<>:\"/\\|?*";
    for (QChar &c : text) {
        if (bad.contains(c) || c.unicode() < 32) c = '_';
    }
    return text.isEmpty() ? QString("_") : text;
}

bool writeFile(const QString &fileName, const QByteArray &data) {
    QDir().mkpath(QFileInfo(fileName).absolutePath());
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(data) == data.size();
}

int commonPrefix(const QString &a, const QString &b) {
    int n = 0;
    while (n < a.size() && n < b.size() && a[n] == b[n]) ++n;
    return n;
}

const char *typeName(P2Block::Type type) {
    switch (type) {
    case P2Block::Image: return "image";
    case P2Block::Model: return "model";
    case P2Block::Motion: return "motion";
    default: return "unknown data";
    }
}

// Files this tool writes, so that converting a folder again skips earlier output.
bool isToolOutput(const QString &relativePath) {
    const QStringList parts = QDir::fromNativeSeparators(relativePath).split('/');
    for (int i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i].endsWith("_extracted") || parts[i].endsWith(" (extracted)")) return true;
    }
    static const QStringList outputs{"png", "gltf", "wav", "m2v", "txt", "vab", "vh", "sq", "hd", "bd"};
    const QString name = parts.last();
    return outputs.contains(QFileInfo(name).suffix().toLower()) || name.endsWith(".decompressed.bin", Qt::CaseInsensitive);
}

} // namespace

int Extractor::process(const QString &path) {
    failures = 0;
    QFileInfo fi(path);
    if (!fi.exists()) {
        fail(QString("%1 does not exist.").arg(path));
        return 1;
    }
    if (fi.isDir()) {
        const QString root = options.outputDir.isEmpty() ? fi.absoluteFilePath() + "_extracted"
                                                         : QDir(options.outputDir).filePath(fi.fileName());
        QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
        QStringList files;
        while (it.hasNext()) {
            const QString file = it.next();
            if (!isToolOutput(QDir(fi.absoluteFilePath()).relativeFilePath(file))) files << file;
        }
        files.sort();
        quietUnsupported = true;
        for (const QString &file : files) {
            const QString relative = QDir(fi.absoluteFilePath()).relativeFilePath(QFileInfo(file).absolutePath());
            processLooseFile(file, QDir(root).filePath(relative));
        }
        if (skippedUnsupported) {
            info(QString("%1 other file(s) in the folder are not formats this tool converts.").arg(skippedUnsupported));
        }
        return failures;
    }
    if (Iso9660::isIsoImage(path)) {
        return processDisc(path);
    }
    const QString root = options.outputDir.isEmpty() ? fi.absolutePath() : options.outputDir;
    processLooseFile(fi.absoluteFilePath(), root);
    return failures;
}

int Extractor::processDisc(const QString &isoPath) {
    Iso9660 iso;
    if (!iso.open(isoPath)) {
        fail(QString("%1: %2").arg(isoPath, iso.errorString()));
        return 1;
    }
    QFileInfo fi(isoPath);
    const QString root = options.outputDir.isEmpty() ? fi.absolutePath() + "/" + fi.completeBaseName() + " (extracted)"
                                                     : options.outputDir;
    info(QString("Disc image: %1 (volume %2, %3 files)").arg(fi.fileName(), iso.volumeName()).arg(iso.files().size()));
    info(QString("Output: %1").arg(QDir::toNativeSeparators(root)));

    // Motion files are needed before the models that use them.
    for (const auto &entry : iso.files()) {
        if (entry.path.contains("/PMT/", Qt::CaseInsensitive)) {
            registerMotionFile(entry.path, entry.path, iso.read(entry));
        }
    }
    for (const auto &entry : iso.files()) {
        const QByteArray data = iso.read(entry);
        if (data.size() != qint64(entry.size)) {
            fail(QString("%1: could not read from the disc image.").arg(entry.path));
            ++failures;
            continue;
        }
        if (options.saveDiscFiles && !writeFile(root + "/disc/" + entry.path, data)) {
            fail(QString("%1: could not write the file.").arg(entry.path));
            ++failures;
            continue;
        }
        QString dir = entry.path;
        dir.chop(QFileInfo(entry.path).suffix().size() + (QFileInfo(entry.path).suffix().isEmpty() ? 0 : 1));
        if (!processData(data, entry.path, root + "/converted/" + dir, true)) {
            ++failures;
        }
    }
    info(QString("Finished with %1 failure(s).").arg(failures));
    return failures;
}

int Extractor::processLooseFile(const QString &filePath, const QString &outputRoot) {
    QFileInfo fi(filePath);
    const QString outDir = QDir(outputRoot).filePath(fi.completeBaseName() + "_" + fi.suffix());
    currentSourceDir = fi.absolutePath();
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(QString("%1: %2").arg(filePath, f.errorString()));
        ++failures;
        return failures;
    }
    const QByteArray data = f.readAll();
    if (!processData(data, fi.fileName(), outDir, false)) {
        ++failures;
    }
    return failures;
}

bool Extractor::processData(const QByteArray &data, const QString &name, const QString &outDir, bool fromDisc) {
    const QString file = QFileInfo(name).fileName();
    const QString suffix = QFileInfo(name).suffix().toUpper();

    if (suffix == "PSS") {
        if (!options.movies) return true;
        QDir().mkpath(outDir);
        QString summary, error;
        const bool ok = Movie::split(data, outDir + "/" + baseName(name) + ".m2v", outDir + "/" + baseName(name) + ".wav",
                                     &summary, &error);
        if (!ok) {
            fail(QString("%1: %2").arg(name, error));
            return false;
        }
        info(QString("%1: movie split into video and audio (%2)").arg(name, summary));
        return true;
    }
    if (SoundBank::isSoundBin(data)) {
        QStringList messages;
        QString error;
        if (!SoundBank::extract(data, outDir, messages, &error)) {
            fail(QString("%1: %2").arg(name, error));
            return false;
        }
        for (const QString &m : messages) info(QString("%1: %2").arg(file, m));
        return true;
    }
    if (suffix == "PAK" && Lzss::looksCompressed(data)) {
        QString error;
        const QByteArray unpacked = Lzss::decompress(data, &error);
        if (unpacked.isEmpty()) {
            fail(QString("%1: %2").arg(name, error));
            return false;
        }
        writeFile(outDir + "/" + baseName(name) + ".decompressed.bin", unpacked);
        if (fromDisc) {
            // On the disc every file in a pack also exists on its own, and those are
            // converted separately.
            info(QString("%1: decompressed (%2 bytes); its contents are the disc's own files")
                     .arg(name).arg(unpacked.size()));
            return true;
        }
        info(QString("%1: decompressed (%2 bytes)").arg(name).arg(unpacked.size()));
        return exportChain(unpacked, name, outDir);
    }
    if (QRegularExpression(R"(^ST\d_GND\.BIN$)", QRegularExpression::CaseInsensitiveOption).match(file).hasMatch()) {
        // The ground files are copies of the start of the matching ST*_BIN.BIN, whose
        // export includes these models with their textures.
        info(QString("%1: same data as the start of %2, which is converted with its textures")
                 .arg(file, QString(file).replace("_GND", "_BIN", Qt::CaseInsensitive)));
        return true;
    }
    if (P2Chain::startsWithBlock(data)) {
        return exportChain(data, name, outDir);
    }
    if (!fromDisc) {
        if (quietUnsupported) ++skippedUnsupported;
        else warn(QString("%1: not a format this tool converts.").arg(file));
    }
    return true;
}

void Extractor::registerMotionFile(const QString &key, const QString &name, const QByteArray &data) {
    MotionFile &mf = motionFiles[key];
    mf.name = baseName(name);
    mf.data = data;
    mf.loaded = false;
}

Extractor::MotionFile *Extractor::motionFile(const QString &key) {
    auto it = motionFiles.find(key);
    if (it == motionFiles.end()) return nullptr;
    MotionFile &mf = it->second;
    if (!mf.loaded) {
        const P2Chain chain = P2Chain::walk(mf.data);
        mf.motions = chain.motions;
        mf.loaded = true;
    }
    return &mf;
}

void Extractor::addJoinedMotions(QVector<GltfMotion> &result, const QVector<P2Motion> &motions,
                                 const QVector<int> &indices, const QString &prefix, QStringList *names) {
    joinedMotions.clear();
    if (!options.joinMotions) return;
    QVector<const P2Motion *> candidates;
    for (int i : indices) candidates.append(&motions[i]);
    QStringList chainNames;
    for (const QVector<int> &chain : findMotionChains(candidates)) {
        QVector<const P2Motion *> parts;
        QStringList numbers;
        for (int c : chain) {
            parts.append(candidates[c]);
            numbers << QString("%1").arg(indices[c], 2, 10, QChar('0'));
        }
        joinedMotions.append(joinMotions(parts));
        chainNames << QString("%1_%2").arg(prefix, numbers.join('+'));
    }
    // joinedMotions is complete, so the pointers stay valid until the next model.
    for (int k = 0; k < joinedMotions.size(); ++k) result.append({&joinedMotions[k], chainNames[k]});
    if (names) *names += chainNames;
}

QVector<GltfMotion> Extractor::motionsFor(const QString &name, const P2Chain &chain, const P2Model &model,
                                          QStringList *joinedNames) {
    QVector<GltfMotion> result;
    if (!options.animations) return result;
    const int parts = model.parts.size();
    // Motions stored in the same file.
    QVector<int> indices;
    for (int i = 0; i < chain.motions.size(); ++i) {
        if (chain.motions[i].bones.size() == parts) {
            result.append({&chain.motions[i], QString("%1_motion_%2").arg(baseName(name)).arg(i, 2, 10, QChar('0'))});
            indices.append(i);
        }
    }
    if (!result.isEmpty()) {
        addJoinedMotions(result, chain.motions, indices, baseName(name) + "_motion", joinedNames);
        return result;
    }

    // Separate motion files only exist for the enemies (ENEMYDT/PMT).
    if (currentSourceDir.isEmpty() && !name.contains("ENEMYDT/", Qt::CaseInsensitive)) return result;
    // Loose files: look in the same folder and a sibling PMT folder.
    if (!currentSourceDir.isEmpty()) {
        QStringList dirs{currentSourceDir, currentSourceDir + "/../PMT"};
        for (const QString &dir : dirs) {
            QDir d(dir);
            for (const QString &f : d.entryList({"*PMT.BIN"}, QDir::Files)) {
                const QString key = QFileInfo(d.filePath(f)).absoluteFilePath();
                if (motionFiles.find(key) == motionFiles.end()) {
                    QFile file(key);
                    if (file.open(QIODevice::ReadOnly)) registerMotionFile(key, f, file.readAll());
                }
            }
        }
    }
    // Pick the file whose bone count matches, preferring the longest shared name prefix
    // (TREXMD -> TREXPMT). A single match is used even without a shared prefix.
    const QString model0 = baseName(name).toUpper();
    MotionFile *best = nullptr;
    int bestPrefix = -1, matches = 0;
    for (auto &entry : motionFiles) {
        MotionFile *mf = motionFile(entry.first);
        if (!mf || mf->motions.isEmpty() || mf->motions.first().bones.size() != parts) continue;
        ++matches;
        const int prefix = commonPrefix(model0, mf->name.toUpper());
        if (prefix > bestPrefix) {
            bestPrefix = prefix;
            best = mf;
        }
    }
    if (best && (bestPrefix >= 2 || matches == 1)) {
        for (int i = 0; i < best->motions.size(); ++i) {
            if (best->motions[i].bones.size() == parts) {
                result.append({&best->motions[i], QString("%1_%2").arg(best->name).arg(i, 2, 10, QChar('0'))});
                indices.append(i);
            }
        }
        addJoinedMotions(result, best->motions, indices, best->name, joinedNames);
    }
    return result;
}

bool Extractor::exportChain(const QByteArray &data, const QString &name, const QString &outDir) {
    const QString file = QFileInfo(name).fileName();
    const P2Chain chain = P2Chain::walk(data);
    QDir().mkpath(outDir);
    const QVector<TextureBinding> levelBindings = textureBindings(file, data.size());

    // Images.
    QVector<QImage> decoded(chain.images.size());
    int imageFailures = 0;
    for (int i = 0; i < chain.images.size(); ++i) {
        QString error;
        decoded[i] = chain.images[i].decode(data, &error);
        if (decoded[i].isNull()) {
            warn(QString("%1: image %2 (%3): %4").arg(file).arg(i).arg(chain.images[i].name, error));
            ++imageFailures;
            continue;
        }
        const QString png = QString("%1/images/%2_%3.png").arg(outDir).arg(i, 3, 10, QChar('0')).arg(safe(chain.images[i].name));
        QDir().mkpath(outDir + "/images");
        if (!decoded[i].save(png)) {
            warn(QString("%1: could not save %2").arg(file, png));
            ++imageFailures;
        }
    }

    // Models, with their textures and motions.
    int modelsWritten = 0, animated = 0;
    QStringList joinedNames;
    for (int m = 0; m < chain.models.size(); ++m) {
        const P2Model &model = chain.models[m];
        if (!model.hasGeometry()) continue;
        // Texture binding: the images that follow the model up to the next model; if
        // there are not enough, the file's image list from the start.
        int blockIndex = -1;
        for (int b = 0; b < chain.blocks.size(); ++b) {
            if (chain.blocks[b].type == P2Block::Model && chain.blocks[b].index == m) blockIndex = b;
        }
        QVector<const QImage *> following, global, bound;
        for (int b = blockIndex + 1; b < chain.blocks.size() && chain.blocks[b].type != P2Block::Model; ++b) {
            if (chain.blocks[b].type == P2Block::Image) following.append(&decoded[chain.blocks[b].index]);
        }
        for (const QImage &img : decoded) global.append(&img);
        const int needed = model.maxTextureIndex() + 1;
        const QVector<const QImage *> *textures = (needed > 0 && following.size() >= needed) ? &following : &global;
        if (!levelBindings.isEmpty()) {
            // Only the images the game binds (see TextureBindings.cpp).
            bound.fill(nullptr, std::max(needed, 0));
            for (const TextureBinding &wb : levelBindings) {
                if (wb.model != m) continue;
                for (int local = 0; local < needed; ++local) {
                    const int image = wb.local < 0 ? wb.image + local : (wb.local == local ? wb.image : -1);
                    if (image >= 0 && image < decoded.size() && !bound[local]) bound[local] = &decoded[image];
                }
            }
            textures = &bound;
        }

        QStringList modelJoined;
        const QVector<GltfMotion> motions = motionsFor(name, chain, model, &modelJoined);
        for (const QString &joined : modelJoined)
            if (!joinedNames.contains(joined)) joinedNames << joined;
        const QString gltf = chain.models.size() == 1
                                 ? QString("%1/%2.gltf").arg(outDir, safe(baseName(name)))
                                 : QString("%1/%2_model_%3.gltf").arg(outDir, safe(baseName(name))).arg(m, 2, 10, QChar('0'));
        QString error;
        const QString modelName = chain.models.size() == 1 ? baseName(name) : QString("%1_model_%2").arg(baseName(name)).arg(m, 2, 10, QChar('0'));
        // Enemies: the size the game draws them at (see EnemySizes.cpp).
        GltfOptions gltfOptions = options.gltf;
        if (gltfOptions.scale <= 0) enemyModelScale(file, m, &gltfOptions.scale);
        if (!GltfWriter::write(gltf, modelName, model, *textures, motions, gltfOptions, &error)) {
            fail(QString("%1: model %2: %3").arg(file).arg(m).arg(error));
            return false;
        }
        ++modelsWritten;
        if (!motions.isEmpty()) ++animated;
    }

    // A listing of everything in the file, including what was not recognised.
    QFile listing(outDir + "/contents.txt");
    if (listing.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&listing);
        out << file << ": " << chain.images.size() << " images, " << chain.models.size() << " models, "
            << chain.motions.size() << " motions\n\n";
        for (const P2Block &b : chain.blocks) {
            out << QString("0x%1  0x%2  %3").arg(b.offset, 8, 16, QChar('0')).arg(b.size, 8, 16, QChar('0')).arg(typeName(b.type));
            if (b.type == P2Block::Image) {
                const P2Image &img = chain.images[b.index];
                out << QString(" %1  %2  %3x%4 %5").arg(b.index, 3).arg(img.name, -8).arg(img.width).arg(img.height).arg(img.formatName());
            } else if (b.type == P2Block::Model) {
                const P2Model &mdl = chain.models[b.index];
                out << QString(" %1  %2 parts, textures 0-%3").arg(b.index, 3).arg(mdl.parts.size()).arg(mdl.maxTextureIndex());
            } else if (b.type == P2Block::Motion) {
                const P2Motion &mot = chain.motions[b.index];
                out << QString(" %1  %2 bones, %3 frames").arg(b.index, 3).arg(mot.bones.size()).arg(mot.length);
            }
            out << "\n";
        }
        if (!joinedNames.isEmpty()) {
            out << "\nJoined animations (motions the game plays one after another, also exported as one):\n";
            for (const QString &joined : joinedNames) out << "  " << joined << "\n";
        }
    }

    QString summary = QString("%1: %2 images, %3 models").arg(file).arg(chain.images.size()).arg(modelsWritten);
    if (animated) summary += QString(" (%1 animated)").arg(animated);
    if (chain.motions.size()) summary += QString(", %1 motions").arg(chain.motions.size());
    if (chain.unknownBytes()) summary += QString(", %1 bytes of other data").arg(chain.unknownBytes());
    info(summary);
    return imageFailures == 0;
}

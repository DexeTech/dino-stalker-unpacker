#include "Extractor.h"

#include <QCoreApplication>
#include <QStringList>
#include <iostream>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

const char *HELP = R"HELP(dino-stalker-unpacker: extracts data from Dino Stalker (PlayStation 2)

Usage: dino-stalker-unpacker [options] <disc image | folder | file>...
       (or drag files, folders or the disc image onto the exe)

A disc image (.iso) is extracted to "<name> (extracted)" next to it:
  disc\       every file on the disc, unchanged
  converted\  the converted files, in the disc's folder layout
A folder is converted to "<folder>_extracted" next to it, keeping its layout;
a single file to a "<name>_<ext>" folder next to it.

What is converted:
  P2IG images            PNG (images\NNN_NAME.png)
  P2OD models            glTF with textures (and animations, see below)
  P2MT motions           glTF animations on the matching models
  DATA\PACK\*.PAK        decompressed (.decompressed.bin)
  SOUND.BIN              WAV samples, plus raw banks and sequences
  MOVIE*\*.PSS           video (.m2v) and audio (.wav)
Each converted P2 file also gets a contents.txt listing its blocks.

Options:
  --output-dir=PATH   write everything under PATH
  --no-animations     export models without animations
  --no-movies         skip the movies (they are about 2.4 GB)
  --no-disc-files     with a disc image, do not copy the unchanged files
  --fps=N             animation frames per second (default 60)
  --scale=N           scale models by N (default 1: game units)
  --help              show this text
)HELP";

// When the exe is started by double-click or drag and drop, Windows gives it its own
// console window, which closes as soon as the program ends. Keep it open so the
// messages can be read.
void waitIfOwnConsole() {
#ifdef Q_OS_WIN
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1) {
        std::cout << "\nPress Enter to close this window." << std::endl;
        std::cin.get();
    }
#endif
}

bool parseNumber(const QString &text, double &value) {
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || !(v > 0) || v > 1e6) return false;
    value = v;
    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    // Only QImage is used from Qt Gui (to write PNGs), which needs no window system.
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();
    args.removeFirst();

    ExtractOptions options;
    QStringList inputs;
    for (const QString &arg : args) {
        if (arg == "--help" || arg == "-h" || arg == "/?") {
            std::cout << HELP;
            return 0;
        } else if (arg.startsWith("--output-dir=")) {
            options.outputDir = arg.mid(13);
        } else if (arg == "--no-animations") {
            options.animations = false;
        } else if (arg == "--no-movies") {
            options.movies = false;
        } else if (arg == "--no-disc-files") {
            options.saveDiscFiles = false;
        } else if (arg.startsWith("--fps=")) {
            if (!parseNumber(arg.mid(6), options.gltf.framesPerSecond)) {
                std::cout << "[ERROR] --fps needs a positive number." << std::endl;
                return 1;
            }
        } else if (arg.startsWith("--scale=")) {
            if (!parseNumber(arg.mid(8), options.gltf.scale)) {
                std::cout << "[ERROR] --scale needs a positive number." << std::endl;
                return 1;
            }
        } else if (arg.startsWith("--")) {
            std::cout << "[ERROR] Unknown option " << arg.toStdString() << ". Use --help to see the options." << std::endl;
            return 1;
        } else {
            inputs << arg;
        }
    }
    if (inputs.isEmpty()) {
        std::cout << HELP;
        waitIfOwnConsole();
        return 1;
    }
    int failures = 0;
    for (const QString &input : inputs) {
        Extractor extractor(options);
        failures += extractor.process(input);
    }
    waitIfOwnConsole();
    return failures ? 1 : 0;
}

#include "EnemySizes.h"

namespace {

// From SLES_509.30. Enemy_ObjDtSet sets an enemy's scale from its type:
//   types 0-23:  gsDinoInfos[type]->scale (offset 0x8C) * 0.001, plus a random
//                0-0.135 * 0.001 for ordinary enemies, so in-game sizes vary a little
//   types 24-31: 1.0 (MUST, WULF and HHN*, made in metres)
//   types 32-33: 1.5 (IWA*, made in metres)
// Enemy_DataLoadExceptPmt gives the model file for each type; Enemy_EveryStgDtLd and
// Enemy_BossDtLd give the type of each model in the stage files (STGENEDT). The
// names are those of the model files.
enum Type {
    RptR = 0, RptB, RptY, Trnty, Cmp, RptOb, Trkr, Trex,
    CrnR, PtrR, PtrB, PtrY, Prso, Crn, CrnB, PrsoLarge, RptObB,
    Must = 24, Wulf, HhnA, HhnB, HhnC, HhnD, HhnE, HhnF, IwaA, IwaB,
    TypeCount
};

const double gameScale[TypeCount] = {
    1.4, 1.0, 1.2, 1.2, 1.0, 0.85, 1.5, 1.5, 1.2, 2.2, 1.0, 1.6, 1.0, 1.5, 1.2, 1.6, 0.85,
    1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0,   // 17-23: further RPTB types
    1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 1.5,
};

double metresPerUnit(int type) {
    return type < Must ? gameScale[type] * 0.001 : gameScale[type];
}

struct FileTypes {
    const char *file;
    int types[12];   // type of each model in the file, -1 after the last
};

const FileTypes files[] = {
    {"RPTRMD.BIN", {RptR, -1}},
    {"RPTBMD.BIN", {RptB, -1}},
    {"RPTYMD.BIN", {RptY, -1}},
    {"TRNTYMD.BIN", {Trnty, -1}},
    {"CMPMD.BIN", {Cmp, -1}},
    {"RPTOBMD.BIN", {RptOb, -1}},
    {"RPTOBBMD.BIN", {RptObB, -1}},
    {"TRKRMD.BIN", {Trkr, -1}},
    {"TREXMD.BIN", {Trex, -1}},
    {"CRNRMD.BIN", {CrnR, -1}},
    {"CRNMD.BIN", {Crn, -1}},
    {"CRNBMD.BIN", {CrnB, -1}},
    {"PTRRMD.BIN", {PtrR, -1}},
    {"PTRBMD.BIN", {PtrB, -1}},
    {"PTRYMD.BIN", {PtrY, -1}},
    {"PRSOMD.BIN", {Prso, -1}},   // also drawn 1.6 times larger as type 15
    {"MUSTMD.BIN", {Must, -1}},
    {"WULFMD.BIN", {Wulf, -1}},
    {"HHNAMD.BIN", {HhnA, -1}},
    {"HHNBMD.BIN", {HhnB, -1}},
    {"HHNCMD.BIN", {HhnC, -1}},
    {"HHNDMD.BIN", {HhnD, -1}},
    {"HHNEMD.BIN", {HhnE, -1}},
    {"HHNFMD.BIN", {HhnF, -1}},
    {"IWAAMD.BIN", {IwaA, -1}},
    {"IWABMD.BIN", {IwaB, -1}},
    {"ST1ENE.BIN", {PtrB, PtrY, PtrR, Must, Wulf, HhnA, HhnB, HhnC, HhnD, HhnE, HhnF, -1}},
    {"ST2ENE.BIN", {RptB, RptY, Cmp, -1}},
    {"ST3ENE.BIN", {PtrB, PtrY, Crn, Prso, -1}},
    {"ST456AEN.BIN", {RptB, RptY, RptOb, RptObB, PtrB, PtrY, Cmp, -1}},
    {"ST456BEN.BIN", {RptY, RptR, RptOb, RptObB, PtrY, PtrR, -1}},
    {"ST7ENE.BIN", {RptB, RptY, RptR, RptOb, RptObB, PtrR, Trkr, Cmp, -1}},
    {"ST8ENE.BIN", {RptR, Trnty, -1}},
    {"ST9ENE.BIN", {Trex, IwaA, IwaB, -1}},
    {"BS00ENE.BIN", {CrnR, CrnB, -1}},
    {"BS01ENE.BIN", {CrnR, CrnB, Trex, IwaA, IwaB, -1}},
    {"BS02ENE.BIN", {Trex, IwaA, IwaB, -1}},
};

} // namespace

bool enemyModelScale(const QString &fileName, int model, double *result) {
    for (const FileTypes &f : files) {
        if (fileName.compare(QLatin1String(f.file), Qt::CaseInsensitive) != 0) continue;
        for (int i = 0; i < 12 && f.types[i] >= 0; ++i) {
            if (i == model) {
                *result = metresPerUnit(f.types[i]);
                return true;
            }
        }
        return false;
    }
    return false;
}

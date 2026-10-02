#pragma once

#include <QString>

// The size the game draws each enemy model at, in metres per model unit.
//
// Enemy models are made in millimetres, but the game scales every enemy type by its
// own factor (1.4 for RPTR, 0.85 for RPTOB, 2.2 for PTRR...), so the files alone
// give every raptor the same size. This table holds those factors for the files that
// contain enemy models (see the .cpp).
//
// Returns false for models that are not in the table.
bool enemyModelScale(const QString &fileName, int model, double *metresPerUnit);

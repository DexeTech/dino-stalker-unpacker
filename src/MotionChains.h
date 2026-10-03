#pragma once

#include "P2Motion.h"

#include <QVector>

// Actions the game stores as several motions played back to back.
//
// The roar of the T-Rex is three motions: TREXPMT_54 rises from the idle pose into the
// roar, 55 holds it (it ends in its own start pose, so the game can repeat it) and 56
// returns to the idle pose. Each one alone stops part-way through the action. The
// motions do not say which ones belong together; the game's code decides. They are
// found from the poses instead: a motion continues another when it starts in the pose
// the other one ends in (every bone within 3 degrees and 2% of the skeleton's size).
//
// Poses that many motions start or end in (the idle pose, which almost every motion
// returns to) say nothing about the order and are not followed, and motions that key
// fewer than half the bones (head or tail movements layered on others) are left out.
// A hold that repeats is followed once.
//
// Returns the motions of each chain, as indices into `motions`, in playing order.
QVector<QVector<int>> findMotionChains(const QVector<const P2Motion *> &motions);

// The motions played one after another as one motion. Every bone gets rotation,
// translation and scale keys throughout, taken from each part's own rest pose where
// that part does not key them.
P2Motion joinMotions(const QVector<const P2Motion *> &parts);

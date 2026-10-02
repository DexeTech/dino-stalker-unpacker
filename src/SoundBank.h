#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Extracts SOUND.BIN, which holds three kinds of data on 0x800 boundaries:
//
// - VAB sound-effect banks ("pBAV"): header (0x20 + 0x800 programs + 0x200 per
//   program of tones + 0x200 VAG size table), then the ADPCM samples. A sample's
//   rate comes from the first tone that uses it: 44100 / 2^((centre note - 60) / 12).
// - Music, as Sony's PS2 sound-library files: an SQ sequence ("IECSsreV" then
//   "IECSuqeS"), and an HD instrument header ("IECSsreV" then "IECSdaeH") whose BD
//   sample body normally starts at the next 0x800 boundary. The HD "Vagi" table
//   gives each sample's offset in the BD and its sample rate.
// - ADPCM data that no header describes (the game plays it by offset). It is split
//   into samples at the ADPCM end flags and saved at an assumed 22050 Hz.
//
// Every bank and track is also saved raw (.vab, .sq, .hd, .bd) for tools such as
// VGMTrans, which can turn the sequences into MIDI with their instruments.
namespace SoundBank {
bool isSoundBin(const QByteArray &data);
// Returns false on failure; messages describes what was written or skipped.
bool extract(const QByteArray &data, const QString &outputDir, QStringList &messages, QString *error = nullptr);
}

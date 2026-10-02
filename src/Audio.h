#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace Audio {

// Decodes PlayStation SPU ADPCM (16-byte blocks: shift/filter, flags, 14 data bytes).
// Stops after a block with the end flag (bit 0) when stopAtEnd is set.
QVector<qint16> decodeAdpcm(const QByteArray &data, qint64 offset, qint64 size, bool stopAtEnd = true,
                            qint64 *consumed = nullptr);

// Writes 16-bit PCM WAV. Stereo samples are interleaved L, R.
bool writeWav(const QString &fileName, const QVector<qint16> &samples, int sampleRate, int channels,
              QString *error = nullptr);

} // namespace Audio

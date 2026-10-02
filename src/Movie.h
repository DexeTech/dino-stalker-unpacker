#pragma once

#include <QByteArray>
#include <QString>

// Splits a PS2 movie (.PSS, an MPEG-2 program stream) into its video elementary
// stream (.m2v, playable in VLC/mpv/ffmpeg) and its audio (.wav).
//
// Audio is in private stream 1, sub-stream 0xFF. The first packet starts with an
// "SShd" header (u32 size 0x18, u32 format, u32 rate, u32 channels, u32 interleave)
// and an "SSbd" body header (u32 size). Format 1 is 16-bit little-endian PCM with
// the channels interleaved in blocks of `interleave` bytes.
namespace Movie {
bool isMovie(const QByteArray &head);
bool split(const QByteArray &data, const QString &videoFile, const QString &audioFile, QString *summary,
           QString *error = nullptr);
}

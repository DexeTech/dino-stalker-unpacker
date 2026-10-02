#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <cstring>

// Bounds-checked little-endian reads from a QByteArray. Out-of-range reads return 0
// and set the failed flag, so parsers can check once at the end instead of after
// every field.
class Reader {
public:
    explicit Reader(const QByteArray &data) : data(data) {}

    bool has(qint64 offset, qint64 size) const {
        return offset >= 0 && size >= 0 && offset + size <= data.size();
    }

    template <typename T>
    T get(qint64 offset) const {
        T value{};
        if (!has(offset, sizeof(T))) {
            failed = true;
            return value;
        }
        std::memcpy(&value, data.constData() + offset, sizeof(T));
        return value;
    }

    uint8_t u8(qint64 offset) const { return get<uint8_t>(offset); }
    uint16_t u16(qint64 offset) const { return get<uint16_t>(offset); }
    int16_t s16(qint64 offset) const { return get<int16_t>(offset); }
    uint32_t u32(qint64 offset) const { return get<uint32_t>(offset); }
    int32_t s32(qint64 offset) const { return get<int32_t>(offset); }
    float f32(qint64 offset) const { return get<float>(offset); }

    // A fixed-size, zero-padded ASCII name.
    QString name(qint64 offset, int size) const {
        if (!has(offset, size)) {
            failed = true;
            return QString();
        }
        const char *text = data.constData() + offset;
        return QString::fromLatin1(text, int(strnlen(text, size)));
    }

    bool matches(qint64 offset, const char *magic) const {
        const qint64 size = qint64(std::strlen(magic));
        return has(offset, size) && std::memcmp(data.constData() + offset, magic, size) == 0;
    }

    qint64 size() const { return data.size(); }
    bool ok() const { return !failed; }

    const QByteArray &data;
    mutable bool failed = false;
};

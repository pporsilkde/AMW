#ifndef OPENMW_BSA_COMPRESSEDBSAUTILS_H
#define OPENMW_BSA_COMPRESSEDBSAUTILS_H
#include <lz4frame.h>
#include <cstdint>
#include <istream>
#include <stdexcept>
#include <string>

namespace Bsa
{
    struct PayloadSizes { std::size_t stored, unpacked; };

    inline PayloadSizes readPayloadHeader(std::istream& stream, std::size_t size,
        bool embeddedName, bool compressed)
    {
        if (embeddedName)
        {
            unsigned char length = 0;
            if (size < 1 || !stream.read(reinterpret_cast<char*>(&length), 1)
                || size - 1 < length)
                throw std::runtime_error("BSA: truncated embedded filename");
            stream.ignore(length);
            if (!stream) throw std::runtime_error("BSA: truncated embedded filename");
            size -= std::size_t(length) + 1;
        }
        std::size_t unpacked = size;
        if (compressed)
        {
            unsigned char bytes[4];
            if (size < 4 || !stream.read(reinterpret_cast<char*>(bytes), 4))
                throw std::runtime_error("BSA: missing unpacked-size header");
            unpacked = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
                | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
            size -= 4;
        }
        if (unpacked > 1024u * 1024u * 1024u)
            throw std::runtime_error("BSA: single unpacked resource exceeds 1 GiB");
        return {size, unpacked};
    }

    inline void decompressLz4Frame(const char* source, std::size_t sourceSize,
        char* destination, std::size_t destinationSize)
    {
        char empty = 0;
        if (destinationSize == 0) destination = &empty;
        if (sourceSize == 0) source = &empty;
        struct Context
        {
            LZ4F_decompressionContext_t value = nullptr;
            ~Context() { if (value) LZ4F_freeDecompressionContext(value); }
        } context;
        const auto created = LZ4F_createDecompressionContext(&context.value, LZ4F_VERSION);
        if (LZ4F_isError(created)) throw std::runtime_error(LZ4F_getErrorName(created));
        std::size_t in = 0, out = 0, remaining = 1;
        // One call need not consume a complete frame. A nonzero return is a hint
        // that more bytes are required, not a successful end-of-frame marker.
        while (remaining != 0)
        {
            std::size_t consumed = sourceSize - in;
            std::size_t produced = destinationSize - out;
            remaining = LZ4F_decompress(context.value, destination + out, &produced,
                source + in, &consumed, nullptr);
            if (LZ4F_isError(remaining))
                throw std::runtime_error(std::string("BSA: LZ4 decode failed: ") + LZ4F_getErrorName(remaining));
            in += consumed;
            out += produced;
            if (remaining != 0 && (consumed == 0 && produced == 0))
                throw std::runtime_error("BSA: incomplete LZ4 frame or incorrect unpacked size");
        }
        if (in != sourceSize || out != destinationSize)
            throw std::runtime_error("BSA: LZ4 payload/size mismatch");
    }
}
#endif

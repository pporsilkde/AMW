#include "ktx2reader.hpp"
#include <ktx.h>
#include <osg/GLExtensions>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// Windows GL 1.1 headers do not declare the texture-RG extension tokens.
// Values from Khronos glcorearb.h; runtime format support is still checked separately.
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif

namespace
{
    constexpr std::size_t maxBytes = 512u * 1024u * 1024u;
    void check(KTX_error_code code)
    {
        if (code != KTX_SUCCESS)
            throw std::runtime_error(std::string("KTX2: ") + ktxErrorString(code));
    }
    struct DestroyTexture
    {
        void operator()(ktxTexture2* texture) const { ktxTexture_Destroy(ktxTexture(texture)); }
    };
    bool powerOfTwo(unsigned value) { return value && !(value & (value - 1)); }
    struct Format
    {
        GLenum internal, pixel, type;
        unsigned bytes; // per pixel for plain images; per 4x4 block otherwise
        bool compressed, dxt;
    };
    Format formatFor(unsigned vk)
    {
        // VkFormat values are part of KTX2; no Vulkan SDK or loader is needed.
        switch (vk)
        {
            case 9: return { GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, false, false };
            case 16: return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, false, false };
            case 23: return { GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3, false, false };
            case 29: return { GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE, 3, false, false };
            case 37: return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false };
            case 43: return { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false };
            case 44: return { GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 4, false, false };
            case 50: return { GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, 4, false, false };
            case 131: return { 0x83F0, 0x83F0, GL_UNSIGNED_BYTE, 8, true, true };
            case 132: return { 0x8C4C, 0x8C4C, GL_UNSIGNED_BYTE, 8, true, true };
            case 133: return { 0x83F1, 0x83F1, GL_UNSIGNED_BYTE, 8, true, true };
            case 134: return { 0x8C4D, 0x8C4D, GL_UNSIGNED_BYTE, 8, true, true };
            case 135: return { 0x83F2, 0x83F2, GL_UNSIGNED_BYTE, 16, true, true };
            case 136: return { 0x8C4E, 0x8C4E, GL_UNSIGNED_BYTE, 16, true, true };
            case 137: return { 0x83F3, 0x83F3, GL_UNSIGNED_BYTE, 16, true, true };
            case 138: return { 0x8C4F, 0x8C4F, GL_UNSIGNED_BYTE, 16, true, true };
            case 139: return { 0x8DBB, 0x8DBB, GL_UNSIGNED_BYTE, 8, true, false };
            case 140: return { 0x8DBC, 0x8DBC, GL_UNSIGNED_BYTE, 8, true, false };
            case 141: return { 0x8DBD, 0x8DBD, GL_UNSIGNED_BYTE, 16, true, false };
            case 142: return { 0x8DBE, 0x8DBE, GL_UNSIGNED_BYTE, 16, true, false };
            case 145: return { 0x8E8C, 0x8E8C, GL_UNSIGNED_BYTE, 16, true, false };
            case 146: return { 0x8E8D, 0x8E8D, GL_UNSIGNED_BYTE, 16, true, false };
            default: throw std::runtime_error("KTX2: unsupported native VkFormat " + std::to_string(vk)
                + "; use a 2D LDR ETC1S/UASTC or RGBA8 texture");
        }
    }
}

osg::ref_ptr<osg::Image> Resource::readKtx2(std::istream& stream)
{
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 80 || length > static_cast<std::streamoff>(maxBytes))
        throw std::runtime_error("KTX2: invalid file size (limit 512 MiB)");
    stream.seekg(0);
    std::vector<unsigned char> source(static_cast<std::size_t>(length));
    stream.read(reinterpret_cast<char*>(source.data()), static_cast<std::streamsize>(source.size()));
    if (!stream) throw std::runtime_error("KTX2: truncated input");

    ktxTexture2* raw = nullptr;
    check(ktxTexture2_CreateFromMemory(source.data(), source.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &raw));
    std::unique_ptr<ktxTexture2, DestroyTexture> texture(raw);
    if (raw->numDimensions != 2 || raw->isArray || raw->isCubemap || raw->numFaces != 1
        || raw->numLayers > 1 || raw->baseDepth > 1 || raw->baseWidth == 0 || raw->baseHeight == 0
        || raw->baseWidth > 16384 || raw->baseHeight > 16384 || raw->numLevels == 0 || raw->numLevels > 15)
        throw std::runtime_error("KTX2: only single 2D LDR textures up to 16384 pixels are supported");
    unsigned maxLevels = 1;
    for (unsigned dim = std::max(raw->baseWidth, raw->baseHeight); dim > 1; dim >>= 1) ++maxLevels;
    if (raw->numLevels > maxLevels || ktxTexture_GetDataSizeUncompressed(ktxTexture(raw)) > maxBytes)
        throw std::runtime_error("KTX2: invalid mip chain or excessive decoded size");
    // Conservative bound includes the RGBA fallback for Basis textures.
    if (static_cast<std::uint64_t>(raw->baseWidth) * raw->baseHeight * 4u * 2u > maxBytes)
        throw std::runtime_error("KTX2: decoded texture exceeds the memory limit");

    const bool flipX = raw->orientation.x == KTX_ORIENT_X_LEFT;
    const bool flipY = raw->orientation.y == KTX_ORIENT_Y_DOWN;
    unsigned swizzleSize = 0;
    void* swizzle = nullptr;
    const auto swizzleResult = ktxHashList_FindValue(&raw->kvDataHead, "KTXswizzle", &swizzleSize, &swizzle);
    if (swizzleResult != KTX_NOT_FOUND)
    {
        check(swizzleResult);
        if (swizzleSize < 4 || std::memcmp(swizzle, "rgba", 4) != 0)
            throw std::runtime_error("KTX2: bake non-identity KTXswizzle into the source pixels");
    }
    check(ktxTexture_LoadImageData(ktxTexture(raw), nullptr, 0));
    const auto* extensions = osg::GLExtensions::Get(0, false);
    const bool s3tc = extensions && extensions->isTextureCompressionS3TCSupported;
    if (ktxTexture2_NeedsTranscoding(raw))
    {
        // OSG can losslessly flip BC1/BC3 mip chains with power-of-two dimensions.
        // Other orientations/shapes and PCs without S3TC get a safe RGBA fallback.
        const bool useBC = s3tc && !std::getenv("OPENMW_DECOMPRESS_TEXTURES") && !flipX
            && (!flipY || (powerOfTwo(raw->baseWidth) && powerOfTwo(raw->baseHeight)));
        // libktx 4.3.2 initializes shared Basis lookup tables lazily.
        // Resource preload workers may arrive here concurrently.
        static std::once_flag transcodeInitialized;
        bool done = false;
        const auto transcode = [&] {
            check(ktxTexture2_TranscodeBasis(raw, useBC ? KTX_TTF_BC1_OR_3 : KTX_TTF_RGBA32, 0));
        };
        std::call_once(transcodeInitialized, [&] { transcode(); done = true; });
        if (!done) transcode();
    }
    const Format format = formatFor(raw->vkFormat);
    if (format.compressed)
    {
        if (flipX || (flipY && (!format.dxt || !powerOfTwo(raw->baseWidth) || !powerOfTwo(raw->baseHeight))))
            throw std::runtime_error("KTX2: native compressed texture needs KTXorientation=ru; use Basis for automatic orientation");
        if (format.dxt && !s3tc)
            throw std::runtime_error("KTX2: native BC1/BC2/BC3 requires S3TC; use Basis for a software fallback");
        if (!format.dxt && !osg::isGLExtensionSupported(0,
                raw->vkFormat >= 145 ? "GL_ARB_texture_compression_bptc" : "GL_ARB_texture_compression_rgtc"))
            throw std::runtime_error("KTX2: native BC4/BC5/BC7 format is not supported by this GPU");
    }

    osg::Image::MipmapDataType mipOffsets;
    std::vector<ktx_size_t> offsets, sizes;
    std::size_t total = 0;
    for (unsigned level = 0; level < raw->numLevels; ++level)
    {
        ktx_size_t offset = 0;
        check(ktxTexture_GetImageOffset(ktxTexture(raw), level, 0, 0, &offset));
        const unsigned width = std::max(1u, raw->baseWidth >> level);
        const unsigned height = std::max(1u, raw->baseHeight >> level);
        const std::size_t size = format.compressed
            ? std::size_t((width + 3) / 4) * ((height + 3) / 4) * format.bytes
            : std::size_t(width) * height * format.bytes;
        if (offset > raw->dataSize || size > raw->dataSize - offset
            || ktxTexture_GetImageSize(ktxTexture(raw), level) != size || size > maxBytes - total)
            throw std::runtime_error("KTX2: invalid mip size/offset");
        if (level) mipOffsets.push_back(static_cast<unsigned>(total));
        offsets.push_back(offset);
        sizes.push_back(size);
        total += size;
    }
    std::unique_ptr<unsigned char[]> pixels(new unsigned char[total]);
    std::size_t destination = 0;
    for (unsigned level = 0; level < raw->numLevels; ++level)
    {
        const auto* input = raw->pData + offsets[level];
        if (format.compressed || (!flipX && !flipY))
            std::memcpy(pixels.get() + destination, input, sizes[level]);
        else
        {
            const unsigned width = std::max(1u, raw->baseWidth >> level);
            const unsigned height = std::max(1u, raw->baseHeight >> level);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                    std::memcpy(pixels.get() + destination + (std::size_t(y) * width + x) * format.bytes,
                        input + (std::size_t(flipY ? height - 1 - y : y) * width
                            + (flipX ? width - 1 - x : x)) * format.bytes, format.bytes);
        }
        destination += sizes[level];
    }
    osg::ref_ptr<osg::Image> image = new osg::Image;
    image->setImage(raw->baseWidth, raw->baseHeight, 1, format.internal, format.pixel, format.type,
        pixels.release(), osg::Image::USE_NEW_DELETE, 1);
    image->setMipmapLevels(mipOffsets);
    if (format.compressed && flipY) image->flipVertical();
    image->setOrigin(osg::Image::BOTTOM_LEFT);
    return image;
}

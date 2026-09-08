#ifndef OPENMW_RESOURCE_KTX2READER_H
#define OPENMW_RESOURCE_KTX2READER_H
#include <istream>
#include <osg/Image>
#include <osg/ref_ptr>
namespace Resource
{
    // Reads a 2D LDR KTX2 from a VFS stream, including Basis ETC1S/UASTC.
    // Throws with a diagnostic on malformed data or unsupported native formats.
    osg::ref_ptr<osg::Image> readKtx2(std::istream& stream);
}
#endif

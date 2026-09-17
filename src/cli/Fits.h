#pragma once
// Minimal FITS support for the CLI harness and tests. Not a general FITS
// library: primary HDU only, uncompressed, BITPIX 16 / -32 / -64, NAXIS 2 or
// 3 (NAXIS3 = 1 or 3). Written files are BITPIX -32.
#include <string>

#include "repatch/Image.h"

namespace repatch::fits {

bool Read( const std::string& path, Image& out, std::string& err );
bool Write( const std::string& path, const Image& img, std::string& err );

} // namespace repatch::fits

#ifndef CFF_SERIALIZER_H
#define CFF_SERIALIZER_H

#include <string>
#include "modules/cff/cff.pb.h"

// Compile a structured CffFont into a byte-valid CFF (Type1C) font program --
// the bytes that go into a PDF /FontFile3 stream. Every INDEX/DICT offset is
// computed from scratch (Header, Name/TopDICT/String/GlobalSubr/CharStrings
// INDEXes, Private DICT, Local Subr INDEX). See cff.proto for the layout and
// the Adobe TN#5176 / TN#5177 spec references.
std::string SerializeCff(const pdf_cff::CffFont& font);

#endif // CFF_SERIALIZER_H

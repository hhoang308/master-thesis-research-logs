#ifndef PDF_PROTO_MODULES_TEXTLARGEY_TEXT_LARGE_Y_SERIALIZER_H_
#define PDF_PROTO_MODULES_TEXTLARGEY_TEXT_LARGE_Y_SERIALIZER_H_

#include <string>

#include "modules/textlargey/text_large_y.pb.h"

pdf_textlargey::TextLargeYDocument CanonicalizeTextLargeYDocument(
    const pdf_textlargey::TextLargeYDocument& doc);

std::string SerializeTextLargeYPdf(
    const pdf_textlargey::TextLargeYDocument& doc);

#endif  // PDF_PROTO_MODULES_TEXTLARGEY_TEXT_LARGE_Y_SERIALIZER_H_

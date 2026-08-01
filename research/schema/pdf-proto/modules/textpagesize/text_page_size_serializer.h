#ifndef PDF_PROTO_MODULES_TEXTPAGESIZE_TEXT_PAGE_SIZE_SERIALIZER_H_
#define PDF_PROTO_MODULES_TEXTPAGESIZE_TEXT_PAGE_SIZE_SERIALIZER_H_

#include <string>

#include "modules/textpagesize/text_page_size.pb.h"

pdf_textpagesize::TextPageSizeDocument CanonicalizeTextPageSizeDocument(
    const pdf_textpagesize::TextPageSizeDocument& doc);

std::string SerializeTextPageSizePdf(
    const pdf_textpagesize::TextPageSizeDocument& doc);

#endif  // PDF_PROTO_MODULES_TEXTPAGESIZE_TEXT_PAGE_SIZE_SERIALIZER_H_

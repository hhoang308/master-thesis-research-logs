#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <string>
#include "pdf.pb.h"

// True when the document carries something SerializePdf() can actually turn into
// renderable content. Keep this in sync with the module dispatch at the top of
// SerializePdf() -- callers use it to skip inputs that would render nothing.
bool HasAnyContent(const pdf_proto::PdfDocument& doc);

std::string SerializePdf(const pdf_proto::PdfDocument& doc);

#endif // SERIALIZER_H

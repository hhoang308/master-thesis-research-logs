#ifndef PDF_PROTO_MODULES_JPXSTREAM_JPX_STREAM_SERIALIZER_H_
#define PDF_PROTO_MODULES_JPXSTREAM_JPX_STREAM_SERIALIZER_H_

#include <string>

#include "modules/jpxstream/jpx_stream.pb.h"

pdf_jpxstream::JpxStreamDocument CanonicalizeJpxStreamDocument(
    const pdf_jpxstream::JpxStreamDocument& doc);

std::string SerializeJpxCodestream(
    const pdf_jpxstream::JpxStreamDocument& doc);

std::string SerializeJpxPdf(const pdf_jpxstream::JpxStreamDocument& doc);

#endif  // PDF_PROTO_MODULES_JPXSTREAM_JPX_STREAM_SERIALIZER_H_

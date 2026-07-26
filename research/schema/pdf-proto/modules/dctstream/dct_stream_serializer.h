#ifndef PDF_PROTO_MODULES_DCTSTREAM_DCT_STREAM_SERIALIZER_H_
#define PDF_PROTO_MODULES_DCTSTREAM_DCT_STREAM_SERIALIZER_H_

#include <string>

#include "modules/dctstream/dct_stream.pb.h"

pdf_dctstream::DctStreamDocument CanonicalizeDctStreamDocument(
    const pdf_dctstream::DctStreamDocument& doc);

std::string SerializeDctStreamJpeg(
    const pdf_dctstream::DctStreamDocument& doc);

std::string SerializeDctStreamPdf(
    const pdf_dctstream::DctStreamDocument& doc);

#endif  // PDF_PROTO_MODULES_DCTSTREAM_DCT_STREAM_SERIALIZER_H_

#ifndef PDF_PROTO_MODULES_OBJSTMLENGTH_OBJSTM_LENGTH_SERIALIZER_H_
#define PDF_PROTO_MODULES_OBJSTMLENGTH_OBJSTM_LENGTH_SERIALIZER_H_

#include <string>

#include "modules/objstmlength/objstm_length.pb.h"

pdf_objstmlength::ObjstmLengthDocument CanonicalizeObjstmLengthDocument(
    const pdf_objstmlength::ObjstmLengthDocument& doc);

std::string SerializeObjstmLengthPdf(
    const pdf_objstmlength::ObjstmLengthDocument& doc);

#endif  // PDF_PROTO_MODULES_OBJSTMLENGTH_OBJSTM_LENGTH_SERIALIZER_H_

#ifndef PDF_PROTO_MODULES_ACROFORMLOOP_ACROFORM_LOOP_SERIALIZER_H_
#define PDF_PROTO_MODULES_ACROFORMLOOP_ACROFORM_LOOP_SERIALIZER_H_

#include <string>

#include "modules/acroformloop/acroform_loop.pb.h"

pdf_acroformloop::AcroFormLoopDocument CanonicalizeAcroFormLoopDocument(
    const pdf_acroformloop::AcroFormLoopDocument& doc);

std::string SerializeAcroFormLoopPdf(
    const pdf_acroformloop::AcroFormLoopDocument& doc);

#endif  // PDF_PROTO_MODULES_ACROFORMLOOP_ACROFORM_LOOP_SERIALIZER_H_

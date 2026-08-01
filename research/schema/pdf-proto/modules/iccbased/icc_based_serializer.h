#ifndef PDF_PROTO_MODULES_ICCBASED_ICC_BASED_SERIALIZER_H_
#define PDF_PROTO_MODULES_ICCBASED_ICC_BASED_SERIALIZER_H_

#include <string>

#include "modules/iccbased/icc_based.pb.h"

pdf_iccbased::IccBasedDocument CanonicalizeIccBasedDocument(
    const pdf_iccbased::IccBasedDocument& doc);

std::string SerializeIccBasedPdf(const pdf_iccbased::IccBasedDocument& doc);

#endif  // PDF_PROTO_MODULES_ICCBASED_ICC_BASED_SERIALIZER_H_

#ifndef SERIALIZER_H
#define SERIALIZER_H

#include <string>
#include "pdf.pb.h"

std::string SerializePdf(const PdfDocument& doc);

#endif // SERIALIZER_H

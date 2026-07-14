#ifndef PDF_PROTO_MODULES_TYPE3CACHE_TYPE3_CACHE_SERIALIZER_H_
#define PDF_PROTO_MODULES_TYPE3CACHE_TYPE3_CACHE_SERIALIZER_H_

#include <string>

#include "modules/type3cache/type3_cache.pb.h"

std::string SerializeType3CachePdf(
    const pdf_type3cache::Type3CacheDocument& doc);

#endif  // PDF_PROTO_MODULES_TYPE3CACHE_TYPE3_CACHE_SERIALIZER_H_

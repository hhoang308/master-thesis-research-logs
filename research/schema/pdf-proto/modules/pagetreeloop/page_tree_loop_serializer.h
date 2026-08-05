#ifndef PDF_PROTO_MODULES_PAGETREELOOP_PAGE_TREE_LOOP_SERIALIZER_H_
#define PDF_PROTO_MODULES_PAGETREELOOP_PAGE_TREE_LOOP_SERIALIZER_H_

#include <string>

#include "modules/pagetreeloop/page_tree_loop.pb.h"

pdf_pagetreeloop::PageTreeLoopDocument CanonicalizePageTreeLoopDocument(
    const pdf_pagetreeloop::PageTreeLoopDocument& doc);

std::string SerializePageTreeLoopPdf(
    const pdf_pagetreeloop::PageTreeLoopDocument& doc);

#endif  // PDF_PROTO_MODULES_PAGETREELOOP_PAGE_TREE_LOOP_SERIALIZER_H_

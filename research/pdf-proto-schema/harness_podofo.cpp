#include <string>
#include "libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h"
#include "pdf.pb.h"
#include "serializer.h"
#include <podofo/podofo.h>

// PoDoFo parses lazily: LoadFromBuffer + GetPageCount only touches the structure
// (xref/trailer/catalog/page-tree). To exercise real depth we must (1) tokenize each
// page's content stream -- which decodes it through its filter chain -- and (2) force
// every object that has a stream (font programs, images, ToUnicode, CIDToGIDMap,
// content) through GetFilteredCopy so its filters actually run. Errors are expected
// (we feed malformed PDFs); only a sanitizer abort counts as a real crash.
DEFINE_PROTO_FUZZER(const pdf_proto::PdfDocument& doc) {
    if (doc.pages_size() == 0) return;

    std::string bytes = SerializePdf(doc);

    try {
        PoDoFo::PdfMemDocument pdf;
        pdf.LoadFromBuffer(bytes.data(), static_cast<long>(bytes.size()));

        // (1) Tokenize page contents (decodes the content stream + walks operators).
        int n = pdf.GetPageCount();
        if (n > 10) n = 10;  // bound per-input work
        for (int i = 0; i < n; i++) {
            try {
                PoDoFo::PdfPage* page = pdf.GetPage(i);
                if (!page) continue;
                PoDoFo::PdfContentsTokenizer tok(page);
                PoDoFo::EPdfContentsType type;
                const char* keyword = nullptr;
                PoDoFo::PdfVariant var;
                while (tok.ReadNext(type, keyword, var)) { /* discard */ }
            } catch (...) { /* per-page parse error -- keep going */ }
        }

        // (2) Force every stream object through its filter chain.
        const PoDoFo::PdfVecObjects& objs = pdf.GetObjects();
        for (PoDoFo::TCIVecObjects it = objs.begin(); it != objs.end(); ++it) {
            PoDoFo::PdfObject* o = *it;
            if (o && o->HasStream()) {
                try {
                    char* buf = nullptr;
                    PoDoFo::pdf_long len = 0;
                    o->GetStream()->GetFilteredCopy(&buf, &len);
                    if (buf) PoDoFo::podofo_free(buf);
                } catch (...) { /* bad filter data -- expected */ }
            }
        }
    } catch (const PoDoFo::PdfError&) {
        // structural parse errors are expected -- not a crash
    } catch (...) {
    }
}

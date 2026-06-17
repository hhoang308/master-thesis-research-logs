#include <string>
#include "libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h"
#include "pdf.pb.h"
#include "serializer.h"
#include <podofo/podofo.h>

DEFINE_PROTO_FUZZER(const pdf_proto::PdfDocument& doc) {
    if (doc.pages_size() == 0) return;

    std::string bytes = SerializePdf(doc);

    try {
        PoDoFo::PdfMemDocument pdf;
        // LoadFromBuffer feeds bytes directly -- no temp file needed
        pdf.LoadFromBuffer(bytes.data(), static_cast<long>(bytes.size()));
        pdf.GetPageCount();
    } catch (const PoDoFo::PdfError&) {
        // parse errors are expected -- not a crash
    } catch (...) {
        // catch anything else to avoid aborting the fuzzer
    }
}

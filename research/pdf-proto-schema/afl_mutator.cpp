// AFL++ custom mutator for the structure-aware PDF pipeline.
//
// AFL++'s inputs/queue/crash files are serialized `PdfDocument` protos (binary). This
// .so plugs two hooks into AFL++ (AFL_CUSTOM_MUTATOR_LIBRARY):
//
//   afl_custom_fuzz        -- THE MUTATION. Parse proto bytes -> PdfDocument, mutate the
//                             message tree with libprotobuf-mutator, re-serialize to proto
//                             bytes. Stays proto -> proto. (Set AFL_CUSTOM_MUTATOR_ONLY=1 to
//                             use only this and disable AFL's byte-level havoc/splice.)
//   afl_custom_post_process-- THE CONVERSION. Parse proto bytes -> PdfDocument, run
//                             SerializePdf() -> raw PDF bytes that the target actually
//                             parses. proto -> PDF. (One-way; this is why crash files stay
//                             protos -- convert them with proto2pdf for a portable .pdf.)
//
// NB: keep CmpLog OFF -- it patches bytes into the input at offsets that are meaningless
// after the proto->PDF serialize step.
//
// Built non-ASan / -fPIC -shared: it is loaded by afl-fuzz, not the instrumented target.
#include <cstdint>
#include <cstddef>
#include <string>
#include "pdf.pb.h"
#include "serializer.h"
#include "src/mutator.h"   // protobuf_mutator::Mutator

using pdf_proto::PdfDocument;

namespace {
struct PdfMutator {
    protobuf_mutator::Mutator mutator;
    std::string fuzz_out;   // serialized proto (afl_custom_fuzz output)
    std::string pp_out;     // serialized PDF  (afl_custom_post_process output)
};
}  // namespace

extern "C" {

void* afl_custom_init(void* /*afl*/, unsigned int seed) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    PdfMutator* m = new PdfMutator();
    m->mutator.Seed(seed);
    return m;
}

// Mutation: proto -> (LPM mutate) -> proto.
size_t afl_custom_fuzz(void* data, uint8_t* buf, size_t buf_size, uint8_t** out_buf,
                       uint8_t* add_buf, size_t add_buf_size, size_t max_size) {
    PdfMutator* m = static_cast<PdfMutator*>(data);
    PdfDocument doc;
    if (buf && buf_size) doc.ParsePartialFromArray(buf, static_cast<int>(buf_size));

    // Optional structure-aware cross-over with the splice partner AFL provides.
    if (add_buf && add_buf_size) {
        PdfDocument other;
        if (other.ParsePartialFromArray(add_buf, static_cast<int>(add_buf_size)))
            m->mutator.CrossOver(other, &doc, max_size ? max_size : 4096);
    }
    m->mutator.Mutate(&doc, max_size ? max_size : 4096);

    m->fuzz_out.clear();
    doc.SerializePartialToString(&m->fuzz_out);
    if (max_size && m->fuzz_out.size() > max_size) m->fuzz_out.resize(max_size);
    *out_buf = reinterpret_cast<uint8_t*>(m->fuzz_out.data());
    return m->fuzz_out.size();
}

// Conversion for the target: proto -> SerializePdf() -> PDF bytes.
size_t afl_custom_post_process(void* data, uint8_t* buf, size_t buf_size, uint8_t** out_buf) {
    PdfMutator* m = static_cast<PdfMutator*>(data);
    PdfDocument doc;
    if (buf && buf_size) doc.ParsePartialFromArray(buf, static_cast<int>(buf_size));
    m->pp_out = SerializePdf(doc);
    *out_buf = reinterpret_cast<uint8_t*>(m->pp_out.data());
    return m->pp_out.size();
}

void afl_custom_deinit(void* data) { delete static_cast<PdfMutator*>(data); }

}  // extern "C"

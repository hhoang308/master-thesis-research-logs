#include <cstdio>
#include <unistd.h>
#include "libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h"
#include "pdf.pb.h"
#include "serializer.h"
#include "GlobalParams.h"
#include "PDFDoc.h"
#include "OutputDev.h"
#include "Stream.h"

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    globalParams = new GlobalParams("");
    return 0;
}

// Minimal rendering device. The point is to make Gfx actually EXECUTE the page
// content streams: that is what triggers font loading (Tf -> GfxFont::makeFont ->
// FoFi parsers) and image setup (Do -> Gfx::doImage). The base OutputDev only
// reads *inline* image data, so we override drawImage/drawImageMask to pull the
// whole XObject image stream through its filter chain (DCT/JPX/CCITT/LZW/Flate),
// forcing the decoders to run. Pixels are discarded -- we only care about coverage.
class FuzzOutputDev : public OutputDev {
public:
    GBool upsideDown() override { return gTrue; }
    GBool useDrawChar() override { return gFalse; }
    GBool interpretType3Chars() override { return gFalse; }

    void drawImage(GfxState*, Object*, Stream *str, int /*w*/, int /*h*/,
                   GfxImageColorMap*, int* /*maskColors*/,
                   GBool /*inlineImg*/, GBool /*interpolate*/) override {
        drainStream(str);
    }
    void drawImageMask(GfxState*, Object*, Stream *str, int /*w*/, int /*h*/,
                       GBool /*invert*/, GBool /*inlineImg*/,
                       GBool /*interpolate*/) override {
        drainStream(str);
    }

private:
    static void drainStream(Stream *str) {
        if (!str) return;
        str->reset();
        while (str->getChar() != EOF) {}
        str->close();
    }
};

DEFINE_PROTO_FUZZER(const pdf_proto::PdfDocument& doc) {
    if (doc.pages_size() == 0) return;

    std::string bytes = SerializePdf(doc);

    char path[] = "/tmp/pdf_fuzz_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return;
    if (write(fd, bytes.data(), bytes.size()) != (ssize_t)bytes.size()) {
        close(fd); unlink(path); return;
    }
    close(fd);

    PDFDoc *pdf = new PDFDoc(path);
    if (pdf->isOk()) {
        // Render pages so content streams execute (fonts load, images decode).
        // Cap at 10 pages to bound per-input work (a fuzzed doc may declare many).
        int n = pdf->getNumPages();
        if (n > 10) n = 10;
        if (n >= 1) {
            FuzzOutputDev dev;
            pdf->displayPages(&dev, NULL, 1, n,
                              72, 72, 0,               // hDPI, vDPI, rotate
                              gFalse, gTrue, gFalse);  // useMediaBox, crop, printing
        }
    }
    delete pdf;
    unlink(path);
}

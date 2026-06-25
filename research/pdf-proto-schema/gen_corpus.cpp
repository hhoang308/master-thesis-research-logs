// Deterministic diverse-corpus generator for serializer/schema validation and as a
// seed corpus for the AFL++ campaign. Produces N PdfDocument protos that systematically
// exercise every schema feature (all font subtypes, all image filters, CID, embedded
// font programs, encodings, every malformation toggle, and boundary numeric values),
// using a FIXED RNG seed so the set is reproducible. Writes each as <dir>/gen_NNNNNN.txtpb
// and prints a diversity histogram so "diverse" is a measured number, not a claim.
//
// Usage: gen_corpus <out_dir> [count=2000] [seed=12345]
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <google/protobuf/text_format.h>
#include "pdf.pb.h"

using namespace pdf_proto;
static std::mt19937 rng;
static bool g_clean = false;   // --clean: sane values, no malformations (proper AFL seeds)
static bool g_binary = false;  // --binary: emit binary-serialized .pb (for AFL++ mutator) vs text .txtpb
static int  ri(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }
static bool rb(double p)     { return std::uniform_real_distribution<double>(0,1)(rng) < p; }

// A numeric value that is sometimes a boundary/pathological one (0, 1, -1, huge).
// In --clean mode always returns the sane `normal` value.
static int boundary_or(int normal) {
    if (g_clean) return normal;
    switch (ri(0, 6)) {
        case 0: return 0;
        case 1: return 1;
        case 2: return -1;
        case 3: return 2147483647;            // INT_MAX
        case 4: return (int)0xFFFFFFFF;       // -1 as uint / huge
        case 5: return ri(2, 100000);
        default: return normal;
    }
}
static std::string rbytes(int n) {
    std::string s; for (int i = 0; i < n; i++) s += (char)ri(0, 255); return s;
}
static std::string rname() {
    static const char* names[] = {"Helvetica","Times-Roman","Courier","Arial","Symbol",
                                   "weird name/with()delims","","F\x01\x02"};
    return names[ri(0, g_clean ? 4 : 7)];   // clean: only the 5 valid names
}

// Diversity counters.
struct Stats {
    long docs=0, multipage=0, withFont=0, withImage=0, withCS=0, cid=0, embFontFile=0,
         encDiff=0, toUnicode=0, malformed=0, imageMask=0, hugeBpc=0;
    long fsub[4]={0,0,0,0};        // TYPE1,TRUETYPE,TYPE3,TYPE0
    long ifilt[7]={0,0,0,0,0,0,0}; // RAW..RUNLENGTH
} st;

static void fill_descriptor(FontDescriptor* fd, bool with_file) {
    fd->set_flags(boundary_or(4));
    fd->set_italic_angle(boundary_or(0));
    fd->set_ascent(boundary_or(700));
    fd->set_descent(boundary_or(-200));
    fd->set_stem_v(boundary_or(80));
    if (rb(0.5)) { for (int i=0;i<4;i++) fd->add_font_bbox(boundary_or(i?1000:0)); }
    if (with_file) {
        auto* ff = fd->mutable_font_file();
        ff->set_key((EmbeddedFontFile::Key)ri(0,2));
        if (ff->key()==EmbeddedFontFile::FONTFILE3) ff->set_subtype(ri(0,1)?"Type1C":"OpenType");
        ff->set_program(rb(0.3) ? std::string("%!PS-AdobeFont-1.0: G\n")
                                 : rbytes(ri(4,64)));
        if (!g_clean && rb(0.3)) ff->set_length_delta(boundary_or(0));
        st.embFontFile++;
    }
}

static void fill_font(Font* f) {
    f->set_subtype((Font::Subtype)ri(0,3));
    st.fsub[f->subtype()]++;
    f->set_base_font(rname());
    if (!g_clean && rb(0.2)) f->set_omit_type(true);
    if (!g_clean && rb(0.2)) f->set_omit_subtype(true);
    bool malformed=false;
    if (f->subtype()==Font::TYPE0 && rb(0.8)) {
        auto* c = f->mutable_cid();
        c->set_cid_subtype((Font::CidFont::CidSubtype)ri(0,1));
        c->set_registry((!g_clean && rb(0.1))?"A(d)obe":"Adobe");
        c->set_ordering("Identity");
        c->set_encoding("Identity-H");
        if (rb(0.5)) c->set_cid_to_gid_map_stream(rbytes(ri(2,32)));
        int wn = ri(0,6); for (int i=0;i<wn;i++) c->add_w(boundary_or(1000));
        c->set_dw(boundary_or(1000));
        if (rb(0.6)) fill_descriptor(c->mutable_descendant_descriptor(), rb(0.6));
        st.cid++;
    } else {
        if (rb(0.6)) fill_descriptor(f->mutable_font_descriptor(), rb(0.6));
        if (rb(0.5)) f->set_base_encoding((Font::BaseEncoding)ri(0,4));
        if (rb(0.4)) {
            int dn = ri(1,5);
            for (int i=0;i<dn;i++){ auto* d=f->add_differences(); d->set_code(boundary_or(65)); d->set_name(rname()); }
            st.encDiff++;
        }
        if (rb(0.4)) { f->set_first_char(boundary_or(32)); int wn=ri(0,8); for(int i=0;i<wn;i++) f->add_widths(boundary_or(500)); }
    }
    if (rb(0.3)) { f->set_to_unicode("/CIDInit /ProcSet findresource begin end\n"); st.toUnicode++; }
    if (f->omit_type()||f->omit_subtype()) malformed=true;
    if (malformed) st.malformed++;
}

static void fill_image(ImageXObject* im) {
    im->set_filter((ImageXObject::Filter)ri(0,6));
    st.ifilt[im->filter()]++;
    im->set_width(boundary_or(4));
    im->set_height(boundary_or(4));
    int bpc = boundary_or(8); im->set_bits_per_component(bpc);
    if ((unsigned)bpc > 16) st.hugeBpc++;
    im->set_color_space((ImageXObject::ColorSpace)ri(0,2));
    if (rb(0.3)) { im->set_image_mask(true); im->set_bits_per_component(1); st.imageMask++; }
    im->set_data(rbytes(ri(0,48)));
    if (!g_clean && rb(0.3)) { im->set_length_delta(boundary_or(0)); st.malformed++; }
}

static void fill_cs(ContentStream* cs) {
    cs->set_filter(rb(0.5)?ContentStream::FLATE:ContentStream::NONE);
    cs->set_raw_content(rb(0.5) ? std::string("BT /F0 12 Tf (x) Tj ET q Q") : rbytes(ri(0,64)));
    if (!g_clean && rb(0.4)) { cs->set_length_delta(boundary_or(0)); st.malformed++; }
    if (!g_clean && cs->filter()==ContentStream::FLATE && rb(0.4)) { cs->set_skip_compression(true); st.malformed++; }
}

int main(int argc, char** argv) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    std::vector<std::string> pos;
    for (int i=1;i<argc;i++){ std::string a=argv[i];
        if(a=="--clean") g_clean=true; else if(a=="--binary") g_binary=true; else pos.push_back(a); }
    if (pos.empty()) { fprintf(stderr,"usage: %s <out_dir> [count=2000] [seed=12345] [--clean] [--binary]\n",argv[0]); return 1; }
    std::string dir = pos[0];
    long count = (pos.size()>=2)?atol(pos[1].c_str()):2000;
    unsigned seed = (pos.size()>=3)?(unsigned)atol(pos[2].c_str()):12345u;
    mkdir(dir.c_str(), 0775);
    rng.seed(seed);

    for (long i=0;i<count;i++) {
        PdfDocument doc;
        int npages = ri(1,3); if (i%50==0) npages = ri(1,4);
        bool hasF=false,hasI=false,hasC=false;
        for (int p=0;p<npages;p++) {
            Page* pg = doc.add_pages();
            pg->set_width((float)boundary_or(612));
            pg->set_height((float)boundary_or(792));
            int ncs=ri(0,2); for(int k=0;k<ncs;k++){ fill_cs(pg->add_content_streams()); hasC=true; }
            int nf=ri(0,2);  for(int k=0;k<nf;k++){ fill_font(pg->add_fonts()); hasF=true; }
            int ni=ri(0,2);  for(int k=0;k<ni;k++){ fill_image(pg->add_images()); hasI=true; }
        }
        st.docs++;
        if (npages>1) st.multipage++;
        if (hasF) st.withFont++; if (hasI) st.withImage++; if (hasC) st.withCS++;

        std::string out; char fn[64];
        if (g_binary) {
            doc.SerializeToString(&out);                            // binary wire format -> AFL mutator
            snprintf(fn, sizeof(fn), "%s/gen_%06ld.pb", dir.c_str(), i);
        } else {
            google::protobuf::TextFormat::PrintToString(doc, &out); // human-readable text proto
            snprintf(fn, sizeof(fn), "%s/gen_%06ld.txtpb", dir.c_str(), i);
        }
        std::ofstream(fn, std::ios::binary).write(out.data(), out.size());
    }

    printf("=== generated %ld protos -> %s (seed %u, mode=%s, format=%s) ===\n",
           count, dir.c_str(), seed, g_clean ? "CLEAN" : "adversarial",
           g_binary ? "binary .pb" : "text .txtpb");
    printf("multipage:           %ld (%.0f%%)\n", st.multipage, 100.0*st.multipage/count);
    printf("with content stream: %ld (%.0f%%)\n", st.withCS, 100.0*st.withCS/count);
    printf("with font:           %ld (%.0f%%)   [Type1=%ld TrueType=%ld Type3=%ld Type0=%ld]\n",
           st.withFont,100.0*st.withFont/count, st.fsub[0],st.fsub[1],st.fsub[2],st.fsub[3]);
    printf("  CID(Type0) docs:   %ld   embedded FontFile: %ld   enc/diff: %ld   ToUnicode: %ld\n",
           st.cid, st.embFontFile, st.encDiff, st.toUnicode);
    printf("with image:          %ld (%.0f%%)   [RAW=%ld FLATE=%ld DCT=%ld JPX=%ld CCITT=%ld LZW=%ld RL=%ld]\n",
           st.withImage,100.0*st.withImage/count,
           st.ifilt[0],st.ifilt[1],st.ifilt[2],st.ifilt[3],st.ifilt[4],st.ifilt[5],st.ifilt[6]);
    printf("  ImageMask: %ld   huge BitsPerComponent: %ld\n", st.imageMask, st.hugeBpc);
    printf("malformation toggles fired: %ld\n", st.malformed);
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}

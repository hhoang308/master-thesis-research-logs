# Báo cáo tiến độ luận văn -- 2026-08-06

**Đề tài:** Structure-aware fuzzing cho định dạng PDF (libprotobuf-mutator + serializer tự
tính xref/Length), so sánh với AFL++ thuần trên các PDF reader ít được fuzzing (xpdf,
PoDoFo, qpdf).

Tóm tắt theo nhật ký commit (git) trong khoảng 3 tuần gần đây (16/07 -- 05/08/2026).

---

## 1. Tóm tắt nhanh (TL;DR)

- **Mở rộng bộ tái hiện CVE đã biết**: thêm **8 CVE Xpdf mới** thành module protobuf riêng
  (mỗi module = proto + serializer + verifier + PoC), tất cả đều **trigger đúng hành vi CVE
  gốc** qua kiểm chứng semantic round-trip + qpdf + (khi có) differential 4.02/4.04 vs 4.05/4.06.
- **Cải tiến (tổng quát hoá) schema của 2 CVE đã có** để "đào" quanh lỗi gốc tìm biến thể mới:
  `type3cache` (CVE-2020-25725, 25/7) và `objstmlength` (CVE-2023-3436, 05/8). Sau khi fuzz lại
  bằng schema mở rộng: **KHÔNG ra lỗi lớp mới** -- toàn bộ crash thu được vẫn là bug đã biết,
  chỉ khác hình dạng input sinh ra nó.
- **Củng cố hạ tầng fuzzing**: root-cause bằng gdb một lỗi ASan-startup-SIGSEGV giả (do
  ASLR + clang cũ), chuyển `short`/`long` sang libFuzzer **fork mode** + thêm **AFL++
  persistent mode** (chạy xuyên qua crash thay vì dừng ở lỗi đầu), build PoDoFo **instrument
  từ source** (trước đó fuzz vào binary hệ thống -- coverage-blind), viết runbook 3 pha
  (smoke/short/long).
- **Chạy nhiều campaign dài** sau khi hạ tầng ổn định: xpdf 4.06 libFuzzer 8h, PoDoFo 8h,
  jpxstream 24h+ -- **cả 3 đều 0 crash mới**; PoDoFo bão hoà coverage (~24.7k feature, gần như
  đứng yên suốt ~2h cuối).
- **7 finding** ghi nhận trong kỳ (chủ yếu 19-22/7): differential 4.02 vs 4.06 cho thấy
  **5/7 đã được vá trong 4.06** (không phải 0-day, chỉ có giá trị kiểm chứng pipeline), 1
  PoDoFo alloc-size đang đánh giá mức độ, 1 JPX-OOM chưa xác nhận.
- **Vướng mắc chính hiện tại**: pipeline tái hiện CVE + tổng quát hoá schema đều chạy đúng về
  mặt kỹ thuật, nhưng **chưa sinh ra được bug mới nào** sau 3 tuần -- cần thầy góp ý hướng đi.

---

## 2. Mở rộng bộ tái hiện CVE đã biết (8 module mới)

| CVE | Module proto | Lớp lỗi / vị trí | Ngày thêm |
|---|---|---|---|
| CVE-2022-24106 | `dctstream` | Buffer overflow -- JPEG scan-mode (interleaved) trong `DCTStream` | 26/7 |
| CVE-2022-24107 | `jpxstream` | Buffer overflow -- cấp phát/tile-part trong JPX (JPEG2000) | 26/7 |
| CVE-2023-2662 | `iccbased` | Division-by-zero -- ảnh `/ColorSpace [/ICCBased]` với `/N 0` | 01/8 |
| CVE-2023-3044 | `textpagesize` | Division-by-zero / lỗi trích xuất text trên PDF hỏng (page-size) | 01-02/8 |
| CVE-2018-7453 | `acroformloop` | Đệ quy không kiểm soát -- chu trình `/Kids` trong `/AcroForm` | 02/8 |
| CVE-2023-3436 | `objstmlength` | Deadlock -- vòng phụ thuộc `/Length` giữa các `/ObjStm` | 03/8 |
| CVE-2022-30524 | `textlargey` | Lỗi toạ độ Y quá lớn trong `TextOutputDev` | 03/8 |
| CVE-2019-9587 | `pagetreeloop` | Vòng lặp vô hạn -- chu trình object `/Pages` | 05/8 |

Mỗi module dùng chung một bộ tiêu chí kiểm chứng: canonicalization, semantic round-trip
(serialize -> parse lại), bất biến cấu trúc + xref, `qpdf --check`, và (với các CVE có mốc vá
rõ ràng) smoke test khác biệt giữa bản Xpdf dễ tổn thương và bản đã vá. Tất cả 8 module **đều
trigger đúng hành vi gốc của CVE** -- đây là phần đã "chạy được".

---

## 3. Cải tiến (tổng quát hoá) schema cho 2 CVE -- chi tiết đã thêm gì

### 3.1 Type 3 cache lifecycle knobs (CVE-2020-25725) -- 25/7

Grammar gốc chỉ mô tả đúng hình dạng UAF trong `SplashOutputDev::endType3Char`. Đã thêm 5
"knob" (tham số điều khiển hình dạng) nhắm vào **4 lớp lỗi lân cận** ngoài shape gốc:

- `recursion_pattern`: `CHAIN` / `SELF` / `MUTUAL` / `FANOUT` -- đổi kiểu đệ quy giữa các
  CharProc.
- `nested_call_timing`: gọi glyph lồng nhau trước/sau/cả hai vị trí quanh `d0`/`d1`.
- `in_progress_cache_hits`: gọi lại chính glyph đang render (nhắm cache-hit khi `cacheData`
  chưa được điền xong).
- `malformed_charproc`: chèn lỗi vào content stream của CharProc (`d1` sai số tham số, toán tử
  lạ, `q` không cân bằng, vẽ text qua font không tồn tại).
- `resource_alias_mode`: cho tên resource của trang và của CharProc cùng trỏ về một font Type 3
  (alias trùng cache key).

### 3.2 ObjStm `/Length` dependency graph (CVE-2023-3436) -- 05/8 (hôm nay)

Grammar gốc chỉ có đúng 1 hình dạng cố định (2 object stream, 1 tham chiếu `/Length` chéo).
Đã tổng quát hoá thành một **đồ thị phụ thuộc `/Length` bất kỳ**, cho phép sinh:

- object `/Length` tự tham chiếu (nằm trong chính `/ObjStm` mà nó mô tả độ dài);
- đồ thị phụ thuộc `/ObjStm` lẫn nhau hoặc có chu trình (không chỉ 2 node);
- chuỗi phụ thuộc dài hơn 2 object stream cha;
- nhiều object nén trong cùng một `/ObjStm`;
- thứ tự đánh số object phụ thuộc trước/sau khi object stream chủ được đăng ký vào xref
  (`resolve_length_before_register`).

Đã sinh 4 PDF mẫu cho 4 hình dạng mới (self-ref, chu trình 2 node, chuỗi 3 node, 4 object/1
stream) để kiểm chứng thủ công.

### Kết quả sau khi fuzz lại với schema mở rộng

| Run | Ngày | Target | Kết quả |
|---|---|---|---|
| `afl-xpdf402-type3cache-asan-run1` | 19/7 (trước lifecycle knobs) | xpdf 4.02 + Splash + ASan | 1 batch crash = đúng UAF CVE-2020-25725 gốc, không phải lỗi mới |
| `20260805T161434Z-6ccbf40-objstm404` | 05/8 | xpdf 4.04 | 155 "crash" -- nhưng log ASan cho thấy **127 là stack-overflow đã biết** (bug CFF/Type1C `cvtGlyph`, tìm thấy từ 21/7) + **~24-31 là timeout** đúng deadlock CVE-2023-3436 gốc |
| `20260803...-cff4cac-short` (x5) | 03/8 | xpdf 4.06 | chỉ 1 crash + vài `slow-unit`/`oom`, trùng lớp đệ quy CFF đã biết |

=> **0 lớp lỗi mới** sau cả hai lần tổng quát hoá schema, dù grammar giờ sinh được nhiều hình
dạng cấu trúc hơn hẳn shape gốc của CVE.

---

## 4. Củng cố hạ tầng fuzzing -- khó khăn & cách giải quyết

| Vấn đề | Nguyên nhân | Cách giải quyết |
|---|---|---|
| ASan-startup SIGSEGV giả, chập chờn ~15-30% lượt chạy | Runtime ASan tĩnh của clang < 18 SIGSEGV lúc tự khởi tạo (`AsanInitInternal` -> `mmap`) trên kernel có entropy ASLR cao -- root-cause bằng gdb backtrace + test `int main(){return 0;}` (clang-14: 12/40 lần lỗi, clang-18: 0/40) | Runner tự nhận diện compiler đã build binary: giữ ASLR nếu clang >= 18, còn lại tự re-exec toàn bộ tiến trình dưới `setarch -R` (tắt ASLR) |
| libFuzzer dừng ngay ở crash đầu tiên -- campaign dài chỉ thu được 1 bug/lần chạy | Chế độ single-process mặc định | `short`/`long` chuyển sang **fork mode** (`-fork`, `-ignore_crashes/-ignore_timeouts/-ignore_ooms`) để tiếp tục fuzz qua crash, mỗi crash lưu riêng |
| `FORK_JOBS` mặc định = `nproc` làm tràn RAM (máy 28 core / 16 GB) -- coverage đứng ở 0%, hết ngân sách thời gian mà không chạy được | Không tính đến giới hạn RAM khi chọn số worker | Đổi mặc định thành `min(nproc, 4)` |
| Fuzz PoDoFo nhưng "coverage" không đổi thật -- vì link vào `libpodofo` hệ thống (không instrument) | Chưa build PoDoFo từ source với ASan/SanitizerCoverage | Thêm `PODOFO_SOURCE_BUILD`, link `libpodofo.a` tự build có `-fsanitize=address` + coverage |
| Log PoDoFo tràn ổ đĩa (~6 GB sau 24h), làm ngợp log của libFuzzer | Debug/warning logging của PoDoFo bật mặc định | Tắt logging trong `LLVMFuzzerInitialize`; báo lỗi sanitizer vẫn in bình thường |
| Campaign xpdf 4.06 chết giữa chừng, không phải do bug thật | `GMemException` (cơ chế OOM-guard nội bộ của xpdf) không được bắt -> `std::terminate` | Bọc `displayPages` trong `try/catch` chỉ bắt riêng `GMemException`, các lỗi khác vẫn báo bình thường |
| Runner đôi khi "nuốt" mất crash thật, coi như chạy thành công | `set -e` + pipe qua `tee` làm mất exit code của lệnh fuzz | Bắt exit code qua `PIPESTATUS` thay vì exit code của `tee` |

---

## 5. Kết quả finding & campaign trong 3 tuần

### 5.1 Finding ghi nhận (19-22/7)

| Finding | Lớp lỗi | Trạng thái |
|---|---|---|
| `xpdf402-acroform-scanfield-recursion` | CWE-674 đệ quy không kiểm soát | **Đã vá ở 4.06** -- không phải bug mới |
| `xpdf402-catalog-dtor-nullderef` | CWE-476 NULL deref | **Đã vá ở 4.06** -- không phải bug mới |
| `xpdf402-uncaught-gmemexception-abort` | Exception không bắt -> abort | **Đã vá ở 4.06** -- không phải bug mới |
| `xpdf402-cff-cvtglyph-stackoverflow` | CWE-674 đệ quy charstring CFF/Type1C | **Đã vá ở 4.06** -- không phải bug mới |
| `xpdf402-cff-type1cop-toint-divzero` | CWE-369 chia cho 0 trong CFF/Type1C | **Đã vá ở 4.06** -- không phải bug mới |
| `podofo-alloc-size` | CWE-789 cấp phát không kiểm tra kích thước (`PdfPredictorDecoder`) | Đã định vị root cause; đang đánh giá DoS hay memory-safety thật |
| `xpdf402-jpx-oom` | OOM khi decode JPX | Candidate, chưa xác nhận |

### 5.2 Campaign dài sau khi có hạ tầng mới -- 0 crash mới

| Run | Target | Thời lượng | Kết quả |
|---|---|---|---|
| `20260727-3227057-long` | xpdf 4.06 (module jpxstream) | ~24h | 0 crash; `cov 5904 / ft 28625` cuối kỳ |
| `20260801-72533a0-long` | xpdf 4.06 (harness đã sửa GMemException) | 8h (28800s) | 0 crash; `cov 6060 / ft 30780` cuối kỳ |
| `20260802-1dfcfa6-long-podofo` | PoDoFo 0.9.7 (instrument từ source) | 8h (28945s) | 0 crash; `cov 23663 / ft 24721` -- **bão hoà**, gần như không đổi trong ~2h cuối |

---

## 6. Vướng mắc hiện tại

Đúng như đã trao đổi: pipeline **chạy đúng về kỹ thuật** ở cả hai việc --

1. **Tái hiện CVE**: 8 module mới đều trigger đúng hành vi CVE gốc, verifier + qpdf +
   differential đều pass.
2. **Tổng quát hoá schema**: đã mở rộng thành công grammar của `type3cache` (5 knob, 4 lớp lỗi
   mục tiêu) và `objstmlength` (đồ thị phụ thuộc `/Length` bất kỳ thay vì 1 hình dạng cố định).

Nhưng **cả hai đều chưa sinh ra được lớp lỗi mới nào** khi fuzz lại: mọi crash thu được (kể cả
với schema đã mở rộng, kể cả trong các campaign dài 8-24h) đều trùng khớp với bug đã biết từ
trước (CVE gốc, hoặc bug CFF tìm được hồi 21-22/7), không có dạng crash lạ.

**Một vài giả thuyết (chưa chắc chắn, cần thầy góp ý):**

1. **Coverage bão hoà**: campaign PoDoFo 8h gần như đứng yên ở `ft ~24.7k` trong ~2h cuối --
   có thể corpus/dictionary hiện tại đã "vét cạn" không gian mà mutator hiện có thể tới; cần
   thêm bậc tự do cấu trúc mới hoặc dictionary/hint tốt hơn để nhảy sang vùng code path khác.
2. **Target 4.02 đã quá cũ**: phần lớn finding kỳ này khi differential đều "đã vá ở 4.06" --
   nghĩa là không gian bug dễ trúng nhất trên 4.02 gần như đã được các fuzzer trước (kể cả
   người viết xpdf) quét qua. Bản mới hơn (4.05/4.06) sạch hơn nên khó trúng hơn nhưng có khả
   năng còn bug thật hơn.
3. **Grammar tổng quát hoá vẫn nằm trong "họ hình dạng" của CVE gốc** (structural
   neighborhood) -- các knob thêm vào vẫn xoay quanh đúng object/toán tử liên quan tới CVE, nên
   dù đa dạng hình dạng hơn, code path chạm tới có thể vẫn là cùng một vùng đã biết.
4. **Thời lượng/ngân sách còn ngắn** so với không gian trạng thái (một số run chỉ vài trăm giây
   đến vài giờ) để tăng khả năng trúng biến thể hiếm.

---

## 7. Việc tiếp theo (đề xuất, chờ ý kiến thầy)

- [ ] Chạy campaign dài hơn (24h+) trực tiếp trên schema `objstmlength`/`type3cache` đã tổng
      quát hoá (mới chỉ chạy smoke/short) để loại trừ khả năng "chưa đủ thời gian".
- [ ] Cân nhắc chuyển trọng tâm fuzz từ xpdf 4.02 sang 4.05/4.06 (ít bug đã biết hơn, khả năng
      trúng 0-day cao hơn dù coverage khó tăng hơn).
- [ ] Xem xét thêm dictionary/token gợi ý hoặc CmpLog/laf-intel để vượt qua các so sánh
      magic-byte/checksum đang chặn mutator.
- [ ] Kết hợp nhiều module (đa-CVE) trong cùng một corpus/campaign thay vì fuzz tách rời từng
      module, để tăng khả năng tổ hợp gây lỗi mới giữa các thành phần.
- [ ] Đóng gói lại 5 finding "đã vá ở 4.06" thành phần minh hoạ pipeline (không phải kết quả
      chính), tập trung báo cáo vào `podofo-alloc-size` và `jpx-oom` (đang cần xác nhận thêm).

---

## 8. Câu hỏi cho thầy/cô

1. Sau 3 tuần, pipeline tái hiện CVE + tổng quát hoá schema đều chạy đúng nhưng chưa ra bug
   mới -- thầy có gợi ý cụ thể nào để "đào sâu" hơn quanh một CVE (ví dụ: kết hợp nhiều biến dị
   cấu trúc cùng lúc, dùng coverage differential giữa các phiên bản để khoanh vùng code path
   nghi ngờ...) không?
2. Có nên tiếp tục dồn công sức vào xpdf 4.02 (nhiều bug đã biết, dễ kiểm chứng pipeline) hay
   chuyển hẳn sang bản mới nhất / PoDoFo / qpdf để tăng cơ hội tìm bug thật, dù khó hơn?
3. Tiêu chí "kết quả" của luận văn có bắt buộc phải là 0-day mới hay không, hay việc xây dựng
   được pipeline tái hiện + tổng quát hoá CVE có hệ thống (methodology contribution) kèm đo
   lường định lượng (coverage, valid-rate) đã được xem là đủ đóng góp?
4. Với 2 finding còn "mở" (`podofo-alloc-size`, `xpdf402-jpx-oom`), thầy có gợi ý hướng xác
   minh thêm (memory-safety thật hay chỉ DoS) không?

## CVE-2024-7973

### References

CVE Information: https://nvd.nist.gov/vuln/detail/cve-2024-7973
Issue: https://issues.chromium.org/issues/345518608
Commit: https://pdfium-review.googlesource.com/c/pdfium/+/120331

### Description

Heap buffer overflow in PDFium in Google Chrome prior to 128.0.6613.84 allowed a remote attacker to perform an out of bounds memory read via a crafted PDF file. (Chromium security severity: Medium)

### Vulnerability Detail
Tolerate extra JPEG2000 image channels in CPDF_DIB::LoadJpxBitmap()
JPEG2000 images can have more color channels than the number of color components. Thus checking for exactly 4 channels may be too strict. In the case where the `JpxDecodeAction::kConvertArgbToRgb` action is in use, remove the channel check when selecting the output image format. Instead, use CHECK_GE() to make sure the code that chose to use `kConvertArgbToRgb` only did that when there are 4 or more channels.
#### Version

#### POC Files
```
%PDF-1.5
1 0 obj <<
    /Pages 2 0 R
    /Type /Catalog
>> endobj
2 0 obj <<
    /Count 1
    /Kids [ 3 0 R ]
    /Type /Pages
>> endobj
3 0 obj <<
    /Contents 4 0 R
    /MediaBox [ 0 0 24.0 24.0 ]
    /Parent 2 0 R
    /Resources <<
        /XObject <<
            /Im0 5 0 R
        >>
    >>
    /Type /Page
>> endobj
4 0 obj <<
    /Length 42
>>
stream
q
24.000000 0 0 24.000000 0 0 cm
/Im0 Do
Q
endstream
endobj
5 0 obj <<
    /BitsPerComponent 8
    /ColorSpace /DeviceRGB
    /Filter [ /JPXDecode ]
    /Height 32
    /Length 520
    /Subtype /Image
    /Type /XObject
    /Width 32
>>
stream

```
#### Explain

`CHECK_GE` là một Security Assertion với mục đích kill chương trình nếu có điều kiện nào đó không thỏa mãn. 
```
#define CHECK(condition)          \
  do {                            \
    if (UNLIKELY(!(condition))) { \
      pdfium::ImmediateCrash();   \
    }                             \
  } while (0)
```
Trong hàm `ImmediateCrash` sẽ gọi trực tiếp các lệnh CPU để gọi các hardware exception thay vì các system call để đóng chương trình. Mục đích của việc này là để đảm bảo chắc chắn không còn một lệnh dư thừa nào nữa được thực hiện mà phải crash chương trình luôn. Cụ thể, khi điều kiện trong `CHECK` không thỏa mãn, nguyên nhân thường thấy là do memory đã bị corrupt, nếu chương trình còn thực hiện tiếp các lệnh nào đó nữa (kể cả lệnh `exit()` hay `abort()` thì vẫn là một chuỗi >= 2 lệnh trong CPU) vẫn có thể bị hacker tận dụng được. Ngoài ra khi sử dụng cách này để terminate chương trình, crash dump sẽ trỏ về chính xác dòng code fail, nếu sử dụng `abort()` để terminate thì crash dump sext rỏ về đâu đó trong thư viện C, làm dev phải trace ngược lại một đoạn mới ra chỗ thực sự gây lỗi.
Trong file PDF POC có nội dung yêu cầu chuyển đổi hình ảnh này từ ARGB sang RGB, do đó trong file PDF POC này cần chứa 4 channels màu, tuy nhiên file này lại chỉ chứa 3 channels nên đã rơi xuống trường hợp `else` cuối cùng của lệnh điều kiện. Trong phần `else` cuối cùng này lại khai báo `format = FXDIB_Format::kRgb` và `image_info.width = (image_info.width * image_info.channels + 2) / 3;` nên chương trình chỉ cấp phát 3 bytes mỗi pixel thôi. Sau đó, `convert_argb_to_rgb` lại là `true` nên file này lại được xử lý như thế nó chứa 4 channel màu.

-----------------------

"/Filter [ /JPXDecode ]"


```
template <typename T>
  pdfium::span<const T> GetScanlineAs(int line) const {
    return fxcrt::reinterpret_span<const T>(GetScanline(line))
        .first(static_cast<size_t>(GetWidth()));
  }
 ```

 ```
 template <typename T>
  pdfium::span<const T> GetScanlineAs(int line) const {
    return fxcrt::truncating_reinterpret_span<const T>(GetScanline(line));
  }
 ```
--------
1. `JPEG 2000 data` in PDF format?
2. raw data or standard deflation in PDF format?
3. What is stream?
4. ISO and Adobe PDF format standard? the difference ? what should I learn?
5. Explain this object in the pdf content?
```
5 0 obj <<
    /Type /XObject            % It is an external resource
    /Subtype /Image           % It is an image (not a form or font)
    /Width 32                 % LIE #1: "Allocated memory for 32 pixels wide"
    /Height 32                % LIE #1: "Allocated memory for 32 pixels high"
    /ColorSpace /DeviceRGB    % "This is standard RGB (3 channels)"
    /BitsPerComponent 8       % 8 bits per color
    /Filter [ /JPXDecode ]    % "Use the Vulnerable CJPX_Decoder!"
    /Length 520               % Size of the compressed data block
>>
stream
... (The Malicious JPEG 2000 Data) ...
endstream
```
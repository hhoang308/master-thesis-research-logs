#include "modules/textpagesize/text_page_size_serializer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {

constexpr char kDefaultHeaderMagic[] = "%iDF";
constexpr char kDefaultPageWidth[] = "612.0000";
constexpr char kDefaultPageHeight[] = "79299999999999999999.0000";
constexpr char kMediaBoxNeedle[] =
    "/MediaBox [0 0 612.0000 79299999999999999999.0000]";
constexpr char kFlateTailAnchor[] = "/Filter /FlateDecode";

constexpr char kPublicTriggerB64[] =
R"(JWlERgoyIDAgb2JqDQo8PA0KL1Q+Pg0KYmoNCg0KMyBCIG9hIHd4dC4gQW5kIIYZVA0KLzkNCi9L
aWRzIC9GMSAwIFIgNiAwIFIgXSANCh0NCi9Db250ZW50cyA3IDAgVw10IDgCMCA+DQoEAC8wMDAw
MDAwMDBzID0SMSA+DTUgjCBvPDwAAXN0CmVuA+hiajQgMCBqYmoNbW9yZSB0ZZJ0LiBBbmQgbW//
/zw8IC9MZW5ndGggMTA3NAo5OTIwIFRkDQooIEFuZCBtb3JlIHRleHQAAEFuZCBtb3JlIHRleHQu
ICkgVGoNCkVUDQpCVA0KL0YxIDAwMTAgVGYNCjY5LjI1MDAgNTY5LjA4ODAgVGQNCiggQW5kIG1v
cmUgdGV4dC4gQW5kIG1vcmUgdGV4dC4gQYD/IG1vcjAgdGV4dC4gQW5kIG1vcmUgQ29udGkgQW5k
IG1vcmUgKSBUag0KRVQNCkJUDQovRjEgMDAxMCBUZg0KNjkuMjUwMCA1NTdkMTM2MCBUZA0KKCB0
ZXh0LiBBbmQgbW9yZSB0ZXh0LiBBbmQgbW9yZSB0ZXh0LiBFdmVuIG1vcmUuIHRleHQubnVlZCBv
biBwYWdlIDIgLi4uKSBUag0KRVQM7GWOZHN0cmVhbQ0KZW5kb2JqDQoNCjYgMCBvYmoNCjw8DQov
VHlwZSAvUGFnZQ0KL1BhcmVudCAzIDAgUg0KL1Jlc291cmNlcyA8PA0KL0ZvbnQgPDwNCi9GMSA5
IDAgUiANCj4+DQovUHJvY1NldCA4IDAgUg0KPj4NCi9NZWRpYUJveCBbMCAwIDYxMi4wMDAwIDc5
Mjk5OTk5OTk5OTk5OTk5OTk5LjAwMDBdDQovQ29udGVudHMgNyAwIFINCj4+DQplbmRvYmoNCg0K
NyAwIG9iag0KPDwgL0xlbmd0aCA2NzYgPj4NCnN0cmVhbQ0KMiBKDQpCVA0KMCAwIDAgcmc3LjM3
NTAgNzIyLjI4MDAgVGQNCiggU2ltcGxlIFBERiBGaZNlIDIgKSBUag0KRVQNCkJUDQovRjEgMDAx
MCBUZg0KNjkuMjUwMCA2ODguNjA4MCBUZA0KKCAuLi5jb250aW51ZWQgZnJvbSBwYWdlIDEuIFll
dCBtb3JlIHRleHQgKSBUdC4gKSBUag1vZGkNCkJUDSovRjEgMDAxMGoNCkVUDQpCVA0KL0YxIDAw
NzAgVGYNCjY5LjI1MDAgNjc2LjY1NjAgVGQNCiggQW5kIG1vcnN0cmUrbRoKMiBKMCAwIHIQMjcg
VGYuMw0KPDzqCh4wZSB0ZXh0LiBBbmQgbU9yZSB0ZSAvQ291bnQKCjMgMCB4dC4gZCBtb09lIHQN
gEJUDSBBCi9GMSAwMDEwIFRmLjI1MDAgNjE2LmQANjAgVGQNCiggKSBUAP/1QHp6enkuIEEgA2Ug
gGV4dC4gQW5kIDExIDDA/y4yNTAwDQ5CVDAgMCByZw0KL3//IDByIDAgbyBhbmQganVzdCBdIA0K
XQ0KL0NvbnRlbnRzIDcgMCBSDWVuMCA1IGljYQ0EAAAAZXhuQWp4////Y29kaW4+DQplbmRvICB0
ZXh0LiBBbmQgbW9ybW///3//IFRpbmcuICBNb3JlLC8wMFRkDQovRjEgMDAxMCBUZg0KNjkuMjUw
AEA5NDQwIFRkDQooIG1vQW5kIHQwIG4gCgp0cmFpbGVyCjw8L28gMSBkIFIvUm9vdCA1IDAgUi9J
RFs8Ni4AAAD/HDYwQUU1AAEkICAgQe5kIG1vcmUyRDQyMzM1Qz5dL1ByICAKNSAwIG9iago8PC9U
eXBlIC9DYXRhbG9nIC9QYWdlcyAyIDAgUgovUGFnZUxheW9ldC9TTm9uZQovUGFnZdsxAS9NZShh
ZGF0YZJAIDAgUoD///8vSFtobmRvYmoKNiAwIG9igAohPC9UeWpqanBAL1BhZ2UvTWX/f2FCb3gg
WzAgMCA1OTUgODQyXQovUm90YXRlIDAvUGGIZW90IDIgMCBSCi9SZXNvdXJjZXM8PC9QZG98U2V8
WxBQKkYgL1RleHZdCi9FeHRHU3RhdGUgCi+Ab250ZW50cyA3IDAvgAo+PgplbmRvYmsKNyAwIG9i
ago8PC9MZVVnAAAAAAIwIFIvRmlsdGVyIC9GbGF0ZURlY29kZT4+CnN0cmVhbQp4nO1QsU7EMAzd
8xXZSJBqbDdO4hUBA2IAlA0xoNPdgUQlTrfw+aStykViYYCtsWQ58Xsv9jtYIAAEgYhjLMVmMBcA
AAEAAO3eHEyGhjxTr603g70sFZptJZadQVDVQAAAgP//olApg3lyxRAAbiCkYWdlIDI7Li4uKSBU
aibvoUog7KJ9ixxGfhB//4zvJ8WmbBWXM/CziiD0SN96wlIFnsutoTGyLVumXs/mPc5X0//ZrNWA
1VU2MEFFNTBBRDBEPj4uLtEuLi4uLi4uLi4uLi4uLhkuLi4uLi4uq6urq6urq6urq6urq6uryKur
LlR5cOgDAAD6ZW7w+aStykViYYCtsWQ58Xsv9jtYIAAEgohjLMVmMBeMye4BAO3eHEyGhjxTr603
g70sFZptJZadQVDVQACPw6KhSiDso32LHEZ+AAEAAO8nxaZsFZcz8LOKI/RI33rCUgWey62hg70a
FZptJZadQVDVQACPw6JNolApg3lyxRAAmm0llp1BUNVAAACA//+iUCmDeXLFdwBuIIhhZ2UgMjsu
Li4pIFRqDe+hSiDson2LHEZ+EH//gO8nxf9sFZcz8LOKIPRI33rCUgWey62hMbItW6Zez+Y9zlfT
/9ms1YDVVTYwQUU1MEFEMEQ+Pi4u0S4uLi4uLi4uLi4uLi4uGS4uLi4uLi6rq6urq6urq6urq6ur
q6vrq6suVHlw6AMAAPplbvD5pK3KRWJhgK2xZDnxey/2O1ggAASCiGMsxWYwF4zJ7gEA7d4cTIaG
PFOvrTeDvSwVmm0llp1BUNVAAI/Dok2iUCmDeXLFEABuIIhhZ2UgMjsuLi4pIFRqDe+hSiDson2L
HEZ+AAEAAO8nxaZsFZcz8LOKI/RI33rCUgWey62hMbItW6ZezwAABADT/9ms1YDVVTZ4bkGGhjwA
AAQAg70sFZptJZadQVDVQACPw6JNolApg3lyxRAAbiCIYWdlIDI7LtEuKSBUag3voUog7KJ9ixxG
fgABAADvJ8UAbBWXM/CziiD0SAACwlIFnsutoTGyLVumXs8AAAQA0//ZrNWAAAAAf1JQUFBQUFBQ
UFAtUFBQUFAKMS9QcmV2IDM5NziTPgpzdGFydHg+CmVuZG9iago7MkFpdC4gKSBUag0KMDP7lK6L
AajxBWVuZHN0cmVhbQ==)";

int Base64Value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

std::string DecodeBase64(const std::string& input) {
  std::string out;
  int val = 0;
  int valb = -8;
  for (unsigned char c : input) {
    if (std::isspace(c)) {
      continue;
    }
    if (c == '=') {
      break;
    }
    int d = Base64Value(c);
    if (d < 0) {
      continue;
    }
    val = (val << 6) | d;
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xff));
      valb -= 8;
    }
  }
  return out;
}

bool ReplaceOnce(std::string* data, const std::string& needle,
                 const std::string& replacement) {
  const size_t pos = data->find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  data->replace(pos, needle.size(), replacement);
  return true;
}

std::string SanitizeHeaderMagic(const std::string& header) {
  std::string out;
  for (unsigned char ch : header) {
    if (ch >= 0x21 && ch <= 0x7e) {
      out.push_back(static_cast<char>(ch));
    }
    if (out.size() == 4) {
      break;
    }
  }
  if (out.empty()) {
    out = kDefaultHeaderMagic;
  }
  if (out.size() < 4) {
    out.append(4 - out.size(), 'X');
  } else if (out.size() > 4) {
    out.resize(4);
  }
  if (out[0] != '%') {
    out[0] = '%';
  }
  return out;
}

bool ParsePositiveFinite(const std::string& token) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const double value = std::strtod(token.c_str(), &end);
  return end == token.c_str() + token.size() && std::isfinite(value) &&
         value > 0.0;
}

std::string SanitizeDecimalToken(const std::string& token,
                                 const std::string& fallback) {
  std::string out;
  bool saw_dot = false;
  for (unsigned char ch : token) {
    if (std::isdigit(ch)) {
      out.push_back(static_cast<char>(ch));
    } else if (ch == '.' && !saw_dot) {
      if (out.empty()) {
        out.push_back('0');
      }
      out.push_back('.');
      saw_dot = true;
    }
    if (out.size() >= 32) {
      break;
    }
  }
  if (!out.empty() && out.back() == '.') {
    out.push_back('0');
  }
  if (!ParsePositiveFinite(out)) {
    return fallback;
  }
  return out;
}

}  // namespace

pdf_textpagesize::TextPageSizeDocument CanonicalizeTextPageSizeDocument(
    const pdf_textpagesize::TextPageSizeDocument& doc) {
  pdf_textpagesize::TextPageSizeDocument canon;
  canon.set_header_magic(SanitizeHeaderMagic(doc.header_magic()));
  canon.set_page_width_decimal(
      SanitizeDecimalToken(doc.page_width_decimal(), kDefaultPageWidth));
  canon.set_page_height_decimal(
      SanitizeDecimalToken(doc.page_height_decimal(), kDefaultPageHeight));
  canon.set_keep_flate_tail(doc.keep_flate_tail());
  return canon;
}

std::string SerializeTextPageSizePdf(
    const pdf_textpagesize::TextPageSizeDocument& doc) {
  const pdf_textpagesize::TextPageSizeDocument canon =
      CanonicalizeTextPageSizeDocument(doc);
  std::string pdf = DecodeBase64(kPublicTriggerB64);

  if (pdf.size() >= 4) {
    pdf.replace(0, 4, canon.header_magic());
  }

  const std::string media_box =
      std::string("/MediaBox [0 0 ") + canon.page_width_decimal() + " " +
      canon.page_height_decimal() + "]";
  (void)ReplaceOnce(&pdf, kMediaBoxNeedle, media_box);

  if (!canon.keep_flate_tail()) {
    const size_t flate = pdf.find(kFlateTailAnchor);
    if (flate != std::string::npos) {
      pdf.erase(flate);
      pdf += "\n%%EOF\n";
    }
  }

  return pdf;
}

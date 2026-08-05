#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "modules/objstmlength/objstm_length.pb.h"
#include "modules/objstmlength/objstm_length_serializer.h"

namespace {

constexpr char kDefaultHeader[] = "%PDF-1.5";
constexpr uint32_t kMaxPaddingSpaces = 32;
constexpr size_t kXrefEntryWidth = 7;

struct CommandResult {
  int status = -1;
  std::string output;
};

struct XrefEntry {
  uint8_t type = 0;
  uint32_t field2 = 0;
  uint16_t field3 = 0;
};

struct ParsedCompressedObject {
  int host_objstm = 0;
  int index_in_host = 0;
  std::string body;
};

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string ShellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

bool CommandExists(const char* cmd) {
  std::string probe = "command -v ";
  probe += cmd;
  probe += " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

CommandResult RunCommandCapture(const std::string& cmd) {
  CommandResult result;
  FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
  if (!pipe) {
    return result;
  }
  char buf[512];
  while (fgets(buf, sizeof(buf), pipe)) {
    result.output += buf;
  }
  result.status = pclose(pipe);
  return result;
}

bool WriteTemp(const std::string& data, std::string* path) {
  char tmp[] = "/tmp/objstmlength_verify_XXXXXX";
  int fd = mkstemp(tmp);
  if (fd < 0) {
    return false;
  }
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = write(fd, data.data() + off, data.size() - off);
    if (n <= 0) {
      close(fd);
      unlink(tmp);
      return false;
    }
    off += static_cast<size_t>(n);
  }
  close(fd);
  *path = tmp;
  return true;
}

std::string Trim(const std::string& s) {
  size_t start = 0;
  while (start < s.size() &&
         std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

bool ParsePositiveIntToken(const std::string& token, int* out) {
  if (token.empty()) {
    return false;
  }
  int value = 0;
  for (char c : token) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  if (value <= 0 || value > 1'000'000) {
    return false;
  }
  *out = value;
  return true;
}

bool ParseNonNegativeIntToken(const std::string& token, int* out) {
  if (token.empty()) {
    return false;
  }
  int value = 0;
  for (char c : token) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  if (value < 0 || value > 1'000'000) {
    return false;
  }
  *out = value;
  return true;
}

bool ParseCompressedObjectSpec(const std::string& raw, int* obj_num,
                               std::string* body) {
  const size_t first_space = raw.find(' ');
  if (first_space == std::string::npos) {
    return false;
  }
  size_t gen_start = first_space + 1;
  while (gen_start < raw.size() && raw[gen_start] == ' ') {
    ++gen_start;
  }
  const size_t second_space = raw.find(' ', gen_start);
  if (second_space == std::string::npos) {
    return false;
  }
  int parsed_obj = 0;
  int parsed_gen = 0;
  if (!ParsePositiveIntToken(raw.substr(0, first_space), &parsed_obj) ||
      !ParseNonNegativeIntToken(raw.substr(gen_start, second_space - gen_start),
                                &parsed_gen) ||
      parsed_gen != 0) {
    return false;
  }
  *obj_num = parsed_obj;
  *body = raw.substr(second_space + 1);
  return true;
}

bool ParseIndirectRef(const std::string& ref, int* obj_num) {
  const size_t first_space = ref.find(' ');
  if (first_space == std::string::npos) {
    return false;
  }
  const size_t second_space = ref.find(' ', first_space + 1);
  if (second_space == std::string::npos) {
    return false;
  }
  if (ref.substr(second_space + 1) != "R") {
    return false;
  }
  int parsed_obj = 0;
  int parsed_gen = 0;
  if (!ParsePositiveIntToken(ref.substr(0, first_space), &parsed_obj) ||
      !ParseNonNegativeIntToken(ref.substr(first_space + 1,
                                           second_space - (first_space + 1)),
                                &parsed_gen) ||
      parsed_gen != 0) {
    return false;
  }
  *obj_num = parsed_obj;
  return true;
}

std::string ExtractObjectBody(const std::string& pdf, int obj_num,
                              std::string* error) {
  const std::string anchor = std::to_string(obj_num) + " 0 obj\n";
  const size_t start = pdf.find(anchor);
  if (start == std::string::npos) {
    *error = "missing object anchor " + anchor;
    return {};
  }
  const size_t body_start = start + anchor.size();
  const size_t end = pdf.find("\nendobj\n", body_start);
  if (end == std::string::npos) {
    *error = "missing endobj for object " + std::to_string(obj_num);
    return {};
  }
  return pdf.substr(body_start, end - body_start);
}

std::string ExtractObjectDict(const std::string& body, std::string* error) {
  const size_t stream = body.find("\nstream\n");
  if (stream == std::string::npos) {
    if (!Contains(body, "<<")) {
      *error = "missing object dictionary";
      return {};
    }
    return body;
  }
  return body.substr(0, stream);
}

std::string ExtractStreamData(const std::string& body, std::string* error) {
  const size_t stream = body.find("stream\n");
  if (stream == std::string::npos) {
    *error = "missing stream keyword";
    return {};
  }
  const size_t data_start = stream + 7;
  const size_t end = body.find("\nendstream", data_start);
  if (end == std::string::npos) {
    *error = "missing endstream keyword";
    return {};
  }
  return body.substr(data_start, end - data_start);
}

bool ExtractMediaBoxInts(const std::string& page_body, int* width, int* height) {
  const std::string key = "/MediaBox [0 0 ";
  size_t pos = page_body.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  const size_t width_end = page_body.find(' ', pos);
  if (width_end == std::string::npos) {
    return false;
  }
  const size_t height_end = page_body.find(']', width_end + 1);
  if (height_end == std::string::npos) {
    return false;
  }
  return ParsePositiveIntToken(page_body.substr(pos, width_end - pos), width) &&
         ParsePositiveIntToken(
             Trim(page_body.substr(width_end + 1,
                                   height_end - (width_end + 1))),
             height);
}

bool ExtractSingleInt(const std::string& dict, const std::string& key, int* out) {
  size_t pos = dict.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  while (pos < dict.size() &&
         std::isspace(static_cast<unsigned char>(dict[pos]))) {
    ++pos;
  }
  size_t end = pos;
  while (end < dict.size() &&
         std::isdigit(static_cast<unsigned char>(dict[end]))) {
    ++end;
  }
  return ParseNonNegativeIntToken(dict.substr(pos, end - pos), out);
}

bool ExtractIndirectLengthRef(const std::string& dict, std::string* ref) {
  size_t pos = dict.find("/Length ");
  if (pos == std::string::npos) {
    return false;
  }
  pos += 8;
  while (pos < dict.size() &&
         std::isspace(static_cast<unsigned char>(dict[pos]))) {
    ++pos;
  }
  size_t end = pos;
  int spaces = 0;
  while (end < dict.size() && spaces < 2) {
    if (dict[end] == ' ') {
      ++spaces;
    }
    ++end;
  }
  if (end >= dict.size() || dict[end] != 'R') {
    return false;
  }
  *ref = dict.substr(pos, end - pos + 1);
  return true;
}

bool ExtractContentsRef(const std::string& page_body, int* obj_num) {
  const std::string key = "/Contents ";
  size_t pos = page_body.find(key);
  if (pos == std::string::npos) {
    return false;
  }
  pos += key.size();
  size_t end = pos;
  while (end < page_body.size() &&
         std::isdigit(static_cast<unsigned char>(page_body[end]))) {
    ++end;
  }
  return ParsePositiveIntToken(page_body.substr(pos, end - pos), obj_num);
}

uint32_t ReadBe32(const std::string& s, size_t off) {
  return (static_cast<uint32_t>(static_cast<unsigned char>(s[off])) << 24) |
         (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 1])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(s[off + 2])) << 8) |
         static_cast<uint32_t>(static_cast<unsigned char>(s[off + 3]));
}

uint16_t ReadBe16(const std::string& s, size_t off) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(static_cast<unsigned char>(s[off])) << 8) |
      static_cast<uint16_t>(static_cast<unsigned char>(s[off + 1])));
}

bool ParseXrefEntries(const std::string& data, int size,
                      std::vector<XrefEntry>* entries, std::string* error) {
  if (data.size() != static_cast<size_t>(size) * kXrefEntryWidth) {
    *error = "xref payload size does not match /Size";
    return false;
  }
  entries->clear();
  for (size_t i = 0; i < data.size(); i += kXrefEntryWidth) {
    XrefEntry entry;
    entry.type = static_cast<uint8_t>(data[i]);
    entry.field2 = ReadBe32(data, i + 1);
    entry.field3 = ReadBe16(data, i + 5);
    entries->push_back(entry);
  }
  return true;
}

bool ParseObjectStreamPayload(const std::string& payload, int n, int first,
                              int host_objstm,
                              std::map<int, ParsedCompressedObject>* objects,
                              std::string* error) {
  if (n <= 0 || first < 0 || static_cast<size_t>(first) > payload.size()) {
    *error = "invalid /ObjStm /N or /First";
    return false;
  }
  std::istringstream header(payload.substr(0, static_cast<size_t>(first)));
  std::vector<int> nums;
  std::vector<int> offsets;
  for (int i = 0; i < n; ++i) {
    int obj_num = 0;
    int offset = 0;
    if (!(header >> obj_num >> offset) || obj_num <= 0 || offset < 0) {
      *error = "malformed object-stream header table";
      return false;
    }
    nums.push_back(obj_num);
    offsets.push_back(offset);
  }
  const std::string bodies = payload.substr(static_cast<size_t>(first));
  for (int i = 0; i < n; ++i) {
    const int start = offsets[i];
    const int end = (i + 1 < n) ? offsets[i + 1] : static_cast<int>(bodies.size());
    if (start < 0 || end < start || static_cast<size_t>(end) > bodies.size()) {
      *error = "malformed object-stream body offsets";
      return false;
    }
    int body_end = end;
    if (i + 1 < n && body_end > start && bodies[static_cast<size_t>(body_end - 1)] == '\n') {
      --body_end;
    }
    (*objects)[nums[i]] = ParsedCompressedObject{
        host_objstm, i, bodies.substr(static_cast<size_t>(start),
                                      static_cast<size_t>(body_end - start))};
  }
  return true;
}

uint32_t FindObjectOffset(const std::string& pdf, int obj_num) {
  const std::string anchor = std::to_string(obj_num) + " 0 obj\n";
  const size_t pos = pdf.find(anchor);
  return pos == std::string::npos ? 0 : static_cast<uint32_t>(pos);
}

bool MeaningfulQpdfOutcome(const std::string& pdf, std::string* why) {
  if (!CommandExists("qpdf")) {
    *why = "qpdf not installed; skipped";
    return true;
  }
  std::string path;
  if (!WriteTemp(pdf, &path)) {
    *why = "failed to create temp PDF";
    return false;
  }
  const CommandResult cmd =
      RunCommandCapture("qpdf --check " + ShellQuote(path));
  unlink(path.c_str());
  if (Contains(cmd.output, "operation succeeded with warnings")) {
    *why = "accepted by qpdf --check with warnings";
    return true;
  }
  if (WIFEXITED(cmd.status) && WEXITSTATUS(cmd.status) == 0) {
    *why = "accepted by qpdf --check";
    return true;
  }
  *why = "qpdf rejected serialized PDF: " + Trim(cmd.output);
  return false;
}

bool RunPdftoppmSmoke(const std::string& pdftoppm_path, const std::string& label,
                      const std::string& pdf, bool expect_timeout,
                      bool expect_success) {
  std::string pdf_path;
  if (!WriteTemp(pdf, &pdf_path)) {
    std::fprintf(stderr, "smoke %s: failed to create temp PDF\n", label.c_str());
    return false;
  }

  const std::string out_prefix = std::string("/tmp/objstmlength_smoke_") + label;
  std::string cmd = "timeout 5s ";
  if (Contains(label, "asan")) {
    cmd = "env ASAN_OPTIONS=detect_leaks=0 timeout 5s ";
  }
  cmd += ShellQuote(pdftoppm_path) + " " + ShellQuote(pdf_path) + " " +
         ShellQuote(out_prefix);

  const CommandResult result = RunCommandCapture(cmd);
  unlink(pdf_path.c_str());
  const int status = result.status;
  if (WIFEXITED(status)) {
    const int exit_code = WEXITSTATUS(status);
    if (expect_timeout) {
      return exit_code == 124;
    }
    if (expect_success) {
      return exit_code == 0;
    }
  }
  return false;
}

bool Check(bool condition, const char* message, int* failures) {
  if (condition) {
    std::fprintf(stderr, "PASS: %s\n", message);
    return true;
  }
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++*failures;
  return false;
}

bool VerifySerializedPdf(const std::string& pdf,
                         const pdf_objstmlength::ObjstmLengthDocument& canon,
                         std::string* why) {
  const size_t header_end = pdf.find('\n');
  if (header_end == std::string::npos ||
      pdf.substr(0, header_end) != canon.pdf_header()) {
    *why = "header mismatch";
    return false;
  }
  const bool has_binary_comment =
      header_end + 6 <= pdf.size() &&
      pdf.compare(header_end + 1, 5, "%\xE2\xE3\xCF\xD3") == 0;
  if (has_binary_comment != canon.include_binary_comment()) {
    *why = "binary comment mismatch";
    return false;
  }

  std::string error;
  const std::string page_body = ExtractObjectBody(pdf, 3, &error);
  if (!error.empty()) {
    *why = error;
    return false;
  }
  int width = 0;
  int height = 0;
  if (!ExtractMediaBoxInts(page_body, &width, &height) ||
      width != static_cast<int>(canon.page_width()) ||
      height != static_cast<int>(canon.page_height())) {
    *why = "page MediaBox mismatch";
    return false;
  }

  int contents_obj_num = 0;
  if (!ExtractContentsRef(page_body, &contents_obj_num)) {
    *why = "missing /Contents reference";
    return false;
  }
  const std::string contents_body = ExtractObjectBody(pdf, contents_obj_num, &error);
  if (!error.empty()) {
    *why = error;
    return false;
  }
  const std::string contents_dict = ExtractObjectDict(contents_body, &error);
  if (!error.empty()) {
    *why = error;
    return false;
  }
  const std::string contents_data = ExtractStreamData(contents_body, &error);
  if (!error.empty()) {
    *why = error;
    return false;
  }
  int contents_length = 0;
  if (!ExtractSingleInt(contents_dict, "/Length", &contents_length) ||
      contents_length != static_cast<int>(contents_data.size()) ||
      contents_data != canon.page_content()) {
    *why = "content stream mismatch";
    return false;
  }

  std::map<int, std::string> objstm_dicts;
  std::map<int, std::string> objstm_payloads;
  std::map<int, ParsedCompressedObject> compressed_objects;
  std::map<int, int> payload_sizes;
  for (int i = 0; i < canon.objstm_entries_size(); ++i) {
    const int objstm_number = static_cast<int>(canon.objstm_entries(i).objstm_number());
    const std::string objstm_body = ExtractObjectBody(pdf, objstm_number, &error);
    if (!error.empty()) {
      *why = error;
      return false;
    }
    const std::string dict = ExtractObjectDict(objstm_body, &error);
    if (!error.empty()) {
      *why = error;
      return false;
    }
    const std::string payload = ExtractStreamData(objstm_body, &error);
    if (!error.empty()) {
      *why = error;
      return false;
    }
    int n = 0;
    int first = 0;
    if (!Contains(dict, "/Type /ObjStm") || !ExtractSingleInt(dict, "/N ", &n) ||
        !ExtractSingleInt(dict, "/First ", &first)) {
      *why = "malformed object-stream dictionary";
      return false;
    }
    if (n != canon.objstm_entries(i).objects_size() +
                 canon.objstm_entries(i).length_dependencies_size()) {
      *why = "unexpected object-stream /N";
      return false;
    }
    if (!ParseObjectStreamPayload(payload, n, first, objstm_number,
                                  &compressed_objects, &error)) {
      *why = error;
      return false;
    }
    objstm_dicts[objstm_number] = dict;
    objstm_payloads[objstm_number] = payload;
    payload_sizes[objstm_number] = static_cast<int>(payload.size());
  }

  const std::string catalog_body =
      std::string(canon.catalog_padding_spaces(), ' ') +
      "<< /Type /Catalog /Pages 2 0 R >>";
  const auto catalog_it = compressed_objects.find(1);
  if (catalog_it == compressed_objects.end() || catalog_it->second.body != catalog_body) {
    *why = "catalog object 1 is missing from compressed objects";
    return false;
  }

  std::map<int, std::string> controlling_length_refs;
  std::map<int, int> first_host_for_target;
  std::map<int, int> first_dep_index_for_target;
  for (int i = 0; i < canon.objstm_entries_size(); ++i) {
    for (int j = 0; j < canon.objstm_entries(i).length_dependencies_size(); ++j) {
      const auto& dep = canon.objstm_entries(i).length_dependencies(j);
      const int target = static_cast<int>(dep.objstm_number());
      if (!controlling_length_refs.count(target)) {
        controlling_length_refs[target] = dep.obj_ref();
        first_host_for_target[target] =
            static_cast<int>(canon.objstm_entries(i).objstm_number());
        first_dep_index_for_target[target] = j;
      }
    }
  }

  for (int i = 0; i < canon.objstm_entries_size(); ++i) {
    const auto& entry = canon.objstm_entries(i);
    const int objstm_number = static_cast<int>(entry.objstm_number());
    const std::string& dict = objstm_dicts[objstm_number];
    const std::string& payload = objstm_payloads[objstm_number];

    if (controlling_length_refs.count(objstm_number)) {
      std::string actual_ref;
      if (!ExtractIndirectLengthRef(dict, &actual_ref) ||
          actual_ref != controlling_length_refs[objstm_number]) {
        *why = "indirect /Length reference mismatch";
        return false;
      }
    } else {
      int direct_length = 0;
      if (!ExtractSingleInt(dict, "/Length", &direct_length) ||
          direct_length != static_cast<int>(payload.size())) {
        *why = "direct /Length mismatch";
        return false;
      }
    }

    for (int j = 0; j < entry.objects_size(); ++j) {
      int obj_num = 0;
      std::string body;
      if (!ParseCompressedObjectSpec(entry.objects(j), &obj_num, &body)) {
        *why = "canonical object spec is malformed";
        return false;
      }
      const auto it = compressed_objects.find(obj_num);
      if (it == compressed_objects.end() || it->second.host_objstm != objstm_number ||
          it->second.body != body) {
        *why = "compressed object body mismatch";
        return false;
      }
    }

    for (int j = 0; j < entry.length_dependencies_size(); ++j) {
      const auto& dep = entry.length_dependencies(j);
      int dep_obj_num = 0;
      if (!ParseIndirectRef(dep.obj_ref(), &dep_obj_num)) {
        *why = "canonical dependency ref is malformed";
        return false;
      }
      const auto it = compressed_objects.find(dep_obj_num);
      if (it == compressed_objects.end() || it->second.host_objstm != objstm_number) {
        *why = "dependency object missing from expected host stream";
        return false;
      }
      const int target_objstm = static_cast<int>(dep.objstm_number());
      const std::string expected_body =
          std::to_string(payload_sizes[target_objstm]) +
          std::string(canon.length_object_trailing_spaces(), ' ');
      if (it->second.body != expected_body) {
        *why = "dependency integer body mismatch";
        return false;
      }
    }
  }

  int xref_obj_num = -1;
  std::string xref_body;
  for (int obj_num = 1; obj_num < 128; ++obj_num) {
    error.clear();
    const std::string body = ExtractObjectBody(pdf, obj_num, &error);
    if (!error.empty()) {
      continue;
    }
    const std::string dict = ExtractObjectDict(body, &error);
    if (!error.empty()) {
      continue;
    }
    if (Contains(dict, "/Type /XRef")) {
      xref_obj_num = obj_num;
      xref_body = body;
      break;
    }
  }
  if (xref_obj_num < 0) {
    *why = "missing xref stream";
    return false;
  }

  const std::string xref_dict = ExtractObjectDict(xref_body, &error);
  if (!error.empty()) {
    *why = error;
    return false;
  }
  const std::string xref_data = ExtractStreamData(xref_body, &error);
  if (!error.empty()) {
    *why = error;
    return false;
  }
  int xref_size = 0;
  if (!Contains(xref_dict, "/Root 1 0 R") || !Contains(xref_dict, "/W [1 4 2]") ||
      !ExtractSingleInt(xref_dict, "/Size ", &xref_size)) {
    *why = "xref stream dictionary mismatch";
    return false;
  }
  std::vector<XrefEntry> xref_entries;
  if (!ParseXrefEntries(xref_data, xref_size, &xref_entries, &error)) {
    *why = error;
    return false;
  }

  for (const auto& [obj_num, parsed] : compressed_objects) {
    if (obj_num >= xref_size) {
      *why = "xref /Size too small for compressed object";
      return false;
    }
    const XrefEntry& entry = xref_entries[static_cast<size_t>(obj_num)];
    if (entry.type != 2 || entry.field2 != static_cast<uint32_t>(parsed.host_objstm) ||
        entry.field3 != static_cast<uint16_t>(parsed.index_in_host)) {
      *why = "compressed xref entry mismatch";
      return false;
    }
  }

  std::set<int> direct_object_numbers = {2, 3, contents_obj_num, xref_obj_num};
  for (int i = 0; i < canon.objstm_entries_size(); ++i) {
    direct_object_numbers.insert(
        static_cast<int>(canon.objstm_entries(i).objstm_number()));
  }
  for (int obj_num : direct_object_numbers) {
    const uint32_t off = FindObjectOffset(pdf, obj_num);
    if (off == 0 || obj_num >= xref_size) {
      *why = "failed to recover direct object offset";
      return false;
    }
    const XrefEntry& entry = xref_entries[static_cast<size_t>(obj_num)];
    if (entry.type != 1 || entry.field2 != off) {
      *why = "direct-object xref entry mismatch";
      return false;
    }
  }

  return true;
}

bool StructuralInvariants(
    const pdf_objstmlength::ObjstmLengthDocument& doc, std::string* why) {
  if (doc.pdf_header().size() != 8 || doc.pdf_header().rfind("%PDF-1.", 0) != 0) {
    *why = "pdf_header is not a canonical %PDF-1.x line";
    return false;
  }
  if (doc.page_width() == 0 || doc.page_height() == 0) {
    *why = "page dimensions must be positive";
    return false;
  }
  if (doc.catalog_padding_spaces() > kMaxPaddingSpaces ||
      doc.length_object_trailing_spaces() > kMaxPaddingSpaces) {
    *why = "padding fields exceeded serializer clamp";
    return false;
  }
  if (doc.page_content().size() > 512) {
    *why = "page_content exceeded serializer cap";
    return false;
  }
  if (doc.objstm_entries_size() <= 0) {
    *why = "canonical document did not materialize objstm_entries";
    return false;
  }
  return true;
}

bool ResolveOrderAssertions(
    const pdf_objstmlength::ObjstmLengthDocument& canon, std::string* why) {
  for (int i = 0; i < canon.objstm_entries_size(); ++i) {
    const auto& entry = canon.objstm_entries(i);
    const int host_objstm = static_cast<int>(entry.objstm_number());
    for (int j = 0; j < entry.length_dependencies_size(); ++j) {
      int dep_obj_num = 0;
      if (!ParseIndirectRef(entry.length_dependencies(j).obj_ref(), &dep_obj_num)) {
        *why = "invalid canonical dependency ref";
        return false;
      }
      if (entry.resolve_length_before_register() && dep_obj_num >= host_objstm) {
        *why = "expected dependency object number before host objstm";
        return false;
      }
      if (!entry.resolve_length_before_register() && dep_obj_num <= host_objstm) {
        *why = "expected dependency object number after host objstm";
        return false;
      }
    }
  }
  return true;
}

bool RunCase(const char* name, const pdf_objstmlength::ObjstmLengthDocument& doc,
             bool expect_default_shape) {
  std::fprintf(stderr, "\n=== %s ===\n", name);
  const pdf_objstmlength::ObjstmLengthDocument canon =
      CanonicalizeObjstmLengthDocument(doc);
  std::string why;
  if (!StructuralInvariants(canon, &why)) {
    std::fprintf(stderr, "FAIL: structural invariant: %s\n", why.c_str());
    return false;
  }

  const std::string pdf = SerializeObjstmLengthPdf(doc);
  if (!VerifySerializedPdf(pdf, canon, &why)) {
    std::fprintf(stderr, "FAIL: serialized PDF verification: %s\n", why.c_str());
    return false;
  }
  if (!ResolveOrderAssertions(canon, &why)) {
    std::fprintf(stderr, "FAIL: resolve-order assertion: %s\n", why.c_str());
    return false;
  }
  if (!MeaningfulQpdfOutcome(pdf, &why)) {
    std::fprintf(stderr, "FAIL: qpdf sanity: %s\n", why.c_str());
    return false;
  }
  std::fprintf(stderr, "qpdf sanity: %s\n", why.c_str());

  if (expect_default_shape) {
    if (canon.pdf_header() != kDefaultHeader ||
        !canon.include_binary_comment() ||
        canon.page_width() != 16 || canon.page_height() != 16 ||
        canon.catalog_padding_spaces() != 0 ||
        canon.length_object_trailing_spaces() != 0 ||
        !canon.page_content().empty() ||
        canon.objstm_entries_size() != 2) {
      std::fprintf(stderr, "FAIL: default CVE-2023-3436 trigger shape changed\n");
      return false;
    }
    if (pdf.size() != 613) {
      std::fprintf(stderr, "FAIL: default trigger size drifted (%zu bytes)\n",
                   pdf.size());
      return false;
    }
  }

  std::fprintf(stderr, "PASS: %s (%zu PDF bytes)\n", name, pdf.size());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int failures = 0;

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    failures += RunCase("default-cve-2023-3436-shape", doc, true) ? 0 : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    doc.set_page_width(32);
    doc.set_page_height(48);
    doc.set_page_content("q\nQ\n");
    doc.set_catalog_padding_spaces(4);
    doc.set_length_object_trailing_spaces(3);
    failures += RunCase("non-empty-content-with-objstm-padding", doc, false)
                    ? 0
                    : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    doc.set_pdf_header("%notpdf!");
    doc.set_page_width(0);
    doc.set_page_height(999999);
    doc.set_catalog_padding_spaces(999999);
    doc.set_length_object_trailing_spaces(999999);
    doc.set_page_content(std::string(800, 'A'));
    failures += RunCase("canonicalizes-noisy-mutator-fields", doc, false) ? 0 : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    doc.set_chain_depth(1);
    doc.set_enable_self_refs(true);
    failures += RunCase("self-reference-single-objstm", doc, false) ? 0 : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    doc.set_chain_depth(2);
    doc.set_enable_circular_refs(true);
    failures += RunCase("mutual-two-objstm-cycle", doc, false) ? 0 : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    doc.set_chain_depth(4);
    doc.set_objects_per_stream(4);
    failures += RunCase("linear-chain-four-objstm-four-objects", doc, false)
                    ? 0
                    : 1;
  }

  {
    pdf_objstmlength::ObjstmLengthDocument doc;
    auto* a = doc.add_objstm_entries();
    a->set_objstm_number(4);
    a->set_resolve_length_before_register(false);
    auto* b = doc.add_objstm_entries();
    b->set_objstm_number(8);
    b->set_resolve_length_before_register(true);
    auto* dep_b = b->add_length_dependencies();
    dep_b->set_objstm_number(4);
    auto* c = doc.add_objstm_entries();
    c->set_objstm_number(9);
    c->set_resolve_length_before_register(false);
    auto* dep_c = c->add_length_dependencies();
    dep_c->set_objstm_number(8);
    failures += RunCase("explicit-before-after-registration-order", doc, false)
                    ? 0
                    : 1;
  }

  if (argc >= 4) {
    pdf_objstmlength::ObjstmLengthDocument doc;
    const std::string pdf = SerializeObjstmLengthPdf(doc);
    Check(RunPdftoppmSmoke(argv[1], "4.04-release", pdf, true, false),
          "xpdf 4.04 release smoke hangs on the object-stream /Length trigger",
          &failures);
    Check(RunPdftoppmSmoke(argv[2], "4.04-asan", pdf, true, false),
          "xpdf 4.04 ASan smoke also hangs on the same trigger",
          &failures);
    Check(RunPdftoppmSmoke(argv[3], "4.05-asan", pdf, false, true),
          "xpdf 4.05 ASan smoke no longer deadlocks on the trigger",
          &failures);
  } else {
    std::fprintf(stderr,
                 "\nsmoke: skipped (pass <xpdf-4.04-release-pdftoppm>"
                 " <xpdf-4.04-asan-pdftoppm> <xpdf-4.05-asan-pdftoppm>)\n");
  }

  google::protobuf::ShutdownProtobufLibrary();
  if (failures) {
    std::fprintf(stderr, "\nverify_objstm_length: %d failure(s)\n", failures);
    return 1;
  }
  std::fprintf(stderr, "\nverify_objstm_length: all checks passed\n");
  return 0;
}

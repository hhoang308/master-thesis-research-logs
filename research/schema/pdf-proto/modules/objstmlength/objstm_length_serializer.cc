#include "modules/objstmlength/objstm_length_serializer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using pdf_objstmlength::LengthDependency;
using pdf_objstmlength::ObjectStreamEntry;
using pdf_objstmlength::ObjstmLengthDocument;

constexpr char kDefaultHeader[] = "%PDF-1.5";
constexpr int kMaxPageDim = 4096;
constexpr size_t kMaxPageContent = 512;
constexpr uint32_t kMaxPaddingSpaces = 32;
constexpr uint32_t kMaxChainDepth = 8;
constexpr uint32_t kMaxObjectsPerStream = 8;
constexpr uint32_t kMaxObjstmEntries = 8;

struct CompressedObjectSpec {
  int obj_num = 0;
  std::string body;
};

struct HostedLengthDependencySpec {
  int obj_num = 0;
  int dependent_objstm_number = 0;
  std::string obj_ref;
};

struct CanonicalStreamSpec {
  int objstm_number = 0;
  bool has_self_reference = false;
  bool resolve_length_before_register = true;
  std::vector<CompressedObjectSpec> objects;
  std::vector<HostedLengthDependencySpec> hosted_length_dependencies;
};

struct CanonicalModel {
  std::string pdf_header;
  bool include_binary_comment = true;
  uint32_t page_width = 16;
  uint32_t page_height = 16;
  std::string page_content;
  uint32_t catalog_padding_spaces = 0;
  uint32_t length_object_trailing_spaces = 0;
  uint32_t chain_depth = 2;
  bool enable_circular_refs = false;
  bool enable_self_refs = false;
  uint32_t objects_per_stream = 1;
  std::vector<CanonicalStreamSpec> streams;
};

struct PayloadBuildResult {
  std::string payload;
  int first = 0;
};

struct HostedDependencyLocation {
  size_t stream_index = 0;
  size_t dependency_index = 0;
};

uint32_t ClampU32(uint32_t value, uint32_t lo, uint32_t hi) {
  return std::max(lo, std::min(value, hi));
}

std::string Spaces(uint32_t count) {
  return std::string(count, ' ');
}

std::string HeaderLine(const std::string& candidate) {
  if (candidate.size() == 8 && candidate.rfind("%PDF-1.", 0) == 0 &&
      candidate[7] >= '0' && candidate[7] <= '9') {
    return candidate;
  }
  return kDefaultHeader;
}

std::string StreamObject(const std::string& dict_body,
                         const std::string& stream_data) {
  std::ostringstream out;
  out << "<< " << dict_body << " >>\nstream\n"
      << stream_data << "\nendstream";
  return out.str();
}

std::string PackXrefEntry(uint8_t type, uint32_t field2, uint16_t field3) {
  std::string entry;
  entry.push_back(static_cast<char>(type));
  entry.push_back(static_cast<char>((field2 >> 24) & 0xff));
  entry.push_back(static_cast<char>((field2 >> 16) & 0xff));
  entry.push_back(static_cast<char>((field2 >> 8) & 0xff));
  entry.push_back(static_cast<char>(field2 & 0xff));
  entry.push_back(static_cast<char>((field3 >> 8) & 0xff));
  entry.push_back(static_cast<char>(field3 & 0xff));
  return entry;
}

bool ParsePositiveInt(const std::string& s, int* value) {
  if (s.empty()) {
    return false;
  }
  int out = 0;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
    out = out * 10 + (c - '0');
  }
  if (out <= 0) {
    return false;
  }
  *value = out;
  return true;
}

bool ParseNonNegativeInt(const std::string& s, int* value) {
  if (s.empty()) {
    return false;
  }
  int out = 0;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
    out = out * 10 + (c - '0');
  }
  if (out < 0) {
    return false;
  }
  *value = out;
  return true;
}

bool ParseIndirectRef(const std::string& ref, int* obj_num) {
  size_t first_space = ref.find(' ');
  if (first_space == std::string::npos) {
    return false;
  }
  size_t second_space = ref.find(' ', first_space + 1);
  if (second_space == std::string::npos) {
    return false;
  }
  if (ref.substr(second_space + 1) != "R") {
    return false;
  }
  const std::string obj = ref.substr(0, first_space);
  const std::string gen = ref.substr(first_space + 1,
                                     second_space - (first_space + 1));
  int parsed_obj = 0;
  int parsed_gen = 0;
  if (!ParsePositiveInt(obj, &parsed_obj) ||
      !ParseNonNegativeInt(gen, &parsed_gen) ||
      parsed_gen != 0) {
    return false;
  }
  *obj_num = parsed_obj;
  return true;
}

bool ParseCompressedObjectSpec(const std::string& raw, int* obj_num,
                               std::string* body) {
  size_t first_space = raw.find(' ');
  if (first_space == std::string::npos) {
    return false;
  }
  size_t second_token_start = first_space + 1;
  while (second_token_start < raw.size() && raw[second_token_start] == ' ') {
    ++second_token_start;
  }
  size_t second_space = raw.find(' ', second_token_start);
  if (second_space == std::string::npos) {
    return false;
  }
  int parsed_obj = 0;
  int parsed_gen = 0;
  if (!ParsePositiveInt(raw.substr(0, first_space), &parsed_obj) ||
      !ParseNonNegativeInt(raw.substr(second_token_start,
                                      second_space - second_token_start),
                           &parsed_gen) ||
      parsed_gen != 0) {
    return false;
  }
  *obj_num = parsed_obj;
  *body = raw.substr(second_space + 1);
  return true;
}

std::string FormatCompressedObjectSpec(int obj_num, const std::string& body) {
  return std::to_string(obj_num) + " 0 " + body;
}

int AllocateFreeNumber(const std::set<int>& used_numbers, int anchor,
                       bool prefer_before_anchor) {
  if (prefer_before_anchor) {
    for (int candidate = anchor - 1; candidate >= 1; --candidate) {
      if (!used_numbers.count(candidate)) {
        return candidate;
      }
    }
  }
  for (int candidate = std::max(anchor + 1, 1); candidate < 1'000'000;
       ++candidate) {
    if (!used_numbers.count(candidate)) {
      return candidate;
    }
  }
  for (int candidate = 1; candidate < anchor; ++candidate) {
    if (!used_numbers.count(candidate)) {
      return candidate;
    }
  }
  return anchor + 1;
}

int AllocateLowestFreeNumber(const std::set<int>& used_numbers) {
  return AllocateFreeNumber(used_numbers, 0, false);
}

PayloadBuildResult BuildObjectStreamPayload(
    const std::vector<CompressedObjectSpec>& objects) {
  std::vector<int> offsets;
  offsets.reserve(objects.size());
  int running_body_size = 0;
  for (size_t i = 0; i < objects.size(); ++i) {
    const CompressedObjectSpec& object = objects[i];
    offsets.push_back(running_body_size);
    running_body_size += static_cast<int>(object.body.size());
    if (i + 1 < objects.size()) {
      ++running_body_size;
    }
  }

  std::ostringstream header;
  for (size_t i = 0; i < objects.size(); ++i) {
    header << objects[i].obj_num << " " << offsets[i] << " ";
  }

  PayloadBuildResult result;
  result.first = static_cast<int>(header.str().size());
  result.payload = header.str();
  for (size_t i = 0; i < objects.size(); ++i) {
    const CompressedObjectSpec& object = objects[i];
    result.payload += object.body;
    if (i + 1 < objects.size()) {
      result.payload += "\n";
    }
  }
  return result;
}

bool HasCatalogObject(const CanonicalModel& model) {
  for (const CanonicalStreamSpec& stream : model.streams) {
    for (const CompressedObjectSpec& object : stream.objects) {
      if (object.obj_num == 1) {
        return true;
      }
    }
  }
  return false;
}

void EnsureCatalogObject(CanonicalModel* model, std::set<int>* used_numbers) {
  const std::string catalog_body =
      Spaces(model->catalog_padding_spaces) +
      "<< /Type /Catalog /Pages 2 0 R >>";

  for (CanonicalStreamSpec& stream : model->streams) {
    for (CompressedObjectSpec& object : stream.objects) {
      if (object.obj_num == 1) {
        object.body = catalog_body;
        return;
      }
    }
  }

  if (model->streams.empty()) {
    CanonicalStreamSpec stream;
    stream.objstm_number = 4;
    stream.resolve_length_before_register = false;
    model->streams.push_back(stream);
    used_numbers->insert(4);
  }

  CompressedObjectSpec catalog;
  catalog.obj_num = 1;
  catalog.body = catalog_body;
  model->streams.front().objects.insert(model->streams.front().objects.begin(),
                                        catalog);
  used_numbers->insert(1);
}

CanonicalModel BuildCanonicalModel(const ObjstmLengthDocument& doc) {
  CanonicalModel model;
  model.pdf_header = HeaderLine(doc.pdf_header());
  model.include_binary_comment = doc.include_binary_comment();
  model.page_width = ClampU32(doc.page_width(), 1, kMaxPageDim);
  model.page_height = ClampU32(doc.page_height(), 1, kMaxPageDim);
  model.catalog_padding_spaces =
      ClampU32(doc.catalog_padding_spaces(), 0, kMaxPaddingSpaces);
  model.length_object_trailing_spaces =
      ClampU32(doc.length_object_trailing_spaces(), 0, kMaxPaddingSpaces);
  model.chain_depth = ClampU32(doc.chain_depth(), 1, kMaxChainDepth);
  model.enable_circular_refs = doc.enable_circular_refs();
  model.enable_self_refs = doc.enable_self_refs();
  model.objects_per_stream =
      ClampU32(doc.objects_per_stream(), 1, kMaxObjectsPerStream);

  model.page_content = doc.page_content();
  if (model.page_content.size() > kMaxPageContent) {
    model.page_content.resize(kMaxPageContent);
  }

  std::set<int> used_numbers = {2, 3};

  if (doc.objstm_entries_size() == 0) {
    const uint32_t stream_count = model.chain_depth;
    for (uint32_t i = 0; i < stream_count && i < kMaxObjstmEntries; ++i) {
      CanonicalStreamSpec stream;
      stream.objstm_number = static_cast<int>(4 + i);
      stream.resolve_length_before_register = false;
      model.streams.push_back(stream);
      used_numbers.insert(stream.objstm_number);
    }

    EnsureCatalogObject(&model, &used_numbers);

    for (size_t i = 1; i < model.streams.size(); ++i) {
      HostedLengthDependencySpec dep;
      dep.dependent_objstm_number = model.streams[i - 1].objstm_number;
      dep.obj_num =
          AllocateFreeNumber(used_numbers, model.streams[i].objstm_number,
                             model.streams[i].resolve_length_before_register);
      dep.obj_ref = std::to_string(dep.obj_num) + " 0 R";
      used_numbers.insert(dep.obj_num);
      model.streams[i].hosted_length_dependencies.push_back(dep);
    }

    if (model.enable_circular_refs && model.streams.size() > 1) {
      HostedLengthDependencySpec dep;
      dep.dependent_objstm_number = model.streams.back().objstm_number;
      dep.obj_num =
          AllocateFreeNumber(used_numbers, model.streams.front().objstm_number,
                             model.streams.front().resolve_length_before_register);
      dep.obj_ref = std::to_string(dep.obj_num) + " 0 R";
      used_numbers.insert(dep.obj_num);
      model.streams.front().hosted_length_dependencies.push_back(dep);
    } else if (model.enable_self_refs && !model.streams.empty()) {
      model.streams.back().has_self_reference = true;
    }

    for (size_t i = 0; i < model.streams.size(); ++i) {
      CanonicalStreamSpec& stream = model.streams[i];
      while (stream.objects.size() + stream.hosted_length_dependencies.size() <
             model.objects_per_stream) {
        CompressedObjectSpec filler;
        filler.obj_num = AllocateLowestFreeNumber(used_numbers);
        filler.body = (i == 0 ? "null" : "0");
        used_numbers.insert(filler.obj_num);
        stream.objects.push_back(filler);
      }
    }
  } else {
    const int stream_count =
        std::min<int>(doc.objstm_entries_size(), static_cast<int>(kMaxObjstmEntries));

    std::set<int> used_objstms;
    for (int i = 0; i < stream_count; ++i) {
      CanonicalStreamSpec stream;
      int requested = static_cast<int>(doc.objstm_entries(i).objstm_number());
      if (requested < 4 || used_objstms.count(requested)) {
        requested = 4;
        while (used_objstms.count(requested) || used_numbers.count(requested)) {
          ++requested;
        }
      }
      stream.objstm_number = requested;
      stream.has_self_reference = doc.objstm_entries(i).has_self_reference();
      stream.resolve_length_before_register =
          doc.objstm_entries(i).resolve_length_before_register();
      used_objstms.insert(stream.objstm_number);
      used_numbers.insert(stream.objstm_number);
      model.streams.push_back(stream);
    }

    for (int i = 0; i < stream_count; ++i) {
      const ObjectStreamEntry& input_entry = doc.objstm_entries(i);
      CanonicalStreamSpec& stream = model.streams[i];

      for (int j = 0; j < input_entry.objects_size(); ++j) {
        int parsed_num = 0;
        std::string parsed_body;
        if (!ParseCompressedObjectSpec(input_entry.objects(j), &parsed_num,
                                       &parsed_body) ||
            parsed_num <= 0 || used_numbers.count(parsed_num)) {
          parsed_num = AllocateLowestFreeNumber(used_numbers);
          parsed_body = input_entry.objects(j);
        }
        used_numbers.insert(parsed_num);
        stream.objects.push_back({parsed_num, parsed_body});
      }
    }

    EnsureCatalogObject(&model, &used_numbers);

    std::set<int> valid_objstms;
    for (const CanonicalStreamSpec& stream : model.streams) {
      valid_objstms.insert(stream.objstm_number);
    }

    for (int i = 0; i < stream_count; ++i) {
      const ObjectStreamEntry& input_entry = doc.objstm_entries(i);
      CanonicalStreamSpec& stream = model.streams[i];
      for (int j = 0; j < input_entry.length_dependencies_size(); ++j) {
        const LengthDependency& input_dep = input_entry.length_dependencies(j);

        HostedLengthDependencySpec dep;
        dep.dependent_objstm_number =
            valid_objstms.count(static_cast<int>(input_dep.objstm_number()))
                ? static_cast<int>(input_dep.objstm_number())
                : stream.objstm_number;

        int parsed_obj = 0;
        if (!ParseIndirectRef(input_dep.obj_ref(), &parsed_obj) ||
            parsed_obj <= 0 || used_numbers.count(parsed_obj)) {
          parsed_obj =
              AllocateFreeNumber(used_numbers, stream.objstm_number,
                                 stream.resolve_length_before_register);
        }
        dep.obj_num = parsed_obj;
        dep.obj_ref = std::to_string(dep.obj_num) + " 0 R";
        used_numbers.insert(dep.obj_num);
        stream.hosted_length_dependencies.push_back(dep);
      }
    }
  }

  for (CanonicalStreamSpec& stream : model.streams) {
    bool already_hosts_self = false;
    for (const HostedLengthDependencySpec& dep : stream.hosted_length_dependencies) {
      if (dep.dependent_objstm_number == stream.objstm_number) {
        already_hosts_self = true;
        break;
      }
    }
    if (stream.has_self_reference && !already_hosts_self) {
      HostedLengthDependencySpec dep;
      dep.dependent_objstm_number = stream.objstm_number;
      dep.obj_num = AllocateFreeNumber(used_numbers, stream.objstm_number,
                                       stream.resolve_length_before_register);
      dep.obj_ref = std::to_string(dep.obj_num) + " 0 R";
      used_numbers.insert(dep.obj_num);
      stream.hosted_length_dependencies.push_back(dep);
    }
  }

  return model;
}

ObjstmLengthDocument ModelToProto(const CanonicalModel& model) {
  ObjstmLengthDocument canon;
  canon.set_pdf_header(model.pdf_header);
  canon.set_include_binary_comment(model.include_binary_comment);
  canon.set_page_width(model.page_width);
  canon.set_page_height(model.page_height);
  canon.set_page_content(model.page_content);
  canon.set_catalog_padding_spaces(model.catalog_padding_spaces);
  canon.set_length_object_trailing_spaces(model.length_object_trailing_spaces);
  canon.set_chain_depth(model.chain_depth);
  canon.set_enable_circular_refs(model.enable_circular_refs);
  canon.set_enable_self_refs(model.enable_self_refs);
  canon.set_objects_per_stream(model.objects_per_stream);

  for (const CanonicalStreamSpec& stream : model.streams) {
    ObjectStreamEntry* out_stream = canon.add_objstm_entries();
    out_stream->set_objstm_number(stream.objstm_number);
    out_stream->set_has_self_reference(stream.has_self_reference);
    out_stream->set_resolve_length_before_register(
        stream.resolve_length_before_register);
    for (const CompressedObjectSpec& object : stream.objects) {
      out_stream->add_objects(FormatCompressedObjectSpec(object.obj_num, object.body));
    }
    for (const HostedLengthDependencySpec& dep : stream.hosted_length_dependencies) {
      LengthDependency* out_dep = out_stream->add_length_dependencies();
      out_dep->set_obj_ref(dep.obj_ref);
      out_dep->set_objstm_number(dep.dependent_objstm_number);
    }
  }

  return canon;
}

}  // namespace

pdf_objstmlength::ObjstmLengthDocument CanonicalizeObjstmLengthDocument(
    const pdf_objstmlength::ObjstmLengthDocument& doc) {
  return ModelToProto(BuildCanonicalModel(doc));
}

std::string SerializeObjstmLengthPdf(
    const pdf_objstmlength::ObjstmLengthDocument& doc) {
  const CanonicalModel model = BuildCanonicalModel(doc);

  std::map<int, size_t> stream_index_by_number;
  for (size_t i = 0; i < model.streams.size(); ++i) {
    stream_index_by_number[model.streams[i].objstm_number] = i;
  }

  std::map<int, HostedDependencyLocation> controlling_dependency;
  for (size_t i = 0; i < model.streams.size(); ++i) {
    for (size_t j = 0; j < model.streams[i].hosted_length_dependencies.size(); ++j) {
      const HostedLengthDependencySpec& dep =
          model.streams[i].hosted_length_dependencies[j];
      controlling_dependency.emplace(dep.dependent_objstm_number,
                                     HostedDependencyLocation{i, j});
    }
  }

  std::vector<std::vector<std::string>> dependency_bodies(model.streams.size());
  for (size_t i = 0; i < model.streams.size(); ++i) {
    dependency_bodies[i].assign(
        model.streams[i].hosted_length_dependencies.size(),
        "0" + Spaces(model.length_object_trailing_spaces));
  }

  std::vector<PayloadBuildResult> payloads(model.streams.size());
  for (int iteration = 0; iteration < 32; ++iteration) {
    for (size_t i = 0; i < model.streams.size(); ++i) {
      std::vector<CompressedObjectSpec> objects = model.streams[i].objects;
      for (size_t j = 0; j < model.streams[i].hosted_length_dependencies.size(); ++j) {
        objects.push_back({model.streams[i].hosted_length_dependencies[j].obj_num,
                           dependency_bodies[i][j]});
      }
      payloads[i] = BuildObjectStreamPayload(objects);
    }

    bool changed = false;
    for (size_t i = 0; i < model.streams.size(); ++i) {
      for (size_t j = 0; j < model.streams[i].hosted_length_dependencies.size(); ++j) {
        const HostedLengthDependencySpec& dep =
            model.streams[i].hosted_length_dependencies[j];
        const size_t dependent_index =
            stream_index_by_number[dep.dependent_objstm_number];
        const std::string next_body =
            std::to_string(payloads[dependent_index].payload.size()) +
            Spaces(model.length_object_trailing_spaces);
        if (next_body != dependency_bodies[i][j]) {
          dependency_bodies[i][j] = next_body;
          changed = true;
        }
      }
    }
    if (!changed) {
      break;
    }
  }

  std::vector<std::vector<CompressedObjectSpec>> final_objects(model.streams.size());
  for (size_t i = 0; i < model.streams.size(); ++i) {
    final_objects[i] = model.streams[i].objects;
    for (size_t j = 0; j < model.streams[i].hosted_length_dependencies.size(); ++j) {
      final_objects[i].push_back(
          {model.streams[i].hosted_length_dependencies[j].obj_num,
           dependency_bodies[i][j]});
    }
    payloads[i] = BuildObjectStreamPayload(final_objects[i]);
  }

  std::set<int> used_numbers;
  used_numbers.insert(1);
  used_numbers.insert(2);
  used_numbers.insert(3);
  for (const CanonicalStreamSpec& stream : model.streams) {
    used_numbers.insert(stream.objstm_number);
    for (const CompressedObjectSpec& object : stream.objects) {
      used_numbers.insert(object.obj_num);
    }
    for (const HostedLengthDependencySpec& dep : stream.hosted_length_dependencies) {
      used_numbers.insert(dep.obj_num);
    }
  }

  const int contents_obj_num = AllocateLowestFreeNumber(used_numbers);
  used_numbers.insert(contents_obj_num);
  const int xref_obj_num = AllocateLowestFreeNumber(used_numbers);
  used_numbers.insert(xref_obj_num);

  std::vector<std::pair<int, std::string>> direct_objects;
  direct_objects.push_back({2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"});

  std::ostringstream page;
  page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << model.page_width
       << " " << model.page_height
       << "] /Resources << >> /Contents " << contents_obj_num << " 0 R >>";
  direct_objects.push_back({3, page.str()});

  for (size_t i = 0; i < model.streams.size(); ++i) {
    std::ostringstream dict;
    dict << "/Type /ObjStm /N " << final_objects[i].size() << " /First "
         << payloads[i].first << " /Length ";

    const auto control_it =
        controlling_dependency.find(model.streams[i].objstm_number);
    if (control_it != controlling_dependency.end()) {
      const HostedLengthDependencySpec& dep =
          model.streams[control_it->second.stream_index]
              .hosted_length_dependencies[control_it->second.dependency_index];
      dict << dep.obj_ref;
    } else {
      dict << payloads[i].payload.size();
    }

    direct_objects.push_back(
        {model.streams[i].objstm_number,
         StreamObject(dict.str(), payloads[i].payload)});
  }

  std::ostringstream contents_dict;
  contents_dict << "/Length " << model.page_content.size();
  direct_objects.push_back(
      {contents_obj_num, StreamObject(contents_dict.str(), model.page_content)});

  std::sort(direct_objects.begin(), direct_objects.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  std::string out = model.pdf_header + "\n";
  if (model.include_binary_comment) {
    out += "%\xE2\xE3\xCF\xD3\n";
  }

  const int xref_size = *used_numbers.rbegin() + 1;
  std::vector<uint32_t> offsets(static_cast<size_t>(xref_size), 0);
  for (const auto& [obj_num, body] : direct_objects) {
    offsets[static_cast<size_t>(obj_num)] = static_cast<uint32_t>(out.size());
    out += std::to_string(obj_num) + " 0 obj\n";
    out += body;
    out += "\nendobj\n";
  }

  offsets[static_cast<size_t>(xref_obj_num)] = static_cast<uint32_t>(out.size());

  std::string xref_entries;
  xref_entries += PackXrefEntry(0, 0, 65535);
  for (int obj_num = 1; obj_num < xref_size; ++obj_num) {
    bool emitted = false;
    for (size_t i = 0; i < final_objects.size() && !emitted; ++i) {
      for (size_t j = 0; j < final_objects[i].size(); ++j) {
        if (final_objects[i][j].obj_num == obj_num) {
          xref_entries += PackXrefEntry(2, model.streams[i].objstm_number,
                                        static_cast<uint16_t>(j));
          emitted = true;
          break;
        }
      }
    }
    if (!emitted && offsets[static_cast<size_t>(obj_num)] != 0) {
      xref_entries +=
          PackXrefEntry(1, offsets[static_cast<size_t>(obj_num)], 0);
      emitted = true;
    }
    if (!emitted) {
      xref_entries += PackXrefEntry(0, 0, 0);
    }
  }

  std::ostringstream xref_dict;
  xref_dict << "/Type /XRef /Size " << xref_size << " /Root 1 0 R /W [1 4 2]"
            << " /Index [0 " << xref_size << "] /Length "
            << xref_entries.size();
  out += std::to_string(xref_obj_num) + " 0 obj\n";
  out += StreamObject(xref_dict.str(), xref_entries);
  out += "\nendobj\n";
  out += "startxref\n" + std::to_string(offsets[static_cast<size_t>(xref_obj_num)]) +
         "\n%%EOF\n";
  return out;
}

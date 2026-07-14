cmake_minimum_required(VERSION 3.14)
project(pdf_fuzzer CXX C)

set(CMAKE_CXX_STANDARD 17)

set(ENABLE_FUZZER_TARGETS ON)
if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  message(WARNING "libFuzzer targets require Clang; only verifier targets will be configured")
  set(ENABLE_FUZZER_TARGETS OFF)
endif()

# Abseil (via protobuf) includes sanitizer headers when building with sanitizers.
# Use the selected Clang's resource dir instead of hardcoding an LLVM version.
set(CLANG_RUNTIME_INCLUDES "")
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
    OUTPUT_VARIABLE CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(CLANG_RESOURCE_DIR)
    list(APPEND CLANG_RUNTIME_INCLUDES -I${CLANG_RESOURCE_DIR}/include)
  endif()
endif()

execute_process(
  COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.so
  OUTPUT_VARIABLE LIBSTDCXX_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(LIBSTDCXX_PATH AND EXISTS "${LIBSTDCXX_PATH}")
  get_filename_component(LIBSTDCXX_DIR "${LIBSTDCXX_PATH}" DIRECTORY)
endif()

# COV_BUILD=ON: build a coverage-instrumented cov_driver (LLVM source coverage, no
# fuzzer/ASan) for the fair AFL++-vs-libFuzzer comparison. Default OFF: the normal
# fuzzer build with libFuzzer + ASan.
option(COV_BUILD "Build cov_driver with LLVM source coverage instead of the fuzzers" OFF)
# AFL_BUILD=ON (configure with CMAKE_CXX_COMPILER=afl-clang-fast++): build the AFL++
# target harness (xpdf instrumented by afl-clang-fast). afl-clang-fast++ here wraps
# conda-forge clang 18, which finds its own C++ headers -- so NO GCC11 include hacks
# and NO sanitizer/fuzzer flags (afl-clang-fast injects its own instrumentation).
option(AFL_BUILD "Build the AFL++ target harness with afl-clang-fast instrumentation" OFF)

if((COV_BUILD OR AFL_BUILD) AND NOT ENABLE_FUZZER_TARGETS)
  message(FATAL_ERROR "COV_BUILD/AFL_BUILD require Clang")
endif()

if(COV_BUILD)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping -g -O1 ${CLANG_RUNTIME_INCLUDES})
  add_link_options(-fprofile-instr-generate -fcoverage-mapping)
  if(LIBSTDCXX_DIR)
    add_link_options(-L${LIBSTDCXX_DIR})
  endif()
elseif(AFL_BUILD)
  add_compile_options(-O2 -g)
elseif(ENABLE_FUZZER_TARGETS)
  # Sanitizer flags propagate into the xpdf subdirectory too.
  add_compile_options(-fsanitize=fuzzer-no-link,address -g -O1 ${CLANG_RUNTIME_INCLUDES})
  add_link_options(-fsanitize=address)
  if(LIBSTDCXX_DIR)
    add_link_options(-L${LIBSTDCXX_DIR})
  endif()
endif()

# ── Protobuf ──────────────────────────────────────────────────────────────────
# Prefer CONFIG mode when available (protobuf >= v22 often carries transitive
# abseil deps there), but Ubuntu's libprotobuf-dev 3.21 ships only module/pkg-config
# metadata. Fall back to CMake's FindProtobuf module for that environment.
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()

if(TARGET protobuf::protoc)
  set(PROTOC_EXE protobuf::protoc)
else()
  set(PROTOC_EXE ${Protobuf_PROTOC_EXECUTABLE})
endif()

if(TARGET protobuf::libprotobuf)
  set(PROTOBUF_LIB_TARGET protobuf::libprotobuf)
else()
  set(PROTOBUF_LIB_TARGET ${Protobuf_LIBRARIES})
endif()

set(CFF_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/modules/cff)
set(CFF_SERIALIZER_SRC ${CFF_MODULE_DIR}/cff_serializer.cc)
set(TYPE3CACHE_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/modules/type3cache)
set(TYPE3CACHE_SERIALIZER_SRC ${TYPE3CACHE_MODULE_DIR}/type3_cache_serializer.cc)

set(PROTO_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/pdf.proto
  ${CFF_MODULE_DIR}/cff.proto
  ${TYPE3CACHE_MODULE_DIR}/type3_cache.proto
)
set(PROTO_SRCS
  ${CMAKE_CURRENT_BINARY_DIR}/pdf.pb.cc
  ${CMAKE_CURRENT_BINARY_DIR}/modules/cff/cff.pb.cc
  ${CMAKE_CURRENT_BINARY_DIR}/modules/type3cache/type3_cache.pb.cc
)
set(PROTO_HDRS
  ${CMAKE_CURRENT_BINARY_DIR}/pdf.pb.h
  ${CMAKE_CURRENT_BINARY_DIR}/modules/cff/cff.pb.h
  ${CMAKE_CURRENT_BINARY_DIR}/modules/type3cache/type3_cache.pb.h
)
add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
  COMMAND ${CMAKE_COMMAND} -E make_directory
          ${CMAKE_CURRENT_BINARY_DIR}/modules/cff
          ${CMAKE_CURRENT_BINARY_DIR}/modules/type3cache
  COMMAND ${PROTOC_EXE}
          --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
          --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
          ${PROTO_FILES}
  DEPENDS ${PROTO_FILES}
  COMMENT "Generating protobuf C++ sources"
  VERBATIM
)

# ── libprotobuf-mutator ───────────────────────────────────────────────────────
# Not needed for the coverage driver or the AFL harness (neither mutates -- the AFL
# mutator lives in afl_pdf_mutator.so, built separately).
if(NOT COV_BUILD AND NOT AFL_BUILD)
  set(LIB_PROTO_MUTATOR_TESTING OFF CACHE BOOL "" FORCE)
  set(LIB_PROTO_MUTATOR_DOWNLOAD_PROTOBUF OFF CACHE BOOL "" FORCE)
  add_subdirectory(libprotobuf-mutator)
endif()

# ── xpdf (gives us xpdf_objs, goo, fofi targets) ─────────────────────────────
set(XPDF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../targets/xpdf/4.06/source" CACHE PATH "xpdf source tree (override with -DXPDF_SRC=.../xpdf-4.02)")
# xpdf <= 4.02: PDFDoc::displayPages() has no LocalParams* arg (added ~4.05). Set this
# when building against 4.02 so the harness calls the correct signature.
option(XPDF_LEGACY_DISPLAYPAGES "xpdf<=4.02 displayPages has no LocalParams arg" OFF)
if(XPDF_LEGACY_DISPLAYPAGES)
  add_compile_definitions(XPDF_LEGACY_DISPLAYPAGES)
endif()
if(ENABLE_FUZZER_TARGETS AND EXISTS "${XPDF_SRC}/CMakeLists.txt")
add_subdirectory(${XPDF_SRC} ${CMAKE_CURRENT_BINARY_DIR}/xpdf-build EXCLUDE_FROM_ALL)
elseif(ENABLE_FUZZER_TARGETS)
  message(WARNING "XPDF_SRC does not point to an xpdf source tree (${XPDF_SRC}); fuzzer targets will be skipped")
endif()

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/libprotobuf-mutator   # port/protobuf.h
  ${CMAKE_CURRENT_BINARY_DIR}/xpdf-build            # aconf.h -- generated by xpdf cmake
  ${XPDF_SRC}
  ${XPDF_SRC}/goo
  ${XPDF_SRC}/xpdf
  ${XPDF_SRC}/fofi
  ${Protobuf_INCLUDE_DIRS}
)

# ── PoDoFo ────────────────────────────────────────────────────────────────────
if(ENABLE_FUZZER_TARGETS AND TARGET xpdf_objs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PODOFO libpodofo)

# ── Coverage driver target (COV_BUILD) ───────────────────────────────────────
if(COV_BUILD)
  add_executable(cov_driver
    coverage/cov_driver.cpp
    $<TARGET_OBJECTS:xpdf_objs>
  )
  target_link_libraries(cov_driver goo fofi z pthread ${FONTCONFIG_LIBRARY})
  message(STATUS "COV_BUILD: building cov_driver (LLVM source coverage)")
  return()  # skip the fuzzer targets in coverage mode
endif()

# ── AFL++ target harness (AFL_BUILD) ──────────────────────────────────────────
# Reads a PDF (from the mutator's post_process), renders it through xpdf. xpdf is
# instrumented by afl-clang-fast; the harness gets the persistent-mode runtime +
# fork server. No protobuf/serializer/LPM here (the mutator .so handles those).
if(AFL_BUILD)
  add_executable(afl_harness_xpdf
    afl/afl_harness_xpdf.cpp
    $<TARGET_OBJECTS:xpdf_objs>
  )
  # xpdf has its own FlateDecode -> no system zlib needed (conda linker can't find -lz anyway).
  target_link_libraries(afl_harness_xpdf goo fofi pthread ${FONTCONFIG_LIBRARY})
  message(STATUS "AFL_BUILD: building afl_harness_xpdf (afl-clang-fast instrumented)")
  return()  # skip the libFuzzer targets in AFL mode
endif()

# ── xpdf fuzzer target ────────────────────────────────────────────────────────
add_executable(pdf_fuzzer
  harness.cpp
  serializer.cpp
  ${CFF_SERIALIZER_SRC}
  ${PROTO_SRCS}
  $<TARGET_OBJECTS:xpdf_objs>
)
target_link_options(pdf_fuzzer PRIVATE -fsanitize=fuzzer,address)

target_link_libraries(pdf_fuzzer
  protobuf-mutator-libfuzzer
  protobuf-mutator
  goo
  fofi
  ${PROTOBUF_LIB_TARGET}
  z
  pthread
  ${FONTCONFIG_LIBRARY}
)

# ── PoDoFo fuzzer target ──────────────────────────────────────────────────────
if(PODOFO_FOUND)
  add_executable(pdf_fuzzer_podofo
    harness_podofo.cpp
    serializer.cpp
    ${CFF_SERIALIZER_SRC}
    ${PROTO_SRCS}
  )
  target_link_options(pdf_fuzzer_podofo PRIVATE -fsanitize=fuzzer,address)
  target_include_directories(pdf_fuzzer_podofo PRIVATE ${PODOFO_INCLUDE_DIRS})
  target_link_libraries(pdf_fuzzer_podofo
    protobuf-mutator-libfuzzer
    protobuf-mutator
    ${PROTOBUF_LIB_TARGET}
    ${PODOFO_LIBRARIES}
    z
    pthread
  )
  message(STATUS "PoDoFo found (${PODOFO_VERSION}) -- pdf_fuzzer_podofo will be built")
else()
  message(STATUS "PoDoFo not found -- skipping pdf_fuzzer_podofo")
endif()
endif()

add_executable(verify_serializer
  tests/verify_serializer.cpp
  serializer.cpp
  ${CFF_SERIALIZER_SRC}
  ${PROTO_SRCS}
)
target_link_libraries(verify_serializer ${PROTOBUF_LIB_TARGET} z)

add_executable(verify_cff
  modules/cff/verify_cff.cc
  ${CFF_SERIALIZER_SRC}
  ${PROTO_SRCS}
)
target_link_libraries(verify_cff ${PROTOBUF_LIB_TARGET})

add_executable(gen_corpus
  tools/gen_corpus.cpp
  ${PROTO_SRCS}
)
target_link_libraries(gen_corpus ${PROTOBUF_LIB_TARGET})

add_executable(proto2pdf
  tools/proto2pdf.cpp
  serializer.cpp
  ${CFF_SERIALIZER_SRC}
  ${PROTO_SRCS}
)
target_link_libraries(proto2pdf ${PROTOBUF_LIB_TARGET} z)

add_executable(check_serializer
  tools/check_serializer.cpp
  serializer.cpp
  ${CFF_SERIALIZER_SRC}
  ${PROTO_SRCS}
)
target_link_libraries(check_serializer ${PROTOBUF_LIB_TARGET} z)

add_executable(measure_valid_rate
  tools/measure_valid_rate.cpp
  serializer.cpp
  ${CFF_SERIALIZER_SRC}
  ${PROTO_SRCS}
)
target_link_libraries(measure_valid_rate ${PROTOBUF_LIB_TARGET} z)

add_executable(type3cache2pdf
  modules/type3cache/tools/type3cache2pdf.cpp
  ${TYPE3CACHE_SERIALIZER_SRC}
  ${PROTO_SRCS}
)
target_link_libraries(type3cache2pdf ${PROTOBUF_LIB_TARGET})

add_executable(gen_type3cache_corpus
  modules/type3cache/tools/gen_type3cache_corpus.cpp
  ${PROTO_SRCS}
)
target_link_libraries(gen_type3cache_corpus ${PROTOBUF_LIB_TARGET})

add_executable(check_type3cache_corpus
  modules/type3cache/tools/check_type3cache_corpus.cpp
  ${TYPE3CACHE_SERIALIZER_SRC}
  ${PROTO_SRCS}
)
target_link_libraries(check_type3cache_corpus ${PROTOBUF_LIB_TARGET})

add_executable(gen_cve_2020_35376_seed
  modules/cff/seeds/gen_cve_2020_35376_seed.cpp
  ${PROTO_SRCS}
)
target_link_libraries(gen_cve_2020_35376_seed ${PROTOBUF_LIB_TARGET})

add_executable(inspect_cff_seed
  modules/cff/tools/inspect_cff_seed.cpp
  ${PROTO_SRCS}
)
target_link_libraries(inspect_cff_seed ${PROTOBUF_LIB_TARGET})

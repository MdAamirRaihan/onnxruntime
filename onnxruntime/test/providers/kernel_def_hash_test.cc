// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/**
 * IMPORTANT NOTE AT THE TOP OF THE FILE
 *
 * This file contains tests which verify expected kernel def hashes.
 * It is important for these to remain stable so that ORT format models are
 * backward compatible.
 *
 * If you are seeing a test failure from one of these tests, it is likely that
 * some kernel definition changed in a way that updated its hash value.
 * This is what we want to catch! Please update the kernel definition.
 * If adding more supported types to an existing kernel definition, consider
 * using KernelDefBuilder::FixedTypeConstraintForHash().
 *
 * For example:
 * Say we have a kernel definition like this, which supports types int and
 * double:
 *     KernelDefBuilder{}
 *         .TypeConstraint(
 *             "T", BuildKernelDefConstraints<int, double>())
 * If we want to update the kernel definition to add support for float, we can
 * change it to something like this:
 *     KernelDefBuilder{}
 *         .TypeConstraint(
 *             "T", BuildKernelDefConstraints<int, double, float>())
 *         .FixedTypeConstraintForHash(
 *             "T", BuildKernelDefConstraints<int, double>())
 * In the updated kernel definition, the original types are specified with
 * FixedTypeConstraintForHash().
 *
 * New kernel definitions should not use FixedTypeConstraintForHash().
 * It is a way to keep the hash stable as kernel definitions change.
 *
 * It is also possible that you have added a new kernel definition and are
 * seeing a message from one of these tests about updating the expected data.
 * Please do that if appropriate.
 *
 * The expected value files are in this directory:
 *     onnxruntime/test/testdata/kernel_def_hashes
 * The data is specified in JSON as an array of key-value arrays.
 * Example data can be written to stdout with this test:
 *     KernelDefHashTest.DISABLED_PrintCpuKernelDefHashes
 * Use the option --gtest_also_run_disabled_tests to enable it.
 * Be careful about updating the expected values - as mentioned before, the
 * values should be stable. Typically, we should only add new entries.
 *
 * In the unlikely event that we need to make a change to the kernel def
 * hashing that breaks backward compatibility, the expected values may need to
 * be updated.
 */

#include <algorithm>
#include <cinttypes>
#include <fstream>
#include <iostream>

#include "gtest/gtest.h"
#include "onnxruntime_config.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 28020)
#elif __aarch64__ && defined(HAS_FORMAT_TRUNCATION)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
#include "nlohmann/json.hpp"
#ifdef _WIN32
#pragma warning(pop)
#elif __aarch64__ && defined(HAS_FORMAT_TRUNCATION)
#pragma GCC diagnostic pop
#endif

#include "asserts.h"
#include "core/common/common.h"
#include "core/common/path_string.h"
#include "core/framework/kernel_registry.h"
#include "core/mlas/inc/mlas.h"
#include "core/platform/env_var_utils.h"
#include "core/providers/cpu/cpu_execution_provider.h"
#include "gtest/gtest.h"

using json = nlohmann::json;

namespace onnxruntime {
namespace test {

namespace {
// If set to 1, do strict checking of the kernel def hash values.
// With strict checking, the expected and actual values must match exactly.
// Otherwise, the expected values must be present in the actual values.
static constexpr const char* kStrictKernelDefHashCheckEnvVar =
    "ORT_TEST_STRICT_KERNEL_DEF_HASH_CHECK";

std::string DumpKernelDefHashes(const onnxruntime::KernelDefHashes& kernel_def_hashes) {
  const json j(kernel_def_hashes);
  return j.dump(/* indent */ 4);
}

KernelDefHashes ParseKernelDefHashes(std::istream& in) {
  KernelDefHashes kernel_def_hashes{};
  const json j = json::parse(in);
  j.get_to<onnxruntime::KernelDefHashes>(kernel_def_hashes);
  return kernel_def_hashes;
}

void AppendKernelDefHashesFromFile(const PathString& path, KernelDefHashes& kernel_def_hashes) {
  std::ifstream in{path};
  ORT_ENFORCE(in, "Failed to open file: ", ToUTF8String(path));
  const auto file_kernel_def_hashes = ParseKernelDefHashes(in);
  kernel_def_hashes.insert(
      kernel_def_hashes.end(), file_kernel_def_hashes.begin(), file_kernel_def_hashes.end());
}

void CheckKernelDefHashes(const KernelDefHashes& actual, const KernelDefHashes& expected, bool is_strict) {
  ASSERT_TRUE(std::is_sorted(actual.begin(), actual.end()));
  ASSERT_TRUE(std::is_sorted(expected.begin(), expected.end()));

  constexpr const char* kNoteReference = "Note: Please read the note at the top of this file: " __FILE__;

  KernelDefHashes expected_minus_actual{};
  std::set_difference(expected.begin(), expected.end(), actual.begin(), actual.end(),
                      std::back_inserter(expected_minus_actual));
  if (!expected_minus_actual.empty()) {
    const auto message = MakeString(
        "Some expected kernel def hashes were not found.\n",
        kNoteReference, "\n",
        DumpKernelDefHashes(expected_minus_actual));
    ADD_FAILURE() << message;
  }

  KernelDefHashes actual_minus_expected{};
  std::set_difference(actual.begin(), actual.end(), expected.begin(), expected.end(),
                      std::back_inserter(actual_minus_expected));
  if (!actual_minus_expected.empty()) {
    const auto message = MakeString(
        "Unexpected kernel def hashes were found, please update the expected values as needed "
        "(see the output below).\n",
        kNoteReference, "\n",
        DumpKernelDefHashes(actual_minus_expected));
    if (is_strict) {
      ADD_FAILURE() << message;
    } else {
      std::cerr << message << "\n";
    }
  }
}
}  // namespace

TEST(KernelDefHashTest, DISABLED_PrintCpuKernelDefHashes) {
  KernelRegistry kernel_registry{};
  ASSERT_STATUS_OK(RegisterCPUKernels(kernel_registry));
  const auto cpu_kernel_def_hashes = kernel_registry.ExportKernelDefHashes();
  std::cout << DumpKernelDefHashes(cpu_kernel_def_hashes) << "\n";
}

TEST(KernelDefHashTest, ExpectedCpuKernelDefHashes) {
  const bool is_strict = ParseEnvironmentVariableWithDefault<bool>(kStrictKernelDefHashCheckEnvVar, false);

  const auto expected_cpu_kernel_def_hashes = []() {
    KernelDefHashes result{};
    AppendKernelDefHashesFromFile(ORT_TSTR("testdata/kernel_def_hashes/onnx.cpu.json"), result);
#if !defined(DISABLE_ML_OPS)
    AppendKernelDefHashesFromFile(ORT_TSTR("testdata/kernel_def_hashes/onnx.ml.cpu.json"), result);
#endif  // !DISABLE_ML_OPS
#if !defined(DISABLE_CONTRIB_OPS)
    AppendKernelDefHashesFromFile(ORT_TSTR("testdata/kernel_def_hashes/contrib.cpu.json"), result);
    // NCHWc kernels are enabled if MlasNchwcGetBlockSize() > 1
    if (MlasNchwcGetBlockSize() > 1) {
      AppendKernelDefHashesFromFile(ORT_TSTR("testdata/kernel_def_hashes/contrib.nchwc.cpu.json"), result);
    }
#endif  // !DISABLE_CONTRIB_OPS
#if defined(ENABLE_TRAINING_OPS)
    AppendKernelDefHashesFromFile(ORT_TSTR("testdata/kernel_def_hashes/training_ops.cpu.json"), result);
#endif  // ENABLE_TRAINING_OPS
#if !defined(DISABLE_OPTIONAL_TYPE)
    AppendKernelDefHashesFromFile(ORT_TSTR("testdata/kernel_def_hashes/onnx.optional_type_ops.cpu.json"), result);
#endif  // !DISABLE_OPTIONAL_TYPE
    // TODO also handle kernels enabled by these symbols: BUILD_MS_EXPERIMENTAL_OPS
    std::sort(result.begin(), result.end());
    return result;
  }();

  KernelRegistry kernel_registry{};
  ASSERT_STATUS_OK(RegisterCPUKernels(kernel_registry));
  auto cpu_kernel_def_hashes = kernel_registry.ExportKernelDefHashes();

  CheckKernelDefHashes(cpu_kernel_def_hashes, expected_cpu_kernel_def_hashes, is_strict);
}

// This test is to ensure the latest opset version for ops which can be added
// during layout transformation step are added. IF this test fails then it means
// there is a new version available for one of the ops in the map.
// Adding this test here because resolution for this test failure requires fetching the hash
// for one of the ops in the list below and this file has information around that.
// Please update the following 3 places:
// 1. api_impl.cc "onnx_ops_available_versions" map, include the latest version in the map
// 2. static_kernel_def_hashes.cc "static_kernel_hashes" include an entry for latest version and it's associated hash
// 3. This file "onnx_ops_available_versions" map, include the latest version in the map
TEST(KernelDefHashTest, TestNewOpsVersionSupportDuringLayoutTransform) {
  static const std::unordered_map<std::string, std::vector<int>> onnx_ops_available_versions = {
      {"Squeeze", {1, 11, 13}},
      {"Unsqueeze", {1, 11, 13}},
      {"Gather", {1, 11, 13}},
      {"Transpose", {1, 13}},
      {"Identity", {1, 13, 14, 16}},
  };

  auto schema_registry = ONNX_NAMESPACE::OpSchemaRegistry::Instance();
  for (const auto& [op_type, version_list] : onnx_ops_available_versions) {
    auto schema = schema_registry->GetSchema(op_type, INT_MAX, kOnnxDomain);
    EXPECT_EQ(schema->SinceVersion(), version_list[version_list.size() - 1]) << "A new version for op: " << op_type
                                                                             << "is available. Please update the files mentioned in the comments of this test.";
  }
}
}  // namespace test
}  // namespace onnxruntime

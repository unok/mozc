// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// myime-owned runner derived from session_handler_scenario_test.cc.

#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "base/file_stream.h"
#include "base/file_util.h"
#include "engine/engine_interface.h"
#include "engine/mock_data_engine_factory.h"
#include "protocol/commands.pb.h"
#include "request/request_test_util.h"
#include "session/session_handler_test_util.h"
#include "session/session_handler_tool.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

ABSL_DECLARE_FLAG(bool, use_history_rewriter);

namespace mozc {

using ::mozc::session::SessionHandlerInterpreter;
using ::mozc::session::testing::SessionHandlerTestBase;
using ::testing::WithParamInterface;

class SessionHandlerMyimeScenarioTestBase : public SessionHandlerTestBase {
 protected:
  void SetUp() override {
    flagsaver_ = std::make_unique<absl::FlagSaver>();
    absl::SetFlag(&FLAGS_use_history_rewriter, true);
    SessionHandlerTestBase::SetUp();
    std::unique_ptr<EngineInterface> engine =
        MockDataEngineFactory::Create().value();
    handler_ = std::make_unique<SessionHandlerInterpreter>(std::move(engine));
  }

  void TearDown() override {
    handler_.reset();
    SessionHandlerTestBase::TearDown();
    flagsaver_.reset(nullptr);
  }

  std::unique_ptr<SessionHandlerInterpreter> handler_;
  std::unique_ptr<absl::FlagSaver> flagsaver_;
};

const std::vector<std::string>& GetMyimeScenarioFileList() {
  static const absl::NoDestructor<std::vector<std::string>> scenario_files([] {
    const absl::StatusOr<std::string> list_path = mozc::testing::GetSourceFile(
        {"data", "test/session/scenario/myime/scenario_list.txt"});
    if (!list_path.ok()) {
      LOG(FATAL) << list_path.status();
    }

    InputFileStream input_stream(*list_path);
    if (!input_stream) {
      LOG(FATAL) << "Cannot open scenario list: " << *list_path;
    }

    std::vector<std::string> result;
    std::unordered_set<std::string> paths;
    std::unordered_map<std::string, std::string> basename_to_path;
    std::string line;
    while (std::getline(input_stream, line)) {
      const std::string::size_type begin = line.find_first_not_of(" \t\r\n");
      if (begin == std::string::npos || line[begin] == '#') {
        continue;
      }
      const std::string::size_type end = line.find_last_not_of(" \t\r\n");
      std::string path = line.substr(begin, end - begin + 1);
      if (!paths.insert(path).second) {
        LOG(FATAL) << "Duplicate scenario path in scenario_list.txt: " << path;
      }
      const std::string basename = FileUtil::Basename(
          FileUtil::NormalizeDirectorySeparator(path));
      const auto [it, inserted] = basename_to_path.emplace(basename, path);
      if (!inserted) {
        LOG(FATAL) << "Duplicate scenario basename in scenario_list.txt: "
                   << basename << " (" << it->second << " and " << path << ")";
      }
#if defined(__APPLE__)
      // Match the conditional entry in the upstream scenario list.
      if (path == "test/session/scenario/input_mode.txt") {
        continue;
      }
#endif  // defined(__APPLE__)
      result.push_back(std::move(path));
    }
    return result;
  }());
  return *scenario_files;
}

void ParseMyimeScenarioLine(SessionHandlerInterpreter& handler,
                            const std::string& line) {
  std::vector<std::string> args = handler.Parse(line);
  if (args.empty()) {
    return;
  }
  const absl::Status status = handler.Eval(args);
  if (!status.ok()) {
    FAIL() << line << "\n" << status.message();
  }
}

class SessionHandlerMyimeScenarioTest
    : public SessionHandlerMyimeScenarioTestBase,
      public WithParamInterface<std::string> {
 public:
  static std::string GetTestName(
      const ::testing::TestParamInfo<ParamType>& info) {
    return absl::StrReplaceAll(
        FileUtil::Basename(FileUtil::NormalizeDirectorySeparator(info.param)),
        {{".", "_"}});
  }
};

INSTANTIATE_TEST_SUITE_P(
    SessionHandlerMyimeScenarioParameters, SessionHandlerMyimeScenarioTest,
    ::testing::ValuesIn(GetMyimeScenarioFileList()),
    SessionHandlerMyimeScenarioTest::GetTestName);

TEST_P(SessionHandlerMyimeScenarioTest, TestImplBase) {
  const absl::StatusOr<std::string> scenario_path =
      mozc::testing::GetSourceFile({"data", GetParam()});
  ASSERT_TRUE(scenario_path.ok()) << scenario_path.status();
  handler_->ClearAll();
  LOG(INFO) << "Testing " << FileUtil::Basename(*scenario_path);
  InputFileStream input_stream(*scenario_path);

  std::string line_text;
  int line_number = 0;
  while (std::getline(input_stream, line_text)) {
    ++line_number;
    SCOPED_TRACE(absl::StrFormat("Scenario: %s [%s:%d]", line_text,
                                 *scenario_path, line_number));
    ParseMyimeScenarioLine(*handler_, line_text);
  }
}

class SessionHandlerMyimeScenarioTestForRequest
    : public SessionHandlerMyimeScenarioTestBase,
      public WithParamInterface<std::tuple<const char*, commands::Request>> {
 public:
  static std::string GetTestName(
      const ::testing::TestParamInfo<ParamType>& info) {
    return absl::StrCat(
        info.index, "_",
        absl::StrReplaceAll(
            FileUtil::Basename(
                FileUtil::NormalizeDirectorySeparator(std::get<0>(info.param))),
            {{".", "_"}}));
  }
};

const char* kMyimeScenariosForExperimentParams[] = {
#define DATA_DIR "test/session/scenario/"
    DATA_DIR "myime/mobile_apply_user_segment_history_rewriter.txt",
    DATA_DIR "mobile_delete_history.txt",
    DATA_DIR "mobile_preedit.txt",
    DATA_DIR "mobile_qwerty_transliteration_scenario.txt",
    DATA_DIR "myime/mobile_revert_user_history_learning.txt",
    DATA_DIR "mobile_switch_composition_mode.txt",
    DATA_DIR "mobile_t13n_candidates.txt",
    DATA_DIR "mobile_zero_query.txt",
#undef DATA_DIR
};

TEST(SessionHandlerMyimeScenarioListTest, CoversExpectedScenariosExactlyOnce) {
  std::unordered_map<std::string, int> reference_count;
  std::unordered_map<std::string, std::vector<std::string>> reference_paths;
  for (const std::string& path : GetMyimeScenarioFileList()) {
    const std::string basename = FileUtil::Basename(
        FileUtil::NormalizeDirectorySeparator(path));
    ++reference_count[basename];
    reference_paths[basename].push_back(path);
  }
  for (const char* path : kMyimeScenariosForExperimentParams) {
    const std::string basename = FileUtil::Basename(
        FileUtil::NormalizeDirectorySeparator(path));
    ++reference_count[basename];
    reference_paths[basename].push_back(path);
  }
#if defined(__APPLE__)
  // GetMyimeScenarioFileList filters this runtime-incompatible scenario, but
  // scenario_list.txt still directly references it for coverage purposes.
  ++reference_count["input_mode.txt"];
  reference_paths["input_mode.txt"].push_back(
      "test/session/scenario/input_mode.txt");
#endif  // defined(__APPLE__)

  const absl::StatusOr<std::string> expected_path =
      mozc::testing::GetSourceFile(
          {"session", "myime_expected_scenarios.txt"});
  ASSERT_TRUE(expected_path.ok()) << expected_path.status();
  InputFileStream expected_stream(*expected_path);
  ASSERT_TRUE(expected_stream) << "Cannot open expected scenarios: "
                               << *expected_path;

  // These files exist in the upstream scenario package but are intentionally
  // absent from both of upstream's scenario arrays.
  const std::unordered_set<std::string> upstream_runner_exclusions = {
      "b7321313_scenario.txt",  // Legacy regression not selected upstream.
      "kana_modifier_insensitive_conversion.txt",  // Not selected upstream.
  };
  // AzooKey has no partial variant candidates for the measured input, so item
  // 2 requires this scenario to remain documented but not executed.
  const std::unordered_set<std::string> myime_engine_exclusions = {
      "mobile_partial_variant_candidates.txt",
  };

  std::unordered_set<std::string> expected_basenames;
  std::string basename;
  while (std::getline(expected_stream, basename)) {
    const std::string::size_type begin =
        basename.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
      continue;
    }
    const std::string::size_type end = basename.find_last_not_of(" \t\r\n");
    basename = basename.substr(begin, end - begin + 1);
    ASSERT_TRUE(expected_basenames.insert(basename).second)
        << "Duplicate basename in myime_expected_scenarios.txt: " << basename;
    if (upstream_runner_exclusions.find(basename) !=
            upstream_runner_exclusions.end() ||
        myime_engine_exclusions.find(basename) !=
            myime_engine_exclusions.end()) {
      EXPECT_EQ(reference_count[basename], 0) << basename;
      continue;
    }
    EXPECT_EQ(reference_count[basename], 1) << basename;
    if (reference_count[basename] == 1) {
      const std::string direct_path = "test/session/scenario/" + basename;
      const std::string myime_path =
          "test/session/scenario/myime/" + basename;
      EXPECT_TRUE(reference_paths[basename][0] == direct_path ||
                  reference_paths[basename][0] == myime_path)
          << basename << " is referenced through unexpected path: "
          << reference_paths[basename][0];
    }
  }
}

commands::Request GetMyimeMobileRequest() {
  commands::Request request = commands::Request::default_instance();
  request_test_util::FillMobileRequest(&request);
  return request;
}

INSTANTIATE_TEST_SUITE_P(
    TestForExperimentParams, SessionHandlerMyimeScenarioTestForRequest,
    ::testing::Combine(
        ::testing::ValuesIn(kMyimeScenariosForExperimentParams),
        ::testing::Values(GetMyimeMobileRequest())),
    SessionHandlerMyimeScenarioTestForRequest::GetTestName);

TEST_P(SessionHandlerMyimeScenarioTestForRequest, TestImplBase) {
  const absl::StatusOr<std::string> scenario_path =
      mozc::testing::GetSourceFile({"data", std::get<0>(GetParam())});
  ASSERT_TRUE(scenario_path.ok()) << scenario_path.status();
  handler_->ClearAll();
  handler_->SetRequest(std::get<1>(GetParam()));

  LOG(INFO) << "Testing " << FileUtil::Basename(*scenario_path);
  InputFileStream input_stream(*scenario_path);
  std::string line_text;
  int line_number = 0;
  while (std::getline(input_stream, line_text)) {
    ++line_number;
    SCOPED_TRACE(absl::StrFormat("Scenario: %s [%s:%d]", line_text,
                                 *scenario_path, line_number));
    ParseMyimeScenarioLine(*handler_, line_text);
  }
}

}  // namespace mozc

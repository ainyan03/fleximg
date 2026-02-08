// fleximg Test Suite - Main Entry Point
// doctestフレームワークのmain関数を提供

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// PlatformIO の DoctestTestCaseParser は === 区切りの verbose 出力を期待するが、
// doctest のデフォルト出力にはこれが含まれない。
// Listener を登録し、テストケース開始時に区切りブロックを出力することで
// PlatformIO 側で全テストケースが認識されるようにする。
#ifdef PIO_UNIT_TESTING
#include <cstdio>

struct PioListener : public doctest::IReporter {
  PioListener(const doctest::ContextOptions&) {}

  void test_case_start(const doctest::TestCaseData& tc) override {
    std::printf(
        "======================================================="
        "================\n");
    std::printf("%s:%d:\n", tc.m_file.c_str(), tc.m_line);
    if (tc.m_test_suite[0])
      std::printf("TEST SUITE: %s\n", tc.m_test_suite);
    std::printf("TEST CASE:  %s\n\n", tc.m_name);
  }

  // 以下は空実装（Listener として必要なインターフェース）
  void report_query(const doctest::QueryData&) override {}
  void test_run_start() override {}
  void test_run_end(const doctest::TestRunStats&) override {}
  void test_case_reenter(const doctest::TestCaseData&) override {}
  void test_case_end(const doctest::CurrentTestCaseStats&) override {}
  void test_case_exception(const doctest::TestCaseException&) override {}
  void subcase_start(const doctest::SubcaseSignature&) override {}
  void subcase_end() override {}
  void log_assert(const doctest::AssertData&) override {}
  void log_message(const doctest::MessageData&) override {}
  void test_case_skipped(const doctest::TestCaseData&) override {}
};

REGISTER_LISTENER("pio", 0, PioListener);
#endif

#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestRegistry.h"
#include "CppUTestExt/MockSupport.h"
#include "CppUTestExt/MockSupportPlugin.h"

/*
 * MockSupportPlugin checks mock expectations, then clears them, in its
 * postTestAction. CppUTest runs a group's teardown() before that, so a
 * mock().clear() in teardown() (or setup(), or a test body) discards pending
 * expectations first and an unfulfilled expectOneCall passes silently.
 *
 * Tests therefore never call mock().clear(): the plugin already checks and
 * clears after every test. This guard enforces that. It plants a marker in
 * mock()'s data store before each test; mock().clear() erases it. If the
 * marker is gone when the test finishes, the test fails.
 */
class MockClearGuardPlugin : public TestPlugin {
 public:
  MockClearGuardPlugin() : TestPlugin("MockClearGuard") {}

  void preTestAction(UtestShell&, TestResult&) override
  {
    mock().setData(marker, 1);
  }

  void postTestAction(UtestShell& test, TestResult& result) override
  {
    if(!test.hasFailed() && !mock().hasData(marker)) {
      result.addFailure(TestFailure(&test,
        "mock().clear() was called during this test. It discards pending "
        "expectations before MockSupportPlugin checks them, so unmet "
        "expectations pass silently. Remove it: the plugin checks and "
        "clears after every test. Use mock().checkExpectations() to check "
        "mid-test."));
    }
  }

 private:
  static constexpr const char* marker = "__mock_clear_guard__";
};

int main(int argc, char** argv)
{
  /* Plugins run their postTestAction in reverse install order: the guard is
   * installed first so it inspects mock() before MockSupportPlugin clears it. */
  MockClearGuardPlugin guardPlugin;
  TestRegistry::getCurrentRegistry()->installPlugin(&guardPlugin);

  MockSupportPlugin mockPlugin;
  TestRegistry::getCurrentRegistry()->installPlugin(&mockPlugin);

  return RUN_ALL_TESTS(argc, argv);
}

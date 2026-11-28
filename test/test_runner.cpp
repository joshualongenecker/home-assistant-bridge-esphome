#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestRegistry.h"
#include "CppUTestExt/MockSupport.h"
#include "CppUTestExt/MockSupportPlugin.h"

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
        "mock() was cleared during this test (mock().clear() or "
        "mock().clearAllData()). A clear discards pending expectations "
        "before MockSupportPlugin checks them, so unmet expectations pass "
        "silently. Remove it: the plugin checks and clears after every "
        "test. Use mock().checkExpectations() to check mid-test. If every "
        "test fails with this message, the guard was installed after "
        "MockSupportPlugin in main()."));
    }
  }

 private:
  static constexpr const char* marker = "__mock_clear_guard__";
};

int main(int argc, char** argv)
{
  MockClearGuardPlugin guardPlugin;
  TestRegistry::getCurrentRegistry()->installPlugin(&guardPlugin);

  MockSupportPlugin mockPlugin;
  TestRegistry::getCurrentRegistry()->installPlugin(&mockPlugin);

  return RUN_ALL_TESTS(argc, argv);
}

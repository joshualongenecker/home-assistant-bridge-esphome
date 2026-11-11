TARGET := home-assistant-bridge_tests
BUILD_DIR := ./build

# ThreadSanitizer variant for the concurrency tests: `make test TSAN=1`.
# TSan cannot share a binary with ASan, so it gets its own build dir (set
# here, before OBJS expands BUILD_DIR). It checks the happens-before edges
# of the discovery lifecycle claim protocol (the release store on phase_
# publishing trigger_) the way the dual-core device will; the plain ASan
# build only catches invariant violations that happen to manifest.
# TSan does not run LeakSanitizer: leak detection stays with the ASan
# variants (ASAN_OPTIONS below applies to the run lines of both suites).
TSAN ?= 0
ifeq ($(TSAN),1)
BUILD_DIR := ./build-tsan
endif
# The integration build links a separate binary so the two targets can never
# run each other's stale binary (make cannot tell which source set produced a
# shared output path).
TARGET_INTEGRATION := home-assistant-bridge_tests_integration

INC_DIRS := \
  components/geappliances_bridge \
  lib/tiny/include \
  lib/tiny/test/include \
  lib/tiny-gea-api/include \
  lib/tiny-gea-api/test/include \
  test/include \
  test/simulation \

SRC_DIRS := \
  lib/tiny/src \
  lib/tiny/test/src \
  lib/tiny-gea-api/src \
  lib/tiny-gea-api/test/src \
  test \
  test/src \
  test/tests \
  test/simulation \

SRC_FILES := $(wildcard components/geappliances_bridge/*.cpp)

SRCS := $(SRC_FILES) $(shell find $(SRC_DIRS) -maxdepth 1 \( -name '*.cpp' -or -name '*.c' -or -name '*.s' \) -not -name 'startup_integration_test.cpp')

# Integration test sources (includes startup_integration_test.cpp)
SRCS_INTEGRATION := $(SRC_FILES) $(shell find $(SRC_DIRS) -maxdepth 1 \( -name '*.cpp' -or -name '*.c' -or -name '*.s' \))
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

INC_DIRS += $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

ifeq ($(TSAN),1)
SANITIZE_FLAGS := -fsanitize=thread
else
SANITIZE_FLAGS := -fsanitize=address -fsanitize=undefined
endif


# Leak detection is on for both suites: no test owns memory that outlives its
# teardown, so a leak is a real leak — e.g. a discovery run orphaning its
# ~52 KB of run allocations (buffers + tinfl state).
#
# halt_on_error must stay at its default (1) here: with halt_on_error=0
# AddressSanitizer prints the LeakSanitizer report but still exits 0, so a leak
# would not fail CI. Debug with halting disabled when you want to see several
# mid-run ASan errors at once (leaks then only print, never fail):
#   make test ASAN_OPTIONS="detect_leaks=1 halt_on_error=0"
ASAN_OPTIONS ?= detect_leaks=1:detect_stack_use_after_return=0

# CppUTest installation paths — auto-detect common locations
# Override with: make CPPUTEST_PREFIX=/path/to/cpputest
#   macOS (Apple Silicon):  /opt/homebrew  (default)
#   macOS (Intel):          /usr/local
#   Linux (system pkg):     /usr
#   Linux (local build):    /path/to/CppUTest
UNAME_S := $(shell uname -s)
ifeq ($(TSAN)$(UNAME_S),1Darwin)
$(error TSAN=1 is unsupported on Darwin: clang ships no macOS ThreadSanitizer runtime)
endif
ifeq ($(UNAME_S),Darwin)
  # Apple Silicon → homebrew at /opt/homebrew; Intel → /usr/local
  ifeq ($(UNAME_P),arm)
    CPPUTEST_PREFIX ?= /opt/homebrew
  else
    CPPUTEST_PREFIX ?= /usr/local
  endif
else
  # Linux: default to system-wide install
  CPPUTEST_PREFIX ?= /usr
endif
CPPUTEST_INC := -I$(CPPUTEST_PREFIX)/include
CPPUTEST_LIB := -L$(CPPUTEST_PREFIX)/lib

CFLAGS += -std=c11 -pedantic
CPPFLAGS += $(SANITIZE_FLAGS) -fno-omit-frame-pointer -fprofile-arcs -ftest-coverage -DUSE_ESP_IDF -DUSE_ESP_IDF_STUBS -DUNIT_TEST_BUILD -DHA_DISCOVERY_TEST_EXPORT -DHA_DISCOVERY_CLEANUP_TEST_BUF_SIZE=512 -DHA_CLEANUP_TEST_BUF_SIZE=512 -DHA_DISCOVERY_CLEANUP_TEST_EXPORT 
CPPFLAGS += $(INC_FLAGS) $(CPPUTEST_INC) -MMD -MP -g -Wall -Wextra -Wcast-qual -Werror
CXXFLAGS += -std=c++17 -pthread
LDFLAGS := $(SANITIZE_FLAGS) $(CPPUTEST_LIB) --coverage -pthread
LDLIBS := -lstdc++ -lCppUTest -lCppUTestExt -lm --coverage

BUILD_DEPS += $(MAKEFILE_LIST)



.PHONY: test
test: $(BUILD_DIR)/$(TARGET)
	@echo Running tests...
	@ASAN_OPTIONS='$(ASAN_OPTIONS)' $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@echo Linking $@...
	@mkdir -p $(dir $@)
	@$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.s.o: %.s $(BUILD_DEPS)
	@echo Assembling $<...
	@mkdir -p $(dir $@)
	@$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.c.o: %.c $(BUILD_DEPS)
	@echo Compiling $<...
	@mkdir -p $(dir $@)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp $(BUILD_DEPS)
	@echo Compiling $<...
	@mkdir -p $(dir $@)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo Cleaning...
	@rm -rf ./build ./build-tsan

.PHONY: pytest
pytest:
	@python3 -m pytest scripts/test_generate_erd_lists.py scripts/test_ha_discovery.py -v

.PHONY: check-generated
check-generated:
	@bash scripts/check_generated_sync.sh

.PHONY: integration-test
integration-test:
	@echo Building integration tests...
	@$(MAKE) $(BUILD_DIR)/$(TARGET_INTEGRATION) TARGET="$(TARGET_INTEGRATION)" SRCS="$(SRCS_INTEGRATION)"
	@echo Running integration tests...
	@ASAN_OPTIONS='$(ASAN_OPTIONS)' $(BUILD_DIR)/$(TARGET_INTEGRATION)

-include $(DEPS)

.PHONY: coverage
coverage: $(BUILD_DIR)/$(TARGET)
	@which lcov >/dev/null 2>&1 || { echo "lcov not found. Install with: sudo apt install lcov"; exit 1; }
	@echo Generating coverage report...
	@mkdir -p coverage
	@lcov --capture --directory . --output-file coverage/raw.info \
		--exclude "*/test/*" \
		--exclude "*/lib/tiny/test/*" \
		--exclude "*/lib/tiny-gea-api/test/*" \
		--exclude "*/build/*" \
		--exclude "*/build-tsan/*" \
		--exclude "*/esphome_stubs.cpp" \
		--exclude "*/mqtt_client_double.cpp" \
		--exclude "*/tiny_*_double.cpp" \
		--exclude "*/test_runner.cpp" \
		--exclude "*/simulation/*" 2>/dev/null || true
	@lcov --extract coverage/raw.info "*/components/*" --output-file coverage/components.info 2>/dev/null || true
	@lcov --list coverage/components.info
	@echo "Generating HTML report in coverage/html/..."
	@genhtml coverage/components.info --output-directory coverage/html
	@echo "HTML report: coverage/html/index.html"

.PHONY: cov-clean
cov-clean:
	@find . -name "*.gcda" -delete 2>/dev/null || true
	@echo Cleaned gcda files.

TARGET := home-assistant-bridge_tests
BUILD_DIR := ./build

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

SRC_FILES := \
  components/geappliances_bridge/mqtt_bridge.cpp \
  components/geappliances_bridge/mqtt_bridge_polling.cpp \
  components/geappliances_bridge/uptime_monitor.c \

SRCS := $(SRC_FILES) $(shell find $(SRC_DIRS) -maxdepth 1 -name *.cpp -or -name *.c -or -name *.s)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

INC_DIRS += $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

SANITIZE_FLAGS := -fsanitize=address -fsanitize=undefined

CFLAGS += -std=c11 -pedantic
CPPFLAGS += $(SANITIZE_FLAGS) -fno-omit-frame-pointer
CPPFLAGS += $(INC_FLAGS) -MMD -MP -g -Wall -Wextra -Wcast-qual -Werror
CXXFLAGS += -std=c++17
LDFLAGS := $(SANITIZE_FLAGS)
LDLIBS := -lstdc++ -lCppUTest -lCppUTestExt -lm

BUILD_DEPS += $(MAKEFILE_LIST)

# Generate erd_lists.h from JSON before building
ERD_LISTS_HEADER := components/geappliances_bridge/erd_lists.h
ERD_DEFINITIONS_JSON := lib/public-appliance-api-documentation/appliance_api_erd_definitions.json

$(ERD_LISTS_HEADER): $(ERD_DEFINITIONS_JSON) scripts/generate_erd_lists.py
	@echo Generating $@...
	@python3 scripts/generate_erd_lists.py

BUILD_DEPS += $(ERD_LISTS_HEADER)

.PHONY: test
test: $(BUILD_DIR)/$(TARGET)
	@echo Running tests...
	@$(BUILD_DIR)/$(TARGET)

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
	@rm -rf $(BUILD_DIR)

-include $(DEPS)

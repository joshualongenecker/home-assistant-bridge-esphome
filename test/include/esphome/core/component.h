#ifndef esphome_core_component_h
#define esphome_core_component_h

#include "hal.h"

namespace esphome {

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0.0f; }
  virtual bool teardown() { return true; }
};

namespace setup_priority {
  constexpr float DATA = 600.0f;
}

}

#endif

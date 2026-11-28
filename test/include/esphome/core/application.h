#ifndef esphome_core_application_h
#define esphome_core_application_h

namespace esphome {

class Application {
 public:
  void reboot() {}
  void safe_reboot() {}
};

inline Application App;

}

#endif

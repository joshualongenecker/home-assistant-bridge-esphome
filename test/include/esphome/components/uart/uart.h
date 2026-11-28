#ifndef esphome_components_uart_uart_h
#define esphome_components_uart_uart_h

#include <cstdint>
#include <cstddef>

namespace esphome {
namespace uart {

class UARTComponent {
 public:
  virtual ~UARTComponent() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual void write(uint8_t data) = 0;
  virtual void write(const uint8_t* data, size_t len) = 0;
  virtual int read_byte(uint8_t* byte) = 0;
  virtual void write_byte(uint8_t byte) = 0;
};

}
}

#endif

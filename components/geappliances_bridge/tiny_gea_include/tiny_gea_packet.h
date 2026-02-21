/*!
 * @file
 * @brief GEA packet definition.
 */

#ifndef tiny_gea_packet_h
#define tiny_gea_packet_h

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t destination;
  uint8_t payload_length;
  uint8_t source;
  uint8_t payload[1];
} tiny_gea_packet_t;

enum {
  // STX, ETX, CRC (MSB + LSB), source, destination, length
  tiny_gea_packet_transmission_overhead = 7,
  tiny_gea_packet_overhead = offsetof(tiny_gea_packet_t, payload),
  tiny_gea_packet_max_payload_length = 255 - tiny_gea_packet_transmission_overhead,
};

/*!
 * Macro for allocating a GEA packet with a given payload size on the stack.  Payload size is set automatically.
 */
#define tiny_gea_STACK_ALLOC_PACKET(_name, _payloadLength)                                   \
  uint8_t _name##Storage[_payloadLength + tiny_gea_packet_overhead] = { 0, _payloadLength }; \
  tiny_gea_packet_t* const _name = (tiny_gea_packet_t*)_name##Storage

/*!
 * Macro for allocating a GEA packet with with a provided payload type.  This sets the payload length
 * automatically and overlays the payload type on the packet payload.
 *
 * @note The payload types should have no alignment requirements (all fields should have single-byte
 * alignment, ie: should be u8s).
 */
#define tiny_gea_STACK_ALLOC_PACKET_TYPE(_packetName, _payloadName, _payloadType) \
  tiny_gea_STACK_ALLOC_PACKET(_packetName, sizeof(_payloadType));                 \
  _payloadType* _payloadName;                                                     \
  _payloadName = (_payloadType*)_packetName->payload;

/*!
 * Macro for statically allocating a GEA packet with a given payload size.  Payload size is set automatically.
 */
#define tiny_gea_STATIC_ALLOC_PACKET(_name, _payloadLength)                                         \
  static uint8_t _name##Storage[_payloadLength + tiny_gea_packet_overhead] = { 0, _payloadLength }; \
  static tiny_gea_packet_t* const _name = (tiny_gea_packet_t*)_name##Storage

#endif

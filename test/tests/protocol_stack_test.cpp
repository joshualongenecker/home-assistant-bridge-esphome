/*!
 * @file
 * @brief Unit tests for ProtocolStack (architecture review rec 1).
 *
 * Validates stack initialization for zero/one/two UARTs, the per-iteration
 * dispatch (GEA2 tight loop vs GEA3 iteration vs timer-only), the dual-UART
 * adapter gating, the GEA2-active latch, the stack-owned timer group, and
 * the GEA2 tick source wiring (the GEA2 interface's internal ack timeout
 * advances through the stack's own tick counter — the property the old
 * file-scope s_gea2_tick_source global could lose when more than one bridge
 * existed).
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

/* Undef CppUTest's new macro before including any STL headers */
#ifdef new
#undef new
#endif

#include "protocol_stack.h"
#include "double/esphome_hal_double.hpp"
#include "esphome/components/uart/uart.h"

#include <cstring>

using esphome::geappliances_bridge::ProtocolStack;
extern "C" {
#include "tiny_event.h"
#include "tiny_gea3_erd_client.h"
}

/* ------------------------------------------------------------------ */
/* Mock UARTComponent for testing                                      */
/* ------------------------------------------------------------------ */

struct StackTestUart : public esphome::uart::UARTComponent {
  int available_count;
  int read_count;
  int write_count;
  int write_byte_count;
  uint8_t last_written_byte;
  uint8_t read_buffer[256];
  int read_buffer_index;
  int read_buffer_size;

  StackTestUart()
    : available_count(0), read_count(0), write_count(0),
      write_byte_count(0), last_written_byte(0),
      read_buffer_index(0), read_buffer_size(0)
  {
    std::memset(read_buffer, 0, sizeof(read_buffer));
  }

  int available() override { return available_count; }

  int read() override
  {
    if (read_buffer_index < (int)sizeof(read_buffer)) {
      return read_buffer[read_buffer_index++];
    }
    read_count++;
    return -1;
  }

  void write(uint8_t data) override
  {
    last_written_byte = data;
    write_count++;
  }

  void write(const uint8_t* data, size_t len) override
  {
    for (size_t i = 0; i < len; i++) {
      last_written_byte = data[i];
      write_count++;
    }
  }

  int read_byte(uint8_t* byte) override
  {
    if (read_buffer_index < read_buffer_size) {
      *byte = read_buffer[read_buffer_index++];
      return 1;
    } else {
      *byte = 0;
      read_count++;
      return -1;
    }
  }

  void write_byte(uint8_t byte) override
  {
    last_written_byte = byte;
    write_byte_count++;
  }

  void clear()
  {
    available_count = 0;
    read_count = 0;
    write_count = 0;
    write_byte_count = 0;
    last_written_byte = 0;
    read_buffer_index = 0;
    read_buffer_size = 0;
    std::memset(read_buffer, 0, sizeof(read_buffer));
  }
};

/* ------------------------------------------------------------------ */
/* Activity recorder for the GEA2 adapter client's GEA3-typed events   */
/* ------------------------------------------------------------------ */

struct ActivityRecorder {
  int read_failed_count{0};
  tiny_erd_t last_failed_erd{0};

  static void on_activity(void* context, const void* args)
  {
    auto self = static_cast<ActivityRecorder*>(context);
    auto activity = static_cast<const tiny_gea3_erd_client_on_activity_args_t*>(args);
    if (activity->type == tiny_gea3_erd_client_activity_type_read_failed) {
      self->read_failed_count++;
      self->last_failed_erd = activity->read_failed.erd;
    }
  }
};

/* ------------------------------------------------------------------ */
/* Test group                                                           */
/* ------------------------------------------------------------------ */

TEST_GROUP(protocol_stack)
{
  ProtocolStack stack;
  StackTestUart gea3_uart;
  StackTestUart gea2_uart;

  void setup()
  {
    esphome_hal_double_set_millis(0);
  }

  void teardown()
  {
    mock().clear();
  }
};

/* ------------------------------------------------------------------ */
/* init() tests                                                         */
/* ------------------------------------------------------------------ */

TEST(protocol_stack, init_without_uarts_initializes_timer_group_only)
{
  stack.init(0xE4, nullptr, nullptr);
  CHECK(stack.timer_group() != nullptr);
  CHECK(stack.get_gea3_uart_adapter()->interface.api == nullptr);
  CHECK(stack.get_gea2_uart_adapter()->interface.api == nullptr);
  CHECK(stack.get_gea3_client()->api == nullptr);
  CHECK(stack.get_gea2_client()->api == nullptr);
  CHECK(stack.get_gea2_adapter_client()->api == nullptr);
  CHECK_FALSE(stack.has_gea3_uart());
  CHECK_FALSE(stack.has_gea2_uart());
}

TEST(protocol_stack, run_without_uarts_runs_stack_timer_group)
{
  stack.init(0xE4, nullptr, nullptr);

  // A timer registered on the stack's timer group fires through run():
  // the stack owns the group every data-path component registers on.
  static bool fired = false;
  fired = false;
  tiny_timer_t timer;
  tiny_timer_start(stack.timer_group(), &timer, 10, &fired, +[](void* context) {
    *static_cast<bool*>(context) = true;
  });

  esphome_hal_double_set_millis(50);
  stack.run(false);
  stack.run(true);
  CHECK(fired);
}

TEST(protocol_stack, init_with_gea3_uart_initializes_gea3_path)
{
  stack.init(0xE4, &gea3_uart, nullptr);
  CHECK(stack.get_gea3_uart_adapter()->interface.api != nullptr);
  CHECK(stack.get_gea3_uart_adapter()->uart == &gea3_uart);
  CHECK(stack.get_gea3_client()->api != nullptr);
  CHECK(stack.get_gea2_client()->api == nullptr);
  CHECK(stack.get_gea2_adapter_client()->api == nullptr);
  CHECK(stack.has_gea3_uart());
  CHECK_FALSE(stack.has_gea2_uart());
}

TEST(protocol_stack, init_with_gea2_uart_initializes_gea2_path)
{
  stack.init(0xE4, nullptr, &gea2_uart);
  CHECK(stack.get_gea2_uart_adapter()->interface.api != nullptr);
  CHECK(stack.get_gea2_uart_adapter()->uart == &gea2_uart);
  CHECK(stack.get_gea2_client()->api != nullptr);
  CHECK(stack.get_gea2_adapter_client()->api != nullptr);
  CHECK(stack.has_gea2_uart());
  CHECK_FALSE(stack.has_gea3_uart());
}

TEST(protocol_stack, init_with_both_uarts_initializes_both_paths)
{
  stack.init(0xE4, &gea3_uart, &gea2_uart);
  CHECK(stack.get_gea3_client()->api != nullptr);
  CHECK(stack.get_gea2_client()->api != nullptr);
  CHECK(stack.get_gea2_adapter_client()->api != nullptr);
  CHECK(stack.has_gea3_uart());
  CHECK(stack.has_gea2_uart());
}

/* ------------------------------------------------------------------ */
/* run() dispatch and dual-UART adapter gating                          */
/* ------------------------------------------------------------------ */

TEST(protocol_stack, run_with_both_uarts_gates_adapters_by_gea2_pending)
{
  stack.init(0xE4, &gea3_uart, &gea2_uart);

  stack.run(true);
  CHECK(stack.get_gea2_uart_adapter()->enabled);
  CHECK_FALSE(stack.get_gea3_uart_adapter()->enabled);

  stack.run(false);
  CHECK(stack.get_gea3_uart_adapter()->enabled);
  CHECK_FALSE(stack.get_gea2_uart_adapter()->enabled);
}

TEST(protocol_stack, gea2_pending_ignored_without_gea2_uart)
{
  stack.init(0xE4, &gea3_uart, nullptr);
  stack.run(true);
  // No GEA2 adapter exists to gate; the GEA3 adapter stays enabled.
  CHECK(stack.get_gea3_uart_adapter()->enabled);
}

TEST(protocol_stack, run_with_gea2_active_latch_prefers_gea2)
{
  stack.init(0xE4, &gea3_uart, &gea2_uart);
  stack.set_gea2_active(true);

  stack.run(false);
  CHECK(stack.get_gea2_uart_adapter()->enabled);
  CHECK_FALSE(stack.get_gea3_uart_adapter()->enabled);
}

TEST(protocol_stack, gea2_active_latch_ignored_without_gea2_uart)
{
  stack.init(0xE4, &gea3_uart, nullptr);
  stack.set_gea2_active(true);
  stack.run(false);
  // The latch cannot force a GEA2 iteration without a GEA2 UART; the GEA3
  // adapter stays enabled.
  CHECK(stack.get_gea3_uart_adapter()->enabled);
}

TEST(protocol_stack, teardown_resets_gea2_active_latch)
{
  stack.init(0xE4, nullptr, &gea2_uart);
  stack.set_gea2_active(true);
  CHECK(stack.is_gea2_active());

  stack.teardown();
  CHECK_FALSE(stack.is_gea2_active());
}

/* ------------------------------------------------------------------ */
/* GEA2 tick source                                                     */
/* ------------------------------------------------------------------ */

/* The GEA2 interface transmits only when the stack's msec interrupt fires
 * (one per caught-up millisecond inside the GEA2 iteration), and its
 * internal reflection/ack timeouts advance only through the stack's own
 * tick source. Advancing simulated time therefore drives the send out, the
 * retry, and eventually the client's read-failed activity — the wiring the
 * old file-scope s_gea2_tick_source global could lose. */
TEST(protocol_stack, gea2_tick_source_drives_interface_ack_timeout)
{
  stack.init(0xE4, nullptr, &gea2_uart);

  ActivityRecorder recorder;
  tiny_event_subscription_t subscription;
  tiny_event_subscription_init(&subscription, &recorder, &ActivityRecorder::on_activity);
  tiny_event_subscribe(
      tiny_gea3_erd_client_on_activity(stack.get_gea2_adapter_client()),
      &subscription);

  tiny_gea3_erd_client_request_id_t request_id = 0;
  CHECK(tiny_gea3_erd_client_read(
      stack.get_gea2_adapter_client(), &request_id, 0xC0, 0x0008));

  esphome_hal_double_set_millis(1);
  stack.run(true);  // first iteration latches gea2_last_ms_ at t=1

  esphome_hal_double_set_millis(51);
  stack.run(true);
  CHECK(gea2_uart.write_byte_count > 0);
  int writes_after_send = gea2_uart.write_byte_count;

  // The client's request timeout (250 ms) plus its one retry means the
  // read-failed activity lands around 500 ms of simulated time.
  for (int i = 2; i <= 12; i++) {
    esphome_hal_double_set_millis(i * 50);
    stack.run(true);
    if (recorder.read_failed_count > 0) {
      break;
    }
  }

  CHECK(gea2_uart.write_byte_count > writes_after_send);
  CHECK(recorder.read_failed_count > 0);
  CHECK(recorder.last_failed_erd == 0x0008);
}

/* The tick source is per-stack: a second stack's GEA2 interface gets its own
 * counter, so both stacks' sends time out independently (the old file-scope
 * s_gea2_tick_source pointed at whichever bridge initialized last). */
TEST(protocol_stack, tick_source_is_per_stack_not_shared)
{
  ProtocolStack stack_a;
  ProtocolStack stack_b;
  StackTestUart uart_a;
  StackTestUart uart_b;

  stack_a.init(0xE4, nullptr, &uart_a);
  stack_b.init(0xE4, nullptr, &uart_b);

  tiny_gea3_erd_client_request_id_t id_a = 0;
  tiny_gea3_erd_client_request_id_t id_b = 0;
  CHECK(tiny_gea3_erd_client_read(stack_a.get_gea2_adapter_client(), &id_a, 0xC0, 0x0008));
  CHECK(tiny_gea3_erd_client_read(stack_b.get_gea2_adapter_client(), &id_b, 0xC0, 0x0008));

  esphome_hal_double_set_millis(1);
  stack_a.run(true);  // first iteration latches each stack's gea2_last_ms_
  stack_b.run(true);

  esphome_hal_double_set_millis(51);
  stack_a.run(true);
  stack_b.run(true);
  CHECK(uart_a.write_byte_count > 0);
  CHECK(uart_b.write_byte_count > 0);
  int writes_a = uart_a.write_byte_count;
  int writes_b = uart_b.write_byte_count;

  for (int i = 2; i <= 8; i++) {
    esphome_hal_double_set_millis(i * 50);
    stack_a.run(true);
    stack_b.run(true);
  }

  // Both stacks retry their own sends: each interface's timers advanced
  // through its own stack's tick source.
  CHECK(uart_a.write_byte_count > writes_a);
  CHECK(uart_b.write_byte_count > writes_b);
}

/*!
 * @file
 * @brief Tests for the GEA2 ERD client adapter.
 *
 * Validates that the adapter correctly delegates GEA3-style calls to the
 * underlying GEA2 client, rejects subscription operations, and re-publishes
 * GEA2 activity events through its GEA3-typed on_activity event.
 */

extern "C" {
#include "gea2_erd_client_adapter.h"
#include "tiny_list.h"
}

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "double/tiny_gea2_erd_client_double.hpp"

TEST_GROUP(gea2_erd_client_adapter)
{
  enum {
    test_address = 0xC0,
    test_erd = 0x0001
  };

  gea2_erd_client_adapter_t adapter;
  tiny_gea2_erd_client_double_t gea2_client;
  tiny_event_subscription_t activity_subscription;

  static tiny_erd_t erd_of(const tiny_gea3_erd_client_on_activity_args_t* args)
  {
    switch(args->type) {
      case tiny_gea3_erd_client_activity_type_read_completed:
        return args->read_completed.erd;
      case tiny_gea3_erd_client_activity_type_read_failed:
        return args->read_failed.erd;
      case tiny_gea3_erd_client_activity_type_write_completed:
        return args->write_completed.erd;
      case tiny_gea3_erd_client_activity_type_write_failed:
        return args->write_failed.erd;
      default:
        return 0xFFFF;
    }
  }

  static void on_adapter_activity(void*, const void* _args)
  {
    auto args = static_cast<const tiny_gea3_erd_client_on_activity_args_t*>(_args);
    mock()
      .actualCall("on_activity")
      .withParameter("type", static_cast<unsigned>(args->type))
      .withParameter("address", args->address)
      .withParameter("erd", erd_of(args));
  }

  void given_a_listener_on_the_adapter_activity_event()
  {
    tiny_event_subscription_init(&activity_subscription, nullptr, on_adapter_activity);
    tiny_event_subscribe(tiny_gea3_erd_client_on_activity(&adapter.interface), &activity_subscription);
  }

  void should_publish_gea3_activity(tiny_gea3_erd_client_activity_type_t type)
  {
    mock()
      .expectOneCall("on_activity")
      .withParameter("type", static_cast<unsigned>(type))
      .withParameter("address", test_address)
      .withParameter("erd", test_erd);
  }

  void setup()
  {
    mock().strictOrder();
    tiny_gea2_erd_client_double_init(&gea2_client);
  }

  void when_the_adapter_is_initialized()
  {
    gea2_erd_client_adapter_init(&adapter, &gea2_client.interface);
  }

  void given_that_the_adapter_has_been_initialized()
  {
    mock().disable();
    when_the_adapter_is_initialized();
    mock().enable();
  }

  void should_request_gea2_read(uint8_t address, tiny_erd_t erd)
  {
    mock()
      .expectOneCall("read")
      .onObject(&gea2_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  void should_request_gea2_write(uint8_t address, tiny_erd_t erd,
                                  const void* data, uint8_t data_size)
  {
    mock()
      .expectOneCall("write")
      .onObject(&gea2_client)
      .withParameter("address", address)
      .withParameter("erd", erd)
      .withMemoryBufferParameter("data",
        reinterpret_cast<const unsigned char*>(data), data_size)
      .ignoreOtherParameters()
      .andReturnValue(true);
  }

  void trigger_gea2_read_completed(uint8_t address, tiny_erd_t erd,
                                    const void* data, uint8_t data_size)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_read_completed;
    args.address = address;
    args.read_completed.request_id = 1;
    args.read_completed.erd = erd;
    args.read_completed.data = data;
    args.read_completed.data_size = data_size;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }

  void trigger_gea2_read_failed(uint8_t address, tiny_erd_t erd)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_read_failed;
    args.address = address;
    args.read_failed.request_id = 1;
    args.read_failed.erd = erd;
    args.read_failed.reason = tiny_gea2_erd_client_read_failure_reason_retries_exhausted;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }

  void trigger_gea2_write_completed(uint8_t address, tiny_erd_t erd,
                                     const void* data, uint8_t data_size)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_write_completed;
    args.address = address;
    args.write_completed.request_id = 1;
    args.write_completed.erd = erd;
    args.write_completed.data = data;
    args.write_completed.data_size = data_size;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }

  void trigger_gea2_write_failed(uint8_t address, tiny_erd_t erd)
  {
    tiny_gea2_erd_client_on_activity_args_t args;
    args.type = tiny_gea2_erd_client_activity_type_write_failed;
    args.address = address;
    args.write_failed.request_id = 1;
    args.write_failed.erd = erd;
    args.write_failed.reason = tiny_gea2_erd_client_write_failure_reason_retries_exhausted;
    args.write_failed.data = nullptr;
    args.write_failed.data_size = 0;
    tiny_gea2_erd_client_double_trigger_activity_event(&gea2_client, &args);
  }
};

/* ------------------------------------------------------------------ */
/* Read delegation                                                      */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, read_delegates_to_gea2_client)
{
  given_that_the_adapter_has_been_initialized();
  should_request_gea2_read(test_address, test_erd);

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_read(
    &adapter.interface, &request_id, test_address, test_erd);

  CHECK_TRUE(result);
}

TEST(gea2_erd_client_adapter, read_returns_false_when_gea2_fails)
{
  given_that_the_adapter_has_been_initialized();

  mock()
    .expectOneCall("read")
    .onObject(&gea2_client)
    .ignoreOtherParameters()
    .andReturnValue(false);

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_read(
    &adapter.interface, &request_id, test_address, test_erd);

  CHECK_FALSE(result);
}

/* ------------------------------------------------------------------ */
/* Write delegation                                                     */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, write_delegates_to_gea2_client)
{
  given_that_the_adapter_has_been_initialized();
  uint8_t test_data = 0x42;
  should_request_gea2_write(test_address, test_erd, &test_data, sizeof(test_data));

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_write(
    &adapter.interface, &request_id, test_address, test_erd,
    &test_data, sizeof(test_data));

  CHECK_TRUE(result);
}

TEST(gea2_erd_client_adapter, write_returns_false_when_gea2_fails)
{
  given_that_the_adapter_has_been_initialized();
  uint8_t test_data = 0x42;

  mock()
    .expectOneCall("write")
    .onObject(&gea2_client)
    .ignoreOtherParameters()
    .andReturnValue(false);

  tiny_gea3_erd_client_request_id_t request_id = 0;
  bool result = tiny_gea3_erd_client_write(
    &adapter.interface, &request_id, test_address, test_erd,
    &test_data, sizeof(test_data));

  CHECK_FALSE(result);
}

/* ------------------------------------------------------------------ */
/* Subscription rejection                                               */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, subscribe_returns_false)
{
  given_that_the_adapter_has_been_initialized();
  bool result = tiny_gea3_erd_client_subscribe(&adapter.interface, test_address);
  CHECK_FALSE(result);
}

TEST(gea2_erd_client_adapter, retain_subscription_returns_false)
{
  given_that_the_adapter_has_been_initialized();
  bool result = tiny_gea3_erd_client_retain_subscription(&adapter.interface, test_address);
  CHECK_FALSE(result);
}

/* ------------------------------------------------------------------ */
/* Activity event passthrough                                           */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, on_activity_returns_event_interface)
{
  given_that_the_adapter_has_been_initialized();
  i_tiny_event_t* event = tiny_gea3_erd_client_on_activity(&adapter.interface);
  CHECK(event != nullptr);
}

TEST(gea2_erd_client_adapter, gea2_read_completed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  uint8_t data = 0x05;

  given_a_listener_on_the_adapter_activity_event();
  should_publish_gea3_activity(tiny_gea3_erd_client_activity_type_read_completed);

  trigger_gea2_read_completed(test_address, test_erd, &data, sizeof(data));
}

TEST(gea2_erd_client_adapter, gea2_read_failed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  given_a_listener_on_the_adapter_activity_event();
  should_publish_gea3_activity(tiny_gea3_erd_client_activity_type_read_failed);

  trigger_gea2_read_failed(test_address, test_erd);
}

TEST(gea2_erd_client_adapter, gea2_write_completed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  uint8_t data = 0x01;

  given_a_listener_on_the_adapter_activity_event();
  should_publish_gea3_activity(tiny_gea3_erd_client_activity_type_write_completed);

  trigger_gea2_write_completed(test_address, test_erd, &data, sizeof(data));
}

TEST(gea2_erd_client_adapter, gea2_write_failed_republished_as_gea3_event)
{
  given_that_the_adapter_has_been_initialized();

  given_a_listener_on_the_adapter_activity_event();
  should_publish_gea3_activity(tiny_gea3_erd_client_activity_type_write_failed);

  trigger_gea2_write_failed(test_address, test_erd);
}
/* ------------------------------------------------------------------ */
/* Destroy                                                              */
/* ------------------------------------------------------------------ */

TEST(gea2_erd_client_adapter, destroy_unsubscribes_from_gea2_activity)
{
  when_the_adapter_is_initialized();

  /* After init, the adapter subscribed to the GEA2 client's on_activity event. */
  CHECK_EQUAL(1u, tiny_list_count(&gea2_client.on_activity.subscribers));

  gea2_erd_client_adapter_destroy(&adapter, &gea2_client.interface);

  /* After destroy, the subscription must be removed. */
  CHECK_EQUAL(0u, tiny_list_count(&gea2_client.on_activity.subscribers));
}

TEST(gea2_erd_client_adapter, destroy_with_null_self_is_safe)
{
  gea2_erd_client_adapter_destroy(nullptr, &gea2_client.interface);
}

TEST(gea2_erd_client_adapter, destroy_with_null_gea2_client_is_safe)
{
  when_the_adapter_is_initialized();
  gea2_erd_client_adapter_destroy(&adapter, nullptr);
}

TEST(gea2_erd_client_adapter, destroy_without_init_is_safe)
{
  /* adapter is zero-initialized as a TEST_GROUP member; gea2_client is null. */
  gea2_erd_client_adapter_destroy(&adapter, &gea2_client.interface);
}

TEST(gea2_erd_client_adapter, double_destroy_is_safe)
{
  when_the_adapter_is_initialized();
  gea2_erd_client_adapter_destroy(&adapter, &gea2_client.interface);
  gea2_erd_client_adapter_destroy(&adapter, &gea2_client.interface);
}
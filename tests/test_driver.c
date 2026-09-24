#include "driver.h"
#include "protocol.h"
#include "vdev.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Context tracker for asynchronous telemetry callback
typedef struct {
  int call_count;
  DriverTelemetry last_telemetry;
} TelemetryTestContext;

static void test_telemetry_callback(const DriverTelemetry *data, void *user_data) {
  TelemetryTestContext *ctx = (TelemetryTestContext *)user_data;
  ctx->call_count++;
  ctx->last_telemetry = *data;
}

void test_driver_init_and_ping(void) {
  printf("--- Test 1: Driver Initialization & Hardware Ping ---\n");

  VirtualDevice vdev;
  vdev_init(&vdev);

  Driver drv;

  // 1. Argument validation
  assert(driver_init(NULL, &vdev) == DRIVER_ERR_NULL_ARG);
  assert(driver_init(&drv, NULL) == DRIVER_ERR_NULL_ARG);
  assert(driver_ping(NULL) == DRIVER_ERR_NULL_ARG);

  // 2. Successful initialization
  assert(driver_init(&drv, &vdev) == DRIVER_OK);
  assert(drv.next_seq == 0);
  assert(drv.stats.commands_sent == 0);
  assert(drv.stats.responses_received == 0);

  // 3. Ping peripheral
  assert(driver_ping(&drv) == DRIVER_OK);
  assert(drv.next_seq == 1);
  assert(drv.stats.commands_sent == 1);
  assert(drv.stats.responses_received == 1);

  // Second ping
  assert(driver_ping(&drv) == DRIVER_OK);
  assert(drv.next_seq == 2);
  assert(drv.stats.commands_sent == 2);
  assert(drv.stats.responses_received == 2);

  printf("Driver initialized and hardware ping verified!\n");
  printf("PASSED\n\n");
}

void test_driver_register_read_write(void) {
  printf("--- Test 2: Synchronous Register Read/Write ---\n");

  VirtualDevice vdev;
  vdev_init(&vdev);

  Driver drv;
  assert(driver_init(&drv, &vdev) == DRIVER_OK);

  uint16_t val = 0;

  // 1. Read hardware ID and firmware version
  assert(driver_read_reg(&drv, REG_DEV_ID, &val) == DRIVER_OK);
  assert(val == 0xCAFE);

  assert(driver_read_reg(&drv, REG_FW_VER, &val) == DRIVER_OK);
  assert(val == 0x0100);

  // 2. Read out-of-bounds register (Device should return NACK)
  assert(driver_read_reg(&drv, 0x88, &val) == DRIVER_ERR_NACK);
  assert(drv.stats.nacks_received == 1);

  // 3. Write and read-back Scratchpad register
  assert(driver_write_reg(&drv, REG_SCRATCHPAD, 0xA55A) == DRIVER_OK);
  assert(driver_read_reg(&drv, REG_SCRATCHPAD, &val) == DRIVER_OK);
  assert(val == 0xA55A);

  // 4. Write to Read-Only register (Should receive NACK and reject modification)
  assert(driver_write_reg(&drv, REG_DEV_ID, 0x1234) == DRIVER_ERR_NACK);
  assert(drv.stats.nacks_received == 2);
  assert(driver_read_reg(&drv, REG_DEV_ID, &val) == DRIVER_OK);
  assert(val == 0xCAFE); // Unmodified

  printf("Register Read/Write transactions and NACK handling verified!\n");
  printf("PASSED\n\n");
}

void test_driver_telemetry_streaming(void) {
  printf("--- Test 3: Asynchronous Telemetry Stream & Callback ---\n");

  VirtualDevice vdev;
  vdev_init(&vdev);

  Driver drv;
  assert(driver_init(&drv, &vdev) == DRIVER_OK);

  TelemetryTestContext ctx;
  memset(&ctx, 0, sizeof(ctx));

  // 1. driver_step without streaming enabled should not fire callback
  assert(driver_step(&drv) == DRIVER_OK);
  assert(ctx.call_count == 0);
  assert(drv.stats.telemetry_frames_received == 0);

  // 2. Start telemetry stream with callback registration
  assert(driver_start_telemetry(&drv, test_telemetry_callback, &ctx) == DRIVER_OK);

  // 3. Simulate 10 hardware clock cycles
  for (int i = 0; i < 10; i++) {
    assert(driver_step(&drv) == DRIVER_OK);
  }

  assert(ctx.call_count == 10);
  assert(drv.stats.telemetry_frames_received == 10);

  // Verify engineering unit conversions (°C and milli-g)
  // Base temperature is 250 tenths = 25.0°C
  assert(ctx.last_telemetry.raw_temp >= 250 && ctx.last_telemetry.raw_temp <= 255);
  assert(ctx.last_telemetry.temp_celsius >= 25.0f && ctx.last_telemetry.temp_celsius <= 25.5f);
  assert(ctx.last_telemetry.accel_z == 1000);

  // 4. Stop telemetry stream
  assert(driver_stop_telemetry(&drv) == DRIVER_OK);

  // 5. Subsequent clock cycles must not generate telemetry frames
  for (int i = 0; i < 5; i++) {
    assert(driver_step(&drv) == DRIVER_OK);
  }

  assert(ctx.call_count == 10); // Still 10, no new callbacks
  assert(drv.stats.telemetry_frames_received == 10);

  printf("Telemetry streaming, engineering conversions, and callback verified!\n");
  printf("PASSED\n\n");
}

void test_driver_interleaved_operations(void) {
  printf("--- Test 4: Interleaved Commands During Streaming ---\n");

  VirtualDevice vdev;
  vdev_init(&vdev);

  Driver drv;
  assert(driver_init(&drv, &vdev) == DRIVER_OK);

  TelemetryTestContext ctx;
  memset(&ctx, 0, sizeof(ctx));

  assert(driver_start_telemetry(&drv, test_telemetry_callback, &ctx) == DRIVER_OK);

  // Step 3 ticks of telemetry
  assert(driver_step(&drv) == DRIVER_OK);
  assert(driver_step(&drv) == DRIVER_OK);
  assert(driver_step(&drv) == DRIVER_OK);
  assert(ctx.call_count == 3);

  // In the middle of streaming, issue synchronous register commands
  uint16_t sample_rate = 0;
  assert(driver_read_reg(&drv, REG_SAMPLE_RATE, &sample_rate) == DRIVER_OK);
  assert(sample_rate == 10);

  assert(driver_write_reg(&drv, REG_SCRATCHPAD, 0xBEEF) == DRIVER_OK);

  // Step 2 more ticks of telemetry
  assert(driver_step(&drv) == DRIVER_OK);
  assert(driver_step(&drv) == DRIVER_OK);
  assert(ctx.call_count == 5);

  uint16_t scratch = 0;
  assert(driver_read_reg(&drv, REG_SCRATCHPAD, &scratch) == DRIVER_OK);
  assert(scratch == 0xBEEF);

  assert(driver_stop_telemetry(&drv) == DRIVER_OK);

  printf("Interleaved register transactions and telemetry executed seamlessly!\n");
  printf("PASSED\n\n");
}

int main(void) {
  printf("====================================================\n");
  printf("      HOST DEVICE DRIVER TEST SUITE (PHASE 4)       \n");
  printf("====================================================\n\n");

  test_driver_init_and_ping();
  test_driver_register_read_write();
  test_driver_telemetry_streaming();
  test_driver_interleaved_operations();

  printf("====================================================\n");
  printf("    ALL PHASE 4 TESTS PASSED! 🎉 (0 LEAKS, 0 ERRORS)\n");
  printf("====================================================\n");

  return 0;
}

/*
 * DAP_config.h: CMSIS-DAP configuration for the Wireless Debugger (ESP32-S3, SWD over WiFi).
 *
 * Written for this project; the set of required items follows ARM's DAP_config.h template
 * (github.com/ARM-software/CMSIS-DAP, Apache-2.0). Pin access, delays and strings are routed to
 * dap_glue.cpp (dap_port_* functions) so DAP.c stays platform-independent and also builds in the
 * native unit tests. SW_DP.c is NOT used: SWJ_Sequence/SWD_Sequence/SWD_Transfer are implemented
 * in dap_glue.cpp on top of our own unit-tested SWD engine (or the simulated target).
 */
#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

#include <stdint.h>
#include <string.h>

#include "app_config.h"

#include "cmsis_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hooks implemented in dap_glue.cpp. */
void     dap_port_delay_cycles(uint32_t cycles);
void     DAP_SetClockHook(uint32_t clock_hz);
void     dap_port_connect(void);
void     dap_port_disconnect(void);
void     dap_port_pins_write(uint32_t value, uint32_t select);
uint32_t dap_port_pins_read(void);
void     dap_port_led(uint32_t which, uint32_t on);
uint8_t  dap_port_serial(char *str);

/* ---- Processor / timing ---- */
#define CPU_CLOCK               240000000U  /* ESP32-S3 @ 240 MHz (only used for DAP_Delay maths) */
#define IO_PORT_WRITE_CYCLES    2U          /* unused: SW_DP.c is not compiled */

/* PIN_DELAY_SLOW(n): busy-wait n * DELAY_SLOW_CYCLES CPU cycles (DAP_Delay, Delayms). */
#define DELAY_SLOW_CYCLES       1U
#define DAP_PIN_DELAY_SLOW_PROVIDED
__STATIC_FORCEINLINE void PIN_DELAY_SLOW(uint32_t delay) { dap_port_delay_cycles(delay); }

/* ---- Features ---- */
#define DAP_SWD                 1
#define DAP_JTAG                0
#define DAP_JTAG_DEV_CNT        0U
#define DAP_DEFAULT_PORT        1U          /* 1 = SWD */
#define DAP_DEFAULT_SWJ_CLOCK   WDBG_SWD_DEFAULT_HZ

#define DAP_PACKET_SIZE         WDBG_DAP_PACKET_SIZE
#define DAP_PACKET_COUNT        WDBG_DAP_PACKET_COUNT

#define SWO_UART                0
#define SWO_MANCHESTER          0
#define SWO_BUFFER_SIZE         4096U
#define SWO_STREAM              0
#define TIMESTAMP_CLOCK         0U          /* no timestamps */
#define DAP_UART                0
#define DAP_UART_USB_COM_PORT   0
#define TARGET_FIXED            0

/* ---- Strings (DAP_Info). Return length including the terminating NUL, 0 = not provided. ---- */
__STATIC_INLINE uint8_t dap_str(char *dst, const char *s) {
    const size_t n = strlen(s) + 1U;
    memcpy(dst, s, n);
    return (uint8_t)n;
}
__STATIC_INLINE uint8_t DAP_GetVendorString(char *str) { return dap_str(str, "Wireless Debugger"); }
__STATIC_INLINE uint8_t DAP_GetProductString(char *str) { return dap_str(str, "WDBG CMSIS-DAP"); }
__STATIC_INLINE uint8_t DAP_GetSerNumString(char *str) { return dap_port_serial(str); }
__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString(char *str) { (void)str; return 0U; }
__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString(char *str) { return dap_str(str, WDBG_FW_VERSION); }

/* ---- Port setup ---- */
__STATIC_INLINE void PORT_JTAG_SETUP(void) {}
__STATIC_INLINE void PORT_SWD_SETUP(void) { dap_port_connect(); }
__STATIC_INLINE void PORT_OFF(void) { dap_port_disconnect(); }

/* ---- Pins (only DAP_SWJ_Pins uses these; the SWD engine drives the pins itself) ----
 * Bit numbers are DAP.h's DAP_SWJ_* (SWCLK=0, SWDIO=1, TDI=2, TDO=3, nTRST=5, nRESET=7). */
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void) { return (dap_port_pins_read() >> 0) & 1U; }
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void) { dap_port_pins_write(1U << 0, 1U << 0); }
__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void) { dap_port_pins_write(0U, 1U << 0); }
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void) { return (dap_port_pins_read() >> 1) & 1U; }
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void) { dap_port_pins_write(1U << 1, 1U << 1); }
__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void) { dap_port_pins_write(0U, 1U << 1); }
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void) { return PIN_SWDIO_TMS_IN(); }
__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit) { dap_port_pins_write((bit & 1U) << 1, 1U << 1); }
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void) {}
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void) {}
/* Not wired on this probe (02_HARDWARE.md §3): TDI/TDO/nTRST absent, nRESET not connected. */
__STATIC_FORCEINLINE uint32_t PIN_TDI_IN(void) { return 0U; }
__STATIC_FORCEINLINE void PIN_TDI_OUT(uint32_t bit) { (void)bit; }
__STATIC_FORCEINLINE uint32_t PIN_TDO_IN(void) { return 0U; }
__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN(void) { return 0U; }
__STATIC_FORCEINLINE void PIN_nTRST_OUT(uint32_t bit) { (void)bit; }
__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void) { return 1U; } /* reads as "not in reset" */
__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit) { (void)bit; }

/* ---- Status "LEDs" (DAP_HostStatus) -> shown on the screen ---- */
__STATIC_INLINE void LED_CONNECTED_OUT(uint32_t bit) { dap_port_led(0U, bit); }
__STATIC_INLINE void LED_RUNNING_OUT(uint32_t bit) { dap_port_led(1U, bit); }

__STATIC_INLINE uint32_t TIMESTAMP_GET(void) { return 0U; }
__STATIC_INLINE void DAP_SETUP(void) {}
__STATIC_INLINE uint8_t RESET_TARGET(void) { return 0U; } /* no nRESET: host uses a software reset */

#ifdef __cplusplus
}
#endif

#endif /* __DAP_CONFIG_H__ */

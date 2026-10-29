/*
 * cmsis_compiler.h: minimal stand-in for ARM's CMSIS-Core header of the same name.
 *
 * DAP.h includes "cmsis_compiler.h"; on Xtensa (ESP32) and x86 (native tests) there is no CMSIS
 * core, so only the macros DAP.c / DAP.h / DAP_config.h actually use are defined here (GCC).
 */
#ifndef WDBG_CMSIS_COMPILER_H
#define WDBG_CMSIS_COMPILER_H

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif
#ifndef __ASM
#define __ASM __asm__
#endif
#ifndef __NOP
#define __NOP() __asm__ volatile("nop")
#endif

#endif /* WDBG_CMSIS_COMPILER_H */

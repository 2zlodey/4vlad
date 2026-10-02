#include "perf_probe_hal.h"

#include <stdarg.h>
#include <stdio.h>

#include "xil_exception.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xiltimer.h"

#include "xparameters.h"
#include "main_rf.h"
#include "rtos_uart_log.h"
/*
 * Define these in your build flags or project config if possible.
 *
 * Example:
 *   -DPERF_FPGA_CLK_CNT_ADDR=(XPAR_DEMOD_0_BASEADDR+CLK_CNT)
 *   -DPERF_FPGA_COUNTER_HZ=DEMOD_AXI_CLK
 */
#ifndef PERF_FPGA_CLK_CNT_ADDR
#define PERF_FPGA_CLK_CNT_ADDR (XPAR_DEMOD_0_BASEADDR + CLK_CNT)
#endif

#ifndef PERF_FPGA_COUNTER_HZ
#define PERF_FPGA_COUNTER_HZ DEMOD_AXI_CLK
#endif

#define PERF_ZYNQ_TIME_SRC_FPGA_REG 0u
#define PERF_ZYNQ_TIME_SRC_XTIME    1u

/*
 * Select perf_hal_now_us backend:
 *  - PERF_ZYNQ_TIME_SRC_FPGA_REG: derive microseconds from FPGA counter register
 *  - PERF_ZYNQ_TIME_SRC_XTIME:    use XTime_GetTime()/COUNTS_PER_SECOND
 */
#ifndef PERF_ZYNQ_TIME_SOURCE
#define PERF_ZYNQ_TIME_SOURCE PERF_ZYNQ_TIME_SRC_FPGA_REG
#endif

#ifndef PERF_TEMP_DEBUG
#define PERF_TEMP_DEBUG 0
#endif

void perf_hal_init(void)
{
#if PERF_TEMP_DEBUG
#if (PERF_ZYNQ_TIME_SOURCE == PERF_ZYNQ_TIME_SRC_FPGA_REG)
    printf("[PERF_DBG] hal_init src=FPGA_REG addr=0x%08lx hz=%lu\r\n",
               (unsigned long)PERF_FPGA_CLK_CNT_ADDR,
               (unsigned long)PERF_FPGA_COUNTER_HZ);
#else
    printf("[PERF_DBG] hal_init src=XTIME cps=%lu\r\n",
               (unsigned long)COUNTS_PER_SECOND);
#endif
#endif
}

uint64_t perf_hal_now_us(void)
{
#if (PERF_ZYNQ_TIME_SOURCE == PERF_ZYNQ_TIME_SRC_FPGA_REG)
    const uint32_t hz = (uint32_t)PERF_FPGA_COUNTER_HZ;
    const uint32_t cnt = Xil_In32(PERF_FPGA_CLK_CNT_ADDR);

    return (hz != 0u) ? (uint64_t)(((uint64_t)cnt * 1000000ULL) / hz) : 0u;
#else
    XTime now = 0;
    XTime_GetTime(&now);

    return (uint64_t)(((uint64_t)now * 1000000ULL) / (uint64_t)COUNTS_PER_SECOND);
#endif
}

uint64_t perf_hal_counter(void) { return (uint64_t)Xil_In32(PERF_FPGA_CLK_CNT_ADDR); }

uint32_t perf_hal_critical_enter(void)
{
    /*
     * Simple bare-metal version.
     *
     * More correct version should save/restore CPSR IRQ state.
     * But this is often enough if probes are only used from main context.
     */
    // Xil_ExceptionDisable();
    return 0u;
}

void perf_hal_critical_exit(uint32_t state)
{
    (void)state;
    // Xil_ExceptionEnable();
}

void perf_hal_printf(const char *fmt, ...)
{
    // LOG_PRINTF(fmt, ...);
    char buf[256];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    uart_log_write_nonblocking(buf, (int)strlen(buf));
    // printf("%s", buf);
}

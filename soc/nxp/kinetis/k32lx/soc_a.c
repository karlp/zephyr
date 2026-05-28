/*
 * Copy of ke1xz soc.c.  This reallllly needs to be pulled up to common code!
 * But, how do that but preserve the ordering of clock init?
 * But, _MY_ goals are to get the rest of zephyr running.   maybe nxp can clean up nxp support?
 * 
 * Changes are:
 * - dropping all the "div2" clocks and replacing with "div3" as on k32l2a
 * - dropping the LPFLL config and init, as k32lx doesn't have an LPFLL
 * - adding extra clock enables for additional periphs
 * Karl Palsson <karl.palsson@marel.com> May 2026
 * 
 * 
 * Copyright 2024-2025 NXP
 * Copyright (c) 2019-2021 Vestas Wind Systems A/S
 *
 * Based on NXP k6x soc.c, which is:
 * Copyright (c) 2014-2015 Wind River Systems, Inc.
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <fsl_common.h>
#include <fsl_clock.h>
// ?#include <fsl_smc.h>
#include <cmsis_core.h>

#define ASSERT_WITHIN_RANGE(val, min, max, str) BUILD_ASSERT(val >= min && val <= max, str)

#define ASSERT_ASYNC_CLK_DIV_VALID(val, str)                                                       \
	BUILD_ASSERT(val == 0 || val == 1 || val == 2 || val == 4 || val == 8 || val == 16 ||      \
			     val == 2 || val == 64,                                                \
		     str)

#define TO_SYS_CLK_DIV(val) _DO_CONCAT(kSCG_SysClkDivBy, val)

#define kSCG_AsyncClkDivBy0   kSCG_AsyncClkDisable
#define TO_ASYNC_CLK_DIV(val) _DO_CONCAT(kSCG_AsyncClkDivBy, val)

#define SCG_CLOCK_NODE(name) DT_CHILD(DT_INST(0, nxp_kinetis_scg), name)
#define SCG_CLOCK_DIV(name)  DT_PROP(SCG_CLOCK_NODE(name), clock_div)

/* System Clock configuration */
ASSERT_WITHIN_RANGE(SCG_CLOCK_DIV(bus_clk), 2, 8, "Invalid SCG bus clock divider value");
ASSERT_WITHIN_RANGE(SCG_CLOCK_DIV(core_clk), 1, 16, "Invalid SCG core clock divider value");

static const scg_sys_clk_config_t scg_sys_clk_config = {
	.divSlow = TO_SYS_CLK_DIV(SCG_CLOCK_DIV(bus_clk)),
	.divCore = TO_SYS_CLK_DIV(SCG_CLOCK_DIV(core_clk)),
#if DT_SAME_NODE(DT_CLOCKS_CTLR(SCG_CLOCK_NODE(core_clk)), SCG_CLOCK_NODE(sirc_clk))
	.src = kSCG_SysClkSrcSirc,
#elif DT_SAME_NODE(DT_CLOCKS_CTLR(SCG_CLOCK_NODE(core_clk)), SCG_CLOCK_NODE(firc_clk))
	.src = kSCG_SysClkSrcFirc,
#elif DT_SAME_NODE(DT_CLOCKS_CTLR(SCG_CLOCK_NODE(core_clk)), SCG_CLOCK_NODE(sosc_clk))
	.src = kSCG_SysClkSrcSysOsc,
#else
#error Invalid SCG core clock selected
#endif
};

/* Slow Internal Reference Clock (SIRC) configuration */
ASSERT_ASYNC_CLK_DIV_VALID(SCG_CLOCK_DIV(sircdiv3_clk), "Invalid SCG SIRC divider 3 value");
static const scg_sirc_config_t scg_sirc_config = {
	.enableMode = kSCG_SircEnable,
	.div3 = TO_ASYNC_CLK_DIV(SCG_CLOCK_DIV(sircdiv3_clk)),
#if MHZ(2) == DT_PROP(SCG_CLOCK_NODE(sirc_clk), clock_frequency)
	.range = kSCG_SircRangeLow
#elif MHZ(8) == DT_PROP(SCG_CLOCK_NODE(sirc_clk), clock_frequency)
	.range = kSCG_SircRangeHigh
#else
#error Invalid SCG SIRC clock frequency
#endif
};

/* Fast Internal Reference Clock (FIRC) configuration */
ASSERT_ASYNC_CLK_DIV_VALID(SCG_CLOCK_DIV(fircdiv3_clk), "Invalid SCG FIRC divider 3 value");
static const scg_firc_config_t scg_firc_config = {
	.enableMode = kSCG_FircEnable,
	.div3 = TO_ASYNC_CLK_DIV(SCG_CLOCK_DIV(fircdiv3_clk)),
#if MHZ(48) == DT_PROP(SCG_CLOCK_NODE(firc_clk), clock_frequency)
	.range = kSCG_FircRange48M,
#elif MHZ(52) == DT_PROP(SCG_CLOCK_NODE(firc_clk), clock_frequency)
	.range = kSCG_FircRange52M,
#elif MHZ(56) == DT_PROP(SCG_CLOCK_NODE(firc_clk), clock_frequency)
	.range = kSCG_FircRange56M,
#elif MHZ(60) == DT_PROP(SCG_CLOCK_NODE(firc_clk), clock_frequency)
	.range = kSCG_FircRange60M,
#else
#error Invalid SCG FIRC clock frequency
#endif
	.trimConfig = NULL};

#if DT_NODE_HAS_STATUS_OKAY(SCG_CLOCK_NODE(sosc_clk))
/* System Oscillator (SOSC) configuration */
ASSERT_ASYNC_CLK_DIV_VALID(SCG_CLOCK_DIV(soscdiv3_clk), "Invalid SCG SOSC divider 3 value");
static const scg_sosc_config_t scg_sosc_config = {
	.freq = DT_PROP(SCG_CLOCK_NODE(sosc_clk), clock_frequency),
	.monitorMode = kSCG_SysOscMonitorDisable,
	.enableMode = kSCG_SysOscEnable | kSCG_SysOscEnableInLowPower,
	.div3 = TO_ASYNC_CLK_DIV(SCG_CLOCK_DIV(soscdiv3_clk)),
	.workMode = DT_PROP(DT_INST(0, nxp_kinetis_scg), sosc_mode)};
#endif


static void CLOCK_CONFIG_FircSafeConfig(const scg_firc_config_t *fircConfig)
{
	scg_sys_clk_config_t curConfig;
	const scg_sirc_config_t scgSircConfig = {.enableMode = kSCG_SircEnable,
						 .div3 = kSCG_AsyncClkDivBy2,
						 .range = kSCG_SircRangeHigh};
	scg_sys_clk_config_t sysClkSafeConfigSource = {
		.divSlow = kSCG_SysClkDivBy4, /* Slow clock divider */
		.divCore = kSCG_SysClkDivBy1, /* Core clock divider */
		.src = kSCG_SysClkSrcSirc     /* System clock source */
	};
	/* Init Sirc. */
	CLOCK_InitSirc(&scgSircConfig);
	/* Change to use SIRC as system clock source to prepare to change FIRCCFG register. */
	CLOCK_SetRunModeSysClkConfig(&sysClkSafeConfigSource);
	/* Wait for clock source switch finished. */
	do {
		CLOCK_GetCurSysClkConfig(&curConfig);
	} while (curConfig.src != sysClkSafeConfigSource.src);

	/* Init Firc. */
	CLOCK_InitFirc(fircConfig);
	/* Change back to use FIRC as system clock source in order to configure SIRC if needed. */
	sysClkSafeConfigSource.src = kSCG_SysClkSrcFirc;
	CLOCK_SetRunModeSysClkConfig(&sysClkSafeConfigSource);
	/* Wait for clock source switch finished. */
	do {
		CLOCK_GetCurSysClkConfig(&curConfig);
	} while (curConfig.src != sysClkSafeConfigSource.src);
}

#define CLOCK_SETUP_ENTRY(node_label, clock_type) \
	IF_ENABLED(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(node_label)), ( \
		CLOCK_SetIpSrc(clock_type, DT_CLOCKS_CELL(DT_NODELABEL(node_label), ip_source)); \
	))

#define CLOCK_ENABLE_ENTRY(node_label, clock_type) \
	IF_ENABLED(DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(node_label)), ( \
		CLOCK_EnableClock(clock_type); \
	))

__weak void clk_init(void)
{
	scg_sys_clk_config_t current = {0};

#if DT_NODE_HAS_STATUS_OKAY(SCG_CLOCK_NODE(sosc_clk))
	/* Init SOSC according to board configuration. */
	CLOCK_InitSysOsc(&scg_sosc_config);
	CLOCK_SetXtal0Freq(scg_sosc_config.freq);
#endif

	CLOCK_CONFIG_FircSafeConfig(&scg_firc_config);
	CLOCK_InitSirc(&scg_sirc_config);
	CLOCK_SetRunModeSysClkConfig(&scg_sys_clk_config);
	do {
		CLOCK_GetCurSysClkConfig(&current);
	} while (current.src != scg_sys_clk_config.src);

	/* Clock configuration table */
	CLOCK_SETUP_ENTRY(lpuart0, kCLOCK_Lpuart0)
	CLOCK_SETUP_ENTRY(lpuart1, kCLOCK_Lpuart1)
	CLOCK_SETUP_ENTRY(lpuart2, kCLOCK_Lpuart2)
	CLOCK_SETUP_ENTRY(lpi2c0, kCLOCK_Lpi2c0)
	CLOCK_SETUP_ENTRY(lpi2c1, kCLOCK_Lpi2c1)
	CLOCK_SETUP_ENTRY(lpi2c2, kCLOCK_Lpi2c2)
	CLOCK_SETUP_ENTRY(flexio, kCLOCK_Flexio0)
	CLOCK_SETUP_ENTRY(lpspi0, kCLOCK_Lpspi0)
	CLOCK_SETUP_ENTRY(lpspi1, kCLOCK_Lpspi1)
	CLOCK_SETUP_ENTRY(lpspi2, kCLOCK_Lpspi2)
	CLOCK_SETUP_ENTRY(adc0, kCLOCK_Adc0)
	CLOCK_SETUP_ENTRY(lpit0, kCLOCK_Lpit0)
	CLOCK_SETUP_ENTRY(lptmr0, kCLOCK_Lptmr0)
	CLOCK_SETUP_ENTRY(tpm0, kCLOCK_Tpm0)
	CLOCK_SETUP_ENTRY(tpm1, kCLOCK_Tpm1)
	CLOCK_SETUP_ENTRY(tpm2, kCLOCK_Tpm2)
	
}

void soc_early_init_hook(void)

{
	/* Initialize system clocks and PLL */
	clk_init();
#ifdef CONFIG_PM
	SMC_SetPowerModeProtection(SMC, kSMC_AllowPowerModeAll);
#endif /* CONFIG_PM */
}

#ifdef CONFIG_SOC_RESET_HOOK

void soc_reset_hook(void)
{
	/* SystemInit is provided by the NXP SDK */
	SystemInit();
}

#endif /* CONFIG_SOC_RESET_HOOK */

/*
 * Copyright (C) 2016 Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * Based on Allwinner code.
 * Copyright 2007-2012 (C) Allwinner Technology Co., Ltd.
 *
 * SPDX-License-Identifier:	GPL-2.0
 */
#include <config.h>
#include <common.h>

#include <asm/atomic.h>
#include <asm/arch/clock.h>
#include <asm/arch/dram.h>
#include <asm/armv7.h>
#include <asm/io.h>
#include <asm/psci.h>
#include <asm/secure.h>
#include <asm/system.h>

#include <linux/bitops.h>

__weak void __mdelay(u32 ms) {};

#if defined(CONFIG_MACH_SUN5I)
#define NR_CPUS		1
#elif defined(CONFIG_MACH_SUN7I)
#define NR_CPUS		2
#endif

/*
 * The PSCI suspend function switch cpuclk to another source and disable
 * pll1. As this function is called per-CPU, it should only do this when
 * all the CPUs are in idle state.
 *
 * The 'cnt' variable keeps track of the number of CPU which are in the idle
 * state. The last one setup cpuclk for idle.
 *
 * The 'clk_state' varibale holds the cpu clk state (idle or normal).
 */
atomic_t __secure_data cnt, clk_state;

#define CLK_NORMAL	0
#define CLK_IDLE	1

static void __secure sunxi_clock_enter_idle(struct sunxi_ccm_reg *ccm)
{
	/* switch cpuclk to osc24m */
	clrsetbits_le32(&ccm->cpu_ahb_apb0_cfg, 0x3 << CPU_CLK_SRC_SHIFT,
			CPU_CLK_SRC_OSC24M << CPU_CLK_SRC_SHIFT);

	/* disable pll1 */
	clrbits_le32(&ccm->pll1_cfg, CCM_PLL1_CTRL_EN);

#ifndef CONFIG_MACH_SUN7I
	/* switch cpuclk to losc */
	clrbits_le32(&ccm->cpu_ahb_apb0_cfg, 0x3 << CPU_CLK_SRC_SHIFT);
#endif

	/* disable ldo */
	clrbits_le32(&ccm->osc24m_cfg, OSC24M_LDO_EN);
}

static void __secure sunxi_clock_leave_idle(struct sunxi_ccm_reg *ccm)
{
	/* enable ldo */
	setbits_le32(&ccm->osc24m_cfg, OSC24M_LDO_EN);

#ifndef CONFIG_MACH_SUN7I
	/* switch cpuclk to osc24m */
	clrsetbits_le32(&ccm->cpu_ahb_apb0_cfg, 0x3 << CPU_CLK_SRC_SHIFT,
			CPU_CLK_SRC_OSC24M << CPU_CLK_SRC_SHIFT);
#endif

	/* enable pll1 */
	setbits_le32(&ccm->pll1_cfg, CCM_PLL1_CTRL_EN);

	/* switch cpuclk to pll1 */
	clrsetbits_le32(&ccm->cpu_ahb_apb0_cfg, 0x3 << CPU_CLK_SRC_SHIFT,
			CPU_CLK_SRC_PLL1 << CPU_CLK_SRC_SHIFT);
}

void __secure psci_cpu_suspend(void)
{
	struct sunxi_ccm_reg *ccm = (struct sunxi_ccm_reg *)SUNXI_CCM_BASE;

	if (atomic_inc_return(&cnt) == NR_CPUS) {
		/* wait for any sunxi_clock_leave_idle() to finish */
		while (atomic_read(&clk_state) != CLK_NORMAL)
			__mdelay(1);

		sunxi_clock_enter_idle(ccm);
		atomic_set(&clk_state, CLK_IDLE);
	}

	/* idle */
	DSB;
	wfi();

	if (atomic_dec_return(&cnt) == NR_CPUS - 1) {
		/* wait for any sunxi_clock_enter_idle() to finish */
		while (atomic_read(&clk_state) != CLK_IDLE)
			__mdelay(1);

		sunxi_clock_leave_idle(ccm);
		atomic_set(&clk_state, CLK_NORMAL);
	}
}

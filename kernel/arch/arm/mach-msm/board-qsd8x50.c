/* Copyright (c) 2008-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/android_pmem.h>
#include <linux/bootmem.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/mfd/tps65023.h>
#include <linux/bma150.h>
#include <linux/power_supply.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/wakelock.h>
#include <linux/yas529.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/io.h>
#include <asm/setup.h>

#include <asm/mach/mmc.h>
#include <mach/vreg.h>
#include <mach/mpp.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/sirc.h>
#include <mach/dma.h>
#include <mach/rpc_hsusb.h>
#include <mach/rpc_pmapp.h>
#include <mach/msm_hsusb.h>
#include <mach/msm_hsusb_hw.h>
#include <mach/msm_serial_hs.h>
#include <mach/msm_touchpad.h>
#include <mach/msm_i2ckbd.h>
#include <mach/pmic.h>
#include <mach/camera.h>
#include <mach/memory.h>
#include <mach/msm_spi.h>
#include <mach/s1r72v05.h>
#include <mach/msm_tsif.h>
#include <mach/msm_battery.h>

#if defined(CONFIG_ACER_HEADSET_BUTT)
#include <mach/acer_headset_butt.h>
#endif

#include "devices.h"
#include "timer.h"
#include "socinfo.h"
#include "msm-keypad-devices.h"
#include "pm.h"
#include "proc_comm.h"
#include <linux/msm_kgsl.h>
#include <linux/usb/android.h>


#if defined(CONFIG_MOUSE_MSM_TOUCHPAD)
#define TOUCHPAD_SUSPEND 	34
#define TOUCHPAD_IRQ 		38
#endif //defined(CONFIG_MOUSE_MSM_TOUCHPAD)

#define MSM_PMEM_SF_SIZE	0x1700000

#define SMEM_SPINLOCK_I2C	"S:6"

#define MSM_PMEM_ADSP_SIZE	0xFFF000

#ifdef CONFIG_FB_MSM_DOUBLE_BUFFER
#define MSM_FB_SIZE       0x177000
#endif
#ifdef CONFIG_FB_MSM_TRIPLE_BUFFER
#define MSM_FB_SIZE       0x2EE000
#endif
#ifdef CONFIG_FB_MSM_QUADRUPLE_BUFFER
#define MSM_FB_SIZE       0x465000
#endif

#define MSM_AUDIO_SIZE		0x80000

#ifdef CONFIG_MSM_SOC_REV_A
#define MSM_SMI_BASE		0xE0000000
#else
#define MSM_SMI_BASE		0x00000000
#endif

#define MSM_SHARED_RAM_PHYS	(MSM_SMI_BASE + 0x00100000)

#define MSM_PMEM_SMI_BASE	(MSM_SMI_BASE + 0x02B00000)
#define MSM_PMEM_SMI_SIZE	0x01500000

#define MSM_FB_BASE		MSM_PMEM_SMI_BASE
#define MSM_PMEM_SMIPOOL_BASE	(MSM_FB_BASE + MSM_FB_SIZE)
#define MSM_PMEM_SMIPOOL_SIZE	(MSM_PMEM_SMI_SIZE - MSM_FB_SIZE)

#define PMEM_KERNEL_EBI1_SIZE	0x28000

#define PMIC_VREG_WLAN_LEVEL	2600
#define PMIC_VREG_GP6_LEVEL	2900

#define FPGA_SDCC_STATUS	0x70000280

static DEFINE_MUTEX(wifibtmutex);

#if defined(CONFIG_MMC_WIFI) || defined(CONFIG_MMC_WIFI_MODULE)
#define WL_PWR_EN 109
#define WL_RST 147

static int wifi_status_register(void (*callback)(int card_present, void *dev_id), void *dev_id);
int wifi_set_carddetect(int val);
#endif

#ifdef CONFIG_SMC91X
static struct resource smc91x_resources[] = {
	[0] = {
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.flags  = IORESOURCE_IRQ,
	},
};
#endif

static struct usb_mass_storage_platform_data mass_storage_pdata = {
        .nluns = 1,
        .vendor = "ACER",
        .product = "Mass Storage",
        .release = 0x0100,

};

static struct platform_device usb_mass_storage_device = {
        .name = "usb_mass_storage",
        .id = -1,
        .dev = {
                .platform_data = &mass_storage_pdata,
                },
};

#ifdef CONFIG_SMC91X
static struct platform_device smc91x_device = {
	.name           = "smc91x",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(smc91x_resources),
	.resource       = smc91x_resources,
};
#endif

#ifdef CONFIG_BLK_DEV_IDE_S1R72V05
#define S1R72V05_CS_GPIO 152
#define S1R72V05_IRQ_GPIO 148

static int qsd8x50_init_s1r72v05(void)
{
	int rc;
	u8 cs_gpio = S1R72V05_CS_GPIO;
	u8 irq_gpio = S1R72V05_IRQ_GPIO;

	rc = gpio_request(cs_gpio, "ide_s1r72v05_cs");
	if (rc) {
		pr_err("Failed to request GPIO pin %d (rc=%d)\n",
		       cs_gpio, rc);
		goto err;
	}

	rc = gpio_request(irq_gpio, "ide_s1r72v05_irq");
	if (rc) {
		pr_err("Failed to request GPIO pin %d (rc=%d)\n",
		       irq_gpio, rc);
		goto err;
	}
	if (gpio_tlmm_config(GPIO_CFG(cs_gpio,
				      2, GPIO_OUTPUT, GPIO_NO_PULL,
				      GPIO_2MA),
			     GPIO_ENABLE)) {
		printk(KERN_ALERT
		       "s1r72v05: Could not configure CS gpio\n");
		goto err;
	}

	if (gpio_tlmm_config(GPIO_CFG(irq_gpio,
				      0, GPIO_INPUT, GPIO_NO_PULL,
				      GPIO_2MA),
			     GPIO_ENABLE)) {
		printk(KERN_ALERT
		       "s1r72v05: Could not configure IRQ gpio\n");
		goto err;
	}

	if (gpio_configure(irq_gpio, IRQF_TRIGGER_FALLING)) {
		printk(KERN_ALERT
		       "s1r72v05: Could not set IRQ polarity\n");
		goto err;
	}
	return 0;

err:
	gpio_free(cs_gpio);
	gpio_free(irq_gpio);
	return -ENODEV;
}

static struct resource s1r72v05_resources[] = {
	[0] = {
		.start = 0x88000000,
		.end = 0x88000000 + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = MSM_GPIO_TO_INT(S1R72V05_IRQ_GPIO),
		.end = S1R72V05_IRQ_GPIO,
		.flags = IORESOURCE_IRQ,
	},
};

static struct s1r72v05_platform_data s1r72v05_data = {
	.gpio_setup = qsd8x50_init_s1r72v05,
};

static struct platform_device s1r72v05_device = {
	.name           = "s1r72v05",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(s1r72v05_resources),
	.resource       = s1r72v05_resources,
	.dev            = {
		.platform_data          = &s1r72v05_data,
	},
};
#endif //def CONFIG_BLK_DEV_IDE_S1R72V05

#ifdef CONFIG_MSM_RPCSERVER_HANDSET
static struct platform_device hs_device = {
	.name   = "msm-handset",
	.id     = -1,
	.dev    = {
		.platform_data = "8k_handset",
	},
};
#endif

#ifdef CONFIG_USB_FS_HOST
static struct msm_gpio fsusb_config[] = {
	{ GPIO_CFG(139, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "fs_dat" },
	{ GPIO_CFG(140, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "fs_se0" },
	{ GPIO_CFG(141, 3, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), "fs_oe_n" },
};

static int fsusb_gpio_init(void)
{
	return msm_gpios_request(fsusb_config, ARRAY_SIZE(fsusb_config));
}

static void msm_fsusb_setup_gpio(unsigned int enable)
{
	if (enable)
		msm_gpios_enable(fsusb_config, ARRAY_SIZE(fsusb_config));
	else
		msm_gpios_disable(fsusb_config, ARRAY_SIZE(fsusb_config));

}
#endif

#define MSM_USB_BASE              ((unsigned)addr)
static unsigned ulpi_read(void __iomem *addr, unsigned reg)
{
	unsigned timeout = 100000;

	/* initiate read operation */
	writel(ULPI_RUN | ULPI_READ | ULPI_ADDR(reg),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while ((readl(USB_ULPI_VIEWPORT) & ULPI_RUN) && (--timeout))
		cpu_relax();

	if (timeout == 0) {
		printk(KERN_ERR "ulpi_read: timeout %08x\n",
			readl(USB_ULPI_VIEWPORT));
		return 0xffffffff;
	}
	return ULPI_DATA_READ(readl(USB_ULPI_VIEWPORT));
}

static int ulpi_write(void __iomem *addr, unsigned val, unsigned reg)
{
	unsigned timeout = 10000;

	/* initiate write operation */
	writel(ULPI_RUN | ULPI_WRITE |
	       ULPI_ADDR(reg) | ULPI_DATA(val),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while ((readl(USB_ULPI_VIEWPORT) & ULPI_RUN) && (--timeout))
		cpu_relax();

	if (timeout == 0) {
		printk(KERN_ERR "ulpi_write: timeout\n");
		return -1;
	}

	return 0;
}

struct clk *hs_clk, *phy_clk;
#define CLKRGM_APPS_RESET_USBH      37
#define CLKRGM_APPS_RESET_USB_PHY   34
static void msm_hsusb_apps_reset_link(int reset)
{
	if (reset)
		clk_reset(hs_clk, CLK_RESET_ASSERT);
	else
		clk_reset(hs_clk, CLK_RESET_DEASSERT);
}

static void msm_hsusb_apps_reset_phy(void)
{
	clk_reset(phy_clk, CLK_RESET_ASSERT);
	msleep(1);
	clk_reset(phy_clk, CLK_RESET_DEASSERT);
}

#define ULPI_VERIFY_MAX_LOOP_COUNT  3
static int msm_hsusb_phy_verify_access(void __iomem *addr)
{
	int temp;

	for (temp = 0; temp < ULPI_VERIFY_MAX_LOOP_COUNT; temp++) {
		if (ulpi_read(addr, ULPI_DEBUG) != (unsigned)-1)
			break;
		msm_hsusb_apps_reset_phy();
	}

	if (temp == ULPI_VERIFY_MAX_LOOP_COUNT) {
		pr_err("%s: ulpi read failed for %d times\n",
				__func__, ULPI_VERIFY_MAX_LOOP_COUNT);
		return -1;
	}

	return 0;
}

static unsigned msm_hsusb_ulpi_read_with_reset(void __iomem *addr, unsigned reg)
{
	int temp;
	unsigned res;

	for (temp = 0; temp < ULPI_VERIFY_MAX_LOOP_COUNT; temp++) {
		res = ulpi_read(addr, reg);
		if (res != -1)
			return res;
		msm_hsusb_apps_reset_phy();
	}

	pr_err("%s: ulpi read failed for %d times\n",
			__func__, ULPI_VERIFY_MAX_LOOP_COUNT);

	return -1;
}

static int msm_hsusb_ulpi_write_with_reset(void __iomem *addr,
		unsigned val, unsigned reg)
{
	int temp;
	int res;

	for (temp = 0; temp < ULPI_VERIFY_MAX_LOOP_COUNT; temp++) {
		res = ulpi_write(addr, val, reg);
		if (!res)
			return 0;
		msm_hsusb_apps_reset_phy();
	}

	pr_err("%s: ulpi write failed for %d times\n",
			__func__, ULPI_VERIFY_MAX_LOOP_COUNT);
	return -1;
}

static int msm_hsusb_phy_caliberate(void __iomem *addr)
{
	int ret;
	unsigned res;

	ret = msm_hsusb_phy_verify_access(addr);
	if (ret)
		return -ETIMEDOUT;

	res = msm_hsusb_ulpi_read_with_reset(addr, ULPI_FUNC_CTRL_CLR);
	if (res == -1)
		return -ETIMEDOUT;

	res = msm_hsusb_ulpi_write_with_reset(addr,
			res | ULPI_SUSPENDM,
			ULPI_FUNC_CTRL_CLR);
	if (res)
		return -ETIMEDOUT;

	msm_hsusb_apps_reset_phy();

	return msm_hsusb_phy_verify_access(addr);
}

#define USB_LINK_RESET_TIMEOUT      (msecs_to_jiffies(10))
static int msm_hsusb_native_phy_reset(void __iomem *addr)
{
	u32 temp;
	unsigned long timeout;

	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa())
		return msm_hsusb_phy_reset();

	msm_hsusb_apps_reset_link(1);
	msm_hsusb_apps_reset_phy();
	msm_hsusb_apps_reset_link(0);

	/* select ULPI phy */
	temp = (readl(USB_PORTSC) & ~PORTSC_PTS);
	writel(temp | PORTSC_PTS_ULPI, USB_PORTSC);

	if (msm_hsusb_phy_caliberate(addr))
		return -1;

	/* soft reset phy */
	writel(USBCMD_RESET, USB_USBCMD);
	timeout = jiffies + USB_LINK_RESET_TIMEOUT;
	while (readl(USB_USBCMD) & USBCMD_RESET) {
		if (time_after(jiffies, timeout)) {
			pr_err("usb link reset timeout\n");
			break;
		}
		msleep(1);
	}

	return 0;
}

static struct msm_hsusb_platform_data msm_hsusb_pdata = {
};

#ifdef CONFIG_USB_EHCI_MSM
static struct vreg *vreg_usb;
static void msm_hsusb_vbus_power(unsigned phy_info, int on)
{

	switch (PHY_TYPE(phy_info)) {
	case USB_PHY_INTEGRATED:
		if (on)
			msm_hsusb_vbus_powerup();
		else
			msm_hsusb_vbus_shutdown();
		break;
	case USB_PHY_SERIAL_PMIC:
		if (on)
			vreg_enable(vreg_usb);
		else
			vreg_disable(vreg_usb);
		break;
	default:
		pr_err("%s: undefined phy type ( %X ) \n", __func__,
						phy_info);
	}

}

static struct msm_usb_host_platform_data msm_usb_host_pdata = {
	.phy_info	= (USB_PHY_INTEGRATED | USB_PHY_MODEL_180NM),
	.phy_reset = msm_hsusb_native_phy_reset,
	.vbus_power = msm_hsusb_vbus_power,
};

#ifdef CONFIG_USB_FS_HOST
static struct msm_usb_host_platform_data msm_usb_host2_pdata = {
	.phy_info	= USB_PHY_SERIAL_PMIC,
	.config_gpio = msm_fsusb_setup_gpio,
	.vbus_power = msm_hsusb_vbus_power,
};
#endif
#endif //def CONFIG_USB_EHCI_MSM

static struct android_pmem_platform_data android_pmem_kernel_ebi1_pdata = {
	.name = PMEM_KERNEL_EBI1_DATA_NAME,
	/* if no allocator_type, defaults to PMEM_ALLOCATORTYPE_BITMAP,
	 * the only valid choice at this time. The board structure is
	 * set to all zeros by the C runtime initialization and that is now
	 * the enum value of PMEM_ALLOCATORTYPE_BITMAP, now forced to 0 in
	 * include/linux/android_pmem.h.
	 */
	.cached = 0,
};

#ifdef CONFIG_KERNEL_PMEM_SMI_REGION

static struct android_pmem_platform_data android_pmem_kernel_smi_pdata = {
	.name = PMEM_KERNEL_SMI_DATA_NAME,
	/* if no allocator_type, defaults to PMEM_ALLOCATORTYPE_BITMAP,
	 * the only valid choice at this time. The board structure is
	 * set to all zeros by the C runtime initialization and that is now
	 * the enum value of PMEM_ALLOCATORTYPE_BITMAP, now forced to 0 in
	 * include/linux/android_pmem.h.
	 */
	.cached = 0,
};

#endif

static struct android_pmem_platform_data android_pmem_pdata = {
	.name = "pmem",
	.allocator_type = PMEM_ALLOCATORTYPE_ALLORNOTHING,
	.cached = 1,
};

static struct android_pmem_platform_data android_pmem_adsp_pdata = {
	.name = "pmem_adsp",
	.allocator_type = PMEM_ALLOCATORTYPE_BITMAP,
	.cached = 0,
};

static struct android_pmem_platform_data android_pmem_smipool_pdata = {
	.name = "pmem_smipool",
	.start = MSM_PMEM_SMIPOOL_BASE,
	.size = MSM_PMEM_SMIPOOL_SIZE,
	.allocator_type = PMEM_ALLOCATORTYPE_BITMAP,
	.cached = 0,
};


static struct platform_device android_pmem_device = {
	.name = "android_pmem",
	.id = 0,
	.dev = { .platform_data = &android_pmem_pdata },
};

static struct platform_device android_pmem_adsp_device = {
	.name = "android_pmem",
	.id = 1,
	.dev = { .platform_data = &android_pmem_adsp_pdata },
};

static struct platform_device android_pmem_smipool_device = {
	.name = "android_pmem",
	.id = 2,
	.dev = { .platform_data = &android_pmem_smipool_pdata },
};


static struct platform_device android_pmem_kernel_ebi1_device = {
	.name = "android_pmem",
	.id = 3,
	.dev = { .platform_data = &android_pmem_kernel_ebi1_pdata },
};

#ifdef CONFIG_KERNEL_PMEM_SMI_REGION
static struct platform_device android_pmem_kernel_smi_device = {
	.name = "android_pmem",
	.id = 4,
	.dev = { .platform_data = &android_pmem_kernel_smi_pdata },
};
#endif

static struct resource msm_fb_resources[] = {
	{
		.flags  = IORESOURCE_DMA,
	}
};

static int msm_fb_detect_panel(const char *name)
{
	int ret = -EPERM;

	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
		if (!strncmp(name, "mddi_toshiba_wvga_pt", 20))
			ret = 0;
		else
			ret = -ENODEV;
	} else if ((machine_is_qsd8x50_surf() || machine_is_qsd8x50a_surf())
			&& !strcmp(name, "lcdc_external"))
		ret = 0;

	return ret;
}

static struct msm_fb_platform_data msm_fb_pdata = {
	.detect_client = msm_fb_detect_panel,
};

static struct platform_device msm_fb_device = {
	.name   = "msm_fb",
	.id     = 0,
	.num_resources  = ARRAY_SIZE(msm_fb_resources),
	.resource       = msm_fb_resources,
	.dev    = {
		.platform_data = &msm_fb_pdata,
	}
};

#if defined(CONFIG_TOUCHSCREEN_AUO_H353)
static struct auo_platform_data auo_ts_data ={
	.gpio = 108,
};
#endif

#if defined(CONFIG_ACER_HEADSET_BUTT)
static struct hs_butt_gpio hs_butt_data = {
	.gpio_hs_butt = 102,
	.gpio_hs_dett = 151,
	.gpio_hs_mic  = 152,
};

static struct platform_device hs_butt_device = {
	.name   = "acer-hs-butt",
	.id     = 0,
	.dev    = {
		.platform_data	= &hs_butt_data,
	},
};
#endif

#ifdef CONFIG_QSD_SPI
static struct msm_gpio bma_spi_gpio_config_data[] = {
	{ GPIO_CFG(22, 0, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA), "bma_irq" },
};

static int msm_bma_gpio_setup(struct device *dev)
{
	int rc;

	rc = msm_gpios_request_enable(bma_spi_gpio_config_data,
		ARRAY_SIZE(bma_spi_gpio_config_data));

	return rc;
}

static void msm_bma_gpio_teardown(struct device *dev)
{
	msm_gpios_disable_free(bma_spi_gpio_config_data,
		ARRAY_SIZE(bma_spi_gpio_config_data));
}

static struct bma150_platform_data bma_pdata = {
	.setup    = msm_bma_gpio_setup,
	.teardown = msm_bma_gpio_teardown,
};

static struct resource qsd_spi_resources[] = {
	{
		.name   = "spi_irq_in",
		.start	= INT_SPI_INPUT,
		.end	= INT_SPI_INPUT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name   = "spi_irq_out",
		.start	= INT_SPI_OUTPUT,
		.end	= INT_SPI_OUTPUT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name   = "spi_irq_err",
		.start	= INT_SPI_ERROR,
		.end	= INT_SPI_ERROR,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name   = "spi_base",
		.start	= 0xA1200000,
		.end	= 0xA1200000 + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name   = "spidm_channels",
		.flags  = IORESOURCE_DMA,
	},
	{
		.name   = "spidm_crci",
		.flags  = IORESOURCE_DMA,
	},
};

static struct platform_device qsd_device_spi = {
	.name	        = "spi_qsd",
	.id	        = 0,
	.num_resources	= ARRAY_SIZE(qsd_spi_resources),
	.resource	= qsd_spi_resources,
};

static struct spi_board_info msm_spi_board_info[] __initdata = {
	{
		.modalias	= "bma150",
		.mode		= SPI_MODE_3,
		.irq		= MSM_GPIO_TO_INT(22),
		.bus_num	= 0,
		.chip_select	= 0,
		.max_speed_hz	= 10000000,
		.platform_data	= &bma_pdata,
	},
};

#define CT_CSR_PHYS		0xA8700000
#define TCSR_SPI_MUX		(ct_csr_base + 0x54)
static int msm_qsd_spi_dma_config(void)
{
	void __iomem *ct_csr_base = 0;
	u32 spi_mux;
	int ret = 0;

	ct_csr_base = ioremap(CT_CSR_PHYS, PAGE_SIZE);
	if (!ct_csr_base) {
		pr_err("%s: Could not remap %x\n", __func__, CT_CSR_PHYS);
		return -1;
	}

	spi_mux = readl(TCSR_SPI_MUX);
	switch (spi_mux) {
	case (1):
		qsd_spi_resources[4].start  = DMOV_HSUART1_RX_CHAN;
		qsd_spi_resources[4].end    = DMOV_HSUART1_TX_CHAN;
		qsd_spi_resources[5].start  = DMOV_HSUART1_RX_CRCI;
		qsd_spi_resources[5].end    = DMOV_HSUART1_TX_CRCI;
		break;
	case (2):
		qsd_spi_resources[4].start  = DMOV_HSUART2_RX_CHAN;
		qsd_spi_resources[4].end    = DMOV_HSUART2_TX_CHAN;
		qsd_spi_resources[5].start  = DMOV_HSUART2_RX_CRCI;
		qsd_spi_resources[5].end    = DMOV_HSUART2_TX_CRCI;
		break;
	case (3):
		qsd_spi_resources[4].start  = DMOV_CE_OUT_CHAN;
		qsd_spi_resources[4].end    = DMOV_CE_IN_CHAN;
		qsd_spi_resources[5].start  = DMOV_CE_OUT_CRCI;
		qsd_spi_resources[5].end    = DMOV_CE_IN_CRCI;
		break;
	default:
		ret = -1;
	}

	iounmap(ct_csr_base);
	return ret;
}

static struct msm_gpio qsd_spi_gpio_config_data[] = {
	{ GPIO_CFG(17, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA), "spi_clk" },
	{ GPIO_CFG(18, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA), "spi_mosi" },
	{ GPIO_CFG(19, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA), "spi_miso" },
	{ GPIO_CFG(20, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA), "spi_cs0" },
	{ GPIO_CFG(21, 0, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_16MA), "spi_pwr" },
};

static int msm_qsd_spi_gpio_config(void)
{
	int rc;

	rc = msm_gpios_request_enable(qsd_spi_gpio_config_data,
		ARRAY_SIZE(qsd_spi_gpio_config_data));
	if (rc)
		return rc;

	/* Set direction for SPI_PWR */
	gpio_direction_output(21, 1);

	return 0;
}

static void msm_qsd_spi_gpio_release(void)
{
	msm_gpios_disable_free(qsd_spi_gpio_config_data,
		ARRAY_SIZE(qsd_spi_gpio_config_data));
}

static struct msm_spi_platform_data qsd_spi_pdata = {
	.max_clock_speed = 19200000,
	.gpio_config  = msm_qsd_spi_gpio_config,
	.gpio_release = msm_qsd_spi_gpio_release,
	.dma_config = msm_qsd_spi_dma_config,
};

static void __init msm_qsd_spi_init(void)
{
	qsd_device_spi.dev.platform_data = &qsd_spi_pdata;
}
#endif //def CONFIG_QSD_SPI

#ifdef CONFIG_FB_MSM_MDDI_TOSHIBA_WVGA
static int mddi_toshiba_pmic_bl(int level)
{
	int ret = -EPERM;

	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
		ret = pmic_set_led_intensity(LED_LCD, level);

		if (ret)
			printk(KERN_WARNING "%s: can't set lcd backlight!\n",
						__func__);
	}

	return ret;
}

static struct msm_panel_common_pdata mddi_toshiba_pdata = {
	.pmic_backlight = mddi_toshiba_pmic_bl,
};

static struct platform_device mddi_toshiba_device = {
	.name   = "mddi_toshiba",
	.id     = 0,
	.dev    = {
		.platform_data = &mddi_toshiba_pdata,
	}
};
#endif

static void msm_fb_vreg_config(const char *name, int on)
{
	struct vreg *vreg;
	int ret = 0;

	vreg = vreg_get(NULL, name);
	if (IS_ERR(vreg)) {
		printk(KERN_ERR "%s: vreg_get(%s) failed (%ld)\n",
		__func__, name, PTR_ERR(vreg));
		return;
	}

	ret = (on) ? vreg_enable(vreg) : vreg_disable(vreg);
	if (ret)
		printk(KERN_ERR "%s: %s(%s) failed!\n",
			__func__, (on) ? "vreg_enable" : "vreg_disable", name);
}

#ifdef CONFIG_MACH_QSD8X50_SURF
#define MDDI_RST_OUT_GPIO 100
#endif

static int mddi_power_save_on;
static int msm_fb_mddi_power_save(int on)
{
	int flag_on = !!on;
	int ret = 0;


	if (mddi_power_save_on == flag_on)
		return ret;

	mddi_power_save_on = flag_on;

#ifdef CONFIG_MACH_QSD8X50_SURF
	if (!flag_on && (machine_is_qsd8x50_ffa()
				|| machine_is_qsd8x50a_ffa())) {
		gpio_set_value(MDDI_RST_OUT_GPIO, 0);
		mdelay(1);
	}
#endif //CONFIG_MACH_QSD8X50_SURF

	ret = pmic_lp_mode_control(flag_on ? OFF_CMD : ON_CMD,
		PM_VREG_LP_MSME2_ID);
	if (ret)
		printk(KERN_ERR "%s: pmic_lp_mode_control failed!\n", __func__);

	msm_fb_vreg_config("gp5", flag_on);
	msm_fb_vreg_config("boost", flag_on);

#ifdef CONFIG_MACH_QSD8X50_SURF
	if (flag_on && (machine_is_qsd8x50_ffa()
			|| machine_is_qsd8x50a_ffa())) {
		gpio_set_value(MDDI_RST_OUT_GPIO, 0);
		mdelay(1);
		gpio_set_value(MDDI_RST_OUT_GPIO, 1);
		gpio_set_value(MDDI_RST_OUT_GPIO, 1);
		mdelay(1);
	}
#endif //CONFIG_MACH_QSD8X50_SURF
}

static int msm_fb_mddi_sel_clk(u32 *clk_rate)
{
	*clk_rate *= 2;
	return 0;
}

static struct mddi_platform_data mddi_pdata = {
	.mddi_power_save = msm_fb_mddi_power_save,
	.mddi_sel_clk = msm_fb_mddi_sel_clk,
};

#ifndef CONFIG_MACH_ACER_A1
static struct msm_panel_common_pdata mdp_pdata = {
	.gpio = 98,
	.mdp_rev = MDP_REV_31,
};
#endif

static void __init msm_fb_add_devices(void)
{
#ifdef CONFIG_MACH_ACER_A1
	msm_fb_register_device("mdp", 0);
#else
	msm_fb_register_device("mdp", &mdp_pdata);
#endif
	msm_fb_register_device("pmdh", &mddi_pdata);
	msm_fb_register_device("emdh", &mddi_pdata);
#if defined(CONFIG_FB_MSM_TVOUT)
	msm_fb_register_device("tvenc", 0);
#endif
	msm_fb_register_device("lcdc", 0);
}

static struct resource msm_audio_resources[] = {
	{
		.flags  = IORESOURCE_DMA,
	},
	{
		.name   = "aux_pcm_dout",
		.start  = 68,
		.end    = 68,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "aux_pcm_din",
		.start  = 69,
		.end    = 69,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "aux_pcm_syncout",
		.start  = 70,
		.end    = 70,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "aux_pcm_clkin_a",
		.start  = 71,
		.end    = 71,
		.flags  = IORESOURCE_IO,
	},
#ifdef CONFIG_MACH_QSD8X50_SURF
	{
		.name   = "sdac_din",
		.start  = 144,
		.end    = 144,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "sdac_dout",
		.start  = 145,
		.end    = 145,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "sdac_wsout",
		.start  = 143,
		.end    = 143,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "cc_i2s_clk",
		.start  = 142,
		.end    = 142,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "audio_master_clkout",
		.start  = 146,
		.end    = 146,
		.flags  = IORESOURCE_IO,
	},
#endif //def CONFIG_MACH_QSD8X50_SURF
	{
		.name	= "audio_base_addr",
		.start	= 0xa0700000,
		.end	= 0xa0700000 + 4,
		.flags	= IORESOURCE_MEM,
	},

};

#ifdef CONFIG_MACH_ACER_A1
static unsigned audio_gpio_on_v03[] = {
	GPIO_CFG(68, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* PCM_DOUT */
	GPIO_CFG(69, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),	/* PCM_DIN */
	GPIO_CFG(70, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* PCM_SYNC */
	GPIO_CFG(71, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* PCM_CLK */

	GPIO_CFG(39, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),	/* V0.2 HPH_AMP_EN */
	GPIO_CFG(102, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),	/* HS_BUTT */
	GPIO_CFG(151, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA),	/* HS_DETECT */
	GPIO_CFG(152, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* MIC_BIAS_EN */

#ifdef CONFIG_AUDIO_FM2018
	GPIO_CFG(29, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),	/* FM2018_CLK_EN */
	GPIO_CFG(116, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* FM2018_1.8V_EN */
	GPIO_CFG(150, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* Bypass_Normal */
	GPIO_CFG(148, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* FM2018_RESET */
	GPIO_CFG(101, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* FM2018_PWD */
#endif

#ifdef CONFIG_AUDIO_TPA2018
	GPIO_CFG(142, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* SPK_AMP_EN */
#endif
};
#endif

static unsigned audio_gpio_on[] = {
	GPIO_CFG(68, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* PCM_DOUT */
	GPIO_CFG(69, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),	/* PCM_DIN */
	GPIO_CFG(70, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* PCM_SYNC */
	GPIO_CFG(71, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* PCM_CLK */
#ifdef CONFIG_MACH_QSD8X50_SURF
	GPIO_CFG(142, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* CC_I2S_CLK */
	GPIO_CFG(143, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* SADC_WSOUT */
	GPIO_CFG(144, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),	/* SADC_DIN */
	GPIO_CFG(145, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* SDAC_DOUT */
	GPIO_CFG(146, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),	/* MA_CLK_OUT */
#endif
#ifdef CONFIG_MACH_ACER_A1
	GPIO_CFG(39, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),	/* V0.2 HPH_AMP_EN */
	GPIO_CFG(102, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),	/* HS_BUTT */
	GPIO_CFG(151, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA),	/* HS_DETECT */
	GPIO_CFG(152, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* MIC_BIAS_EN */
#ifdef CONFIG_AUDIO_FM2018
	GPIO_CFG(29, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),	/* FM2018_CLK_EN */
	GPIO_CFG(116, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* FM2018_1.8V_EN */
	GPIO_CFG(117, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* Bypass_Normal */
	GPIO_CFG(125, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* FM2018_RESET */
	GPIO_CFG(126, 0, GPIO_OUTPUT, GPIO_PULL_DOWN, GPIO_2MA),/* FM2018_PWD */
#endif // CONFIG_AUDIO_FM2018
#endif // CONFIG_MACH_ACER_A1
};

static void __init audio_gpio_init(void)
{
	int pin, rc;

#ifdef CONFIG_MACH_ACER_A1
	if(hw_version >= 3){
		for (pin = 0; pin < ARRAY_SIZE(audio_gpio_on_v03); pin++) {
			rc = gpio_tlmm_config(audio_gpio_on_v03[pin],
				GPIO_ENABLE);
			if (rc) {
				printk(KERN_ERR
					"%s: gpio_tlmm_config(%#x)=%d\n",
					__func__, audio_gpio_on_v03[pin], rc);
				return;
			}
		}
	} else {
		for (pin = 0; pin < ARRAY_SIZE(audio_gpio_on); pin++) {
			rc = gpio_tlmm_config(audio_gpio_on[pin],
				GPIO_ENABLE);
			if (rc) {
				printk(KERN_ERR
					"%s: gpio_tlmm_config(%#x)=%d\n",
					__func__, audio_gpio_on[pin], rc);
				return;
			}
		}
	}
#else
	for (pin = 0; pin < ARRAY_SIZE(audio_gpio_on); pin++) {
		rc = gpio_tlmm_config(audio_gpio_on[pin],
			GPIO_ENABLE);
		if (rc) {
			printk(KERN_ERR
				"%s: gpio_tlmm_config(%#x)=%d\n",
				__func__, audio_gpio_on[pin], rc);
			return;
		}
	}
#endif
}

static struct platform_device msm_audio_device = {
	.name   = "msm_audio",
	.id     = 0,
	.num_resources  = ARRAY_SIZE(msm_audio_resources),
	.resource       = msm_audio_resources,
};

static void __init avr_gpio_init(void)
{
	int rc;
#if defined(CONFIG_MACH_Q8K_A1_EVT)
	/* The H/W configuration setting for A1 v0.1 */
	const int avr_en_pin = 91;
#else
	/* The H/W configuration setting for A1 v0.2 */
	const int avr_en_pin = 27;
#endif
	rc = gpio_request(avr_en_pin, "AVR_EN");

	if(rc){
		pr_err("AVR gpio_request failed on pin %d (rc=%d)\n", avr_en_pin, rc);
		return ;
	}

	/* Set avr_en_pin as output high */
	gpio_direction_output(avr_en_pin,1);
	mdelay(100);
#if defined(CONFIG_MACH_Q8K_A1_EVT)
	gpio_direction_output(29,1);
	mdelay(100);
#endif

	if(gpio_get_value(avr_en_pin) == 1){
		pr_info("AVR gpio init done.\n");
	}else{
		pr_err("AVR gpio init failed!\n");
	}
}

#if defined(CONFIG_MS3C)
static void __init compass_gpio_init(void)
{
	const int ms3c_reset_pin = 23;
	const int ms3c_pwr_pin = 117;
	int rc = gpio_request(ms3c_reset_pin, "CP_RST");

	if(rc){
		pr_err("gpio_request failed on pin %d (rc=%d)\n", ms3c_reset_pin, rc);
		return ;
	}

	gpio_set_value(ms3c_reset_pin, 0);
	mdelay(100);
	gpio_set_value(ms3c_reset_pin, 1);

	if(hw_version>=3){
		rc = gpio_request(ms3c_pwr_pin, "CP_PWR");
		if(rc){
			pr_err("gpio_request failed on pin %d (rc=%d)\n", ms3c_pwr_pin, rc);
			gpio_free(ms3c_reset_pin);
			return ;
		}
		rc = GPIO_CFG(ms3c_pwr_pin, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_2MA);
		gpio_set_value(ms3c_pwr_pin, 1);
	}

	if(gpio_get_value(ms3c_reset_pin) != 1 ||
		(hw_version>=3 && gpio_get_value(ms3c_pwr_pin) != 1)){
		pr_err("Yamaha MS-3C gpio init failed!\n");
		return;
	}
	pr_info("Yamaha MS-3C gpio init done.\n");
}
#endif //defined(CONFIG_MS3C)


static struct resource bluesleep_resources[] = {
	{
		.name	= "gpio_host_wake",
		.start	= 21,
		.end	= 21,
		.flags	= IORESOURCE_IO,
	},
#ifdef CONFIG_MACH_ACER_A1
	{
		.name	= "gpio_ext_wake",
		.start	= 107,
		.end	= 107,
		.flags	= IORESOURCE_IO,
	},
#else
	{
		.name	= "gpio_ext_wake",
		.start	= 19,
		.end	= 19,
		.flags	= IORESOURCE_IO,
	},
#endif
	{
		.name	= "host_wake",
		.start	= MSM_GPIO_TO_INT(21),
		.end	= MSM_GPIO_TO_INT(21),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device msm_bluesleep_device = {
	.name = "bluesleep",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(bluesleep_resources),
	.resource	= bluesleep_resources,
};

#ifdef CONFIG_BT
static struct platform_device msm_bt_power_device = {
	.name = "bt_power",
};

enum {
	BT_SYSRST,
	BT_WAKE,
	BT_HOST_WAKE,
	BT_VDD_IO,
	BT_RFR,
	BT_CTS,
	BT_RX,
	BT_TX,
	BT_VDD_FREG
};

#ifdef CONFIG_MACH_QSD8X50_SURF
static struct msm_gpio bt_config_power_off[] = {
	{ GPIO_CFG(18, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"BT SYSRST" },
	{ GPIO_CFG(19, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"BT WAKE" },
	{ GPIO_CFG(21, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"HOST WAKE" },
	{ GPIO_CFG(22, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"BT VDD_IO" },
	{ GPIO_CFG(43, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"UART1DM_RFR" },
	{ GPIO_CFG(44, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"UART1DM_CTS" },
	{ GPIO_CFG(45, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"UART1DM_RX" },
	{ GPIO_CFG(46, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"UART1DM_TX" }
};
#endif

#ifdef CONFIG_MACH_ACER_A1
static struct msm_gpio bt_config_power_on[] = {
	{ GPIO_CFG(31, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"BT SYSRST" },
	{ GPIO_CFG(107, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"BT WAKE" },
	{ GPIO_CFG(21, 0, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),
		"HOST WAKE" },
	{ GPIO_CFG(106, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"PWR_EN" },
	{ GPIO_CFG(157, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"UART2DM_RFR" },
	{ GPIO_CFG(141, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),
		"UART2DM_CTS" },
	{ GPIO_CFG(139, 1, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),
		"UART2DM_RX" },
	{ GPIO_CFG(140, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"UART2DM_TX" }
};
#else
static struct msm_gpio bt_config_power_on[] = {
	{ GPIO_CFG(18, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"BT SYSRST" },
	{ GPIO_CFG(19, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"BT WAKE" },
	{ GPIO_CFG(21, 0, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),
		"HOST WAKE" },
	{ GPIO_CFG(22, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"BT VDD_IO" },
	{ GPIO_CFG(43, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"UART1DM_RFR" },
	{ GPIO_CFG(44, 2, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),
		"UART1DM_CTS" },
	{ GPIO_CFG(45, 2, GPIO_INPUT,  GPIO_NO_PULL, GPIO_2MA),
		"UART1DM_RX" },
	{ GPIO_CFG(46, 2, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"UART1DM_TX" }
};
#endif //def CONFIG_MACH_ACER_A1

#ifdef CONFIG_MACH_QSD8X50_SURF
static struct msm_gpio wlan_config_power_off[] = {
	{ GPIO_CFG(62, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"SDC2_CLK" },
	{ GPIO_CFG(63, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"SDC2_CMD" },
	{ GPIO_CFG(64, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"SDC2_D3" },
	{ GPIO_CFG(65, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"SDC2_D2" },
	{ GPIO_CFG(66, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"SDC2_D1" },
	{ GPIO_CFG(67, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"SDC2_D0" },
	{ GPIO_CFG(113, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"VDD_WLAN" },
	{ GPIO_CFG(138, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA),
		"WLAN_PWD" }
};

static struct msm_gpio wlan_config_power_on[] = {
	{ GPIO_CFG(62, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"SDC2_CLK" },
	{ GPIO_CFG(63, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA),
		"SDC2_CMD" },
	{ GPIO_CFG(64, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA),
		"SDC2_D3" },
	{ GPIO_CFG(65, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA),
		"SDC2_D2" },
	{ GPIO_CFG(66, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA),
		"SDC2_D1" },
	{ GPIO_CFG(67, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA),
		"SDC2_D0" },
	{ GPIO_CFG(113, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"VDD_WLAN" },
	{ GPIO_CFG(138, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA),
		"WLAN_PWD" }
};
#endif //def CONFIG_MACH_QSD8X50_SURF

#ifdef CONFIG_MACH_ACER_A1

static int bluetooth_power(int on)
{
	mutex_lock(&wifibtmutex);
	if (on) {
		gpio_set_value(106, on); /* PWR_EN */
		msleep(100);
		gpio_set_value(31, on); /* SYSRST */
	} else {
		gpio_set_value(106, 0); /* PWR_EN */
		msleep(100);
		gpio_set_value(31, 0); /* SYSRST */
	}
	mutex_unlock(&wifibtmutex);

	printk(KERN_DEBUG "Bluetooth power switch: %d\n", on);

	return 0;

}

static void __init bt_power_init(void)
{
	int rc;

	rc = msm_gpios_enable(bt_config_power_on,
			ARRAY_SIZE(bt_config_power_on));
	if (rc < 0) {
		printk(KERN_ERR
				"%s: bt power on gpio config failed: %d\n",
				__func__, rc);
		goto exit;
	}

	if (gpio_request(31, "bt_reset"))
		pr_err("failed to request gpio bt_reset\n");
	if (gpio_request(106, "bt_power_enable"))
		pr_err("failed to request gpio bt_power_enable\n");
	if (gpio_request(157, "bt_rfr"))
		pr_err("failed to request gpio bt_rfr\n");
	if (gpio_request(141, "bt_cts"))
		pr_err("failed to request gpio bt_cts\n");
	if (gpio_request(139, "bt_rx"))
		pr_err("failed to request gpio bt_rx\n");
	if (gpio_request(140, "bt_tx"))
		pr_err("failed to request gpio bt_tx\n");

	msm_bt_power_device.dev.platform_data = &bluetooth_power;

	printk(KERN_DEBUG "Bluetooth power switch: initialized\n");

exit:
	return;
}

#else //#ifdef CONFIG_MACH_ACER_A1

static int bluetooth_power(int on)
{
	int rc;
	struct vreg *vreg_wlan;

	vreg_wlan = vreg_get(NULL, "wlan");

	if (IS_ERR(vreg_wlan)) {
		printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg_wlan));
		return PTR_ERR(vreg_wlan);
	}

	if (on) {
		/* units of mV, steps of 50 mV */
		rc = vreg_set_level(vreg_wlan, PMIC_VREG_WLAN_LEVEL);
		if (rc) {
			printk(KERN_ERR "%s: vreg wlan set level failed (%d)\n",
			       __func__, rc);
			return -EIO;
		}
		rc = vreg_enable(vreg_wlan);
		if (rc) {
			printk(KERN_ERR "%s: vreg wlan enable failed (%d)\n",
			       __func__, rc);
			return -EIO;
		}

		rc = msm_gpios_enable(bt_config_power_on,
					ARRAY_SIZE(bt_config_power_on));
		if (rc < 0) {
			printk(KERN_ERR
				"%s: bt power on gpio config failed: %d\n",
				__func__, rc);
			return rc;
		}

		if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
			rc = msm_gpios_enable
					(wlan_config_power_on,
					 ARRAY_SIZE(wlan_config_power_on));
			if (rc < 0) {
				printk
				 (KERN_ERR
				 "%s: wlan power on gpio config failed: %d\n",
					__func__, rc);
				return rc;
			}
		}

		gpio_set_value(22, on); /* VDD_IO */
		gpio_set_value(18, on); /* SYSRST */

		if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
			gpio_set_value(138, 0); /* WLAN: CHIP_PWD */
			gpio_set_value(113, on); /* WLAN */
		}
	} else {
		if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
			gpio_set_value(138, on); /* WLAN: CHIP_PWD */
			gpio_set_value(113, on); /* WLAN */
		}

		gpio_set_value(18, on); /* SYSRST */
		gpio_set_value(22, on); /* VDD_IO */

		rc = vreg_disable(vreg_wlan);
		if (rc) {
			printk(KERN_ERR "%s: vreg wlan disable failed (%d)\n",
					__func__, rc);
			return -EIO;
		}

		rc = msm_gpios_enable(bt_config_power_off,
					ARRAY_SIZE(bt_config_power_off));
		if (rc < 0) {
			printk(KERN_ERR
				"%s: bt power off gpio config failed: %d\n",
				__func__, rc);
			return rc;
		}

		if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
			rc = msm_gpios_enable
					(wlan_config_power_off,
					 ARRAY_SIZE(wlan_config_power_off));
			if (rc < 0) {
				printk
				 (KERN_ERR
				 "%s: wlan power off gpio config failed: %d\n",
					__func__, rc);
				return rc;
			}
		}
	}

	printk(KERN_DEBUG "Bluetooth power switch: %d\n", on);

	return 0;
}

static void __init bt_power_init(void)
{
	struct vreg *vreg_bt;
	int rc;

	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
		gpio_set_value(138, 0); /* WLAN: CHIP_PWD */
		gpio_set_value(113, 0); /* WLAN */
	}

	gpio_set_value(18, 0); /* SYSRST */
	gpio_set_value(22, 0); /* VDD_IO */

	/* do not have vreg bt defined, gp6 is the same */
	/* vreg_get parameter 1 (struct device *) is ignored */
	vreg_bt = vreg_get(NULL, "gp6");

	if (IS_ERR(vreg_bt)) {
		printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg_bt));
		goto exit;
	}

	/* units of mV, steps of 50 mV */
	rc = vreg_set_level(vreg_bt, PMIC_VREG_GP6_LEVEL);
	if (rc) {
		printk(KERN_ERR "%s: vreg bt set level failed (%d)\n",
		       __func__, rc);
		goto exit;
	}
	rc = vreg_enable(vreg_bt);
	if (rc) {
		printk(KERN_ERR "%s: vreg bt enable failed (%d)\n",
		       __func__, rc);
		goto exit;
	}

	if (bluetooth_power(0))
		goto exit;

	msm_bt_power_device.dev.platform_data = &bluetooth_power;

	printk(KERN_DEBUG "Bluetooth power switch: initialized\n");

exit:
	return;
}
#endif //def CONFIG_MACH_ACER_A1

#else
#define bt_power_init(x) do {} while (0)
#endif //def CONFIG_BT

#if defined(CONFIG_MMC_WIFI) || defined(CONFIG_MMC_WIFI_MODULE)
//for Wifi Module Card Detect
static struct platform_device msm_wifi_power_device = {
	.name = "wifi_power",
};

static struct embedded_sdio_data bcm_wifi_emb_data = {
	.cis = {
		.max_dtr = 25000000,
	},
	.cccr   = {
		.sdio_vsn       = 2,
		.multi_block    = 1,
		.low_speed      = 0,
		.wide_bus       = 0,
		.high_power     = 1,
		.high_speed     = 1,
	},
};

//When sue Android UI to Open Wifi and open Wifi Power
static struct wake_lock wifi_wake_lock;
static int wifi_power(int on)
{
    int bt_on = 0;
    pr_debug("%s\n", __func__);

    //In order to follow wifi power sequence, we have to detect bt power status

    if (on == 1 || on == 3) {
        mutex_lock(&wifibtmutex);
	wake_lock(&wifi_wake_lock);
        bt_on=gpio_get_value(106);
        if(!bt_on){
            //if WLAN on and BT off
            gpio_set_value(WL_PWR_EN, 1); /* WL_PWR_EN */
            gpio_set_value(106, 1); /* BT_PWR_EN */
            msleep(100);
            gpio_set_value(WL_RST, 1); /* WL_RST */
            gpio_set_value(31, 1); /* BT_RST */
            msleep(100);//at last 100 ms
            gpio_set_value(31, 0); /* BT_RST */
            gpio_set_value(106, 0); /* BT_PWR_EN */
        } else {
            //if WLAN on and BT on
            gpio_set_value(WL_PWR_EN, 1); /* WL_PWR_EN */
            msleep(100);
            gpio_set_value(WL_RST, 1); /* WL_RST */
        }
	if(on == 1)
        	wifi_set_carddetect(1);
        pr_info(KERN_INFO"Wifi Power ON\n");
        mutex_unlock(&wifibtmutex);

    } else {
	    wake_lock_timeout(&wifi_wake_lock, 3*HZ);
        gpio_set_value(WL_PWR_EN, 0); /* WL_PWR_EN */
        msleep(100);
        gpio_set_value(WL_RST, 0); /* WL_RST */
	if(on == 0)
        	wifi_set_carddetect(0);
        pr_info("Wifi Power OFF\n");
    }
    return 0;
}

static void __init wifi_power_init(void)
{
	wake_lock_init(&wifi_wake_lock, WAKE_LOCK_SUSPEND, "wifi-interface");
	msm_wifi_power_device.dev.platform_data = &wifi_power;
}

//For Wifi Module Card Detect
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

static int wifi_status_register(void (*callback)(int card_present, void *dev_id), void *dev_id)
{
    if (wifi_status_cb)
        return -EAGAIN;
    wifi_status_cb = callback;
    wifi_status_cb_devid = dev_id;
    return 0;
}

int wifi_set_carddetect(int val)
{
    pr_info("%s: %d\n", __func__, val);
    if (wifi_status_cb) {
        wifi_status_cb(val, wifi_status_cb_devid);
    } else
        pr_debug("%s: Nobody to notify\n", __func__);
    return 0;
}

EXPORT_SYMBOL(wifi_set_carddetect);
void bcm_wlan_power_off(int a) {
//	(void)a;
	wifi_power(2*a-2);
	//wifi_set_carddetect(0);
}
EXPORT_SYMBOL(bcm_wlan_power_off);

void bcm_wlan_power_on(int a) {
//	(void)a;
	wifi_power(2*a-1);
	//wifi_set_carddetect(1);
}
EXPORT_SYMBOL(bcm_wlan_power_on);


#endif//def CONFIG_MMC_WIFI

#if defined(CONFIG_MACH_ACER_A1)
static void __init wlan_init(void)
{
    struct vreg *vreg;
    int rc;

    vreg = vreg_get(NULL, "wlan");
    if (IS_ERR(vreg)) {
        printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg));
        return;
    }

    if(hw_version<3){
        /* units of mV, steps of 50 mV */
        rc = vreg_set_level(vreg, 2500);
        if (rc) {
            printk(KERN_ERR "%s: vreg set level failed (%d)\n",
                   __func__, rc);
            return;
        }
    }

    rc = vreg_enable(vreg);
    if (rc) {
        printk(KERN_ERR "%s: vreg enable failed (%d)\n",
		       __func__, rc);
        return;
    }
}
#endif //def CONFIG_MACH_ACER_A1

/* kgsl-3d0 begin */
static struct resource kgsl_3d0_resources[] = {
       {
		.name  = KGSL_3D0_REG_MEMORY,
		.start = 0xA0000000,
		.end = 0xA001ffff,
		.flags = IORESOURCE_MEM,
       },
       {
		.name = KGSL_3D0_IRQ,
		.start = INT_GRAPHICS,
		.end = INT_GRAPHICS,
		.flags = IORESOURCE_IRQ,
       },
};

static struct kgsl_device_platform_data kgsl_3d0_pdata = {
	.pwr_data = {
		.pwrlevel = {

			{
				.gpu_freq = 152000000,
				.bus_freq = 152000000,
			},
			{
				.gpu_freq = 0,
				.bus_freq = 128000000,
			},
		},
		.init_level = 0,
		.num_levels = 1,
		.set_grp_async = NULL,
		.idle_timeout = HZ/5,
	},
	.clk = {
		.name = {
			.clk = "grp_clk",
		},
	},
	.imem_clk_name = {
		.clk = "imem_clk",
	},
};

struct platform_device msm_kgsl_3d0 = {
       .name = "kgsl-3d0",
       .id = 0,
       .num_resources = ARRAY_SIZE(kgsl_3d0_resources),
       .resource = kgsl_3d0_resources,
	.dev = {
		.platform_data = &kgsl_3d0_pdata,
	},
};
/* kgsl-3d0 end */

static struct platform_device msm_device_pmic_leds = {
	.name	= "pmic-leds",
	.id	= -1,
};

#if defined(CONFIG_ACER_BATTERY)
static struct platform_device battery_device = {
	.name           = "acer-battery",
	.id             = 0,
};
#endif

/* TSIF begin */
#if defined(CONFIG_TSIF) || defined(CONFIG_TSIF_MODULE)

#define TSIF_A_SYNC      GPIO_CFG(106, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA)
#define TSIF_A_DATA      GPIO_CFG(107, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA)
#define TSIF_A_EN        GPIO_CFG(108, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA)
#define TSIF_A_CLK       GPIO_CFG(109, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA)

static const struct msm_gpio tsif_gpios[] = {
	{ .gpio_cfg = TSIF_A_CLK,  .label =  "tsif_clk", },
	{ .gpio_cfg = TSIF_A_EN,   .label =  "tsif_en", },
	{ .gpio_cfg = TSIF_A_DATA, .label =  "tsif_data", },
	{ .gpio_cfg = TSIF_A_SYNC, .label =  "tsif_sync", },
#if 0
	{ .gpio_cfg = 0, .label =  "tsif_error", },
	{ .gpio_cfg = 0, .label =  "tsif_null", },
#endif
};

static struct msm_tsif_platform_data tsif_platform_data = {
	.num_gpios = ARRAY_SIZE(tsif_gpios),
	.gpios = tsif_gpios,
};

#endif /* defined(CONFIG_TSIF) || defined(CONFIG_TSIF_MODULE) */
/* TSIF end   */

#ifdef CONFIG_QSD_SVS
#define TPS65023_MAX_DCDC1	1600
#else
#define TPS65023_MAX_DCDC1	CONFIG_QSD_PMIC_DEFAULT_DCDC1
#endif

static int qsd8x50_tps65023_set_dcdc1(int mVolts)
{
	int rc = 0;
#ifdef CONFIG_QSD_SVS
	rc = tps65023_set_dcdc1_level(mVolts);
	/* By default the TPS65023 will be initialized to 1.225V.
	 * So we can safely switch to any frequency within this
	 * voltage even if the device is not probed/ready.
	 */
	if (rc == -ENODEV && mVolts <= CONFIG_QSD_PMIC_DEFAULT_DCDC1)
		rc = 0;
#else
	/* Disallow frequencies not supported in the default PMIC
	 * output voltage.
	 */
	if (mVolts > CONFIG_QSD_PMIC_DEFAULT_DCDC1)
		rc = -EFAULT;
#endif
	return rc;
}

static struct msm_acpu_clock_platform_data qsd8x50_clock_data = {
	.acpu_switch_time_us = 20,
	.max_speed_delta_khz = 256000,
	.vdd_switch_time_us = 62,
	.max_vdd = TPS65023_MAX_DCDC1,
	.acpu_set_vdd = qsd8x50_tps65023_set_dcdc1,
};

#ifdef CONFIG_MOUSE_MSM_TOUCHPAD
static void touchpad_gpio_release(void)
{
	gpio_free(TOUCHPAD_IRQ);
	gpio_free(TOUCHPAD_SUSPEND);
}

static int touchpad_gpio_setup(void)
{
	int rc;
	int suspend_pin = TOUCHPAD_SUSPEND;
	int irq_pin = TOUCHPAD_IRQ;
	unsigned suspend_cfg =
		GPIO_CFG(suspend_pin, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA);
	unsigned irq_cfg =
		GPIO_CFG(irq_pin, 1, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA);

	rc = gpio_request(irq_pin, "msm_touchpad_irq");
	if (rc) {
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
		       irq_pin, rc);
		goto err_gpioconfig;
	}
	rc = gpio_request(suspend_pin, "msm_touchpad_suspend");
	if (rc) {
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
		       suspend_pin, rc);
		goto err_gpioconfig;
	}
	rc = gpio_tlmm_config(suspend_cfg, GPIO_ENABLE);
	if (rc) {
		pr_err("gpio_tlmm_config failed on pin %d (rc=%d)\n",
		       suspend_pin, rc);
		goto err_gpioconfig;
	}
	rc = gpio_tlmm_config(irq_cfg, GPIO_ENABLE);
	if (rc) {
		pr_err("gpio_tlmm_config failed on pin %d (rc=%d)\n",
		       irq_pin, rc);
		goto err_gpioconfig;
	}
	return rc;

err_gpioconfig:
	touchpad_gpio_release();
	return rc;
}

static struct msm_touchpad_platform_data msm_touchpad_data = {
	.gpioirq     = TOUCHPAD_IRQ,
	.gpiosuspend = TOUCHPAD_SUSPEND,
	.gpio_setup  = touchpad_gpio_setup,
	.gpio_shutdown = touchpad_gpio_release
};
#endif //def CONFIG_MOUSE_MSM_TOUCHPAD

#ifdef CONFIG_KEYBOARD_I2C_MSM
#define KBD_RST 35
#define KBD_IRQ 36

static void kbd_gpio_release(void)
{
	gpio_free(KBD_IRQ);
	gpio_free(KBD_RST);
}

static int kbd_gpio_setup(void)
{
	int rc;
	int respin = KBD_RST;
	int irqpin = KBD_IRQ;
	unsigned rescfg =
		GPIO_CFG(respin, 0, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA);
	unsigned irqcfg =
		GPIO_CFG(irqpin, 0, GPIO_INPUT, GPIO_NO_PULL, GPIO_2MA);

	rc = gpio_request(irqpin, "gpio_keybd_irq");
	if (rc) {
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
			irqpin, rc);
		goto err_gpioconfig;
	}
	rc = gpio_request(respin, "gpio_keybd_reset");
	if (rc) {
		pr_err("gpio_request failed on pin %d (rc=%d)\n",
			respin, rc);
		goto err_gpioconfig;
	}
	rc = gpio_tlmm_config(rescfg, GPIO_ENABLE);
	if (rc) {
		pr_err("gpio_tlmm_config failed on pin %d (rc=%d)\n",
			respin, rc);
		goto err_gpioconfig;
	}
	rc = gpio_tlmm_config(irqcfg, GPIO_ENABLE);
	if (rc) {
		pr_err("gpio_tlmm_config failed on pin %d (rc=%d)\n",
			irqpin, rc);
		goto err_gpioconfig;
	}
	return rc;

err_gpioconfig:
	kbd_gpio_release();
	return rc;
}

/* use gpio output pin to toggle keyboard external reset pin */
static void kbd_hwreset(int kbd_mclrpin)
{
	gpio_direction_output(kbd_mclrpin, 0);
	gpio_direction_output(kbd_mclrpin, 1);
}

static struct msm_i2ckbd_platform_data msm_kybd_data = {
	.hwrepeat = 0,
	.scanset1 = 1,
	.gpioreset = KBD_RST,
	.gpioirq = KBD_IRQ,
	.gpio_setup = kbd_gpio_setup,
	.gpio_shutdown = kbd_gpio_release,
	.hw_reset = kbd_hwreset,
};
#endif //def CONFIG_KEYBOARD_I2C_MSM

static struct yas529_platform_data yas529_pdata = {
        .reset_line = 23,
        .reset_asserted = 0,
};

static struct i2c_board_info msm_i2c_board_info[] __initdata = {
#ifdef CONFIG_MOUSE_MSM_TOUCHPAD
	{
		I2C_BOARD_INFO("glidesensor", 0x2A),
		.irq           =  MSM_GPIO_TO_INT(TOUCHPAD_IRQ),
		.platform_data = &msm_touchpad_data
	},
#endif
#ifdef CONFIG_KEYBOARD_I2C_MSM
	{
		I2C_BOARD_INFO("msm-i2ckbd", 0x3A),
		.type           = "msm-i2ckbd",
		.irq           =  MSM_GPIO_TO_INT(KBD_IRQ),
		.platform_data  = &msm_kybd_data
	},
#endif
#ifdef CONFIG_MT9D112
	{
		I2C_BOARD_INFO("mt9d112", 0x78 >> 1),
	},
#endif
#ifdef CONFIG_S5K3E2FX
	{
		I2C_BOARD_INFO("s5k3e2fx", 0x20 >> 1),
	},
#endif
#ifdef CONFIG_MT9P012
	{
		I2C_BOARD_INFO("mt9p012", 0x6C >> 1),
	},
#endif
#ifdef CONFIG_MT9P012_KM
	{
		I2C_BOARD_INFO("mt9p012_km", 0x6C >> 2),
	},
#endif
#if defined(CONFIG_MT9T013) || defined(CONFIG_SENSORS_MT9T013)
	{
		I2C_BOARD_INFO("mt9t013", 0x6C),
	},
#endif
#ifdef CONFIG_TPS65023
	{
		I2C_BOARD_INFO("tps65023", 0x48),
	},
#endif
#if defined(CONFIG_ACER_BATTERY)
	{
		I2C_BOARD_INFO("acer-battery", 0x55),
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AUO_H353)
	{
		I2C_BOARD_INFO("auo-touch", 0x5C),
		.irq           =  MSM_GPIO_TO_INT(108),
		.platform_data = &auo_ts_data,
	},
#endif
#if defined(CONFIG_AVR)
	{
		I2C_BOARD_INFO("avr", 0x66),
		.irq = MSM_GPIO_TO_INT(145),
	},
#endif
//#if defined(CONFIG_BOSCH_SMB380)
	{
		I2C_BOARD_INFO("smb380", 0x38),
		//.irq           =  MSM_GPIO_TO_INT(22),
	},
//#endif //defined(CONFIG_BOSCH_SMB380)
	{
                I2C_BOARD_INFO("yas529", 0x2e),
                .platform_data = &yas529_pdata,
        },
#if defined(CONFIG_SENSORS_ISL29018)
	{
		I2C_BOARD_INFO("isl29018", 0x44),
		.irq           =  MSM_GPIO_TO_INT(153),
	},
#endif //defined(CONFIG_SENSORS_ISL29018)
#if defined(CONFIG_LEDS_TCA6507)
	{
		I2C_BOARD_INFO("tca6507", 0x45),
	},
#endif
#if defined(CONFIG_AUDIO_FM2018)
	{
		I2C_BOARD_INFO("fm2018", 0x60),
	},
#endif
#if defined(CONFIG_AUDIO_TPA2018)
	{
		I2C_BOARD_INFO("tpa2018", 0x58),
	},
#endif
};

#ifdef CONFIG_MSM_CAMERA
static uint32_t camera_off_gpio_table[] = {
	/* parallel CAMERA interfaces */
	GPIO_CFG(0,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT0 */
	GPIO_CFG(1,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT1 */
	GPIO_CFG(2,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT2 */
	GPIO_CFG(3,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT3 */
	GPIO_CFG(4,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT4 */
	GPIO_CFG(5,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT5 */
	GPIO_CFG(6,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT6 */
	GPIO_CFG(7,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT7 */
	GPIO_CFG(8,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT8 */
	GPIO_CFG(9,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT9 */
	GPIO_CFG(10, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT10 */
	GPIO_CFG(11, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT11 */
	GPIO_CFG(12, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* PCLK */
	GPIO_CFG(13, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* HSYNC_IN */
	GPIO_CFG(14, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* VSYNC_IN */
	GPIO_CFG(15, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), /* MCLK */
};

static uint32_t camera_on_gpio_table[] = {
	/* parallel CAMERA interfaces */
	GPIO_CFG(0,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT0 */
	GPIO_CFG(1,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT1 */
	GPIO_CFG(2,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT2 */
	GPIO_CFG(3,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT3 */
	GPIO_CFG(4,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT4 */
	GPIO_CFG(5,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT5 */
	GPIO_CFG(6,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT6 */
	GPIO_CFG(7,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT7 */
	GPIO_CFG(8,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT8 */
	GPIO_CFG(9,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT9 */
	GPIO_CFG(10, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT10 */
	GPIO_CFG(11, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT11 */
	GPIO_CFG(12, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* PCLK */
	GPIO_CFG(13, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* HSYNC_IN */
	GPIO_CFG(14, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* VSYNC_IN */
	GPIO_CFG(15, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_16MA), /* MCLK */
};

static uint32_t camera_on_gpio_ffa_table[] = {
	/* parallel CAMERA interfaces */
	GPIO_CFG(95,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_16MA), /* I2C_SCL */
	GPIO_CFG(96,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_16MA), /* I2C_SDA */
	/* FFA front Sensor Reset */
	GPIO_CFG(137,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_16MA),
};

static uint32_t camera_off_gpio_ffa_table[] = {
	/* FFA front Sensor Reset */
	GPIO_CFG(137,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_16MA),
};

static void config_gpio_table(uint32_t *table, int len)
{
	int n, rc;
	for (n = 0; n < len; n++) {
		rc = gpio_tlmm_config(table[n], GPIO_ENABLE);
		if (rc) {
			printk(KERN_ERR "%s: gpio_tlmm_config(%#x)=%d\n",
				__func__, table[n], rc);
			break;
		}
	}
}

static struct vreg *vreg_gp2;
static struct vreg *vreg_gp3;

static void msm_camera_vreg_config(int vreg_en)
{
	int rc;

	if (vreg_gp2 == NULL) {
		vreg_gp2 = vreg_get(NULL, "gp2");
		if (IS_ERR(vreg_gp2)) {
			printk(KERN_ERR "%s: vreg_get(%s) failed (%ld)\n",
				__func__, "gp2", PTR_ERR(vreg_gp2));
			return;
		}

		rc = vreg_set_level(vreg_gp2, 1800);
		if (rc) {
			printk(KERN_ERR "%s: GP2 set_level failed (%d)\n",
				__func__, rc);
		}
	}

	if (vreg_gp3 == NULL) {
		vreg_gp3 = vreg_get(NULL, "gp3");
		if (IS_ERR(vreg_gp3)) {
			printk(KERN_ERR "%s: vreg_get(%s) failed (%ld)\n",
				__func__, "gp3", PTR_ERR(vreg_gp3));
			return;
		}

		rc = vreg_set_level(vreg_gp3, 2800);
		if (rc) {
			printk(KERN_ERR "%s: GP3 set level failed (%d)\n",
				__func__, rc);
		}
	}

	if (vreg_en) {
		rc = vreg_enable(vreg_gp2);
		if (rc) {
			printk(KERN_ERR "%s: GP2 enable failed (%d)\n",
				 __func__, rc);
		}

		rc = vreg_enable(vreg_gp3);
		if (rc) {
			printk(KERN_ERR "%s: GP3 enable failed (%d)\n",
				__func__, rc);
		}
	} else {
		rc = vreg_disable(vreg_gp2);
		if (rc) {
			printk(KERN_ERR "%s: GP2 disable failed (%d)\n",
				 __func__, rc);
		}

		rc = vreg_disable(vreg_gp3);
		if (rc) {
			printk(KERN_ERR "%s: GP3 disable failed (%d)\n",
				__func__, rc);
		}
	}
}

static int config_camera_on_gpios(void)
{
	int vreg_en = 1;

	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
		config_gpio_table(camera_on_gpio_ffa_table,
		ARRAY_SIZE(camera_on_gpio_ffa_table));

		msm_camera_vreg_config(vreg_en);
		gpio_set_value(137, 0);
	}
	config_gpio_table(camera_on_gpio_table,
		ARRAY_SIZE(camera_on_gpio_table));
	return 0;
}

static void config_camera_off_gpios(void)
{
	int vreg_en = 0;

	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
		config_gpio_table(camera_off_gpio_ffa_table,
		ARRAY_SIZE(camera_off_gpio_ffa_table));

		msm_camera_vreg_config(vreg_en);
	}
	config_gpio_table(camera_off_gpio_table,
		ARRAY_SIZE(camera_off_gpio_table));
}

static struct resource msm_camera_resources[] = {
	{
		.start	= 0xA0F00000,
		.end	= 0xA0F00000 + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_VFE,
		.end	= INT_VFE,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct msm_camera_device_platform_data msm_camera_device_data = {
	.camera_gpio_on  = config_camera_on_gpios,
	.camera_gpio_off = config_camera_off_gpios,
	.ioext.mdcphy = MSM_MDC_PHYS,
	.ioext.mdcsz  = MSM_MDC_SIZE,
	.ioext.appphy = MSM_CLK_CTL_PHYS,
	.ioext.appsz  = MSM_CLK_CTL_SIZE,
};

#ifndef CONFIG_MACH_ACER_A1
static struct msm_camera_sensor_flash_src msm_flash_src = {
	.flash_sr_type = MSM_CAMERA_FLASH_SRC_PMIC,
	._fsrc.pmic_src.low_current  = 30,
	._fsrc.pmic_src.high_current = 100,
};
#endif

#ifdef CONFIG_MT9D112
static struct msm_camera_sensor_flash_data flash_mt9d112 = {
	.flash_type = MSM_CAMERA_FLASH_LED,
	.flash_src  = &msm_flash_src
};

static struct msm_camera_sensor_info msm_camera_sensor_mt9d112_data = {
	.sensor_name    = "mt9d112",
	.sensor_reset   = 17,
	.sensor_pwd     = 85,
	.vcm_pwd        = 0,
	.vcm_enable     = 0,
	.pdata          = &msm_camera_device_data,
	.resource       = msm_camera_resources,
	.num_resources  = ARRAY_SIZE(msm_camera_resources),
	.flash_data     = &flash_mt9d112
};

static struct platform_device msm_camera_sensor_mt9d112 = {
	.name      = "msm_camera_mt9d112",
	.dev       = {
		.platform_data = &msm_camera_sensor_mt9d112_data,
	},
};
#endif

#ifdef CONFIG_S5K3E2FX
static struct msm_camera_sensor_flash_data flash_s5k3e2fx = {
	.flash_type = MSM_CAMERA_FLASH_LED,
	.flash_src  = &msm_flash_src
};

static struct msm_camera_sensor_info msm_camera_sensor_s5k3e2fx_data = {
	.sensor_name    = "s5k3e2fx",
	.sensor_reset   = 17,
	.sensor_pwd     = 85,
	/*.vcm_pwd = 31, */  /* CAM1_VCM_EN, enabled in a9 */
	.vcm_enable     = 0,
	.pdata          = &msm_camera_device_data,
	.resource       = msm_camera_resources,
	.num_resources  = ARRAY_SIZE(msm_camera_resources),
	.flash_data     = &flash_s5k3e2fx
};

static struct platform_device msm_camera_sensor_s5k3e2fx = {
	.name      = "msm_camera_s5k3e2fx",
	.dev       = {
		.platform_data = &msm_camera_sensor_s5k3e2fx_data,
	},
};
#endif

#ifdef CONFIG_MT9P012
#if defined(CONFIG_MACH_ACER_A1)
static struct msm_camera_sensor_flash_data flash_mt9p012 = {
	.flash_type = MSM_CAMERA_FLASH_NONE,
	.flash_src  = NULL
};

static struct msm_camera_sensor_info msm_camera_sensor_mt9p012_data = {
	.sensor_name    = "mt9p012",
	.sensor_reset   = 146,
	.sensor_pwd     = 94,
	.vcm_pwd        = 57,
	.vcm_enable     = 1,
	.pdata          = &msm_camera_device_data,
	.resource       = msm_camera_resources,
	.num_resources  = ARRAY_SIZE(msm_camera_resources),
	.flash_data     = &flash_mt9p012
};
#else
static struct msm_camera_sensor_flash_data flash_mt9p012 = {
	.flash_type = MSM_CAMERA_FLASH_LED,
	.flash_src  = &msm_flash_src
};

static struct msm_camera_sensor_info msm_camera_sensor_mt9p012_data = {
	.sensor_name    = "mt9p012",
	.sensor_reset   = 17,
	.sensor_pwd     = 85,
	.vcm_pwd        = 88,
	.vcm_enable     = 0,
	.pdata          = &msm_camera_device_data,
	.resource       = msm_camera_resources,
	.num_resources  = ARRAY_SIZE(msm_camera_resources),
	.flash_data     = &flash_mt9p012
};
#endif
static struct platform_device msm_camera_sensor_mt9p012 = {
	.name      = "msm_camera_mt9p012",
	.dev       = {
		.platform_data = &msm_camera_sensor_mt9p012_data,
	},
};
#endif

#ifdef CONFIG_MT9P012_KM
static struct msm_camera_sensor_flash_data flash_mt9p012_km = {
	.flash_type = MSM_CAMERA_FLASH_LED,
	.flash_src  = &msm_flash_src
};

static struct msm_camera_sensor_info msm_camera_sensor_mt9p012_km_data = {
	.sensor_name    = "mt9p012_km",
	.sensor_reset   = 17,
	.sensor_pwd     = 85,
	.vcm_pwd        = 88,
	.vcm_enable     = 0,
	.pdata          = &msm_camera_device_data,
	.resource       = msm_camera_resources,
	.num_resources  = ARRAY_SIZE(msm_camera_resources),
	.flash_data     = &flash_mt9p012_km
};

static struct platform_device msm_camera_sensor_mt9p012_km = {
	.name      = "msm_camera_mt9p012_km",
	.dev       = {
		.platform_data = &msm_camera_sensor_mt9p012_km_data,
	},
};
#endif

#ifdef CONFIG_MT9T013
static struct msm_camera_sensor_flash_data flash_mt9t013 = {
	.flash_type = MSM_CAMERA_FLASH_LED,
	.flash_src  = &msm_flash_src
};

static struct msm_camera_sensor_info msm_camera_sensor_mt9t013_data = {
	.sensor_name    = "mt9t013",
	.sensor_reset   = 17,
	.sensor_pwd     = 85,
	.vcm_pwd        = 0,
	.vcm_enable     = 0,
	.pdata          = &msm_camera_device_data,
	.resource       = msm_camera_resources,
	.num_resources  = ARRAY_SIZE(msm_camera_resources),
	.flash_data     = &flash_mt9t013
};

static struct platform_device msm_camera_sensor_mt9t013 = {
	.name      = "msm_camera_mt9t013",
	.dev       = {
		.platform_data = &msm_camera_sensor_mt9t013_data,
	},
};
#endif
#endif /*CONFIG_MSM_CAMERA*/

#ifdef CONFIG_BATTERY_MSM
static u32 msm_calculate_batt_capacity(u32 current_voltage);

static struct msm_psy_batt_pdata msm_psy_batt_data = {
	.voltage_min_design 	= 3200,
	.voltage_max_design	= 4200,
	.avail_chg_sources   	= AC_CHG | USB_CHG ,
	.batt_technology        = POWER_SUPPLY_TECHNOLOGY_LION,
	.calculate_capacity	= &msm_calculate_batt_capacity,
};

static u32 msm_calculate_batt_capacity(u32 current_voltage)
{
	u32 low_voltage   = msm_psy_batt_data.voltage_min_design;
	u32 high_voltage  = msm_psy_batt_data.voltage_max_design;

	return (current_voltage - low_voltage) * 100
		/ (high_voltage - low_voltage);
}

static struct platform_device msm_batt_device = {
	.name 		    = "msm-battery",
	.id		    = -1,
	.dev.platform_data  = &msm_psy_batt_data,
};
#endif //def #ifdef CONFIG_BATTERY_MSM

static int hsusb_rpc_connect(int connect)
{
	if (connect)
		return msm_hsusb_rpc_connect();
	else
		return msm_hsusb_rpc_close();
}

static struct msm_otg_platform_data msm_otg_pdata = {
	.rpc_connect	= hsusb_rpc_connect,
	.phy_reset	= msm_hsusb_native_phy_reset,
	.pmic_notif_init         = msm_pm_app_rpc_init,
	.pmic_notif_deinit       = msm_pm_app_rpc_deinit,
	.pmic_register_vbus_sn   = msm_pm_app_register_vbus_sn,
	.pmic_unregister_vbus_sn = msm_pm_app_unregister_vbus_sn,
	.pmic_enable_ldo         = msm_pm_app_enable_usb_ldo,
};

static struct msm_hsusb_gadget_platform_data msm_gadget_pdata;

static struct platform_device *devices[] __initdata = {
	&msm_fb_device,
#ifdef CONFIG_FB_MSM_MDDI_TOSHIBA_WVGA
	&mddi_toshiba_device,
#endif
#ifdef CONFIG_SMC91X
	&smc91x_device,
#endif
#ifdef CONFIG_BLK_DEV_IDE_S1R72V05
	&s1r72v05_device,
#endif
	&msm_device_smd,
	&msm_device_dmov,
	&android_pmem_kernel_ebi1_device,
#ifdef CONFIG_KERNEL_PMEM_SMI_REGION
	&android_pmem_kernel_smi_device,
#endif
	&android_pmem_device,
	&android_pmem_adsp_device,
	&android_pmem_smipool_device,
	&msm_device_nand,
	&msm_device_i2c,
#ifdef CONFIG_QSD_SPI
	&qsd_device_spi,
#endif
	&usb_mass_storage_device,
	&msm_device_tssc,
	&msm_audio_device,
	&msm_device_uart_dm1,
#ifdef CONFIG_MACH_ACER_A1
	&msm_device_uart_dm2,
#endif
	&msm_bluesleep_device,
#ifdef CONFIG_BT
	&msm_bt_power_device,
#endif
#if defined(CONFIG_MMC_WIFI) || defined(CONFIG_MMC_WIFI_MODULE)
	&msm_wifi_power_device,
#endif
#if !defined(CONFIG_MSM_SERIAL_DEBUGGER)
	&msm_device_uart3,
#endif
	&msm_device_pmic_leds,
	&msm_kgsl_3d0,
#ifdef CONFIG_MSM_RPCSERVER_HANDSET
	&hs_device,
#endif
#if defined(CONFIG_TSIF) || defined(CONFIG_TSIF_MODULE)
	&msm_device_tsif,
#endif
#ifdef CONFIG_MT9T013
	&msm_camera_sensor_mt9t013,
#endif
#ifdef CONFIG_MT9D112
	&msm_camera_sensor_mt9d112,
#endif
#ifdef CONFIG_S5K3E2FX
	&msm_camera_sensor_s5k3e2fx,
#endif
#ifdef CONFIG_MT9P012
	&msm_camera_sensor_mt9p012,
#endif
#ifdef CONFIG_MT9P012_KM
	&msm_camera_sensor_mt9p012_km,
#endif
#ifdef CONFIG_BATTERY_MSM
	&msm_batt_device,
#endif
#if defined(CONFIG_ACER_BATTERY)
	&battery_device,
#endif
#if defined(CONFIG_ACER_HEADSET_BUTT)
	&hs_butt_device,
#endif
};

static void __init qsd8x50_init_irq(void)
{
	msm_init_irq();
	msm_init_sirc();
}

/*static void kgsl_phys_memory_init(void)
{
	request_mem_region(kgsl_resources[1].start,
		resource_size(&kgsl_resources[1]), "kgsl");
}*/

static void __init qsd8x50_init_usb(void)
{
	hs_clk = clk_get(NULL, "usb_hs_clk");
	if (IS_ERR(hs_clk)) {
		printk(KERN_ERR "%s: hs_clk clk get failed\n", __func__);
		return;
	}

	phy_clk = clk_get(NULL, "usb_phy_clk");
	if (IS_ERR(phy_clk)) {
		printk(KERN_ERR "%s: phy_clk clk get failed\n", __func__);
		return;
	}

#ifdef CONFIG_USB_MSM_OTG_72K
	platform_device_register(&msm_device_otg);
#endif

#ifdef CONFIG_USB_FUNCTION_MSM_HSUSB
	platform_device_register(&msm_device_hsusb_peripheral);
#endif

#ifdef CONFIG_USB_MSM_72K
	platform_device_register(&msm_device_gadget_peripheral);
#endif

#ifdef CONFIG_USB_EHCI_MSM
	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa())
		return;

	vreg_usb = vreg_get(NULL, "boost");

	if (IS_ERR(vreg_usb)) {
		printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg_usb));
		return;
	}

	platform_device_register(&msm_device_hsusb_otg);
	msm_add_host(0, &msm_usb_host_pdata);
#ifdef CONFIG_USB_FS_HOST
	if (fsusb_gpio_init())
		return;
	msm_add_host(1, &msm_usb_host2_pdata);
#endif
#endif //def CONFIG_USB_EHCI_MSM
}

static struct vreg *vreg_mmc;

#if (defined(CONFIG_MMC_MSM_SDC1_SUPPORT)\
	|| defined(CONFIG_MMC_MSM_SDC2_SUPPORT)\
	|| defined(CONFIG_MMC_MSM_SDC3_SUPPORT)\
	|| defined(CONFIG_MMC_MSM_SDC4_SUPPORT))

struct sdcc_gpio {
	struct msm_gpio *cfg_data;
	uint32_t size;
};

static struct msm_gpio sdc1_cfg_data[] = {
	{GPIO_CFG(51, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc1_dat_3"},
	{GPIO_CFG(52, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc1_dat_2"},
	{GPIO_CFG(53, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc1_dat_1"},
	{GPIO_CFG(54, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc1_dat_0"},
	{GPIO_CFG(55, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc1_cmd"},
	{GPIO_CFG(56, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_8MA), "sdc1_clk"},
};

static struct msm_gpio sdc2_cfg_data[] = {
	{GPIO_CFG(62, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_8MA), "sdc2_clk"},
	{GPIO_CFG(63, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc2_cmd"},
	{GPIO_CFG(64, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc2_dat_3"},
	{GPIO_CFG(65, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc2_dat_2"},
	{GPIO_CFG(66, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc2_dat_1"},
	{GPIO_CFG(67, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc2_dat_0"},
};

#ifdef CONFIG_MMC_MSM_SDC3_SUPPORT
static struct msm_gpio sdc3_cfg_data[] = {
	{GPIO_CFG(88, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_8MA), "sdc3_clk"},
	{GPIO_CFG(89, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_cmd"},
	{GPIO_CFG(90, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_3"},
	{GPIO_CFG(91, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_2"},
	{GPIO_CFG(92, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_1"},
	{GPIO_CFG(93, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_0"},

#ifdef CONFIG_MMC_MSM_SDC3_8_BIT_SUPPORT
	{GPIO_CFG(158, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_4"},
	{GPIO_CFG(159, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_5"},
	{GPIO_CFG(160, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_6"},
	{GPIO_CFG(161, 1, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc3_dat_7"},
#endif
};
#endif

#ifdef CONFIG_MMC_MSM_SDC4_SUPPORT
static struct msm_gpio sdc4_cfg_data[] = {
	{GPIO_CFG(142, 3, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_8MA), "sdc4_clk"},
	{GPIO_CFG(143, 3, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc4_cmd"},
	{GPIO_CFG(144, 2, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc4_dat_0"},
	{GPIO_CFG(145, 2, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc4_dat_1"},
	{GPIO_CFG(146, 3, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc4_dat_2"},
	{GPIO_CFG(147, 3, GPIO_OUTPUT, GPIO_PULL_UP, GPIO_8MA), "sdc4_dat_3"},
};
#endif

static struct sdcc_gpio sdcc_cfg_data[] = {
	{
		.cfg_data = sdc1_cfg_data,
		.size = ARRAY_SIZE(sdc1_cfg_data),
	},
	{
		.cfg_data = sdc2_cfg_data,
		.size = ARRAY_SIZE(sdc2_cfg_data),
	},
#ifdef CONFIG_MMC_MSM_SDC3_SUPPORT
	{
		.cfg_data = sdc3_cfg_data,
		.size = ARRAY_SIZE(sdc3_cfg_data),
	},
#endif
#ifdef CONFIG_MMC_MSM_SDC4_SUPPORT
	{
		.cfg_data = sdc4_cfg_data,
		.size = ARRAY_SIZE(sdc4_cfg_data),
	},
#endif
};

#if defined(CONFIG_MACH_ACER_A1)

#define A1_GPIO_SDCARD_DETECT 37
static unsigned int SDMMC_status(struct device *dev)
{
	if(!gpio_get_value(A1_GPIO_SDCARD_DETECT))
		return 1;
	else
		return 0;
}

static void __init sd2p85_init(void)
{
	struct vreg *vreg;
	int rc;

	/* GP6 */
	vreg = vreg_get(NULL, "gp6");
	if (IS_ERR(vreg)) {
		printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg));
		return;
	}

	/* units of mV, steps of 50 mV */
	rc = vreg_set_level(vreg, 2850);
	if (rc) {
		printk(KERN_ERR "%s: vreg set level failed (%d)\n",
		       __func__, rc);
		return;
	}

	rc = vreg_enable(vreg);
	if (rc) {
		printk(KERN_ERR "%s: vreg enable failed (%d)\n",
		       __func__, rc);
		return;
	}

	/* SD2P85 */
	vreg = vreg_get(NULL, "rftx");
	if (IS_ERR(vreg)) {
		printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg));
		return;
	}

	/* units of mV, steps of 50 mV */
	rc = vreg_set_level(vreg, 2850);
	if (rc) {
		printk(KERN_ERR "%s: vreg set level failed (%d)\n",
		       __func__, rc);
		return;
	}

	rc = vreg_enable(vreg);
	if (rc) {
		printk(KERN_ERR "%s: vreg enable failed (%d)\n",
		       __func__, rc);
		return;
	}

	if (gpio_request(A1_GPIO_SDCARD_DETECT, "sdc1_card_detect")) {
		pr_err("failed to request gpio sdc1_card_detect\n");
	} else {
		gpio_tlmm_config(GPIO_CFG(A1_GPIO_SDCARD_DETECT, 0, GPIO_INPUT,
			GPIO_NO_PULL, GPIO_2MA), GPIO_ENABLE);
	}
}

static void __init mmc_init(void)
{
	struct vreg *vreg;
	int rc;

	vreg = vreg_get(NULL, "mmc");
	if (IS_ERR(vreg)) {
		printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg));
		return;
	}

	rc = vreg_enable(vreg);
	if (rc) {
		printk(KERN_ERR "%s: vreg enable failed (%d)\n",
		       __func__, rc);
		return;
	}
}
#endif //defined(CONFIG_MACH_ACER_A1)

static unsigned long vreg_sts, gpio_sts;

static void msm_sdcc_setup_gpio(int dev_id, unsigned int enable)
{
	int rc = 0;
	struct sdcc_gpio *curr;

	curr = &sdcc_cfg_data[dev_id - 1];
	if (!(test_bit(dev_id, &gpio_sts)^enable))
		return;

	if (enable) {
		set_bit(dev_id, &gpio_sts);
		rc = msm_gpios_request_enable(curr->cfg_data, curr->size);
		if (rc)
			printk(KERN_ERR "%s: Failed to turn on GPIOs for slot %d\n",
				__func__,  dev_id);
	} else {
		clear_bit(dev_id, &gpio_sts);
		msm_gpios_disable_free(curr->cfg_data, curr->size);
	}
}

static uint32_t msm_sdcc_setup_power(struct device *dv, unsigned int vdd)
{
#if !defined(CONFIG_MACH_ACER_A1)
	int rc = 0;
#endif
	struct platform_device *pdev;

	pdev = container_of(dv, struct platform_device, dev);
	msm_sdcc_setup_gpio(pdev->id, !!vdd);

#if !defined(CONFIG_MACH_ACER_A1)
	if (vdd == 0) {
		if (!vreg_sts)
			return 0;

		clear_bit(pdev->id, &vreg_sts);

		if (!vreg_sts) {
			rc = vreg_disable(vreg_mmc);
			if (rc)
				printk(KERN_ERR "%s: return val: %d \n",
					__func__, rc);
		}
		return 0;
	}

	if (!vreg_sts) {
		rc = vreg_set_level(vreg_mmc, PMIC_VREG_GP6_LEVEL);
		if (!rc)
			rc = vreg_enable(vreg_mmc);
		if (rc)
			printk(KERN_ERR "%s: return val: %d \n",
					__func__, rc);
	}
#endif
	set_bit(pdev->id, &vreg_sts);
	return 0;
}

#endif

static int msm_sdcc_get_wpswitch(struct device *dv)
{
	void __iomem *wp_addr = 0;
	uint32_t ret = 0;
	struct platform_device *pdev;

	if (!(machine_is_qsd8x50_surf() || machine_is_qsd8x50a_surf()))
		return -1;

	pdev = container_of(dv, struct platform_device, dev);

	wp_addr = ioremap(FPGA_SDCC_STATUS, 4);
	if (!wp_addr) {
		pr_err("%s: Could not remap %x\n", __func__, FPGA_SDCC_STATUS);
		return -ENOMEM;
	}

	ret = (readl(wp_addr) >> ((pdev->id - 1) << 1)) & (0x03);
	pr_info("%s: WP/CD Status for Slot %d = 0x%x \n", __func__,
							pdev->id, ret);
	iounmap(wp_addr);
	return ((ret == 0x02) ? 1 : 0);

}

#ifdef CONFIG_MMC_MSM_SDC1_SUPPORT
static struct mmc_platform_data qsd8x50_sdc1_data = {
	.ocr_mask	= MMC_VDD_27_28 | MMC_VDD_28_29,
	.translate_vdd	= msm_sdcc_setup_power,
	.mmc_bus_width  = MMC_CAP_4_BIT_DATA,
	.wpswitch	= msm_sdcc_get_wpswitch,
#ifdef CONFIG_MMC_MSM_SDC1_DUMMY52_REQUIRED
	.dummy52_required = 1,
#endif
#if defined(CONFIG_MACH_ACER_A1)
	.status_irq = MSM_GPIO_TO_INT(A1_GPIO_SDCARD_DETECT),
	.status = SDMMC_status,
	.irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
#endif
};
#endif

#if defined(CONFIG_MMC_WIFI) || defined(CONFIG_MMC_WIFI_MODULE)
static struct mmc_platform_data qsd8x50_sdcc2_wifi = {
    .ocr_mask = MMC_VDD_20_21, //MMC_VDD_27_28 | MMC_VDD_28_29,
    .translate_vdd = msm_sdcc_setup_power,
    .register_status_notify = wifi_status_register,
    .embedded_sdio = &bcm_wifi_emb_data,
    .mmc_bus_width  = MMC_CAP_4_BIT_DATA,
};
#endif

#ifdef CONFIG_MMC_MSM_SDC2_SUPPORT
static struct mmc_platform_data qsd8x50_sdc2_data = {
	.ocr_mask       = MMC_VDD_20_21,//MMC_VDD_27_28 | MMC_VDD_28_29,
	.translate_vdd  = msm_sdcc_setup_power,
	.mmc_bus_width  = MMC_CAP_4_BIT_DATA,
	.wpswitch	= msm_sdcc_get_wpswitch,
#ifdef CONFIG_MMC_MSM_SDC2_DUMMY52_REQUIRED
	.dummy52_required = 1,
#endif
};
#endif

#ifdef CONFIG_MMC_MSM_SDC3_SUPPORT
static struct mmc_platform_data qsd8x50_sdc3_data = {
	.ocr_mask       = MMC_VDD_27_28 | MMC_VDD_28_29,
	.translate_vdd  = msm_sdcc_setup_power,
#ifdef CONFIG_MMC_MSM_SDC3_8_BIT_SUPPORT
	.mmc_bus_width  = MMC_CAP_8_BIT_DATA,
#else
	.mmc_bus_width  = MMC_CAP_4_BIT_DATA,
#endif
#ifdef CONFIG_MMC_MSM_SDC3_DUMMY52_REQUIRED
	.dummy52_required = 1,
#endif
};
#endif

#ifdef CONFIG_MMC_MSM_SDC4_SUPPORT
static struct mmc_platform_data qsd8x50_sdc4_data = {
	.ocr_mask       = MMC_VDD_27_28 | MMC_VDD_28_29,
	.translate_vdd  = msm_sdcc_setup_power,
	.mmc_bus_width  = MMC_CAP_4_BIT_DATA,
	.wpswitch	= msm_sdcc_get_wpswitch,
#ifdef CONFIG_MMC_MSM_SDC4_DUMMY52_REQUIRED
	.dummy52_required = 1,
#endif
};
#endif

static void __init qsd8x50_init_mmc(void)
{
#if !defined (CONFIG_MACH_ACER_A1)
	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa())
		vreg_mmc = vreg_get(NULL, "gp6");
	else
		vreg_mmc = vreg_get(NULL, "gp5");
#else
	/* A1 device uses gp6 */
	vreg_mmc = vreg_get(NULL, "gp6");
#endif

	if (IS_ERR(vreg_mmc)) {
		printk(KERN_ERR "%s: vreg get failed (%ld)\n",
		       __func__, PTR_ERR(vreg_mmc));
		return;
	}

#ifdef CONFIG_MMC_MSM_SDC1_SUPPORT
	msm_add_sdcc(1, &qsd8x50_sdc1_data);
#endif

#if defined(CONFIG_MMC_MSM_SDC2_SUPPORT) && ( defined(CONFIG_MMC_WIFI) || defined(CONFIG_MMC_WIFI_MODULE) ) && defined(CONFIG_MACH_ACER_A1)
	msm_add_sdcc(2, &qsd8x50_sdcc2_wifi);
#endif

	if (machine_is_qsd8x50_surf() || machine_is_qsd8x50a_surf()) {
#ifdef CONFIG_MMC_MSM_SDC2_SUPPORT
		msm_add_sdcc(2, &qsd8x50_sdc2_data);
#endif
#ifdef CONFIG_MMC_MSM_SDC3_SUPPORT
		msm_add_sdcc(3, &qsd8x50_sdc3_data);
#endif
#ifdef CONFIG_MMC_MSM_SDC4_SUPPORT
		msm_add_sdcc(4, &qsd8x50_sdc4_data);
#endif
	}

}

#ifdef CONFIG_SMC91X
static void __init qsd8x50_cfg_smc91x(void)
{
	int rc = 0;

	if (machine_is_qsd8x50_surf() || machine_is_qsd8x50a_surf()) {
		smc91x_resources[0].start = 0x70000300;
		smc91x_resources[0].end = 0x700003ff;
		smc91x_resources[1].start = MSM_GPIO_TO_INT(156);
		smc91x_resources[1].end = MSM_GPIO_TO_INT(156);
	} else if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa()) {
		smc91x_resources[0].start = 0x84000300;
		smc91x_resources[0].end = 0x840003ff;
		smc91x_resources[1].start = MSM_GPIO_TO_INT(87);
		smc91x_resources[1].end = MSM_GPIO_TO_INT(87);

		rc = gpio_tlmm_config(GPIO_CFG(87, 0, GPIO_INPUT,
					       GPIO_PULL_DOWN, GPIO_2MA),
					       GPIO_ENABLE);
		if (rc) {
			printk(KERN_ERR "%s: gpio_tlmm_config=%d\n",
					__func__, rc);
		}
	} else
		printk(KERN_ERR "%s: invalid machine type\n", __func__);
}
#endif

static struct msm_pm_platform_data msm_pm_data[MSM_PM_SLEEP_MODE_NR] = {
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE].supported = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE].suspend_enabled = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE].idle_enabled = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE].latency = 8594,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE].residency = 23740,

	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_NO_XO_SHUTDOWN].supported = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_NO_XO_SHUTDOWN].suspend_enabled = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_NO_XO_SHUTDOWN].idle_enabled = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_NO_XO_SHUTDOWN].latency = 4594,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_NO_XO_SHUTDOWN].residency = 23740,

	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE].supported = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE].suspend_enabled = 0,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE].idle_enabled = 1,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE].latency = 500,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE].residency = 6000,

	[MSM_PM_SLEEP_MODE_RAMP_DOWN_AND_WAIT_FOR_INTERRUPT].supported = 1,
	[MSM_PM_SLEEP_MODE_RAMP_DOWN_AND_WAIT_FOR_INTERRUPT].suspend_enabled
		= 1,
	[MSM_PM_SLEEP_MODE_RAMP_DOWN_AND_WAIT_FOR_INTERRUPT].idle_enabled = 0,
	[MSM_PM_SLEEP_MODE_RAMP_DOWN_AND_WAIT_FOR_INTERRUPT].latency = 443,
	[MSM_PM_SLEEP_MODE_RAMP_DOWN_AND_WAIT_FOR_INTERRUPT].residency = 1098,

	[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT].supported = 1,
	[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT].suspend_enabled = 1,
	[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT].idle_enabled = 1,
	[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT].latency = 2,
	[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT].residency = 0,
};

static void msm_i2c_gpio_config(int iface, int config_type)
{
	int gpio_scl;
	int gpio_sda;
	if (iface) {
		gpio_scl = 60;
		gpio_sda = 61;
	} else {
		gpio_scl = 95;
		gpio_sda = 96;
	}
	if (config_type) {
		gpio_tlmm_config(GPIO_CFG(gpio_scl, 1, GPIO_INPUT,
					GPIO_NO_PULL, GPIO_16MA), GPIO_ENABLE);
		gpio_tlmm_config(GPIO_CFG(gpio_sda, 1, GPIO_INPUT,
					GPIO_NO_PULL, GPIO_16MA), GPIO_ENABLE);
	} else {
		gpio_tlmm_config(GPIO_CFG(gpio_scl, 0, GPIO_OUTPUT,
					GPIO_NO_PULL, GPIO_16MA), GPIO_ENABLE);
		gpio_tlmm_config(GPIO_CFG(gpio_sda, 0, GPIO_OUTPUT,
					GPIO_NO_PULL, GPIO_16MA), GPIO_ENABLE);
	}
}

static struct msm_i2c_platform_data msm_i2c_pdata = {
	.clk_freq = 100000,
	.rsl_id = SMEM_SPINLOCK_I2C,
	.pri_clk = 95,
	.pri_dat = 96,
	.aux_clk = 60,
	.aux_dat = 61,
	.msm_i2c_config_gpio = msm_i2c_gpio_config,
};

static void __init msm_device_i2c_init(void)
{
	if (gpio_request(95, "i2c_pri_clk"))
		pr_err("failed to request gpio i2c_pri_clk\n");
	if (gpio_request(96, "i2c_pri_dat"))
		pr_err("failed to request gpio i2c_pri_dat\n");
	if (gpio_request(60, "i2c_sec_clk"))
		pr_err("failed to request gpio i2c_sec_clk\n");
	if (gpio_request(61, "i2c_sec_dat"))
		pr_err("failed to request gpio i2c_sec_dat\n");

	msm_i2c_pdata.rmutex = 1;
	msm_i2c_pdata.pm_lat =
		msm_pm_data[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_NO_XO_SHUTDOWN]
		.latency;
	msm_device_i2c.dev.platform_data = &msm_i2c_pdata;
}

static unsigned pmem_kernel_ebi1_size = PMEM_KERNEL_EBI1_SIZE;
static int __init pmem_kernel_ebi1_size_setup(char *p)
{
	pmem_kernel_ebi1_size = memparse(p, NULL);
	return 0;
}
early_param("pmem_kernel_ebi1_size", pmem_kernel_ebi1_size_setup);

#ifdef CONFIG_KERNEL_PMEM_SMI_REGION
static unsigned pmem_kernel_smi_size = MSM_PMEM_SMIPOOL_SIZE;
static int __init pmem_kernel_smi_size_setup(char *p)
{
	pmem_kernel_smi_size = memparse(p, NULL);

	/* Make sure that we don't allow more SMI memory then is
	   available - the kernel mapping code has no way of knowing
	   if it has gone over the edge */

	if (pmem_kernel_smi_size > MSM_PMEM_SMIPOOL_SIZE)
		pmem_kernel_smi_size = MSM_PMEM_SMIPOOL_SIZE;
	return 0;
}
early_param("pmem_kernel_smi_size", pmem_kernel_smi_size_setup);
#endif

static unsigned pmem_sf_size = MSM_PMEM_SF_SIZE;
static int __init pmem_sf_size_setup(char *p)
{
	pmem_sf_size = memparse(p, NULL);
	return 0;
}
early_param("pmem_sf_size", pmem_sf_size_setup);

static unsigned pmem_adsp_size = MSM_PMEM_ADSP_SIZE;
static int __init pmem_adsp_size_setup(char *p)
{
	pmem_adsp_size = memparse(p, NULL);
	return 0;
}
early_param("pmem_adsp_size", pmem_adsp_size_setup);

static unsigned audio_size = MSM_AUDIO_SIZE;
static int __init audio_size_setup(char *p)
{
	audio_size = memparse(p, NULL);
	return 0;
}
early_param("audio_size", audio_size_setup);

static void __init qsd8x50_init(void)
{
	if (socinfo_init() < 0)
		printk(KERN_ERR "%s: socinfo_init() failed!\n",
		       __func__);
#ifdef CONFIG_SMC91X
	qsd8x50_cfg_smc91x();
#endif
	msm_acpu_clock_init(&qsd8x50_clock_data);

	msm_hsusb_pdata.swfi_latency =
		msm_pm_data
		[MSM_PM_SLEEP_MODE_RAMP_DOWN_AND_WAIT_FOR_INTERRUPT].latency;
	msm_device_hsusb_peripheral.dev.platform_data = &msm_hsusb_pdata;

	msm_gadget_pdata.swfi_latency =
		msm_pm_data
		[MSM_PM_SLEEP_MODE_RAMP_DOWN_AND_WAIT_FOR_INTERRUPT].latency;
	msm_device_otg.dev.platform_data = &msm_otg_pdata;
	msm_device_gadget_peripheral.dev.platform_data = &msm_gadget_pdata;

#if defined(CONFIG_TSIF) || defined(CONFIG_TSIF_MODULE)
	msm_device_tsif.dev.platform_data = &tsif_platform_data;
#endif
	platform_add_devices(devices, ARRAY_SIZE(devices));
	msm_fb_add_devices();
#ifdef CONFIG_MSM_CAMERA
	config_camera_off_gpios(); /* might not be necessary */
#endif
	qsd8x50_init_usb();
#if defined(CONFIG_MACH_ACER_A1)
	sd2p85_init();
	mmc_init();
	wlan_init();
#endif
	qsd8x50_init_mmc();
	bt_power_init();
#if defined(CONFIG_MMC_WIFI) || defined(CONFIG_MMC_WIFI_MODULE)
	wifi_power_init();
#endif
	audio_gpio_init();
	msm_device_i2c_init();
#ifdef CONFIG_SPI
	msm_qsd_spi_init();
#endif
#if defined(CONFIG_AVR) || defined(CONFIG_TOUCHSCREEN_AUO_H353)
	avr_gpio_init();
#endif
#if defined(CONFIG_MS3C)
	compass_gpio_init();
#endif
	i2c_register_board_info(0, msm_i2c_board_info,
				ARRAY_SIZE(msm_i2c_board_info));
#ifdef CONFIG_SPI
	spi_register_board_info(msm_spi_board_info,
				ARRAY_SIZE(msm_spi_board_info));
#endif
	msm_pm_set_platform_data(msm_pm_data);
	//kgsl_phys_memory_init();

#ifdef CONFIG_SURF_FFA_GPIO_KEYPAD
	if (machine_is_qsd8x50_ffa() || machine_is_qsd8x50a_ffa())
		platform_device_register(&keypad_device_8k_ffa);
	else
		platform_device_register(&keypad_device_surf);
#endif
}

static void __init qsd8x50_allocate_memory_regions(void)
{
	void *addr;
	unsigned long size;

	size = pmem_kernel_ebi1_size;
	if (size) {
		addr = alloc_bootmem_aligned(size, 0x100000);
		android_pmem_kernel_ebi1_pdata.start = __pa(addr);
		android_pmem_kernel_ebi1_pdata.size = size;
		pr_info("allocating %lu bytes at %p (%lx physical) for kernel"
			" ebi1 pmem arena\n", size, addr, __pa(addr));
	}

#ifdef CONFIG_KERNEL_PMEM_SMI_REGION
	size = pmem_kernel_smi_size;
	if (size > MSM_PMEM_SMIPOOL_SIZE) {
		printk(KERN_ERR "pmem kernel smi arena size %lu is too big\n",
			size);

		size = MSM_PMEM_SMIPOOL_SIZE;
	}

	android_pmem_kernel_smi_pdata.start = MSM_PMEM_SMIPOOL_BASE;
	android_pmem_kernel_smi_pdata.size = size;

	pr_info("allocating %lu bytes at %lx (%lx physical)"
		"for pmem kernel smi arena\n", size,
		(long unsigned int) MSM_PMEM_SMIPOOL_BASE,
		__pa(MSM_PMEM_SMIPOOL_BASE));
#endif

	size = pmem_sf_size;
	if (size) {
		addr = alloc_bootmem(size);
		android_pmem_pdata.start = __pa(addr);
		android_pmem_pdata.size = size;
		pr_info("allocating %lu bytes at %p (%lx physical) for sf "
			"pmem arena\n", size, addr, __pa(addr));
	}

	size = pmem_adsp_size;
	if (size) {
		addr = alloc_bootmem(size);
		android_pmem_adsp_pdata.start = __pa(addr);
		android_pmem_adsp_pdata.size = size;
		pr_info("allocating %lu bytes at %p (%lx physical) for adsp "
			"pmem arena\n", size, addr, __pa(addr));
	}


	size = MSM_FB_SIZE;
	addr = (void *)MSM_FB_BASE;
	msm_fb_resources[0].start = (unsigned long)addr;
	msm_fb_resources[0].end = msm_fb_resources[0].start + size - 1;
	pr_info("using %lu bytes of SMI at %lx physical for fb\n",
	       size, (unsigned long)addr);

	size = audio_size ? : MSM_AUDIO_SIZE;
	addr = alloc_bootmem(size);
	msm_audio_resources[0].start = __pa(addr);
	msm_audio_resources[0].end = msm_audio_resources[0].start + size - 1;
	pr_info("allocating %lu bytes at %p (%lx physical) for audio\n",
		size, addr, __pa(addr));
}

static void __init qsd8x50_map_io(void)
{
	msm_shared_ram_phys = MSM_SHARED_RAM_PHYS;
	msm_map_qsd8x50_io();
	qsd8x50_allocate_memory_regions();
	msm_clock_init(msm_clocks_8x50, msm_num_clocks_8x50);
}

#if defined(CONFIG_MACH_ACER_A1)
MACHINE_START(ACER_A1, "salsa")
#ifdef CONFIG_MSM_DEBUG_UART
	.phys_io  = MSM_DEBUG_UART_PHYS,
	.io_pg_offst = ((MSM_DEBUG_UART_BASE) >> 18) & 0xfffc,
#endif
	.boot_params = 0x20000100,
	.map_io = qsd8x50_map_io,
	.init_irq = qsd8x50_init_irq,
	.init_machine = qsd8x50_init,
	.timer = &msm_timer,
MACHINE_END
#endif //defined(CONFIG_MACH_ACER_A1)

#if defined(CONFIG_MACH_QSD8X50_SURF)
MACHINE_START(QSD8X50_SURF, "QCT QSD8X50 SURF")
#ifdef CONFIG_MSM_DEBUG_UART
	.phys_io  = MSM_DEBUG_UART_PHYS,
	.io_pg_offst = ((MSM_DEBUG_UART_BASE) >> 18) & 0xfffc,
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = qsd8x50_map_io,
	.init_irq = qsd8x50_init_irq,
	.init_machine = qsd8x50_init,
	.timer = &msm_timer,
MACHINE_END
#endif

#if defined(CONFIG_MACH_QSD8X50_FFA)
MACHINE_START(QSD8X50_FFA, "QCT QSD8X50 FFA")
#ifdef CONFIG_MSM_DEBUG_UART
	.phys_io  = MSM_DEBUG_UART_PHYS,
	.io_pg_offst = ((MSM_DEBUG_UART_BASE) >> 18) & 0xfffc,
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = qsd8x50_map_io,
	.init_irq = qsd8x50_init_irq,
	.init_machine = qsd8x50_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(QSD8X50A_SURF, "QCT QSD8X50A SURF")
#ifdef CONFIG_MSM_DEBUG_UART
	.phys_io  = MSM_DEBUG_UART_PHYS,
	.io_pg_offst = ((MSM_DEBUG_UART_BASE) >> 18) & 0xfffc,
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = qsd8x50_map_io,
	.init_irq = qsd8x50_init_irq,
	.init_machine = qsd8x50_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(QSD8X50A_FFA, "QCT QSD8X50A FFA")
#ifdef CONFIG_MSM_DEBUG_UART
	.phys_io  = MSM_DEBUG_UART_PHYS,
	.io_pg_offst = ((MSM_DEBUG_UART_BASE) >> 18) & 0xfffc,
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = qsd8x50_map_io,
	.init_irq = qsd8x50_init_irq,
	.init_machine = qsd8x50_init,
	.timer = &msm_timer,
MACHINE_END
#endif


/*
 * Copyright © 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "brcmnand.h"

struct iproc_nand_soc {
	struct brcmnand_soc soc;

	void __iomem *idm_base;
	void __iomem *ext_base;
	spinlock_t idm_lock;
};

#define IPROC_NAND_CTLR_READY_OFFSET       0x10
#define IPROC_NAND_CTLR_READY              BIT(0)

#define IPROC_NAND_IO_CTRL_OFFSET          0x00
#define IPROC_NAND_APB_LE_MODE             BIT(24)
#define IPROC_NAND_INT_CTRL_READ_ENABLE    BIT(6)

static bool iproc_nand_intc_ack(struct brcmnand_soc *soc)
{
	struct iproc_nand_soc *priv =
			container_of(soc, struct iproc_nand_soc, soc);
	void __iomem *mmio = priv->ext_base + IPROC_NAND_CTLR_READY_OFFSET;
	u32 val = brcmnand_readl(mmio);

	if (val & IPROC_NAND_CTLR_READY) {
		brcmnand_writel(IPROC_NAND_CTLR_READY, mmio);
		return true;
	}

	return false;
}

static u32 iproc_nand_ecc_mips_reg_read(struct brcmnand_soc *soc, u32 offset)
{
	struct iproc_nand_soc *priv =
			container_of(soc, struct iproc_nand_soc, soc);
	void __iomem *mmio = priv->ext_base + offset;
	u32 val = brcmnand_readl(mmio);

	return val;
}

static void iproc_nand_ecc_mips_reg_write(struct brcmnand_soc *soc, u32 offset, u32 value)
{
	struct iproc_nand_soc *priv =
			container_of(soc, struct iproc_nand_soc, soc);
	void __iomem *mmio = priv->ext_base + offset;

	brcmnand_writel(value, mmio);
}

static void iproc_nand_intc_set(struct brcmnand_soc *soc, bool en)
{
	struct iproc_nand_soc *priv =
			container_of(soc, struct iproc_nand_soc, soc);
	void __iomem *mmio = priv->idm_base + IPROC_NAND_IO_CTRL_OFFSET;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&priv->idm_lock, flags);

	val = brcmnand_readl(mmio);

	if (en)
		val |= IPROC_NAND_INT_CTRL_READ_ENABLE;
	else
		val &= ~IPROC_NAND_INT_CTRL_READ_ENABLE;

	brcmnand_writel(val, mmio);

	spin_unlock_irqrestore(&priv->idm_lock, flags);
}

static void iproc_nand_apb_access(struct brcmnand_soc *soc, bool prepare)
{
	struct iproc_nand_soc *priv =
			container_of(soc, struct iproc_nand_soc, soc);
	void __iomem *mmio = priv->idm_base + IPROC_NAND_IO_CTRL_OFFSET;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&priv->idm_lock, flags);

	val = brcmnand_readl(mmio);

	if (prepare)
		val |= IPROC_NAND_APB_LE_MODE;
	else
		val &= ~IPROC_NAND_APB_LE_MODE;

	brcmnand_writel(val, mmio);

	spin_unlock_irqrestore(&priv->idm_lock, flags);
}

static int iproc_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iproc_nand_soc *priv;
	struct brcmnand_soc *soc;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	soc = &priv->soc;

	spin_lock_init(&priv->idm_lock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iproc-idm");
	priv->idm_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->idm_base))
		return PTR_ERR(priv->idm_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iproc-ext");
	priv->ext_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->ext_base))
		return PTR_ERR(priv->ext_base);

	soc->ctlrdy_ack = iproc_nand_intc_ack;
	soc->ctlrdy_set_enabled = iproc_nand_intc_set;
	soc->prepare_data_bus = iproc_nand_apb_access;
	soc->read_ecc_mips_reg = iproc_nand_ecc_mips_reg_read;
	soc->write_ecc_mips_reg = iproc_nand_ecc_mips_reg_write;

	return brcmnand_probe(pdev, soc);
}

static const struct of_device_id iproc_nand_of_match[] = {
	{ .compatible = "brcm,nand-iproc" },
	{},
};
MODULE_DEVICE_TABLE(of, iproc_nand_of_match);

static struct platform_driver iproc_nand_driver = {
	.probe			= iproc_nand_probe,
	.remove			= brcmnand_remove,
	.driver = {
		.name		= "iproc_nand",
		.pm		= &brcmnand_pm_ops,
		.of_match_table	= iproc_nand_of_match,
	}
};
module_platform_driver(iproc_nand_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Brian Norris");
MODULE_AUTHOR("Ray Jui");
MODULE_DESCRIPTION("NAND driver for Broadcom IPROC-based SoCs");

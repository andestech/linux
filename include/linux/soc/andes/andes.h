/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Andes Technology Corporation.
 */

#ifndef __ANDES_ANDES_H
#define __ANDES_ANDES_H

DECLARE_STATIC_KEY_FALSE(andes_legacy_mmu);
DECLARE_STATIC_KEY_FALSE(andes_ppma);

DECLARE_STATIC_KEY_FALSE(andes_pfn_msb_key);
extern phys_addr_t andes_pfn_msb;

#endif /* !__ANDES_ANDES_H */

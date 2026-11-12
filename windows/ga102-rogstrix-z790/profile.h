/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
#include "vendor/vbios_610.h"
BOOLEAN GuGa102AssetsValid(VOID);
BOOLEAN GuGa102PromMatches(const VOID *, UINTN);
BOOLEAN GuGa102FwsecMatches(const struct gpu_unlock_fwsec *);
/* Prepare is read-only on the GPU. It validates ROM, assets, exact PCI/card
 * state and creates the immutable selector latch. No driver release here. */
EFI_STATUS GuGa102Prepare(VOID);
BOOLEAN GuGa102ColdEntry(VOID);

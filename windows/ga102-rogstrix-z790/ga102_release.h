/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
EFI_STATUS GuGa102Preflight(EFI_SYSTEM_TABLE *,EFI_HANDLE,EFI_HANDLE);
EFI_STATUS GuGa102Release(VOID);
BOOLEAN GuGa102Released(VOID);
EFI_STATUS GuGa102Finish(VOID);
BOOLEAN GuGa102ReadyForWindows(VOID);
BOOLEAN GuGa102Mutated(VOID);

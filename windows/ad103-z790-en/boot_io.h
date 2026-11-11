/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "port.h"
EFI_STATUS GuBootOpen(EFI_HANDLE,EFI_SYSTEM_TABLE *);
EFI_STATUS GuBootLoadAssets(VOID);
EFI_STATUS GuBootLoadWindows(VOID);
EFI_STATUS GuBootChoose(BOOLEAN *);
EFI_STATUS GuBootWindows(VOID);
EFI_STATUS GuBootClose(VOID);
VOID GuBootSay(const CHAR16 *);
VOID GuBootSilence(VOID);
VOID GuBootHalt(VOID);
/* v0.5.4: diagnostics log only while the USB log is open (GuLog faults otherwise). */
BOOLEAN GuBootLogOpen(VOID);
/* Added for auto-unlock mode (2026-11-10): arm/disarm the firmware's own
 * watchdog as a safety net around the silent-screen transaction window, so
 * an unattended run that truly hangs resets to normal Windows instead of
 * sitting frozen forever with nobody watching. GuBootOpen() already disarms
 * the platform's own default watchdog on entry; these re-arm/disarm it only
 * around the risky window -- see app.c's UefiMain and boot_io.c's header. */
EFI_STATUS GuBootArmWatchdog(UINTN Seconds);
EFI_STATUS GuBootDisarmWatchdog(VOID);

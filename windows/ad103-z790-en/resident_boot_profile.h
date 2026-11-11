/* SPDX-License-Identifier: GPL-2.0-only */
/* Keep the local resident-partition identifier out of the public source tree.
 * Same pattern as boot_profile.h -> local/boot_profile.h. */
#pragma once
#if defined(GU_HOST_TEST_PROFILE)
#include "resident_boot_profile.example.h"
#else
#include "local/resident_boot_profile.h"
#ifndef GU_RESIDENT_PARTITION_INITIALIZER
#error "Configure the reviewed resident partition GUID in local/resident_boot_profile.h before building GU_RESIDENT_BOOT."
#endif
#endif

/* SPDX-License-Identifier: GPL-2.0-only */
/* Shape only -- see local/resident_boot_profile.h (git-ignored) for the real,
 * machine-specific values. Captured live from this board's own resident
 * unlock partition via `Get-Partition`, never copied from another board.
 * Same pattern as boot_profile.example.h / GU_WINDOWS_PARTITION_INITIALIZER. */
#define GU_RESIDENT_PARTITION_INITIALIZER {0x11111111,0x2222,0x3333,{0x84,0x44,0x55,0x66,0x77,0x88,0x99,0xaa}}
#define GU_RESIDENT_PARTITION_NUMBER 1
#define GU_RESIDENT_PARTITION_START 0
#define GU_RESIDENT_PARTITION_SIZE 0

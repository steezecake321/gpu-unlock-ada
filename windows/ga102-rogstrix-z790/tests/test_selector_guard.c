/* SPDX-License-Identifier: GPL-2.0-only */
/* Host-only regression tests for the GA102 SS1 phase guard and post-fault probe.
 * The mocked PCI I/O callbacks never access physical hardware. */
#include "port.h"
#include <stdio.h>
#include <string.h>

#define SS0_OFFSET 0x82381cU
#define SS1_OFFSET 0x823820U
#define ENTRY_SS0  0x12100152U
#define ENTRY_SS1  0x00000007U
#define UNLOCK_SS0 0x88888888U
#define UNLOCK_SS1 0x00000008U
#define BAR_SIZE   0x00900000ULL

static EFI_PCI_IO_PROTOCOL MockPci;
static UINT32 MockChipId;
static UINT32 MockSs0;
static UINT32 MockSs1;
static UINT32 MockPciIdentity;
static UINTN MemReadCalls;
static UINTN MemWriteCalls;
static UINTN ConfigReadCalls;
static BOOLEAN FailBarRead;
static BOOLEAN FailConfigRead;

static int fail(const char *message, int line)
{
  fprintf(stderr, "FAIL line %d: %s\n", line, message);
  return 1;
}
#define CHECK(c, m) do { if (!(c)) return fail((m), __LINE__); } while (0)

static EFI_STATUS EFIAPI mock_mem_read(
  EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width,
  UINT8 BarIndex, UINT64 Offset, UINTN Count, VOID *Buffer)
{
  UINT32 value;
  if (This != &MockPci || Width != EfiPciIoWidthUint32 || BarIndex != 0 ||
      Count != 1 || Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  MemReadCalls++;
  if (FailBarRead)
    return EFI_DEVICE_ERROR;
  switch (Offset) {
    case 0: value = MockChipId; break;
    case SS0_OFFSET: value = MockSs0; break;
    case SS1_OFFSET: value = MockSs1; break;
    default: return EFI_UNSUPPORTED;
  }
  *(UINT32 *)Buffer = value;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_mem_write(
  EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width,
  UINT8 BarIndex, UINT64 Offset, UINTN Count, VOID *Buffer)
{
  UINT32 value;
  if (This != &MockPci || Width != EfiPciIoWidthUint32 || BarIndex != 0 ||
      Count != 1 || Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  MemWriteCalls++;
  value = *(UINT32 *)Buffer;
  switch (Offset) {
    case SS0_OFFSET: MockSs0 = value; break;
    case SS1_OFFSET: MockSs1 = value; break;
    default: return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_config_read(
  EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width,
  UINT32 Offset, UINTN Count, VOID *Buffer)
{
  if (This != &MockPci || Width != EfiPciIoWidthUint32 || Offset != 0 ||
      Count != 1 || Buffer == NULL)
    return EFI_INVALID_PARAMETER;
  ConfigReadCalls++;
  if (FailConfigRead)
    return EFI_DEVICE_ERROR;
  *(UINT32 *)Buffer = MockPciIdentity;
  return EFI_SUCCESS;
}

static int setup(void)
{
  memset(&MockPci, 0, sizeof(MockPci));
  MockPci.Mem.Read = mock_mem_read;
  MockPci.Mem.Write = mock_mem_write;
  MockPci.Pci.Read = mock_config_read;
  MockChipId = 0xb72000a1U;
  MockSs0 = ENTRY_SS0;
  MockSs1 = ENTRY_SS1;
  MockPciIdentity = 0x220310deU;
  MemReadCalls = MemWriteCalls = ConfigReadCalls = 0;
  FailBarRead = FailConfigRead = FALSE;

  GuInit(NULL, &MockPci);
  Gu.bar_size = BAR_SIZE;
  Gu.profile_verified = TRUE;
  Gu.armed = TRUE;
  Gu.owned = TRUE;
  Gu.display_ok = TRUE;
  Gu.prom_verified = TRUE;
  CHECK(GuLatchEntrySelectors(ENTRY_SS0, ENTRY_SS1),
        "entry selector values should latch for the mock device");
  return 0;
}

static int test_required_phase_order(void)
{
  CHECK(setup() == 0, "mock setup failed");

  GuWrite32(SS0_OFFSET, UNLOCK_SS0);
  CHECK(!Gu.fault && MockSs0 == UNLOCK_SS0 && MemWriteCalls == 1,
        "SS0 write should reach the mocked PCI I/O callback");
  CHECK(GuSelectorPhase(GU_SELECTOR_PHASE_SS0_WRITTEN),
        "SS0_WRITTEN phase should validate the SS0 readback");
  CHECK(GuSelectorPhase(GU_SELECTOR_PHASE_SS1_WRITE),
        "SS1_WRITE phase should be permitted only after SS0_WRITTEN");

  GuWrite32(SS1_OFFSET, UNLOCK_SS1);
  CHECK(!Gu.fault && MockSs1 == UNLOCK_SS1 && MemWriteCalls == 2,
        "authorized SS1 write should reach the mocked PCI I/O callback");
  CHECK(GuSelectorPhase(GU_SELECTOR_PHASE_TARGET),
        "TARGET phase should validate the final SS1 readback");
  CHECK(!Gu.fault, "valid selector sequence must not latch a fault");
  return 0;
}

static int test_ss1_rejected_without_phase(void)
{
  CHECK(setup() == 0, "mock setup failed");
  GuWrite32(SS1_OFFSET, UNLOCK_SS1);
  CHECK(Gu.fault, "SS1 write without phase authorization must latch a fault");
  CHECK(MockSs1 == ENTRY_SS1 && MemWriteCalls == 0,
        "rejected SS1 write must not reach the hardware callback");
  return 0;
}

static int test_wrong_phase_rejected(void)
{
  CHECK(setup() == 0, "mock setup failed");
  CHECK(!GuSelectorPhase(GU_SELECTOR_PHASE_SS1_WRITE),
        "skipping SS0_WRITTEN must be rejected");
  CHECK(Gu.fault, "invalid phase order must latch a fault");
  CHECK(MemWriteCalls == 0, "invalid phase order must not write a selector");
  return 0;
}

static int test_diagnostic_probe_after_fault(void)
{
  UINT32 chip = 0, pciid = 0;
  EFI_STATUS chip_status = EFI_SUCCESS, pci_status = EFI_SUCCESS;
  UINTN reads_before;

  CHECK(setup() == 0, "mock setup failed");
  Gu.fault = TRUE;
  reads_before = MemReadCalls;
  CHECK(GuRead32(0) == 0xffffffffU,
        "normal MMIO wrapper must remain fail-closed after a fault");
  CHECK(MemReadCalls == reads_before,
        "normal MMIO wrapper must not call hardware after a fault");

  GuDiagProbe(&chip, &chip_status, &pciid, &pci_status);
  CHECK(chip_status == EFI_SUCCESS && chip == MockChipId,
        "diagnostic probe should sample BAR0 PMC_BOOT_0 after a latched fault");
  CHECK(pci_status == EFI_SUCCESS && pciid == MockPciIdentity,
        "diagnostic probe should sample PCI identity after a latched fault");
  CHECK(MemReadCalls == reads_before + 1 && ConfigReadCalls == 1,
        "diagnostic probe should perform exactly the two fixed read-only samples");
  CHECK(Gu.fault, "diagnostic probe must not clear the transaction fault");
  return 0;
}

static int test_diagnostic_probe_reports_io_errors(void)
{
  UINT32 chip = 0, pciid = 0;
  EFI_STATUS chip_status = EFI_SUCCESS, pci_status = EFI_SUCCESS;

  CHECK(setup() == 0, "mock setup failed");
  Gu.fault = TRUE;
  FailBarRead = TRUE;
  FailConfigRead = TRUE;
  GuDiagProbe(&chip, &chip_status, &pciid, &pci_status);
  CHECK(EFI_ERROR(chip_status) && EFI_ERROR(pci_status),
        "diagnostic probe must preserve actual PCI I/O errors");
  CHECK(chip == 0xffffffffU && pciid == 0xffffffffU,
        "failed diagnostic outputs should remain explicit all-ones sentinels");
  CHECK(Gu.fault, "diagnostic I/O errors must not clear or rewrite the latched fault");
  return 0;
}

int main(void)
{
  if (test_required_phase_order() ||
      test_ss1_rejected_without_phase() ||
      test_wrong_phase_rejected() ||
      test_diagnostic_probe_after_fault() ||
      test_diagnostic_probe_reports_io_errors())
    return 1;
  puts("selector guard tests: 5 passed; mocked PCI I/O only; no hardware access");
  return 0;
}

/* The production archive leaves this logging callback to its caller. */
VOID GuLog(const char *Format, ...) {(void)Format;}

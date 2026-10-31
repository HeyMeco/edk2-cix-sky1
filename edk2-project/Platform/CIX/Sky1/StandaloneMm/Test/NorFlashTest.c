/** @file
  Host test of NorFlashStandaloneMm (SpiNor.c, NorFlashFvb.c) against a
  simulated SPI NOR behind OpteeSpNorTransaction(): programming only clears
  bits, PAGE PROGRAM and SECTOR ERASE need WRITE ENABLE, an 8 MiB part
  ignores the upper address bits.  Run by scripts/open/test_stmm_nor.py.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define MDEPKG_NDEBUG 1
#include "NorFlashStandaloneMm.h"

EFI_GUID gEfiSystemNvDataFvGuid = { 0xfff12b8d, 0x7696, 0x4c8b, { 0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50 } };
EFI_GUID gEfiAuthenticatedVariableGuid = { 0xaaf32c78, 0x947b, 0x439a, { 0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92 } };
EFI_GUID gEfiVariableGuid = { 0xddcf3616, 0x3275, 0x4164, { 0x98, 0xb6, 0xfe, 0x85, 0x70, 0x7f, 0xfe, 0x7d } };

// ---- library stubs ----
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN L) { return memmove (D, S, L); }
VOID *EFIAPI SetMem (VOID *B, UINTN L, UINT8 V) { return memset (B, V, L); }
VOID *EFIAPI ZeroMem (VOID *B, UINTN L) { return memset (B, 0, L); }
INTN EFIAPI CompareMem (CONST VOID *A, CONST VOID *B, UINTN L) { return memcmp (A, B, L); }
BOOLEAN EFIAPI CompareGuid (CONST GUID *A, CONST GUID *B) { return memcmp (A, B, sizeof (GUID)) == 0; }
GUID *EFIAPI CopyGuid (GUID *D, CONST GUID *S) { memcpy (D, S, sizeof (GUID)); return D; }
VOID *EFIAPI AllocatePool (UINTN S) { return malloc (S); }
VOID *EFIAPI AllocateZeroPool (UINTN S) { return calloc (1, S); }
VOID EFIAPI FreePool (VOID *B) { free (B); }
UINT16 EFIAPI CalculateSum16 (CONST UINT16 *B, UINTN L) { UINT16 s = 0; for (UINTN i = 0; i < L / 2; i++) s += B[i]; return s; }
UINT16 EFIAPI CalculateCheckSum16 (CONST UINT16 *B, UINTN L) { return (UINT16)(0x10000 - CalculateSum16 (B, L)); }
UINT64 EFIAPI LShiftU64 (UINT64 V, UINTN C) { return V << C; }

// ---- simulated flash ----
#define FLASH_SIZE (8 * 1024 * 1024)
static uint8_t flash[FLASH_SIZE];
static int wel, ops, failnext = -1;
static uint32_t jedec = 0xEF6017;

EFI_STATUS EFIAPI OpteeSpConsoleWrite (CONST CHAR8 *S, UINTN L) { return EFI_SUCCESS; }

EFI_STATUS EFIAPI OpteeSpNorTransaction (CONST CIX_SPI_NOR_TRANSACTION *T)
{
  uint8_t *buf = (uint8_t *)(uintptr_t)T->DataBuffer;
  uint32_t a = (uint32_t)T->Address % FLASH_SIZE;   // an 8 MiB part ignores A23
  ops++;
  if (failnext >= 0 && failnext-- == 0) return EFI_DEVICE_ERROR;
  switch (T->Opcode) {
  case 0x9F: buf[0] = jedec >> 16; buf[1] = jedec >> 8; buf[2] = jedec; return 0;
  case 0x05: buf[0] = 0; return 0;
  case 0x35: buf[0] = 0; return 0;
  case 0x06: wel = 1; return 0;
  case 0x04: wel = 0; return 0;
  case 0x03:
    if (T->AddrBytes != 3 || T->DataDir != SPI_NOR_DATA_IN) abort ();
    for (uint32_t i = 0; i < T->DataBytes; i++) buf[i] = flash[(a + i) % FLASH_SIZE];
    return 0;
  case 0x02:
    if (!wel || T->DataDir != SPI_NOR_DATA_OUT || T->DataBytes > 256) abort ();
    if ((a & 0xff) + T->DataBytes > 256) abort ();  // must not wrap in a page
    for (uint32_t i = 0; i < T->DataBytes; i++) flash[a + i] &= buf[i];
    wel = 0; return 0;
  case 0x20:
    if (!wel || (a & 0xfff)) abort ();
    memset (flash + a, 0xff, 4096); wel = 0; return 0;
  case 0x31: if (!wel) abort (); wel = 0; return 0;
  }
  printf ("unexpected opcode %02x\n", T->Opcode); abort ();
}

EFI_STATUS SpiNorInit (VOID);
UINTN SpiNorDeviceSize (VOID);

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf ("FAIL %s:%d: ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); fails++; } } while (0)

static NOR_FLASH_INSTANCE *mk (uint8_t **ram, NVRAM_INFORMATION_HOB_DATA *n)
{
  NOR_FLASH_INSTANCE *I = calloc (1, sizeof (*I));
  UINTN total = n->VariableSize + n->FtwWorkingSize + n->FtwSpareSize;
  *ram = malloc (total);
  I->Signature = NOR_FLASH_SIGNATURE;
  I->Size = SpiNorDeviceSize ();
  I->Media.BlockSize = 4096;
  I->Media.LastBlock = I->Size / 4096 - 1;
  I->ShadowBuffer = malloc (4096);
  I->RegionBaseAddress = (UINTN)*ram;
  I->StartLba = n->VariableBase / 4096;
  I->Blocks = total / 4096;
  I->FvbProtocol.GetAttributes = FvbGetAttributes; I->FvbProtocol.SetAttributes = FvbSetAttributes;
  I->FvbProtocol.GetPhysicalAddress = FvbGetPhysicalAddress; I->FvbProtocol.GetBlockSize = FvbGetBlockSize;
  I->FvbProtocol.Read = FvbRead; I->FvbProtocol.Write = FvbWrite; I->FvbProtocol.EraseBlocks = FvbEraseBlocks;
  SpiNorRead (n->VariableBase, total, *ram);
  return I;
}

int main (void)
{
  NVRAM_INFORMATION_HOB_DATA n = { 0x388000, 0x388000 + 0x28000, 0x388000 + 0x50000, 0x28000, 0x28000, 0x28000 };
  UINTN total = 0x78000, base = 0x388000;
  uint8_t *ram, *model;
  srand (1);
  for (UINTN i = 0; i < FLASH_SIZE; i++) flash[i] = rand ();     // garbage store
  CHECK (SpiNorInit () == 0, "init");
  CHECK (SpiNorDeviceSize () == 8 * 1024 * 1024, "size %lx", (unsigned long)SpiNorDeviceSize ());
  NOR_FLASH_INSTANCE *I = mk (&ram, &n);
  uint8_t outside_before[0x1000]; memcpy (outside_before, flash + base - 0x1000, 0x1000);
  uint8_t outside_after[0x1000]; memcpy (outside_after, flash + base + total, 0x1000);

  // 1. format a garbage store
  CHECK (NorFlashFvbInitialize (I, &n) == 0, "format");
  EFI_FIRMWARE_VOLUME_HEADER *fv = (void *)ram;
  CHECK (fv->Signature == EFI_FVH_SIGNATURE && fv->FvLength == total, "fv header");
  CHECK (memcmp (ram, flash + base, total) == 0, "RAM copy equals flash after format");
  for (UINTN i = 0x100; i < total; i++) if (ram[i] != 0xff && i >= sizeof (EFI_FIRMWARE_VOLUME_HEADER) + sizeof (EFI_FV_BLOCK_MAP_ENTRY) + sizeof (VARIABLE_STORE_HEADER)) { CHECK (0, "not erased at %lx", (unsigned long)i); break; }
  CHECK (memcmp (outside_before, flash + base - 0x1000, 0x1000) == 0 && memcmp (outside_after, flash + base + total, 0x1000) == 0, "flash outside the region untouched");

  // 2. a valid store is kept
  NOR_FLASH_INSTANCE *I2 = mk (&ram, &n);
  int before = ops;
  CHECK (NorFlashFvbInitialize (I2, &n) == 0, "validate");
  CHECK (ops == before, "valid store: no flash access (%d ops)", ops - before);
  I = mk (&ram, &n);

  // 3. random FVB writes, erases and reads against a model
  model = malloc (total); memcpy (model, flash + base, total);
  for (int it = 0; it < 20000; it++) {
    UINTN lba = rand () % (total / 4096), off = rand () % 4096, len = 1 + rand () % 600;
    int op = rand () % 10;
    if (op < 6) {
      uint8_t data[4096]; for (UINTN i = 0; i < len; i++) data[i] = rand ();
      // Half of the writes only clear bits (programmed in place)
      if (rand () % 2) for (UINTN i = 0; i < len && off + i < 4096; i++) data[i] &= model[lba * 4096 + off + i];
      UINTN nb = len;
      EFI_STATUS s = FvbWrite (&I->FvbProtocol, lba, off, &nb, data);
      UINTN expect = off + len > 4096 ? 4096 - off : len;
      CHECK (nb == expect, "write len %lu expect %lu", (unsigned long)nb, (unsigned long)expect);
      CHECK (s == (off + len > 4096 ? EFI_BAD_BUFFER_SIZE : EFI_SUCCESS), "write status %lx", (unsigned long)s);
      memcpy (model + lba * 4096 + off, data, nb);
    } else if (op < 8) {
      UINTN cnt = 1 + rand () % 3; if (lba + cnt > total / 4096) cnt = total / 4096 - lba;
      CHECK (FvbEraseBlocks (&I->FvbProtocol, (EFI_LBA)lba, cnt, EFI_LBA_LIST_TERMINATOR) == 0, "erase");
      memset (model + lba * 4096, 0xff, cnt * 4096);
    } else {
      uint8_t got[4096]; UINTN nb = len;
      EFI_STATUS s = FvbRead (&I->FvbProtocol, lba, off, &nb, got);
      CHECK (s == (off + len > 4096 ? EFI_BAD_BUFFER_SIZE : EFI_SUCCESS), "read status");
      CHECK (memcmp (got, model + lba * 4096 + off, nb) == 0, "read data at lba %lu off %lu", (unsigned long)lba, (unsigned long)off);
    }
    if (memcmp (model, flash + base, total) || memcmp (model, ram, total)) { CHECK (0, "flash/RAM/model differ after op %d it %d", op, it); break; }
  }
  CHECK (memcmp (outside_before, flash + base - 0x1000, 0x1000) == 0 && memcmp (outside_after, flash + base + total, 0x1000) == 0, "flash outside the region untouched after random ops");

  // 4. bounds
  UINTN nb = 16; uint8_t b[16];
  CHECK (FvbWrite (&I->FvbProtocol, total / 4096, 0, &nb, b) == EFI_BAD_BUFFER_SIZE && nb == 0, "write past the volume refused");
  CHECK (FvbEraseBlocks (&I->FvbProtocol, (EFI_LBA)(total / 4096 - 1), (UINTN)2, EFI_LBA_LIST_TERMINATOR) == EFI_INVALID_PARAMETER, "erase past the volume refused");
  CHECK (NorFlashWrite (I, FLASH_SIZE / 4096, 0, 1, b) == EFI_INVALID_PARAMETER, "write past the chip refused");
  CHECK (SpiNorRead (16 * 1024 * 1024 - 2, 4, b) == EFI_INVALID_PARAMETER, "read past 16 MiB refused");

  // 5. a failing transaction mid-write leaves the RAM copy equal to the flash
  uint8_t data[4096];
  for (int k = 0; k < 40; k++) {
    for (UINTN i = 0; i < 4096; i++) data[i] = rand ();
    failnext = 1 + rand () % 40;
    UINTN nb2 = 4096;
    FvbWrite (&I->FvbProtocol, 5, 0, &nb2, data);
    failnext = -1;
    if (memcmp (ram, flash + base, total)) { CHECK (0, "RAM copy differs from flash after a failed write (%d)", k); break; }
  }

  printf ("%s (%d failures, %d flash ops)\n", fails ? "FAILED" : "all ok", fails, ops);
  return fails != 0;
}

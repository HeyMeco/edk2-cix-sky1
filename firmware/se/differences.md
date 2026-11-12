# How the open SE firmware differs from CIX's

Every way this firmware differs from CIX's SE firmware that a user, the
OS or another firmware stage could observe, or that matters for trust.
The reference is CIX's debug build 2.0.2026W01.01 (`debug/se_fw/se_fw.elf`,
pinned in [manifests/cix-debug-firmware.json](../../manifests/cix-debug-firmware.json));
the firmware the boards ship is a later release build (2.0.2026W31.01)
without symbols, which is not compared. Addresses of the form `0x2000…` are
in that debug ELF.

The evidence is the differential test, `sky1emu se-test`
([emu/src/se](../../emu/src/se), [emu/README.md](../../emu/README.md)):
both images run on the same model of the SoC, in step, and everything one
does that the other does not is either listed in the test's table of
expected differences (`EXPECTED` in
[test.rs](../../emu/src/se/test.rs), each entry with its reason) or a
failure. `emu/test-se.sh` runs all of it. A phase of the test is named
below in *italics*.

**Nothing here has run on hardware.** The open SE has only been emulated;
the model is built from what the two images do, not from the SoC's
documentation. What only the hardware can show is at the end.

## What is the same

With the same flash image, eFuses and inputs, both images write the same
values to the same registers in the same order, read the same registers
the same number of times, leave the same contents in DRAM, in the PM's
SRAM and in the sensor hub's SRAM, send the same messages and end in the
same state, in all of the following (but for the differences listed
after):

* the boot from reset to the idle loop (*boot*, *debug service*, *idle*):
  2039 writes to 844 registers, 292 registers read, with the DDR driver's
  entry points stubbed; and with the DDR driver live on the model of the
  controllers (`--ddr`: 51 634 writes to 9759 registers on the O6) for the
  flash images of the O6, the O6N, the Orange Pi 6 and the 6 Plus (the
  MS-R1's memory configuration is format 1.6, which both images refuse
  with the same assertion);
* 11 boot reasons, 8 life cycles with and without keys, 14 flash JEDEC
  IDs, 4 clock settings, 24 damaged pieces of the flash image, the second
  flash copy's strap (`--inputs`);
* 14 PLLs that never lock, a PM that does not answer, answers late or
  with another word or asks for the sensor hub at boot, a sensor hub that
  does not start, the audio subsystem not powering up (`--failures`);
* every request of the run-time interface, about 130 on each of the two
  mailboxes, with valid, boundary and invalid arguments (`--services`);
* suspend to RAM and resume, five times over, with the sensor hub started,
  stopped and left alone, every wake-up source, a PM that says nothing
  (`--suspend`);
* secure boot: 20 cases, among them an altered BL2, header, signature,
  digest, key, key hash and life cycle (`--secure`);
* the crash records and watchdogs, every exception vector, the ten RCSU
  violation interrupts (`--crash`);
* what no board's image takes (`--paths`): the flash behind the FCH's
  xSPI controller (three parts, and a controller that does not answer),
  PVT and glitch-detector trims marked for distribution, repair data in
  the fuses for every block and the repair requests with it, four damaged
  repair directories, nine debug certificates, two DDR channels, the DDR
  driver on the emulation platform, MTE on;
* the DDR driver with 19 feature settings and the eye scan, 7 kinds of
  memory parts, 9 failures of the subsystem, every interrupt status bit of
  every group on every channel, and a suspend (`--ddr-suite --eye`).

`emu/test-se.sh --audit` runs all of it with the interpreter checking
itself on the code it runs (the registers a callee must keep, at every
return): nothing found in either image but CIX's stack overflow below.

## Differences kept on purpose

### Security: what the open firmware refuses

| What | Open firmware | CIX's | Test |
|---|---|---|---|
| Secret eFuse fields (CIX and test device and model keys at 0x54 and 0xb4, OEM model key at 0x168), requests `0x8200000e` and `0x82000002` (id 0xb40) | given only to requests on the TEE mailbox; the non-secure mailbox gets the parameter error | given to either mailbox | *REE 8200000e fuse: …*, *REE 82000002 OEM model key* ([runtime.c](service/runtime.c) `fuse_range_allowed`) |
| Hash service `0x8200000c` | hashes only DRAM (AP addresses 0x80000000 to 0xbfffffff); anything else gets the error 0xfffffffd | hashes any address the SE can read: its own SRAM, the eFuse mirror | *8200000c hash below DRAM*, *hash of the fuse mirror's address*, *hash, end past the window* |
| Request `0x8200001a`, burn the OEM root key hash | refused (0xffffffff); the firmware has no code that programs eFuses | checks the request's CRC and, where the life cycle allows it, programs the hash and its valid bit, which turns secure boot on for good | *8200001a burn the OEM key hash …* |
| A CSU_PM image that fails its digest or signature check under secure boot | the boot stops (error 0x930c, system reset with reason 0x547) | **the PM is started all the same**: see the faults of CIX's image below | *secure boot, CSU_PM altered*, *CSU_PM's header altered*, *CSU_PM's signature altered*, *CIX key hash in the fuses wrong* |

### Left out

* **The UART debug monitor** (`service/debug/monitor`, 26 KB): CIX's debug
  build waits 3 s for ESC before the boot ("boot time config") and, after
  the boot, sits at the monitor's prompt until it is asked to suspend; the
  requests `0x82000013` and `0x82000014` are registered only when it
  leaves the prompt. CIX's release build has no monitor either. The open
  firmware goes straight into the idle loop. (In the test CIX's
  `service_monitor_entry` is replaced by a return.)
* **CIX's crypto library (CE-Lite)**: SHA-2, HKDF and RSA are Mbed TLS,
  SM2 and SM3 are written here; the engine is brought up and used for the
  TRNG and the OTP only. The open firmware reads the engine's
  configuration and status registers less often than CIX's library (six
  registers in a boot; the writes are the same). Signature checks take the
  core tens of milliseconds where the engine is faster.
* **The DDR driver's training report and register dumps**: CIX's reads
  four PHY registers per channel back for its report; the BIST pass/fail
  arrays (only printed) are dropped, the BIST runs kept.
* **The clock and power parts of the hang record**: CIX's records every
  clock's state by walking its monitor's clock tree (record `CLK`, 4304
  bytes) and every power domain (`POWER`, 1088 bytes). The open firmware
  records the raw PLL and S5 power registers instead, under names of
  their own (`CLK_RAW`, `PWR_RAW`, 68 bytes each), so that nothing reads
  them as CIX's. The `AMU` record and the records of the three watchdogs
  (`SE_WDT`, `PM_WDT`, `DDR`) are as CIX's but for the next point.
  (*crash: hang record on request*, [monitor.c](platform/sky1/monitor.c))

### Done differently

* **Error postcodes carry the open source's line numbers.** The postcode
  of the first error is `0x9000 | module << 7 | line & 0x7f`; the module
  is the same as CIX's, the line is the line of the open source. (The
  comparison masks the line bits.)
* **The mailbox request buffer is cleared.** CIX's handler reads a
  request into a buffer on its stack that it does not clear, so words a
  request does not send are whatever the stack held; here they read as 0.
  (*message of size 0*, *size 4*, *size 0x80*, *hash, too few parameters*)
* **Sending to the AP does not wait.** CIX's firmware waits up to 100 ms
  for the AP to take a message; its only such messages (wake-up sources,
  the SE watchdog's report) are sent from interrupt handlers, where the
  acknowledgement cannot be seen: each takes the whole 100 ms and is
  logged as timed out although it was sent. The open firmware sends and
  goes on. The messages are the same. (*wake-up sources*, *crash: SE
  watchdog interrupt*; [mailbox.c](driver/mailbox.c))
* **Message headers are whole.** The first word of the two messages above
  has only its seven size bits set by CIX's code; the other 25 bits are
  stale stack (an address of the SE's stack was seen). Here they are 0.
* **The SE watchdog's record has the interrupted code's frame** (stack
  frame and fault status registers) when the stack pointer saved at the
  interrupt is inside the stack. CIX's test is the wrong way round: its
  record has them only when the pointer is outside.
* **Refused requests are answered as errors.** For an OPN range it
  refuses (`0x82000002`), CIX's handler leaves the answer unset (status 0,
  the previous answer's length); for a fuse field out of range
  (`0x8200000e`) it reads on. Here both get 0xfffffffe. A fuse field is at
  most 0x70 bytes, inside the 2048-byte fuse array.
* **Memory repair group 0** is refused. CIX's takes the table entry
  before the first (`s_memr_get_data`, `0x200171ce`: the index is the
  group less one, unchecked) and goes by what lies there: without repair
  data in the fuses it answers as for an empty group, with it it logs
  "group_id:-1179511735" and fails with an error postcode. (*repair group
  0, index …*)
* **An unlisted flash part** (JEDEC ID not in the bootloader1 header's
  table, life cycle 4) is read with the standard read at the default
  clock; CIX's readers use parameters they never set.
* **An erased memory configuration**: the setup block is not found, every
  setting is taken as erased (0xff, "automatic") and the boot goes on.
  CIX's lookup reads the block's offset from past its table and usually
  ends up with the same 0xff bytes. Other lookups that find no block fail
  where CIX's read from a wrong offset.
* **The memory configuration is written back from a buffer of its own.**
  CIX's uses the boot ROM's copy of the bootloader1 header in SRAM as its
  sector buffer and so reads the header from the flash again before
  loading BL2. The flash ends up the same. (`--ddr`)
* **Each erased flash sector gets its own write enable**; the density
  table of the DDR driver is padded to 16 entries because CIX's lookup
  reads past its 9.
* **The fault report on the UART** is our own (the frame's registers and
  the fault status registers; CIX's has a call stack). Postcode, reset
  and reset reason are the same.
* **Log texts differ** throughout; the RAM log's contents and the version
  strings with them.

## Faults of CIX's image that the open one does not have

Each was seen as a difference in the test and then found in CIX's code.

1. **Secure boot does not protect CSU_PM.** In `fw_xloader_load_img`, when
   the PM image's digest (`0x2000556c`, source line 519) or signature
   (`0x20005588`, line 524) is wrong, the error is logged and its postcode
   set, but the function's result is not: `r4` stays 0, where the two
   paths for BL2 (`0x200056f6`, `0x20005718`) load the error into it. The
   PM's configuration is then loaded and the PM released. A CSU_PM with a
   byte changed, with its section table changed, with its signature
   changed, or signed by a key the fuses do not know runs.
2. **Stack overflow from the non-secure mailbox, `0x8200000e`.**
   `fuse_addr_handler` passes address, bit offset and size to
   `sec_efuse_read` unchecked (`0x20033824`), which clears and fills a
   128-byte buffer on the stack with as many bytes as asked. From 0x7c
   bytes the SE goes on with damaged registers; from 0x200 it takes a
   fault and resets the system (reason 0x447). Fuse bytes past the fuse
   array are given out. (*8200000e fuse, 0x7c bytes* … *0x801 bytes*)
3. **The same through `0x82000002`:** address plus length may wrap, a
   length near 4 GiB passes the range check and `sec_efuse_read` clears
   that many bytes from its stack buffer (`0x2001659c`). (*OPN, length
   wraps*)
4. The request buffer that is not cleared (`cix_mbox_isr`, `0x20027436`),
   the 100 ms wait in interrupt handlers (`mbox_send_data_sync`), the
   stale message headers (`plat_send_wakeup_src`, `0x200358be`), the
   inverted test in `se_save_einfo` (`0x2001560c`), the unset answer of
   `fuse_id_handler` (`0x200337c0`), repair group 0 (`0x200171ce`), the
   flash parameters that are not set (`hal_stor_load_fw`, `0x200189be`):
   above, under "done differently".
5. The time-out messages of the CI-700 cache flushes are printed when the
   flush succeeds at the last try, not when it times out.

Faults the open firmware had and the test found, now fixed: the hash
service took addresses from 0xc0000000 up (the fuse mirror among them); a
fuse field's bit offset could wrap its bounds check; SysTick was set up in
an order that could run a delay with the previous delay's length; the read
of the audio subsystem under its reset watchdog was missing; high words of
64-bit CI-700 registers were not read; the interrupted stack pointer was
never recorded for the watchdog record; an erased memory configuration
stopped the boot; the multimedia hub's port loop stopped one port short
(port 0x1a, with two DDR channels); the xSPI driver read the interrupt
status once where CIX's reads it again; a debug certificate's feature was
looked at before the debug control register was read; flash reads were
split at every 2 MiB boundary where CIX's does not split past 14 MiB.

## What the test cannot show

* Anything the model gets wrong about the hardware in the same way for
  both images. The model's assumptions are marked "modelled, not known" in
  [soc.rs](../../emu/src/se/soc.rs): the watchdog, the SE timers' count,
  the mailbox's status bits, which write ends the PM's wake-up interrupt,
  the audio power state, and all of the DDR subsystem.
* DRAM training: the PI does it in hardware. The model answers what CIX's
  driver waits for.
* The crypto engine's hash and big-number units are not modelled: CIX's
  library is replaced at `c_mbedtls_sha*`, `c_mbedtls_hkdf` and
  `sec_rsa_verify` by the same functions computed on the host. SM2
  signatures (no board's image has one) are not compared with CIX's: the
  open firmware's check is called alone with the example of GB/T 32918.5
  (accepted) and 15 damaged forms of it (refused).
* The FCH's xSPI controller is modelled after its public Linux driver
  (`spi-cadence-xspi.c`), the repair controllers and the RCSU after what
  the two images do with them.
* The boot ROM's hand-over is made by hand (the bootloader1 header copy at
  0x2007e800, the fuse mirror, the registers the firmware reads). CIX's
  boot ROM image (`common/brom/brom.elf` of its debug firmware) has not
  been run to make it: it runs from address 0, where the interpreter
  and the model are set up for an image in the SE's SRAM, and on hardware
  as it is at cold boot (the eFuse controller behind the mirror, whatever
  checks the SE's image), which is not modelled. What the inputs suite varies of the
  hand-over (boot reason, life cycle, header copy, clocks, strap) is what
  the firmware was seen to read of it.

## What no test reaches

`emu/test-se.sh … --cov` (`out/emu/test/se-coverage.txt`): the runs
execute 80.8 % of the open firmware's code bytes (literal pools count as
code). Of its 657 functions 22 never run (2348 bytes), and none of them
can in this firmware:

* `drv_mmhub_remap_init`, `plat_get_csi_model`: the multimedia hub's
  set-up for the FPGA platform; `plat_get_plat_id` is a constant here.
* `MemFuseLp4xSupported`: asked only for an LPDDR4X part, and the driver
  asserts on any part that is not LPDDR5 before that.
* `MemLpddr5CalcMR0Val`, `…MR4Val`, `…MR8Val`, `…MR35Val`, `…MR41Val`:
  entries of the mode-register table for registers the driver never
  computes.
* PKCS#1 v1.5 (`mbedtls_rsa_rsassa_pkcs1_v15_verify`,
  `rsa_rsassa_pkcs1_v15_encode`, `mbedtls_oid_get_oid_by_md`,
  `mbedtls_ct_memcmp`): `sig_verify` asks for PSS only.
* Mbed TLS's private-key and blinding paths (`mbedtls_rsa_private`,
  `mbedtls_mpi_fill_random`, `mpi_fill_random_internal`,
  `mbedtls_mpi_random`, `mbedtls_ct_mpi_uint_lt`, `mbedtls_mpi_lt_mpi_ct`,
  `ecp_randomize_jac`): its verify functions take a mode and a random
  generator; the firmware passes "public" and none.
* `mbedtls_ecp_set_zero`: a sum of points at infinity, which no signature
  tried produces.
* `fw_flush_cache_by_addr`: the RAM log's flush hook, an empty function
  the log does not call in any run.
* `__aeabi_ldiv0`: libgcc's division by zero.

Inside the functions that run, what is left is mostly error returns the
model never provokes (a flash that fails in mid-read, an eFuse read
error, a crypto engine that reports a fault, a heap that runs out) and
log formats.

Of CIX's image the same runs execute 37 %: its debug monitor, its crypto
library and most of its clock and power framework are replaced or never
asked.

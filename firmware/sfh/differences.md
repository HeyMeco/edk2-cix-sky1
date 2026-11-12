# How the open SFH firmware differs from CIX's

Every way this firmware differs from CIX's `sfh_fw.bin` (the image in
`edk2-non-osi/Platform/CIX/Sky1/PackageTool/Firmwares`, 17 360 bytes,
FreeRTOS and newlib based; no symbols) that the AP, the PM, the SE or a
user could observe. The open image is 4.5 KB of C.

The evidence is the differential test, `sky1emu sfh-test OPEN_sfh_fw.bin`
([emu/src/sfh/test.rs](../../emu/src/sfh/test.rs),
[emu/README.md](../../emu/README.md)): both images run on the same model
of the hub's timers, mailboxes and control block, in step, by simulated
time, through 162 phases. Compared in each: every write outside SRAM in
order, the registers read and how often, the words sent, the times of the
wake-up requests and how the image ends. A phase is named below in
*italics*. Addresses `0x2000…` are in CIX's image.

**Nothing here has run on hardware**, and the model is made from what the
two images do with the hardware, not from its documentation.

## What is the same

* The boot: 68 writes in the same order, the handshake `0xaabb` at
  `0x50070100`.
* The services. *AP: suspend, wake after N s* (0, 1, 2, 10 s): the answer
  `0xfff3` on `sfh2ap`, then the wake-up request (3 to `0x50070080`) N
  seconds **and 100 ms** after the message: both images wait the full
  100 ms of their send time-out first, because the wait runs in the
  mailbox interrupt handler, where the "sent" interrupt cannot be taken.
  *AP: resume*: `0xfff5`, then the wake-up request taken back, also after
  100 ms. *PM: wake after N ms* (0 to 60 000 ms): the wake-up request N ms
  after the message.
* A resume that arrives while a wake waits is served after it (the wait
  blocks the handler): wake at 3.1 s, then the resume's answer, in both.
  A second wake request waits for the first.
* Every command from 0 to 0xfffffff2 on both receiving channels, each
  service's command on the other's channel, 16 header sizes, 1 to 40 words
  in the FIFO, two messages in the FIFO at once.
* Every interrupt: the six timer interrupts and the four mailbox
  interrupts with their status set, the others as bare interrupts (0 to
  60), two at once, interrupts while a wait runs.
* NMI (0x11 written to 0x20060000, then stop), the fault vectors, SVCall,
  PendSV and SysTick; a bus fault in the mailbox handler (HardFault: both
  stop).
* The image the SE loads: the SE test (`se-test --suspend`) takes what
  each SE image copied into the hub's SRAM (4400 bytes at its start, the
  flash's image), starts it in this model and sees the handshake.

## Differences

* **Wake-ups are on time after the first resume.** CIX's image turns its
  log on at the first resume and from then on logs through a UART pointer
  that is 0: it reads its own vector table as the UART's flags, sees
  "busy", gives up after 10 000 polls a character and writes nothing.
  Each message it would log costs it tens of milliseconds first (at the
  100 MHz the test assumes for the core; more on a slower one): a wake
  asked for after 5 ms comes after 54 ms, the answer to a suspend after
  22 ms instead of 2. The open image logs nothing unless built with
  `LOG_UART=` and does what it is asked at once. (*PM: wake after 0 ms*:
  CIX's wake-up request at 49.25 ms, ours at 0.01 ms; the test accepts
  CIX's being late, by at most 120 ms, as the one expected difference of
  the kind.)
* **Interrupts 61 to 63 have a handler.** CIX's vector table ends at
  interrupt 60; the three entries after it are the first instructions of
  its code. The open table has the dispatcher up to 63. Nothing raises
  these interrupts. (*interrupt 61*, *62*, *63*)
* **No operating system.** CIX's image links FreeRTOS (the scheduler is
  never started) and newlib's stdio; the open one has neither. Stack use
  and SRAM contents differ accordingly (both start with the main stack at
  0x2007fff0).
* **Log texts**, where a build logs at all.

Faults the open image had and the test found, now fixed: it restarted
itself on a HardFault (CIX's handler logs and stops at `0x200028a0`; the
restart after that call is never reached, and a restart inside the fault
handler takes no interrupt any more); it did not read the FIFO status
register on an overflow or underflow interrupt (CIX's does, for a log
line).

## What no test reaches

Of the open image's code (`sfh-test --cov`, then `sky1emu cov`): every
function runs but libgcc's division-by-zero stub; 65 % of the bytes (78 %
without libgcc's 64-bit division, 18 % of which the log's numbers use). Inside functions, not run: the error returns of `mbox_send` and `mbox_request_channel` (their
callers pass constants), the timer modes other than 0 in `t32_config`
(only mode 0 is used), the formats `log_printf` is never given once the
log is on (`%x`, `%p`, `%c`, `%u`, `%%`, padding), and the branches of
libgcc's 64-bit division for operands the log's numbers never have.

## What the test cannot show

* Whatever the model has wrong about the timers, the mailbox FIFOs or
  the interrupt controller in the same way for both images: the timers'
  count direction and rate (25 MHz), which status bits the mailbox sets
  and when, the FIFO's depth (32 words assumed), that address 0 shows the
  start of SRAM.
* The core's clock: the time code takes between two hardware events is
  compared loosely (100 µs and half a percent).

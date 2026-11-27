# XeLL on DD1: changes and what to strip later

Branch `dd1xell`, on top of upstream `b1bb00b`. These changes bring all six
hardware threads up on a DD1 XeDK and let stage 2 start. Several of them were
added while chasing the wrong cause, so the patch is larger than it needs to be.
This note records what each change does, how well it is backed by a test, and
the order in which to try removing them before sending anything upstream.

## Setup

- Console: DD1 XeDK, Waternoose DD1.0 (PVR `0x00710100`), Xenos ID `0x5811`.
- Boot: JTAG POR from a PicoTitan with the NAND boot config ring, then pre_bl
  in flash (from the 1640 dump), the bootloader chain, and XeLL stage 1
  (`HACK_GGGGGG`) from flash `0xC0000`/`0x100000`.
- XeLL takes the "wakeup" path: thread 0 enters at HRMOR 0 with cores 1 and 2
  parked in the DD1 1BL ROM, waiting on the hold slots at `0x200_0001FFF0`
  (core 2) and `0x200_0001FFF8` (core 1).
- Every result below comes from the console UART at 115200. The POST value read
  over JTAG went stale during these tests, so it was not used.

## Result

```
XeLL - First stage
 * Attempting to wakeup all CPUs...
CPUs online: 3f..
 * success.
```

Stage 2 then runs up to `Looking for files on TFTP and local media...`. That
needs the libxenon change listed under "Outside XeLL".

## Stage 1 changes (`source/lv1`)

| # | Change | File | Evidence | Strip candidate? |
|---|--------|------|----------|------------------|
| 1 | Even threads skip HID1 in the SRAM stub | `startup.S` | Strong: the fix | No |
| 2 | LPCR = 2 at the start of the stub | `startup.S` | Needed only if the stub does I/O | Yes |
| 3 | Branch to the HRMOR-bypass alias before clearing HRMOR | `startup.S` | Not isolated | Yes |
| 4 | Wake one core at a time with 100 ms timeouts | `main.c` | Not isolated | Yes |
| 5 | `eieio` after IIC stores, hold-slot read-back + `sync` | `main.c` | No effect when tested | Yes |
| 6 | `place_jump`: store first, flush only the written lines | `main.c` | No evidence it matters | Yes |
| 7 | `bge` to `bgt` in the three cache loops | `cache.S` | Correct, unrelated to DD1 | Separate PR |

### 1. Skip HID1 in the SRAM stub (`other_threads_startup`)

Upstream's stub calls `init_regs` for even threads (2 and 4). On DD1 those
threads run the stub from the on-chip SRAM copy at `0x200_00011000`. With
`bl init_regs` there, core 1 froze right after entering the stub: two runs, one
with the NAND boot ring and one with a hybrid ring. The stub now writes the same
values as `init_regs` for HID0, HID4, HID6, TSCR and TTR, and leaves HID1 out.
With that, threads 2 to 5 came online.

- HID1 holds instruction-fetch control bits. The DD1 ROM and the 1640 S2 run
  from SRAM without touching those bits, and S2 sets them only together with
  the `rfid` that jumps into RAM.
- HID1 is still written for every thread: `start_from_rom` calls `init_regs`
  right after the stub's `rfid`, and by then the thread runs from RAM.
- Not isolated: the run that worked also had UART writes between the register
  writes. HID1 was the only write removed, though, so it is the likely cause.
- Retail risk: low. HID1 ends up with the same value, just written later.

### 2. LPCR = 2 at the start of the stub

`start_from_rom` already sets LPCR to 2, but the stub runs before it. This
became necessary only because of debug code: a UART write from a new thread
before LPCR was set froze the whole system (thread 1, two runs). S2's
`0x1FC00` stub and the 1640 HV also set LPCR bit 62 (`0x2`) in each new thread
first. The cleaned stub does no I/O before `start_from_rom`, so this may not be
needed. Keep it if any I/O is ever added to the stub.

### 3. HRMOR-bypass branch before clearing HRMOR

Even threads run the stub relative to HRMOR (`0x200_00000000`, the SRAM copy).
Upstream clears HRMOR and keeps going at the same address, which then points at
RAM `0x11xxx`. That works on retail, presumably because already-fetched or
already-translated instructions carry on. The stub now first branches to the
`0x8000…` bypass address of the code after it, as the DD1 ROM, pre_bl and S2 do
before they change HRMOR. This was added together with other changes and never
tested on its own.

### 4. Wake one core at a time

Upstream sends one IPI `0x140078` (threads 2 and 4), starts thread 1 through
CTRL, then waits forever, printing `CPUs online: xx..` every 10 ms. The branch
instead:

1. starts thread 1 through CTRL and waits up to 100 ms;
2. sends IPI `0x040078` (thread 2, which starts thread 3) and waits up to 100 ms;
3. sends IPI `0x100078` (thread 4, which starts thread 5) and waits up to 100 ms;
4. prints a warning if the mask isn't `0x3f`, and carries on.

The single IPI failed on DD1, but only before change 1 existed. It has not been
retried since. This change also turns "wait forever" into "warn and continue",
which upstream may not want. It adds the `wait_online()` helper.

### 5. `eieio` after IIC stores; hold-slot read-back

Copied from the DD1 HVs (1838 at `0x2268`, 1640 at `0x2F98`). They put `eieio`
after every IIC store and read a hold slot back followed by `sync` before
sending the IPI. This was added before change 1 and made no difference then.
It also covers the IRQ-enable loop. It adds the `std_eieio()` helper.

### 6. `place_jump` ordering

Upstream flushes `addr - 0x80` to `addr + 0x80` before storing, then flushes
`0x80` bytes at `addr` after. The branch stores the six-instruction stub and the
branch first, then flushes only what was written (`0x18` bytes, then `4`). This
was added when a stale JTAG POST value seemed to point at the pre-store flush.
UART output later showed that reading was wrong, so nothing backs this change.

### 7. `cache.S` loop bound

`dcache_flush`, `dcache_inv` and `flush_code` branched back with `bge`, which
touched one cache line past the end of the range. `bgt` touches exactly the
lines in the range. This is a correct fix with no known DD1 relevance. It
changes how many lines change 6's `flush_code(addr, 4)` touches (one, not two).
Upstream's `flush_code(addr, 0x80)` with `bge` touched two.

## Outside XeLL

- libxenon `drivers/xenos/xenos.c`: `assert(gpu_clock == 9)` in
  `xenos_init_phase1` is commented out. It is needed on this GPU (ID `0x5811`):
  with the assert in, stage 2 aborts, XeLL reloads and the loop repeats. The raw
  value of GPU register `0x210` isn't 9, though libxenon's formula still prints
  500 MHz. The EDRAM (1800 MHz) and FSB (5400 MHz) figures look wrong, so that
  decoding may not fit this GPU. A friendlier upstream form would print the raw
  value as a warning instead of asserting.

## Ruled out

- The NAND boot ring and pre_bl: woken cores reached XeLL's stub with the NAND
  boot ring and with a hybrid ring. The hybrid ring used stock values for the
  core 1 and core 2 reset fields (ring bytes `0x9d`/`0x9e` and `0xa7`/`0xa8`).
- The hold-slot value: `0xEFFF1000_EFFF1000` passes the DD1 ROM's `~off:off`
  check and lands at `0x200_00011000`.
- The IPI store itself: an IPI to thread 1 while it was already running
  completed normally.

## Strip test plan

The goal is the smallest patch that still gives `CPUs online: 3f..` and reaches
the stage 2 banner. Each test is one build, one flash and one POR run.

1. **Upstream `b1bb00b` plus change 1 only.** If it passes, changes 2–7 are
   optional and the DD1 fix is one small hunk in `startup.S`.
2. If it fails, add the other changes back one per test, in this order:
   3 (HRMOR bypass), 4 (one core at a time), 2 (LPCR), then 5 and 6. Change 3
   is the most likely remaining DD1 issue because the stub runs from SRAM.
   Changes 4 and 5 were added while chasing the wrong cause.
3. Keep change 7 out of the DD1 patch either way, and offer it separately.

Whatever is kept should also run on a retail console (JTAG or RGH XeLL) before
going upstream, since this code runs there too.

If a test fails and needs debugging, UART writes from the stub work as
breadcrumbs, but only after LPCR is set (change 2). Two threads writing to the
UART at the same time lose characters.

## Suggested upstream split

- xell-reloaded: change 1, plus whatever the strip tests show is needed, as a
  DD1 secondary-thread startup fix.
- xell-reloaded: change 7 on its own.
- libxenon: turn the `gpu_clock` assert into a warning that prints the raw value.

## DD1 notes for future debugging

- Any stuck bus access freezes the whole console: UART output stops, the
  probe's MMIO reads return all-ones, and the JTAG POST view stays on the last
  code.
- The flash window at `0x200_C8000000` accepts 32-bit loads (`lwz`). A 64-bit
  `ld` hangs the bus. The DD1 ROM and pre_bl only use `lwz` on flash.
- The stub copied to SRAM is now 352 bytes (upstream: 208).

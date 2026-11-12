// SPDX-License-Identifier: BSD-2-Clause-Patent
//! The interpreter checked against a real AArch64 core (`a64-selfcheck`).
//!
//! There is no second emulator to compare with, but there is the host:
//! on an AArch64 machine every distinct instruction word of the images
//! that user mode may run (data processing, loads and stores, branches on
//! registers and flags) is executed on the CPU and by the interpreter
//! from the same random registers, flags and memory, and everything it
//! can change is compared: x0..x30, SP, NZCV, the memory it addressed,
//! whether a branch was taken.
//!
//! How one word is run on the CPU: a stub in a JIT page loads all
//! registers from a state block, runs the word and stores them back.  The
//! stub needs two registers of its own (the state pointer and a
//! temporary): two that the word does not name.  A load or store gets
//! its base register pointed into a scratch buffer, which the
//! interpreter's bus mirrors at the same address; a PC-relative load has
//! its offset redirected to a literal in the stub, a branch its offset to
//! the stub's "taken"; ADR and ADRP run in the interpreter at the
//! stub's own address.
//!
//! Not checked this way, because user mode cannot run them: system
//! instructions (MRS, MSR, SYS, ERET, WFI) and branches through a
//! register.  Words the interpreter refuses (data in the code, FP) are
//! counted, not run.

use super::cpu::{class, Bus, Cpu, Stop};

/// The scratch buffer: the base register points 0x1000 into it (offsets reach -2552 and +33784).
const BUF: usize = 0xa000;

#[repr(C)]
#[derive(Clone, Default)]
struct State {
    x: [u64; 31],
    sp: u64,
    nzcv: u64,
    saved_sp: u64,
    taken: u64,
}

struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0.wrapping_mul(0x2545_f491_4f6c_dd1d)
    }

    /// Operands that reach the corners: mostly random, often small, zero, all ones or a sign boundary.
    fn operand(&mut self) -> u64 {
        let r = self.next();
        match r % 16 {
            0 => 0,
            1 => u64::MAX,
            2 => 1,
            3 => 0x8000_0000_0000_0000,
            4 => 0x7fff_ffff_ffff_ffff,
            5 => 0x8000_0000,
            6 => 0x7fff_ffff,
            7 => 0xffff_ffff,
            8 | 9 => self.next() % 67,
            10 => (self.next() % 67).wrapping_neg(),
            11 => self.next() & 0xffff_ffff,
            _ => self.next(),
        }
    }
}

/// The scratch buffer and the stub's bytes as the interpreter's memory, at their host addresses.
struct Mirror {
    buf_addr: u64,
    buf: Vec<u8>,
    code_addr: u64,
    code: Vec<u8>,
}

impl Bus for Mirror {
    fn read(&mut self, _core: usize, addr: u64, size: u32) -> Option<u64> {
        let (base, mem) = if addr >= self.buf_addr && addr + size as u64 <= self.buf_addr + self.buf.len() as u64 {
            (self.buf_addr, &self.buf)
        } else if addr >= self.code_addr && addr + size as u64 <= self.code_addr + self.code.len() as u64 {
            (self.code_addr, &self.code)
        } else {
            return None;
        };
        let o = (addr - base) as usize;
        let mut v = 0u64;
        for i in 0..size as usize {
            v |= (mem[o + i] as u64) << (8 * i);
        }
        Some(v)
    }

    fn write(&mut self, _core: usize, addr: u64, size: u32, val: u64) -> bool {
        if addr < self.buf_addr || addr + size as u64 > self.buf_addr + self.buf.len() as u64 {
            return false;
        }
        let o = (addr - self.buf_addr) as usize;
        for i in 0..size as usize {
            self.buf[o + i] = (val >> (8 * i)) as u8;
        }
        true
    }

    fn sysreg(&mut self, _core: usize, _key: u16, _write: Option<u64>) -> Option<u64> {
        None
    }

    fn tick(&mut self) {}
}

/// What of a word the check has to know.
struct Shape {
    /// the word as it is run: offsets of branches and literal loads redirected
    word: u32,
    kind: char,
    literal: bool,
    /// the base register of a load or store (31: SP), and its offset register
    base: Option<u32>,
    index: Option<u32>,
}

/// The word as the check runs it, or why it cannot: None for what the
/// architecture leaves unpredictable (a base written back that is also
/// the register transferred, and the like).
fn shape(insn: u32) -> Option<Shape> {
    let kind = class(insn);
    let mut s = Shape { word: insn, kind, literal: false, base: None, index: None };
    match kind {
        'b' => {
            // three instructions on: the stub's "taken"
            s.word = if (insn >> 26) & 0x1f == 0b00101 {
                (insn & 0xfc00_0000) | 3
            } else if (insn >> 25) & 0x3f == 0b011011 {
                (insn & !(0x3fff << 5)) | (3 << 5)
            } else {
                (insn & !(0x7ffff << 5)) | (3 << 5)
            };
        }
        'm' => {
            let (rt, rn) = (insn & 31, (insn >> 5) & 31);
            match (insn >> 27) & 7 {
                0b011 if (insn >> 24) & 1 == 0 => s.literal = true,
                0b001 if (insn >> 24) & 1 == 0 => {
                    let (o2, load, o1) = ((insn >> 23) & 1 != 0, (insn >> 22) & 1 != 0, (insn >> 21) & 1 != 0);
                    let (rs, rt2) = ((insn >> 16) & 31, (insn >> 10) & 31);
                    if !o2 && !load && (rs == rt || rs == rn || (o1 && rs == rt2)) {
                        return None;
                    }
                    if !o2 && o1 && load && rt == rt2 {
                        return None;
                    }
                    s.base = Some(rn);
                }
                0b101 => {
                    let (mode, load, rt2) = ((insn >> 23) & 3, (insn >> 22) & 1 != 0, (insn >> 10) & 31);
                    if (load && rt == rt2) || ((mode == 1 || mode == 3) && rn != 31 && (rn == rt || rn == rt2)) {
                        return None;
                    }
                    s.base = Some(rn);
                }
                0b111 => {
                    if (insn >> 24) & 1 == 0 && (insn >> 21) & 1 == 0 && (insn >> 10) & 1 != 0 && rn != 31 && rn == rt {
                        return None;
                    }
                    if (insn >> 24) & 1 == 0 && (insn >> 21) & 1 != 0 {
                        let rm = (insn >> 16) & 31;
                        if rm == rn || rm == 31 {
                            return None;
                        }
                        s.index = Some(rm);
                    }
                    s.base = Some(rn);
                }
                _ => {}
            }
        }
        _ => {}
    }
    Some(s)
}

#[cfg(target_arch = "aarch64")]
mod host {
    use std::ffi::c_void;

    extern "C" {
        fn mmap(addr: *mut c_void, len: usize, prot: i32, flags: i32, fd: i32, off: i64) -> *mut c_void;
        fn signal(sig: i32, handler: usize) -> usize;
        fn write(fd: i32, buf: *const c_void, n: usize) -> isize;
        fn _exit(code: i32) -> !;
    }

    #[cfg(target_os = "macos")]
    extern "C" {
        fn pthread_jit_write_protect_np(enabled: i32);
        fn sys_icache_invalidate(start: *mut c_void, len: usize);
    }

    #[cfg(not(target_os = "macos"))]
    extern "C" {
        fn __clear_cache(start: *mut c_void, end: *mut c_void);
    }

    pub static CURRENT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);

    extern "C" fn died(sig: i32) {
        let w = CURRENT.load(std::sync::atomic::Ordering::Relaxed);
        let mut msg = *b"a64-selfcheck: signal 00 from the CPU running word 00000000, which the interpreter accepts\n";
        msg[22] = b'0' + (sig / 10) as u8;
        msg[23] = b'0' + (sig % 10) as u8;
        for i in 0..8 {
            msg[51 + i] = b"0123456789abcdef"[((w >> (28 - 4 * i)) & 15) as usize];
        }
        unsafe {
            write(2, msg.as_ptr() as *const c_void, msg.len());
            _exit(3);
        }
    }

    pub struct Jit {
        pub page: *mut u8,
        pub len: usize,
    }

    impl Jit {
        pub fn new() -> Option<Jit> {
            let len = 0x4000;
            #[cfg(target_os = "macos")]
            let flags = 0x2 | 0x1000 | 0x800; // MAP_PRIVATE | MAP_ANON | MAP_JIT
            #[cfg(not(target_os = "macos"))]
            let flags = 0x2 | 0x20; // MAP_PRIVATE | MAP_ANONYMOUS
            let page = unsafe { mmap(std::ptr::null_mut(), len, 7, flags, -1, 0) };
            if page as isize == -1 || page.is_null() {
                return None;
            }
            unsafe {
                // SIGILL, SIGBUS, SIGSEGV: say which word and stop
                signal(4, died as *const () as usize);
                signal(10, died as *const () as usize);
                signal(11, died as *const () as usize);
                signal(7, died as *const () as usize);
            }
            Some(Jit { page: page as *mut u8, len })
        }

        pub fn put(&self, code: &[u32]) {
            assert!(code.len() * 4 <= self.len);
            unsafe {
                #[cfg(target_os = "macos")]
                pthread_jit_write_protect_np(0);
                std::ptr::copy_nonoverlapping(code.as_ptr() as *const u8, self.page, code.len() * 4);
                #[cfg(target_os = "macos")]
                {
                    pthread_jit_write_protect_np(1);
                    sys_icache_invalidate(self.page as *mut c_void, code.len() * 4);
                }
                #[cfg(not(target_os = "macos"))]
                __clear_cache(self.page as *mut c_void, self.page.add(code.len() * 4) as *mut c_void);
            }
        }

        pub fn call(&self, state: *mut super::State) {
            let f: extern "C" fn(*mut super::State) = unsafe { std::mem::transmute(self.page) };
            f(state);
        }
    }
}

/// The stub for a word: (code, index of the word in it).  `s` and `t` are
/// the stub's own registers.
fn stub(word: u32, kind: char, literal: bool, s: u32, t: u32) -> (Vec<u32>, usize) {
    let ldr = |r: u32, off: u32| 0xf940_0000 | ((off / 8) << 10) | (s << 5) | r;
    let str_ = |r: u32, off: u32| 0xf900_0000 | ((off / 8) << 10) | (s << 5) | r;
    let mut c = vec![0xd101_83ff]; // sub sp, sp, #96
    for i in 0..6u32 {
        c.push(0xa900_0000 | ((2 * i) << 15) | ((20 + 2 * i) << 10) | (31 << 5) | (19 + 2 * i)); // stp x19+2i, x20+2i, [sp, #16i]
    }
    c.push(0xaa00_03e0 | s); // mov s, x0
    c.push(0x9100_03e0 | t); // mov t, sp
    c.push(str_(t, 264));
    c.push(ldr(t, 256));
    c.push(0xd51b_4200 | t); // msr nzcv, t
    c.push(ldr(t, 248));
    c.push(0x9100_001f | (t << 5)); // mov sp, t
    for r in 0..31 {
        if r != s && r != t {
            c.push(ldr(r, 8 * r));
        }
    }
    let at = c.len();
    let mut word = word;
    if literal {
        // the literal sits after the stub: its distance is set below
        word &= !(0x7ffff << 5);
    }
    c.push(word);
    if kind == 'b' {
        c.push(0xd280_0000 | t); // not taken: mov t, #0
        c.push(0x1400_0002); // b +2
        c.push(0xd280_0020 | t); // taken: mov t, #1
        c.push(str_(t, 272));
    }
    for r in 0..31 {
        if r != s && r != t {
            c.push(str_(r, 8 * r));
        }
    }
    c.push(0xd53b_4200 | t); // mrs t, nzcv
    c.push(str_(t, 256));
    c.push(0x9100_03e0 | t); // mov t, sp
    c.push(str_(t, 248));
    c.push(ldr(t, 264));
    c.push(0x9100_001f | (t << 5)); // mov sp, t
    for i in 0..6u32 {
        c.push(0xa940_0000 | ((2 * i) << 15) | ((20 + 2 * i) << 10) | (31 << 5) | (19 + 2 * i)); // ldp
    }
    c.push(0x9101_83ff); // add sp, sp, #96
    c.push(0xd65f_03c0); // ret
    if literal {
        let pool = c.len();
        c[at] |= ((pool - at) as u32) << 5;
        c.extend_from_slice(&[0x89ab_cdef, 0x8123_4567, 0xf0e1_d2c3, 0x7b5a_6978]);
    }
    (c, at)
}

#[cfg(not(target_arch = "aarch64"))]
pub fn check(_words: &[u32], _rounds: u32, _verbose: bool) -> Result<String, String> {
    Err("a64-selfcheck runs the instructions on the host's CPU: it needs an AArch64 host".into())
}

/// Check every word; Ok(summary) when the interpreter agrees with the CPU on all.
#[cfg(target_arch = "aarch64")]
pub fn check(words: &[u32], rounds: u32, verbose: bool) -> Result<String, String> {
    let jit = host::Jit::new().ok_or("cannot map a JIT page")?;
    let mut rng = Rng(0x9e37_79b9_7f4a_7c15);
    let mut buf = vec![0u8; BUF];
    let buf_addr = buf.as_mut_ptr() as u64;
    let mid = (buf_addr + 0x1000) & !15;
    let (mut checked, mut runs, mut refused, mut system, mut unpredictable) = (0u64, 0u64, 0u64, 0u64, 0u64);
    let mut bad: Vec<String> = Vec::new();
    let mut seen = std::collections::BTreeSet::new();
    for &insn in words {
        let kind = class(insn);
        if kind == 'x' {
            refused += 1;
            continue;
        }
        if kind == 's' {
            system += 1;
            continue;
        }
        let Some(sh) = shape(insn) else {
            unpredictable += 1;
            continue;
        };
        if !seen.insert(sh.word) {
            continue;
        }
        // two registers the word does not name (x18 is the platform's: never used by the stub)
        let named = [sh.word & 31, (sh.word >> 5) & 31, (sh.word >> 10) & 31, (sh.word >> 16) & 31];
        let mut free = (9..18).chain(0..9).chain(19..29).filter(|r| !named.contains(r));
        let (s, t) = (free.next().unwrap(), free.next().unwrap());
        let (code, at) = stub(sh.word, sh.kind, sh.literal, s, t);
        let code_addr = jit.page as u64;
        let pc = code_addr + 4 * at as u64;
        let code_bytes: Vec<u8> = code.iter().flat_map(|w| w.to_le_bytes()).collect();
        let mut put = false;
        for round in 0..rounds {
            let mut st = State::default();
            for r in 0..31 {
                st.x[r] = rng.operand();
            }
            st.nzcv = (rng.next() & 0xf) << 28;
            st.sp = mid + ((rng.next() % 64) * 16);
            if let Some(b) = sh.base {
                let p = mid + ((rng.next() % 64) * 16);
                if b == 31 {
                    st.sp = p;
                } else {
                    st.x[b as usize] = p;
                }
            }
            if let Some(m) = sh.index {
                // small, and with rubbish above bit 31 half the time (the 32-bit extends must not see it)
                let small = rng.next() % 256;
                let option = (sh.word >> 13) & 7;
                st.x[m as usize] = if option & 1 == 0 && round % 2 == 1 { small | (rng.next() << 32) } else if round % 4 == 3 && option >= 6 { (small as i64).wrapping_neg() as u64 } else { small };
                if option == 6 && round % 4 == 3 {
                    st.x[m as usize] &= 0xffff_ffff;
                }
            }
            let memory = sh.kind == 'm' && !sh.literal;
            if memory {
                for c in buf.chunks_mut(8) {
                    let v = rng.next().to_le_bytes();
                    c.copy_from_slice(&v[..c.len()]);
                }
            }
            // the interpreter first: a word it refuses is not run on the CPU
            let mut cpu = Cpu::new(0);
            cpu.x = st.x;
            cpu.x[s as usize] = 0;
            cpu.x[t as usize] = 0;
            cpu.sp[3] = st.sp;
            cpu.nzcv = (st.nzcv >> 28) as u32;
            cpu.pc = pc;
            let mut m = Mirror { buf_addr, buf: if memory { buf.clone() } else { Vec::new() }, code_addr, code: code_bytes.clone() };
            match cpu.step(&mut m) {
                Ok(()) => {}
                Err(Stop::Undefined { .. }) => {
                    refused += 1;
                    break;
                }
                Err(Stop::Hang(_)) => {}
                Err(e) => {
                    bad.push(format!("{insn:08x}: the interpreter stopped: {e}"));
                    break;
                }
            }
            if !put {
                jit.put(&code);
                put = true;
            }
            host::CURRENT.store(sh.word, std::sync::atomic::Ordering::Relaxed);
            let before = st.clone();
            let mut diff = String::new();
            // (again on a difference: the kernel may clear x18 when it switches threads)
            for _try in 0..3 {
                let mut native = before.clone();
                // the CPU works on `buf` itself: keep what it leaves and put the buffer back
                let start = if memory { buf.clone() } else { Vec::new() };
                jit.call(&mut native);
                let nbuf = if memory { buf.clone() } else { Vec::new() };
                if memory {
                    buf.copy_from_slice(&start);
                }
                diff.clear();
                for r in 0..31 {
                    if r as u32 != s && r as u32 != t && native.x[r] != cpu.x[r] {
                        diff.push_str(&format!(" x{r}: CPU {:#x}, interpreter {:#x};", native.x[r], cpu.x[r]));
                    }
                }
                if native.sp != cpu.sp[3] {
                    diff.push_str(&format!(" sp: CPU {:#x}, interpreter {:#x};", native.sp, cpu.sp[3]));
                }
                if (native.nzcv >> 28) as u32 != cpu.nzcv {
                    diff.push_str(&format!(" nzcv: CPU {:x}, interpreter {:x};", native.nzcv >> 28, cpu.nzcv));
                }
                if sh.kind == 'b' && (native.taken == 1) != (cpu.pc == pc + 12) {
                    diff.push_str(&format!(" branch: CPU {}, interpreter pc {:+};", native.taken, cpu.pc as i64 - pc as i64));
                }
                if nbuf != m.buf {
                    let i = nbuf.iter().zip(&m.buf).position(|(a, b)| a != b).unwrap();
                    diff.push_str(&format!(" memory at base{:+}: CPU {:#x}, interpreter {:#x};", i as i64 - (mid - buf_addr) as i64, nbuf[i], m.buf[i]));
                }
                if diff.is_empty() {
                    break;
                }
            }
            runs += 1;
            if !diff.is_empty() {
                bad.push(format!(
                    "{insn:08x} (run as {:08x}), nzcv {:x}, sp {:#x}, x[Rd,Rn,Rm,Ra] = {:x?}:{diff}",
                    sh.word,
                    before.nzcv >> 28,
                    before.sp,
                    named.map(|r| if r == 31 { 0 } else { before.x[r as usize] })
                ));
                break;
            }
            if round + 1 == rounds {
                checked += 1;
            }
        }
        if verbose && bad.len() > 40 {
            break;
        }
    }
    let summary = format!(
        "{checked} distinct instruction words agree with the CPU ({runs} runs, {rounds} each); not run: {system} system or register-branch words, {refused} the interpreter refuses (data, FP), {unpredictable} unpredictable"
    );
    if bad.is_empty() {
        Ok(summary)
    } else {
        for b in bad.iter().take(60) {
            eprintln!("  {b}");
        }
        Err(format!("{} words differ; {summary}", bad.len()))
    }
}

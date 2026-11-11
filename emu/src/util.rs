// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Small things the emulators and tests share: SHA-256, the repository's
//! root and CIX's debug builds, command-line arguments, a sequence diff.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

// --- SHA-256, HMAC, HKDF ---------------------------------------------------------

const K: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
];

pub fn sha256(data: &[u8]) -> [u8; 32] {
    let mut h: [u32; 8] =
        [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&((data.len() as u64) * 8).to_be_bytes());
    for block in msg.chunks(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([block[4 * i], block[4 * i + 1], block[4 * i + 2], block[4 * i + 3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let mut v = h;
        for i in 0..64 {
            let s1 = v[4].rotate_right(6) ^ v[4].rotate_right(11) ^ v[4].rotate_right(25);
            let ch = (v[4] & v[5]) ^ (!v[4] & v[6]);
            let t1 = v[7].wrapping_add(s1).wrapping_add(ch).wrapping_add(K[i]).wrapping_add(w[i]);
            let s0 = v[0].rotate_right(2) ^ v[0].rotate_right(13) ^ v[0].rotate_right(22);
            let maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            let t2 = s0.wrapping_add(maj);
            v = [t1.wrapping_add(t2), v[0], v[1], v[2], v[3].wrapping_add(t1), v[4], v[5], v[6]];
        }
        for i in 0..8 {
            h[i] = h[i].wrapping_add(v[i]);
        }
    }
    let mut out = [0u8; 32];
    for i in 0..8 {
        out[4 * i..4 * i + 4].copy_from_slice(&h[i].to_be_bytes());
    }
    out
}

pub fn hmac_sha256(key: &[u8], data: &[u8]) -> [u8; 32] {
    let mut k = [0u8; 64];
    if key.len() > 64 {
        k[..32].copy_from_slice(&sha256(key));
    } else {
        k[..key.len()].copy_from_slice(key);
    }
    let mut inner: Vec<u8> = k.iter().map(|b| b ^ 0x36).collect();
    inner.extend_from_slice(data);
    let mut outer: Vec<u8> = k.iter().map(|b| b ^ 0x5c).collect();
    outer.extend_from_slice(&sha256(&inner));
    sha256(&outer)
}

/// HKDF-SHA256 with an all-zero salt and no info.
pub fn hkdf_sha256(ikm: &[u8], n: usize) -> Vec<u8> {
    let prk = hmac_sha256(&[0u8; 32], ikm);
    let (mut out, mut t, mut i) = (Vec::new(), Vec::new(), 1u8);
    while out.len() < n {
        let mut m = t.clone();
        m.push(i);
        t = hmac_sha256(&prk, &m).to_vec();
        out.extend_from_slice(&t);
        i += 1;
    }
    out.truncate(n);
    out
}

pub fn hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

pub fn hex_sp(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect::<Vec<_>>().join(" ")
}

pub fn u32le(b: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]])
}

pub fn put32(b: &mut [u8], o: usize, v: u32) {
    b[o..o + 4].copy_from_slice(&v.to_le_bytes());
}

pub fn parse_int(s: &str) -> Option<u64> {
    let s = s.trim();
    if let Some(h) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        u64::from_str_radix(h, 16).ok()
    } else {
        s.parse().ok()
    }
}

// --- the repository ----------------------------------------------------------------

/// The repository's root: $SKY1EMU_ROOT, or the directory above the
/// current one (or the executable) that has manifests/.
pub fn root() -> PathBuf {
    if let Ok(r) = std::env::var("SKY1EMU_ROOT") {
        return PathBuf::from(r);
    }
    let mut starts = vec![std::env::current_dir().unwrap_or_default()];
    if let Ok(exe) = std::env::current_exe() {
        starts.push(exe);
    }
    for start in starts {
        let mut d: Option<&Path> = Some(&start);
        while let Some(p) = d {
            if p.join("manifests/cix-debug-firmware.json").exists() {
                return p.to_path_buf();
            }
            d = p.parent();
        }
    }
    PathBuf::from(".")
}

pub fn rooted(path: &str) -> PathBuf {
    let p = Path::new(path);
    if p.is_absolute() || p.exists() {
        p.to_path_buf()
    } else {
        root().join(p)
    }
}

/// A CIX debug build pinned in manifests/cix-debug-firmware.json, from the
/// blob cache; scripts/open/cix_debug.py fetches and verifies it when it
/// is not there yet.
pub fn cix_debug(name: &str) -> Result<String, String> {
    let root = root();
    let manifest = std::fs::read_to_string(root.join("manifests/cix-debug-firmware.json")).map_err(|e| format!("manifest: {e}"))?;
    let field = |key: &str, from: usize| -> Option<String> {
        let i = manifest[from..].find(&format!("\"{key}\""))? + from;
        let rest = &manifest[i + key.len() + 2..];
        let a = rest.find('"')? + 1;
        let b = rest[a..].find('"')? + a;
        Some(rest[a..b].to_string())
    };
    let url = field("url", 0).ok_or("manifest: no url")?;
    let commit = field("commit", 0).ok_or("manifest: no commit")?;
    let at = manifest.find(&format!("\"{name}\"")).ok_or(format!("{name} is not in manifests/cix-debug-firmware.json"))?;
    let want = field("sha256", at).ok_or("manifest: no sha256")?;
    let repo = url.trim_start_matches("https://github.com/");
    let path = root.join("blobs-cache/git").join(repo).join(&commit).join(name);
    let ok = |p: &Path| std::fs::read(p).map(|d| hex(&sha256(&d)) == want).unwrap_or(false);
    if !ok(&path) {
        let st = std::process::Command::new("python3")
            .arg(root.join("scripts/open/cix_debug.py"))
            .arg(name)
            .stdout(std::process::Stdio::null())
            .status()
            .map_err(|e| format!("cix_debug.py: {e}"))?;
        if !st.success() || !ok(&path) {
            return Err(format!("{name}: not in the blob cache and scripts/open/cix_debug.py could not fetch it"));
        }
    }
    Ok(path.display().to_string())
}

// --- command lines -------------------------------------------------------------------

/// Arguments: flags (--name), options with a value (--name VALUE, listed
/// in `valued`, repeatable) and positional ones.
#[derive(Default, Debug)]
pub struct Args {
    pub flags: Vec<String>,
    pub opts: BTreeMap<String, Vec<String>>,
    pub pos: Vec<String>,
}

impl Args {
    pub fn parse(args: &[String], valued: &[&str]) -> Result<Args, String> {
        let mut a = Args::default();
        let mut it = args.iter();
        while let Some(x) = it.next() {
            if valued.contains(&x.as_str()) {
                let v = it.next().ok_or(format!("{x} needs a value"))?;
                a.opts.entry(x.clone()).or_default().push(v.clone());
            } else if x.starts_with('-') && x.len() > 1 && !x[1..].starts_with(|c: char| c.is_ascii_digit()) {
                a.flags.push(x.clone());
            } else {
                a.pos.push(x.clone());
            }
        }
        Ok(a)
    }

    pub fn flag(&self, name: &str) -> bool {
        self.flags.iter().any(|f| f == name)
    }

    pub fn opt(&self, name: &str) -> Option<&str> {
        self.opts.get(name).and_then(|v| v.last()).map(|s| s.as_str())
    }

    pub fn all(&self, name: &str) -> Vec<String> {
        self.opts.get(name).cloned().unwrap_or_default()
    }

    pub fn int(&self, name: &str) -> Option<u64> {
        self.opt(name).and_then(parse_int)
    }

    /// Flags nobody asked about: a mistyped option.
    pub fn check(&self, known: &[&str]) -> Result<(), String> {
        match self.flags.iter().find(|f| !known.contains(&f.as_str())) {
            Some(f) => Err(format!("unknown option {f}")),
            None => Ok(()),
        }
    }
}

// --- sequences -----------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Edit {
    /// a[i] == b[j]
    Same(usize, usize),
    /// only in a
    Del(usize),
    /// only in b
    Ins(usize),
}

/// The edits that turn a into b along a longest common subsequence
/// (Myers' algorithm).
pub fn diff<T: PartialEq>(a: &[T], b: &[T]) -> Vec<Edit> {
    let (n, m) = (a.len() as isize, b.len() as isize);
    let max = n + m;
    // v[k] for k in -d..=d after step d, kept per step for the way back
    let mut trace: Vec<Vec<isize>> = Vec::new();
    let mut v: Vec<isize> = vec![0; 3];
    let get = |v: &Vec<isize>, d: isize, k: isize| -> isize {
        // v holds diagonals -d-1..=d+1 at index k + d + 1
        v[(k + d + 1) as usize]
    };
    let mut found = 0;
    for d in 0..=max {
        // diagonals -d-1..=d+1 of the previous step, padded
        let mut next = vec![0isize; (2 * d + 5) as usize];
        let mut done = false;
        let mut k = -d;
        while k <= d {
            let mut x = if d == 0 {
                0
            } else if k == -d || (k != d && get(&v, d - 1, k - 1) < get(&v, d - 1, k + 1)) {
                get(&v, d - 1, k + 1)
            } else {
                get(&v, d - 1, k - 1) + 1
            };
            let mut y = x - k;
            while x < n && y < m && a[x as usize] == b[y as usize] {
                x += 1;
                y += 1;
            }
            next[(k + d + 2) as usize] = x;
            if x >= n && y >= m {
                done = true;
            }
            k += 2;
        }
        // as diagonals -(d+1)..=d+1 at index k + d + 2 - 1
        let stored: Vec<isize> = next[1..].to_vec();
        trace.push(stored.clone());
        v = stored;
        if done {
            found = d;
            break;
        }
    }
    let mut out = Vec::new();
    let (mut x, mut y) = (n, m);
    let mut d = found;
    while d > 0 {
        let prev = &trace[(d - 1) as usize];
        let k = x - y;
        let pk = if k == -d || (k != d && get(prev, d - 1, k - 1) < get(prev, d - 1, k + 1)) { k + 1 } else { k - 1 };
        let px = get(prev, d - 1, pk);
        let py = px - pk;
        let (sx, sy) = if pk == k + 1 { (px, py + 1) } else { (px + 1, py) };
        while x > sx && y > sy {
            x -= 1;
            y -= 1;
            out.push(Edit::Same(x as usize, y as usize));
        }
        if pk == k + 1 {
            out.push(Edit::Ins(py as usize));
        } else {
            out.push(Edit::Del(px as usize));
        }
        x = px;
        y = py;
        d -= 1;
    }
    while x > 0 && y > 0 {
        x -= 1;
        y -= 1;
        out.push(Edit::Same(x as usize, y as usize));
    }
    out.reverse();
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sha() {
        assert_eq!(hex(&sha256(b"abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        assert_eq!(hex(&sha256(b"")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        let long = vec![b'a'; 1000];
        assert_eq!(hex(&sha256(&long)), "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    }

    #[test]
    fn hmac() {
        let k = [0x0bu8; 20];
        assert_eq!(hex(&hmac_sha256(&k, b"Hi There")), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    }

    fn apply(a: &[u8], b: &[u8]) {
        let e = diff(a, b);
        let (mut x, mut y) = (0, 0);
        for ed in &e {
            match *ed {
                Edit::Same(i, j) => {
                    assert_eq!((i, j), (x, y));
                    assert_eq!(a[i], b[j]);
                    x += 1;
                    y += 1;
                }
                Edit::Del(i) => {
                    assert_eq!(i, x);
                    x += 1;
                }
                Edit::Ins(j) => {
                    assert_eq!(j, y);
                    y += 1;
                }
            }
        }
        assert_eq!((x, y), (a.len(), b.len()), "{a:?} {b:?} {e:?}");
    }

    #[test]
    fn diffs() {
        apply(b"", b"");
        apply(b"abc", b"abc");
        apply(b"abc", b"");
        apply(b"", b"abc");
        apply(b"abcabba", b"cbabac");
        apply(b"the quick brown fox", b"a quick brown dog jumps");
        let n = diff(b"abcabba", b"cbabac").iter().filter(|e| matches!(e, Edit::Same(..))).count();
        assert_eq!(n, 4);
    }
}

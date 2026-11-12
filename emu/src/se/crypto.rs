// SPDX-License-Identifier: BSD-2-Clause-Patent
//! What CIX's image asks its crypto engine (Arm China CE-Lite) for, done
//! here: the engine's hash and big-number units are not modelled, so the
//! functions of CIX's library that drive them are replaced at their entry
//! (`stubs`) by SHA-2, SM3, HKDF and RSA-PSS computed on the host.  CIX's
//! own code above them (key derivation, the hash service, image
//! verification) runs as it is.  The open firmware computes all of this
//! itself (Mbed TLS, its own SM3) and needs none of it.

use super::soc::SkySoc;
use crate::arm::{Arm, Stub};
use crate::util::{hmac_sha256, sha256};
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;

const K512: [u64; 80] = [
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
    0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
    0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
    0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
    0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
    0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
    0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
    0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
    0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
    0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
    0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
    0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
    0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
];

/// SHA-512, or SHA-384 (its first 48 bytes from other initial values).
pub fn sha512(data: &[u8], is384: bool) -> Vec<u8> {
    let mut h: [u64; 8] = if is384 {
        [
            0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939, 0x67332667ffc00b31,
            0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4,
        ]
    } else {
        [
            0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1, 0x510e527fade682d1,
            0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179,
        ]
    };
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 128 != 112 {
        msg.push(0);
    }
    msg.extend_from_slice(&((data.len() as u128) * 8).to_be_bytes());
    for block in msg.chunks(128) {
        let mut w = [0u64; 80];
        for i in 0..16 {
            w[i] = u64::from_be_bytes(block[8 * i..8 * i + 8].try_into().unwrap());
        }
        for i in 16..80 {
            let s0 = w[i - 15].rotate_right(1) ^ w[i - 15].rotate_right(8) ^ (w[i - 15] >> 7);
            let s1 = w[i - 2].rotate_right(19) ^ w[i - 2].rotate_right(61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let mut v = h;
        for i in 0..80 {
            let s1 = v[4].rotate_right(14) ^ v[4].rotate_right(18) ^ v[4].rotate_right(41);
            let ch = (v[4] & v[5]) ^ (!v[4] & v[6]);
            let t1 = v[7].wrapping_add(s1).wrapping_add(ch).wrapping_add(K512[i]).wrapping_add(w[i]);
            let s0 = v[0].rotate_right(28) ^ v[0].rotate_right(34) ^ v[0].rotate_right(39);
            let maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            let t2 = s0.wrapping_add(maj);
            v = [t1.wrapping_add(t2), v[0], v[1], v[2], v[3].wrapping_add(t1), v[4], v[5], v[6]];
        }
        for i in 0..8 {
            h[i] = h[i].wrapping_add(v[i]);
        }
    }
    let out: Vec<u8> = h.iter().flat_map(|x| x.to_be_bytes()).collect();
    out[..if is384 { 48 } else { 64 }].to_vec()
}

/// SHA-224 (SHA-256 from other initial values, 28 bytes): by the same compression as util::sha256.
fn sha224(data: &[u8]) -> Vec<u8> {
    // (nothing asks for it with a result that is compared; kept exact all the same)
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
    let mut h: [u32; 8] = [0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4];
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&((data.len() as u64) * 8).to_be_bytes());
    for block in msg.chunks(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes(block[4 * i..4 * i + 4].try_into().unwrap());
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
    h.iter().take(7).flat_map(|x| x.to_be_bytes()).collect()
}

/// SM3 (GB/T 32905-2016).
pub fn sm3(data: &[u8]) -> Vec<u8> {
    let mut h: [u32; 8] = [0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600, 0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e];
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&((data.len() as u64) * 8).to_be_bytes());
    let p0 = |x: u32| x ^ x.rotate_left(9) ^ x.rotate_left(17);
    let p1 = |x: u32| x ^ x.rotate_left(15) ^ x.rotate_left(23);
    for block in msg.chunks(64) {
        let mut w = [0u32; 68];
        for i in 0..16 {
            w[i] = u32::from_be_bytes(block[4 * i..4 * i + 4].try_into().unwrap());
        }
        for i in 16..68 {
            w[i] = p1(w[i - 16] ^ w[i - 9] ^ w[i - 3].rotate_left(15)) ^ w[i - 13].rotate_left(7) ^ w[i - 6];
        }
        let mut v = h;
        for j in 0..64 {
            let t: u32 = if j < 16 { 0x79cc4519 } else { 0x7a879d8a };
            let a12 = v[0].rotate_left(12);
            let ss1 = a12.wrapping_add(v[4]).wrapping_add(t.rotate_left(j as u32 % 32)).rotate_left(7);
            let ss2 = ss1 ^ a12;
            let (ff, gg) = if j < 16 {
                (v[0] ^ v[1] ^ v[2], v[4] ^ v[5] ^ v[6])
            } else {
                ((v[0] & v[1]) | (v[0] & v[2]) | (v[1] & v[2]), (v[4] & v[5]) | (!v[4] & v[6]))
            };
            let tt1 = ff.wrapping_add(v[3]).wrapping_add(ss2).wrapping_add(w[j] ^ w[j + 4]);
            let tt2 = gg.wrapping_add(v[7]).wrapping_add(ss1).wrapping_add(w[j]);
            v = [tt1, v[0], v[1].rotate_left(9), v[2], p0(tt2), v[4], v[5].rotate_left(19), v[6]];
        }
        for i in 0..8 {
            h[i] ^= v[i];
        }
    }
    h.iter().flat_map(|x| x.to_be_bytes()).collect()
}

/// HKDF-SHA256 (RFC 5869).
pub fn hkdf(salt: &[u8], ikm: &[u8], info: &[u8], n: usize) -> Vec<u8> {
    let zero = [0u8; 32];
    let prk = hmac_sha256(if salt.is_empty() { &zero } else { salt }, ikm);
    let (mut out, mut t, mut i) = (Vec::new(), Vec::new(), 1u8);
    while out.len() < n {
        let mut m = t.clone();
        m.extend_from_slice(info);
        m.push(i);
        t = hmac_sha256(&prk, &m).to_vec();
        out.extend_from_slice(&t);
        i += 1;
    }
    out.truncate(n);
    out
}

// --- big numbers, for RSA -----------------------------------------------------------------------

/// Little-endian 32-bit limbs.
fn from_be(b: &[u8]) -> Vec<u32> {
    let mut v = vec![0u32; b.len().div_ceil(4)];
    for (i, x) in b.iter().rev().enumerate() {
        v[i / 4] |= (*x as u32) << (8 * (i % 4));
    }
    v
}

fn to_be(v: &[u32], n: usize) -> Vec<u8> {
    (0..n).rev().map(|i| v.get(i / 4).map_or(0, |x| (x >> (8 * (i % 4))) as u8)).collect()
}

fn ge(a: &[u32], b: &[u32]) -> bool {
    for i in (0..a.len().max(b.len())).rev() {
        let (x, y) = (a.get(i).copied().unwrap_or(0), b.get(i).copied().unwrap_or(0));
        if x != y {
            return x > y;
        }
    }
    true
}

/// a * b mod m, by shift and add over the bits of b (slow and plain: a few modular products are all
/// an RSA verification with a small exponent takes).
fn mulmod(a: &[u32], b: &[u32], m: &[u32]) -> Vec<u32> {
    let n = m.len();
    let mut r = vec![0u32; n + 1];
    let sub = |r: &mut Vec<u32>| {
        let mut borrow = 0i64;
        for i in 0..r.len() {
            let d = r[i] as i64 - m.get(i).copied().unwrap_or(0) as i64 - borrow;
            r[i] = d as u32;
            borrow = (d < 0) as i64;
        }
    };
    for bit in (0..32 * b.len()).rev() {
        // r = 2r mod m
        let mut carry = 0u32;
        for x in r.iter_mut() {
            let c = *x >> 31;
            *x = *x << 1 | carry;
            carry = c;
        }
        if ge(&r, m) {
            sub(&mut r);
        }
        if b[bit / 32] >> (bit % 32) & 1 != 0 {
            let mut carry = 0u64;
            for i in 0..r.len() {
                let s = r[i] as u64 + a.get(i).copied().unwrap_or(0) as u64 + carry;
                r[i] = s as u32;
                carry = s >> 32;
            }
            if ge(&r, m) {
                sub(&mut r);
            }
        }
    }
    r.truncate(n);
    r
}

fn modexp(base: &[u32], exp: &[u32], m: &[u32]) -> Vec<u32> {
    let mut r = vec![0u32; m.len()];
    r[0] = 1;
    let mut started = false;
    for bit in (0..32 * exp.len()).rev() {
        if started {
            r = mulmod(&r, &r, m);
        }
        if exp[bit / 32] >> (bit % 32) & 1 != 0 {
            r = mulmod(&r, base, m);
            started = true;
        }
    }
    r
}

/// RSASSA-PSS verification with SHA-256 (MGF1 with SHA-256, any salt length), of a digest.
pub fn rsa_pss_verify(n: &[u8], e: &[u8], digest: &[u8], sig: &[u8]) -> bool {
    let k = n.len();
    if sig.len() != k || digest.len() != 32 {
        return false;
    }
    let (nn, s) = (from_be(n), from_be(sig));
    if ge(&s, &nn) || nn.iter().all(|x| *x == 0) {
        return false;
    }
    let em = to_be(&modexp(&s, &from_be(e), &nn), k);
    let bits = 8 * k - n.iter().position(|b| *b != 0).map_or(0, |i| 8 * i + n[i].leading_zeros() as usize);
    let em_bits = bits - 1;
    let em_len = em_bits.div_ceil(8);
    let em = &em[k - em_len..];
    if em_len < 34 || em[em_len - 1] != 0xbc {
        return false;
    }
    let (db, h) = em[..em_len - 1].split_at(em_len - 33);
    let mut mask = Vec::new();
    let mut c = 0u32;
    while mask.len() < db.len() {
        let mut m = h.to_vec();
        m.extend_from_slice(&c.to_be_bytes());
        mask.extend_from_slice(&sha256(&m));
        c += 1;
    }
    let mut db: Vec<u8> = db.iter().zip(&mask).map(|(a, b)| a ^ b).collect();
    let top = 8 * em_len - em_bits;
    if em[0] >> (8 - top) != 0 && top != 0 {
        return false;
    }
    db[0] &= 0xff >> top;
    let Some(one) = db.iter().position(|b| *b != 0) else { return false };
    if db[one] != 1 {
        return false;
    }
    let salt = &db[one + 1..];
    let mut m = vec![0u8; 8];
    m.extend_from_slice(digest);
    m.extend_from_slice(salt);
    sha256(&m)[..] == *h
}

// --- the stubs -----------------------------------------------------------------------------------

#[derive(Clone, Copy)]
enum Hash {
    Sha256,
    Sha224,
    Sha384,
    Sha512,
    Sm3,
}

type Ctxs = Rc<RefCell<HashMap<u32, (Hash, Vec<u8>)>>>;

fn stack(cpu: &Arm<SkySoc>, i: u32) -> u32 {
    cpu.read32(cpu.r[13] + 4 * i)
}

/// The functions of CIX's library replaced, by name.
pub fn stubs() -> Vec<(&'static str, Stub<SkySoc>)> {
    let ctxs: Ctxs = Rc::new(RefCell::new(HashMap::new()));
    let mut v: Vec<(&'static str, Stub<SkySoc>)> = Vec::new();
    let nothing: Stub<SkySoc> = Rc::new(|_| None);
    for name in [
        "c_mbedtls_sha256_init",
        "c_mbedtls_sha384_init",
        "c_mbedtls_sha512_init",
        "c_mbedtls_sm3_init",
    ] {
        v.push((name, nothing.clone()));
    }
    for name in ["c_mbedtls_sha256_free", "c_mbedtls_sha384_free", "c_mbedtls_sha512_free", "c_mbedtls_sm3_free"] {
        let c = ctxs.clone();
        v.push((
            name,
            Rc::new(move |cpu: &mut Arm<SkySoc>| {
                c.borrow_mut().remove(&cpu.r[0]);
                None
            }),
        ));
    }
    // starts(ctx, variant)
    for (name, a, b) in [
        ("c_mbedtls_sha256_starts_ret", Hash::Sha256, Hash::Sha224),
        ("c_mbedtls_sha384_starts_ret", Hash::Sha512, Hash::Sha384),
        ("c_mbedtls_sha512_starts_ret", Hash::Sha512, Hash::Sha384),
        ("c_mbedtls_sm3_starts_ret", Hash::Sm3, Hash::Sm3),
    ] {
        let c = ctxs.clone();
        v.push((
            name,
            Rc::new(move |cpu: &mut Arm<SkySoc>| {
                c.borrow_mut().insert(cpu.r[0], (if cpu.r[1] != 0 { b } else { a }, Vec::new()));
                Some(0)
            }),
        ));
    }
    for name in [
        "c_mbedtls_sha256_update_ret",
        "c_mbedtls_sha384_update_ret",
        "c_mbedtls_sha512_update_ret",
        "c_mbedtls_sm3_update_ret",
    ] {
        let c = ctxs.clone();
        v.push((
            name,
            Rc::new(move |cpu: &mut Arm<SkySoc>| {
                let data = cpu.read_bytes(cpu.r[1], cpu.r[2]);
                match c.borrow_mut().get_mut(&cpu.r[0]) {
                    Some(h) => {
                        h.1.extend_from_slice(&data);
                        Some(0)
                    }
                    None => Some(0xffffffff),
                }
            }),
        ));
    }
    for name in [
        "c_mbedtls_sha256_finish_ret",
        "c_mbedtls_sha384_finish_ret",
        "c_mbedtls_sha512_finish_ret",
        "c_mbedtls_sm3_finish_ret",
    ] {
        let c = ctxs.clone();
        v.push((
            name,
            Rc::new(move |cpu: &mut Arm<SkySoc>| {
                let Some((kind, data)) = c.borrow_mut().remove(&cpu.r[0]) else { return Some(0xffffffff) };
                let d = match kind {
                    Hash::Sha256 => sha256(&data).to_vec(),
                    Hash::Sha224 => sha224(&data),
                    Hash::Sha384 => sha512(&data, true),
                    Hash::Sha512 => sha512(&data, false),
                    Hash::Sm3 => sm3(&data),
                };
                cpu.write_bytes(cpu.r[1], &d);
                Some(0)
            }),
        ));
    }
    // sec_rsa_verify(handle, ctx, digest, digest_size, sig, sig_size, hash): 1 when the signature is good.
    // handle: the key size (4: 2048 bits, 5: 3072); ctx: modulus, exponent (pointers, the key's length each),
    // at +13 the padding (3: PSS; CIX's image verification uses no other)
    v.push((
        "sec_rsa_verify",
        Rc::new(|cpu: &mut Arm<SkySoc>| {
            let klen = match cpu.mem.read8(cpu.r[0]) {
                4 => 256,
                5 => 384,
                _ => return Some(0),
            };
            let ctx = cpu.r[1];
            if ctx == 0 || cpu.r[2] == 0 || cpu.mem.read8(ctx + 13) != 3 {
                return Some(0);
            }
            let (n, e) = (cpu.read_bytes(cpu.read32(ctx), klen), cpu.read_bytes(cpu.read32(ctx + 4), klen));
            let digest = cpu.read_bytes(cpu.r[2], cpu.r[3].min(64));
            let sig = cpu.read_bytes(stack(cpu, 0), stack(cpu, 1).min(1024));
            Some(rsa_pss_verify(&n, &e, &digest, &sig) as u32)
        }),
    ));
    // int c_mbedtls_hkdf(md, salt, salt_len, ikm, ikm_len, info, info_len, okm, okm_len)
    v.push((
        "c_mbedtls_hkdf",
        Rc::new(|cpu: &mut Arm<SkySoc>| {
            let salt = if cpu.r[1] != 0 { cpu.read_bytes(cpu.r[1], cpu.r[2]) } else { Vec::new() };
            let ikm = cpu.read_bytes(cpu.r[3], stack(cpu, 0));
            let info = if stack(cpu, 1) != 0 { cpu.read_bytes(stack(cpu, 1), stack(cpu, 2)) } else { Vec::new() };
            let okm = hkdf(&salt, &ikm, &info, stack(cpu, 4) as usize);
            cpu.write_bytes(stack(cpu, 3), &okm);
            Some(0)
        }),
    ));
    v
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::hex;

    #[test]
    fn hashes() {
        assert_eq!(
            hex(&sha512(b"abc", false)),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
        );
        assert_eq!(
            hex(&sha512(b"abc", true)),
            "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"
        );
        assert_eq!(hex(&sha224(b"abc")), "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7");
        assert_eq!(hex(&sm3(b"abc")), "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0");
        // RFC 5869, test case 1
        let okm = hkdf(&(0..13).collect::<Vec<u8>>(), &[0x0b; 22], &(0xf0..0xfa).collect::<Vec<u8>>(), 42);
        assert_eq!(hex(&okm), "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
    }

    #[test]
    fn bignum() {
        let m = from_be(&[0xff, 0xff, 0xff, 0xfb]);
        let r = modexp(&from_be(&[2]), &from_be(&[0x01, 0x00]), &m);
        // 2^256 mod (2^32 - 5) = 5^8 = 390625
        assert_eq!(to_be(&r, 4), 390625u32.to_be_bytes());
    }
}

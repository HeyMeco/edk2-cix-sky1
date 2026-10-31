#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Open tools for the CIX Sky1 boot images.

Replaces the closed CIX host tools for the parts of the flash image that are
not plain TF-A artefacts, and inspects the vendor images:

  pack     build a full or OTA SPI image from a flash layout JSON
           (byte-identical to cix_package_tool -o / -O)
  fip      list or extract the entries of a TF-A FIP (bootloader2/3)
  bl1      show, verify or re-sign a CIXBTFF! container (bootloader1)
  stmm     extract the StandaloneMM firmware volume embedded in an OP-TEE
           image (CFG_STMM_PATH payload), e.g. from a vendor bootloader2

Usage:
  cixfw.py pack -c spi_flash_config_all.json -o cix_flash_all.bin [--base DIR]
  cixfw.py pack -c spi_flash_config_ota.json -O cix_flash_ota.bin [--base DIR]
  cixfw.py fip FILE [--extract DIR] [--verify [--root PEM]]
  cixfw.py bl1 FILE [--verify] [--extract DIR] [--replace se|pm|ap=FILE ...]
                    [--resign OUT --key1 PEM --key2 PEM]
  cixfw.py stmm FILE -o STMM.fd [--compressed OUT]
"""

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib
from pathlib import Path

# --------------------------------------------------------------------------
# Flash image (cix_package_tool)
# --------------------------------------------------------------------------

FW_HEADER_MAGIC = 0x55AA55AA
IMAGE_BOOTLOADER2 = 2
FIP_TOC_MAGIC = 0xAA640001


class PackError(Exception):
    pass


def _int(v):
    return v if isinstance(v, int) else int(v, 0)


def _read(base, name, what):
    p = base / name
    if not p.is_file():
        raise PackError(f"{what}: file {p} does not exist")
    return p.read_bytes()


def _groups(cfg, base):
    groups = cfg["image_header_groups"]
    if len(groups) != _int(cfg["image_count"]):
        raise PackError(f"image_header_groups count({len(groups)}) don't match image_count({cfg['image_count']})")
    out = []
    for g in groups:
        data = bytearray(_read(base, g["file"], f"image type {g['image_type']}"))
        t, addr, size = _int(g["image_type"]), _int(g["address"]), _int(g["size"])
        if len(data) > size:
            raise PackError(f"file {g['file']} ({len(data):#x}) is larger than its region ({size:#x})")
        if t == IMAGE_BOOTLOADER2 and len(data) >= 8:
            # The FIP TOC serial number carries the firmware version
            # (cix_package_tool writes it whether or not the file is a FIP).
            struct.pack_into("<I", data, 4, _int(cfg["fip_version"]))
        out.append((t, addr, size, bytes(data)))
    return out


def _header(magic_word3, cfs_version, entries):
    hdr = struct.pack("<IIII", FW_HEADER_MAGIC, cfs_version, len(entries), magic_word3)
    for t, addr, length in entries:
        hdr += struct.pack("<IIII", t, addr, length, 0)
    return hdr


def pack_all(cfg, base):
    """Full SPI image, as cix_package_tool -o."""
    flash = _int(cfg["flash_size"])
    images = _groups(cfg, base)
    writes = []
    ec = cfg.get("ec_file", "")
    if ec:
        writes.append((_int(cfg["ec_addr"]), _read(base, ec, "ec firmware")))
    if cfg.get("sec_debug_file"):
        writes.append((_int(cfg["sec_debug_addr"]), _read(base, cfg["sec_debug_file"], "sec_debug")))
    if cfg.get("xip_file"):
        writes.append((_int(cfg["xip_addr"]), _read(base, cfg["xip_file"], "xip firmware")))
    entries = sorted((addr, t, len(d)) for t, addr, _, d in images)
    hdr = _header(0, _int(cfg["cfs_version"]), [(t, a, n) for a, t, n in entries])
    writes.append((_int(cfg["firmware_header_addr"]), hdr))
    writes += [(addr, d) for _, addr, _, d in images]
    end = max(a + len(d) for a, d in writes)
    if end > flash:
        raise PackError(f"image overflows the flash size ({end:#x} > {flash:#x})")
    img = bytearray(end)
    for a, d in writes:
        img[a:a + len(d)] = d
    return bytes(img)


def pack_ota(cfg, base):
    """OTA image, as cix_package_tool -O: header, then the files back to back."""
    images = _groups(cfg, base)
    hdr = _header(_int(cfg["ota_flags"]), _int(cfg["cfs_version"]), [(t, a, len(d)) for t, a, _, d in images])
    return hdr + b"".join(d for *_, d in images)


# --------------------------------------------------------------------------
# FIP
# --------------------------------------------------------------------------

FIP_NAMES = {
    "47d4086d-4cfe-9846-9b95-2950cbbd5a00": "soc-fw (BL31)",
    "05d0e189-53dc-1347-8d2b-500a4b7a3e38": "tos-fw (BL32)",
    "0b70c29b-2a5a-7840-9f65-0a5682738288": "tos-fw-extra1",
    "8ea87bb1-cfa2-3f4d-85fd-e7bba50220d9": "tos-fw-extra2",
    "d6d0eea7-fcea-d54b-9782-9934f234b6e4": "nt-fw (BL33)",
    "827ee890-f860-e411-a1b4-777a21b4f94c": "trusted-key-cert",
    "8ab8becc-f960-e411-9ad0-eb4822d8dcf8": "soc-fw-key-cert",
    "9477d603-fb60-e411-85dd-b7105b8cee04": "tos-fw-key-cert",
    "8ad5832a-fb60-e411-8aaf-df30bbc49859": "nt-fw-key-cert",
    "e2b20c20-5e63-e411-9ce8-abccf92bb666": "soc-fw-cert",
    "a49f4411-5e63-e411-8728-3f05722af33d": "tos-fw-cert",
    "8ec4c1f3-5d63-e411-a7a9-87ee40b23fa7": "nt-fw-cert",
}


def fip_entries(data):
    toc, _serial, _flags = struct.unpack_from("<IIQ", data, 0)
    if toc != FIP_TOC_MAGIC:
        raise ValueError("not a FIP (bad TOC magic)")
    out, o = [], 16
    while True:
        u = str(uuid.UUID(bytes=data[o:o + 16]))
        off, size, flags = struct.unpack_from("<QQQ", data, o + 16)
        if int(u.replace("-", ""), 16) == 0:
            return out
        out.append((u, off, size, flags))
        o += 40


TBBR_OID = "1.3.6.1.4.1.4128.2100."
# (key certificate, extension with the content key, content certificate,
#  [(hash extension, image)], NV counter extension)  -- TF-A TBBR OIDs
TBBR_CHAINS = [
    ("soc-fw-key-cert", "501", "soc-fw-cert", [("603", "soc-fw"), ("604", "soc-fw-config")], "1"),
    ("tos-fw-key-cert", "901", "tos-fw-cert", [("1001", "tos-fw"), ("1002", "tos-fw-extra1"),
                                               ("1003", "tos-fw-extra2"), ("1004", "tos-fw-config")], "1"),
    ("nt-fw-key-cert", "1101", "nt-fw-cert", [("1201", "nt-fw"), ("1202", "nt-fw-config")], "2"),
]
# key certificates are signed by the key in this trusted-key-cert extension
TBBR_WORLD = {"soc-fw-key-cert": "302", "tos-fw-key-cert": "302", "nt-fw-key-cert": "303"}


def _der_strip(v):
    """Contents of a DER OCTET STRING (TBBR wraps every extension value)."""
    if v[0] != 0x04:
        return v
    n = v[1]
    if n < 0x80:
        return v[2:2 + n]
    k = n & 0x7F
    return v[2 + k:2 + k + int.from_bytes(v[2:2 + k], "big")]


class TbbrCert:
    def __init__(self, der):
        from cryptography import x509
        self.cert = x509.load_der_x509_certificate(der)
        self.ext = {}
        for e in self.cert.extensions:
            if e.oid.dotted_string.startswith(TBBR_OID):
                self.ext[e.oid.dotted_string[len(TBBR_OID):]] = _der_strip(e.value.value)

    def spki(self):
        from cryptography.hazmat.primitives import serialization as s
        return self.cert.public_key().public_bytes(s.Encoding.DER, s.PublicFormat.SubjectPublicKeyInfo)

    def nvctr(self, ext="1"):
        v = self.ext.get(ext)
        return int.from_bytes(v[2:2 + v[1]], "big") if v else None

    def key(self, ext):
        return self.ext.get(ext)

    def digest(self, ext):
        v = self.ext.get(ext)
        return v[-32:] if v else None      # DigestInfo, SHA-256 last

    def signed_by(self, spki):
        """TF-A certificates are self-issued: the subject key is the signer."""
        from cryptography.hazmat.primitives.asymmetric import padding
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.serialization import load_der_public_key
        if self.spki() != spki:
            return False
        try:
            load_der_public_key(spki).verify(
                self.cert.signature, self.cert.tbs_certificate_bytes,
                padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=32), hashes.SHA256())
            return True
        except Exception:
            return False


def spki_of(pem):
    """DER SubjectPublicKeyInfo of a PEM private or public key."""
    from cryptography.hazmat.primitives import serialization as s
    data = Path(pem).read_bytes()
    if b"PRIVATE" in data:
        key = s.load_pem_private_key(data, None).public_key()
    else:
        key = s.load_pem_public_key(data)
    return key.public_bytes(s.Encoding.DER, s.PublicFormat.SubjectPublicKeyInfo)


def verify_fip(data, root_spki=None, trusted_key_cert=None):
    """Check a FIP's TBBR chain; returns (problems, info).

    root_spki: the root of trust BL2 checks the trusted key certificate
    against (default: whatever key signed it).  trusted_key_cert: DER of a
    trusted key certificate kept outside the FIP (the CIX BL33 FIP carries
    trusted_key_no.crt in its own entry, so this is only for completeness).
    """
    by_name = {FIP_NAMES.get(u, u).split()[0]: data[o:o + n] for u, o, n, _ in fip_entries(data)}
    problems, info = [], {}
    tk_der = by_name.get("trusted-key-cert", trusted_key_cert)
    if tk_der is None:
        return ["no trusted-key-cert"], info
    tk = TbbrCert(tk_der)
    root = root_spki or tk.spki()
    info["root"] = hashlib.sha256(root).hexdigest()[:16]
    if tk.nvctr() is not None:
        info["nvctr"] = tk.nvctr()
    if not tk.signed_by(root):
        problems.append("trusted-key-cert is not signed by the root of trust")
    for kc_name, kext, cc_name, images, nvext in TBBR_CHAINS:
        if kc_name not in by_name:
            continue
        world = tk.key(TBBR_WORLD[kc_name])
        kc = TbbrCert(by_name[kc_name])
        if world is None or not kc.signed_by(world):
            problems.append(f"{kc_name} is not signed by trusted-key-cert ext {TBBR_WORLD[kc_name]}")
        info.setdefault("nvctr", kc.nvctr(nvext))
        if tk.nvctr(nvext) is not None and kc.nvctr(nvext) != tk.nvctr(nvext):
            problems.append(f"{kc_name} NV counter {kc.nvctr(nvext)} != {tk.nvctr(nvext)}")
        cc = TbbrCert(by_name[cc_name]) if cc_name in by_name else None
        if cc is None or not cc.signed_by(kc.key(kext) or b""):
            problems.append(f"{cc_name} missing or not signed by {kc_name} ext {kext}")
            continue
        if cc.nvctr(nvext) != kc.nvctr(nvext):
            problems.append(f"{cc_name} NV counter {cc.nvctr(nvext)} != {kc.nvctr(nvext)}")
        for hext, img in images:
            dg = cc.digest(hext)
            if img in by_name:
                if dg != hashlib.sha256(by_name[img]).digest():
                    problems.append(f"{img} does not match its hash in {cc_name}")
                else:
                    info[img] = hashlib.sha256(by_name[img]).hexdigest()[:16]
            elif dg is not None and dg.strip(b"\0"):
                problems.append(f"{cc_name} has a hash for {img}, which the FIP lacks")
    return problems, info


def cmd_fip(args):
    data = Path(args.file).read_bytes()
    _, serial, _ = struct.unpack_from("<IIQ", data, 0)
    print(f"FIP serial {serial:#x}, {len(data)} bytes")
    for u, off, size, _ in fip_entries(data):
        name = FIP_NAMES.get(u, "?")
        body = data[off:off + size]
        print(f"  {name:18} {u}  off {off:#8x}  size {size:#8x}  sha256 {hashlib.sha256(body).hexdigest()[:16]}")
        if args.extract:
            d = Path(args.extract)
            d.mkdir(parents=True, exist_ok=True)
            (d / (name.split()[0] + ".bin")).write_bytes(body)
    if args.verify:
        problems, info = verify_fip(data, spki_of(args.root) if args.root else None)
        print(f"  chain: root {info.get('root')}, NV counter {info.get('nvctr')}")
        for p in problems:
            print(f"  FAILED: {p}")
        if problems:
            sys.exit(1)
        print("  chain ok")


# --------------------------------------------------------------------------
# bootloader1 (CIXBTFF! container)
# --------------------------------------------------------------------------
#
# Layout (identical in the CIX, Radxa, Minisforum and Orange Pi images):
#   0x000  "CIXBTFF!", version
#   0x01c  key 1: RSA-3072 modulus (384 B, big-endian), exponent at 0x19c
#   0x324  key 2: same layout, exponent at 0x4a4
#   0x800  supported SPI flash table
#   0x9e8  image count, then 24-byte entries from 0x9ec:
#          type (1 SE, 2 PM, 3 AP/BL2), flag, 0, offset, size, load address
#   0xb7c  SHA-256 of image i's data at 0xb7c + 0x48 * i
#   0xe00  u32 signature length (0x180), 0xe04 signature over [0, 0xe00)
# Each image owns the 4 KiB pages from its offset; the last 0x200 bytes of
# its slot hold a u32 length (0x180) and an RSA-PSS signature over the slot
# up to there.  Key 1 signs the header, key 2 the images.  RSASSA-PSS,
# SHA-256, MGF1-SHA-256, salt 32 (Neol00/edk2-cix-unlocked#12).

BL1_MAGIC = b"CIXBTFF!"
BL1_KEY_OFF = (0x1C, 0x324)
BL1_HDR_SIGNED = 0xE00
BL1_COUNT = 0x9E8
BL1_ENTRY = 0x9EC
BL1_DIGEST = 0xB7C
BL1_NAMES = {1: "se", 2: "pm", 3: "ap"}
PAGE = 0x1000


def _sig_offset(end):
    """Signature slot: 0x200 before the 4 KiB boundary following the data."""
    o = ((end + PAGE - 1) & ~(PAGE - 1)) - 0x200
    return o if o >= end else o + PAGE


def pss_verify(n, e, msg, sig):
    with tempfile.TemporaryDirectory() as td:
        t = Path(td)
        (t / "k.pem").write_bytes(_rsa_pub_pem(n, e))
        (t / "m").write_bytes(msg)
        (t / "s").write_bytes(sig)
        r = subprocess.run(["openssl", "dgst", "-sha256", "-sigopt", "rsa_padding_mode:pss",
                            "-sigopt", "rsa_pss_saltlen:32", "-sigopt", "rsa_mgf1_md:sha256",
                            "-verify", str(t / "k.pem"), "-signature", str(t / "s"), str(t / "m")],
                           capture_output=True)
        return r.returncode == 0


def pss_sign(key_pem, msg):
    with tempfile.TemporaryDirectory() as td:
        t = Path(td)
        (t / "m").write_bytes(msg)
        subprocess.run(["openssl", "dgst", "-sha256", "-sigopt", "rsa_padding_mode:pss",
                        "-sigopt", "rsa_pss_saltlen:32", "-sigopt", "rsa_mgf1_md:sha256",
                        "-sign", str(key_pem), "-out", str(t / "s"), str(t / "m")], check=True)
        return (t / "s").read_bytes()


def _der_len(n):
    if n < 0x80:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(b)]) + b


def _der_int(v):
    b = v.to_bytes((v.bit_length() + 8) // 8, "big")
    return b"\x02" + _der_len(len(b)) + b


def _rsa_pub_pem(n, e):
    import base64
    rsa = _der_int(n) + _der_int(e)
    rsa = b"\x30" + _der_len(len(rsa)) + rsa
    spki = bytes.fromhex("300d06092a864886f70d0101010500") + b"\x03" + _der_len(len(rsa) + 1) + b"\x00" + rsa
    spki = b"\x30" + _der_len(len(spki)) + spki
    b64 = base64.b64encode(spki).decode()
    lines = "\n".join(b64[i:i + 64] for i in range(0, len(b64), 64))
    return f"-----BEGIN PUBLIC KEY-----\n{lines}\n-----END PUBLIC KEY-----\n".encode()


def _modulus_of(pem):
    out = subprocess.run(["openssl", "rsa", "-in", str(pem), "-noout", "-modulus"],
                         capture_output=True, text=True, check=True).stdout
    return int(out.strip().split("=", 1)[1], 16)


class Bl1:
    def __init__(self, data):
        if data[:8] != BL1_MAGIC:
            raise ValueError("not a CIXBTFF! container")
        self.data = bytearray(data)

    def images(self):
        """[(index, name, offset, size, load, signature offset)]"""
        d = self.data
        out = []
        for i in range(struct.unpack_from("<I", d, BL1_COUNT)[0]):
            t, _flag, _z, off, size, load = struct.unpack_from("<6I", d, BL1_ENTRY + 24 * i)
            out.append((i, BL1_NAMES.get(t, f"type{t}"), off, size, load, _sig_offset(off + size)))
        return out

    def keys(self):
        out = []
        for o in BL1_KEY_OFF:
            out.append((int.from_bytes(self.data[o:o + 384], "big"),
                        int.from_bytes(self.data[o + 384:o + 388], "little")))
        return out

    def verify(self):
        d = bytes(self.data)
        (n1, e1), (n2, e2) = self.keys()
        res = [("header", pss_verify(n1, e1, d[:BL1_HDR_SIGNED], d[BL1_HDR_SIGNED + 4:BL1_HDR_SIGNED + 388]))]
        for i, name, off, size, _, sig in self.images():
            ok = (hashlib.sha256(d[off:off + size]).digest() == d[BL1_DIGEST + 0x48 * i:BL1_DIGEST + 0x48 * i + 32]
                  and struct.unpack_from("<I", d, sig)[0] == 0x180
                  and pss_verify(n2, e2, d[off:sig], d[sig + 4:sig + 388]))
            res.append((name, ok))
        return res

    def replace(self, name, payload):
        """Replace one image in place; it must fit before its signature."""
        for i, n, off, size, _, sig in self.images():
            if n != name:
                continue
            if _sig_offset(off + len(payload)) != sig:
                raise ValueError(f"{name}: {len(payload):#x} bytes do not fit the slot at {off:#x} "
                                 f"(at most {sig - off:#x}); moving images is not supported yet")
            self.data[off:sig] = payload + bytes(sig - off - len(payload))
            struct.pack_into("<I", self.data, BL1_ENTRY + 24 * i + 16, len(payload))
            self.data[BL1_DIGEST + 0x48 * i:BL1_DIGEST + 0x48 * i + 32] = hashlib.sha256(payload).digest()
            return
        raise ValueError(f"no image {name}")

    def resign(self, key1, key2):
        """Sign with key1 (header) and key2 (images).  Keys that differ from
        the embedded ones replace them; only unfused chips would accept that."""
        for o, pem in zip(BL1_KEY_OFF, (key1, key2)):
            self.data[o:o + 384] = _modulus_of(pem).to_bytes(384, "big")
        for _, _, off, _, _, sig in self.images():
            self.data[sig:sig + 4] = struct.pack("<I", 0x180)
            self.data[sig + 4:sig + 388] = pss_sign(key2, bytes(self.data[off:sig]))
        self.data[BL1_HDR_SIGNED + 4:BL1_HDR_SIGNED + 388] = pss_sign(key1, bytes(self.data[:BL1_HDR_SIGNED]))


def key_id(n):
    """Key identity as used in the docs: SHA-256 over the 384-byte modulus."""
    return hashlib.sha256(n.to_bytes(384, "big")).hexdigest()[:8]


def cmd_bl1(args):
    b = Bl1(Path(args.file).read_bytes())
    (n1, _), (n2, _) = b.keys()
    print(f"key 1 {key_id(n1)}…  key 2 {key_id(n2)}…")
    for _, name, off, size, load, sig in b.images():
        print(f"  {name}: {off:#8x} +{size:#8x}  load {load:#010x}  signature @{sig:#x}")
    if args.extract:
        out = Path(args.extract)
        out.mkdir(parents=True, exist_ok=True)
        for _, name, off, size, _, _ in b.images():
            (out / f"{name}.bin").write_bytes(b.data[off:off + size])
    for name, src in (r.split("=", 1) for r in args.replace or []):
        b.replace(name, Path(src).read_bytes())
    if args.resign:
        if not (args.key1 and args.key2):
            sys.exit("--resign needs --key1 and --key2")
        b.resign(args.key1, args.key2)
        Path(args.resign).write_bytes(bytes(b.data))
        print(f"wrote {args.resign}")
    if args.verify or args.resign:
        res = b.verify()
        for n, ok in res:
            print(f"  verify {n}: {'ok' if ok else 'FAILED'}")
        if not all(ok for _, ok in res):
            sys.exit(1)


# --------------------------------------------------------------------------
# StandaloneMM inside OP-TEE
# --------------------------------------------------------------------------

def find_stmm(data):
    """(offset, compressed bytes, firmware volume) of the zlib-compressed
    StMM FV that OP-TEE embeds with CFG_STMM_PATH."""
    i = 0
    while True:
        i = data.find(b"\x78", i)
        if i < 0:
            raise ValueError("no embedded StandaloneMM found")
        if data[i + 1] in (0x01, 0x5E, 0x9C, 0xDA):
            o = zlib.decompressobj()
            try:
                fv = o.decompress(data[i:])
            except zlib.error:
                fv = b""
            if len(fv) > 0x10000 and fv[0x28:0x2C] == b"_FVH" and o.eof:
                used = len(data) - i - len(o.unused_data)
                return i, data[i:i + used], fv
        i += 1


def cmd_stmm(args):
    data = Path(args.file).read_bytes()
    if struct.unpack_from("<I", data)[0] == FIP_TOC_MAGIC:
        tos = [e for e in fip_entries(data) if e[0].startswith("05d0e189")]
        if not tos:
            sys.exit("FIP has no tos-fw entry")
        _, off, size, _ = tos[0]
        data = data[off:off + size]
    off, comp, fv = find_stmm(data)
    Path(args.output).write_bytes(fv)
    if args.compressed:
        Path(args.compressed).write_bytes(comp)
    print(f"StMM at {off:#x}: {len(comp)} bytes compressed, FV {len(fv)} bytes, "
          f"sha256 {hashlib.sha256(fv).hexdigest()}")


# --------------------------------------------------------------------------

def cmd_pack(args):
    cfg_path = Path(args.config)
    cfg = json.loads(cfg_path.read_text())
    base = Path(args.base) if args.base else cfg_path.parent
    try:
        img = pack_ota(cfg, base) if args.ota else pack_all(cfg, base)
    except PackError as e:
        sys.exit(f"cixfw pack: {e}")
    Path(args.ota or args.output).write_bytes(img)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("-c", "--config", required=True)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("-o", "--output")
    g.add_argument("-O", "--ota")
    p.add_argument("--base", help="directory the file names are relative to (default: the config's)")
    p.set_defaults(fn=cmd_pack)
    p = sub.add_parser("fip")
    p.add_argument("file")
    p.add_argument("--extract")
    p.add_argument("--verify", action="store_true", help="check the TBBR certificate chain and image hashes")
    p.add_argument("--root", help="root-of-trust key (PEM) BL2 holds; default: the key that signed the chain")
    p.set_defaults(fn=cmd_fip)
    p = sub.add_parser("bl1")
    p.add_argument("file")
    p.add_argument("--verify", action="store_true")
    p.add_argument("--extract", metavar="DIR", help="write se.bin, pm.bin, ap.bin")
    p.add_argument("--replace", action="append", metavar="se|pm|ap=FILE")
    p.add_argument("--resign", metavar="OUT")
    p.add_argument("--key1", help="header key (CIX default chain: oem_privatekey.pem)")
    p.add_argument("--key2", help="image key (CIX default chain: cix_privatekey.pem)")
    p.set_defaults(fn=cmd_bl1)
    p = sub.add_parser("stmm")
    p.add_argument("file", help="OP-TEE image or FIP (bootloader2)")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--compressed")
    p.set_defaults(fn=cmd_stmm)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()

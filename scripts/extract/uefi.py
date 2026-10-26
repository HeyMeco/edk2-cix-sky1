#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Minimal UEFI firmware volume / FFS walker.

Enough of the PI specification to take a CIX SPI image apart: find the BL33
FIP, the UEFI FD inside it, walk firmware volumes (including LZMA-compressed
nested volumes) and return every FFS file with its sections.  Only the
standard library is used so the tool runs on any host.
"""

import lzma
import struct
import uuid
from dataclasses import dataclass, field

FV_SIGNATURE = b"_FVH"
FIP_TOC_MAGIC = 0xAA640001

LZMA_GUID = uuid.UUID("EE4E5898-3914-4259-9D6E-DC7BD79403CF")
CRC32_GUID = uuid.UUID("FC1BCDB0-7D31-49AA-936A-A4600D9DD083")

# EFI_SECTION_* types
SEC_COMPRESSION = 0x01
SEC_GUID_DEFINED = 0x02
SEC_PE32 = 0x10
SEC_PIC = 0x11
SEC_TE = 0x12
SEC_DXE_DEPEX = 0x13
SEC_VERSION = 0x14
SEC_USER_INTERFACE = 0x15
SEC_FV_IMAGE = 0x17
SEC_FREEFORM_SUBTYPE_GUID = 0x18
SEC_RAW = 0x19
SEC_PEI_DEPEX = 0x1B

FFS_TYPES = {
    0x01: "RAW", 0x02: "FREEFORM", 0x03: "SECURITY_CORE", 0x04: "PEI_CORE",
    0x05: "DXE_CORE", 0x06: "PEIM", 0x07: "DRIVER", 0x08: "COMBINED",
    0x09: "APPLICATION", 0x0A: "MM", 0x0B: "FV_IMAGE", 0x0C: "COMBINED_MM_DXE",
    0x0D: "MM_CORE", 0x0E: "MM_STANDALONE", 0x0F: "MM_CORE_STANDALONE",
    0xF0: "PAD",
}


def guid_str(raw: bytes) -> str:
    return str(uuid.UUID(bytes_le=raw)).upper()


@dataclass
class Section:
    type: int
    data: bytes
    children: list = field(default_factory=list)
    guid: str = ""


@dataclass
class FfsFile:
    guid: str
    type: int
    data: bytes
    sections: list = field(default_factory=list)
    fv_depth: int = 0

    @property
    def type_name(self) -> str:
        return FFS_TYPES.get(self.type, f"0x{self.type:02X}")

    def walk_sections(self, types=None):
        stack = list(self.sections)
        while stack:
            s = stack.pop(0)
            if types is None or s.type in types:
                yield s
            stack[0:0] = s.children

    @property
    def ui_name(self) -> str:
        for s in self.walk_sections({SEC_USER_INTERFACE}):
            return s.data.decode("utf-16-le", "replace").rstrip("\x00")
        return ""


def _u24(b: bytes, off: int) -> int:
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16)


def parse_sections(buf: bytes, depth: int, files_out: list) -> list:
    out = []
    off = 0
    while off + 4 <= len(buf):
        size = _u24(buf, off)
        stype = buf[off + 3]
        hdr = 4
        if size == 0xFFFFFF:
            size = struct.unpack_from("<I", buf, off + 4)[0]
            hdr = 8
        if size < hdr or off + size > len(buf):
            break
        body = buf[off + hdr: off + size]
        sec = Section(stype, body)
        if stype == SEC_GUID_DEFINED:
            g = uuid.UUID(bytes_le=body[:16])
            data_off, _attr = struct.unpack_from("<HH", body, 16)
            payload = buf[off + data_off: off + size]
            sec.guid = str(g).upper()
            if g == LZMA_GUID:
                payload = lzma.decompress(payload, format=lzma.FORMAT_ALONE)
                sec.data = payload
                sec.children = parse_sections(payload, depth, files_out)
            elif g == CRC32_GUID:
                sec.data = payload
                sec.children = parse_sections(payload, depth, files_out)
        elif stype == SEC_COMPRESSION:
            # EFI standard (Tiano) compression is not used by CIX builds.
            pass
        elif stype == SEC_FV_IMAGE:
            files_out.extend(parse_fv(body, depth + 1))
        out.append(sec)
        off = (off + size + 3) & ~3
    return out


def parse_fv(buf: bytes, depth: int = 0) -> list:
    if buf[40:44] != FV_SIGNATURE:
        raise ValueError("not a firmware volume")
    fv_len = struct.unpack_from("<Q", buf, 32)[0]
    hdr_len = struct.unpack_from("<H", buf, 48)[0]
    ext_off = struct.unpack_from("<H", buf, 52)[0]
    erase = 0xFF if struct.unpack_from("<I", buf, 44)[0] & 0x800 else 0x00
    off = hdr_len
    if ext_off:
        ext_size = struct.unpack_from("<I", buf, ext_off + 16)[0]
        off = ext_off + ext_size
    off = (off + 7) & ~7
    files = []
    end = min(fv_len, len(buf))
    while off + 24 <= end:
        hdr = buf[off: off + 24]
        if hdr == bytes([erase]) * 24:
            break
        size = _u24(hdr, 20)
        attr = hdr[19]
        hlen = 24
        if attr & 0x01:  # FFS_ATTRIB_LARGE_FILE
            size = struct.unpack_from("<Q", buf, off + 24)[0]
            hlen = 32
        if size < hlen or off + size > end:
            break
        ftype = hdr[18]
        body = buf[off + hlen: off + size]
        f = FfsFile(guid_str(hdr[:16]), ftype, body, fv_depth=depth)
        nested = []
        if ftype not in (0x01, 0xF0):  # RAW and PAD files carry no sections
            f.sections = parse_sections(body, depth, nested)
        files.append(f)
        files.extend(nested)
        off = (off + size + 7) & ~7
    return files


def find_fip(image: bytes, start: int = 0) -> int:
    """Return the offset of the first FIP ToC at or after *start*."""
    magic = struct.pack("<I", FIP_TOC_MAGIC)
    pos = image.find(magic, start)
    while pos >= 0 and pos % 0x1000:
        pos = image.find(magic, pos + 1)
    return pos


def fip_entries(image: bytes, fip_off: int):
    """Yield (uuid, offset, size) for each FIP ToC entry."""
    off = fip_off + 16
    while True:
        u = image[off: off + 16]
        if u == b"\x00" * 16:
            return
        ofs, size, _flags = struct.unpack_from("<QQQ", image, off + 16)
        yield str(uuid.UUID(bytes=u)).upper(), fip_off + ofs, size
        off += 40


def find_fd(image: bytes, start: int) -> int:
    """Locate the first FV header at or after *start* (the BL33 FD base)."""
    # The ZeroVector of the CIX BL33 FD holds the reset branch, so only the
    # signature and a sane header length are checked.
    pos = image.find(FV_SIGNATURE, start + 40)
    while pos >= 0:
        base = pos - 40
        hdr_len = struct.unpack_from("<H", image, base + 48)[0]
        if 0x48 <= hdr_len < 0x1000 and hdr_len % 8 == 0:
            return base
        pos = image.find(FV_SIGNATURE, pos + 1)
    raise ValueError("no firmware volume found")


def acpi_tables(files) -> list:
    """Return (signature, bytes) for every ACPI table in AcpiTables FFS files."""
    tables = []
    for f in files:
        for s in f.walk_sections({SEC_RAW}):
            d = s.data
            if len(d) >= 36 and d[:4].isalnum() and struct.unpack_from("<I", d, 4)[0] <= len(d):
                length = struct.unpack_from("<I", d, 4)[0]
                if length >= 36 and sum(d[:length]) & 0xFF == 0:
                    tables.append((f.guid, d[:4].decode(), d[:length]))
    return tables

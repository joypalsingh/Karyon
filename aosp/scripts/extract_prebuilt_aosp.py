#!/usr/bin/env python3
"""
Pure-Python extractor and packager for prebuilt AOSP Android 14 (API 34) disk images.

Supports:
  - GPT partitioned disk images (`EFI PART`, e.g. Android SDK emulator `system.img`)
  - Android Dynamic Partitions (`super` / liblp `0x616c4467` + `0x414C5030`)
  - Android Sparse Images (`0xED26FF3A`) and raw `ext4` (`0xEF53`) partition images
  - Flattening `.apex` (`apex_payload.img` ext4 inside zip) into `apex/<module>/`
  - Exporting directly into POSIX `.tar` / `.tar.gz` preserving permissions and symlinks

Zero third-party packages required (Python 3.10+ stdlib only).
"""

from __future__ import annotations

import argparse
import dataclasses
import gzip
import io
import json
import os
import struct
import sys
import tarfile
import zipfile
from typing import BinaryIO, Dict, Iterator, List, Optional, Tuple


EXT4_SUPER_MAGIC = 0xEF53
EXT4_EXTENT_MAGIC = 0xF30A
EXT4_EXTENTS_FL = 0x80000

LP_GEOMETRY_MAGIC = 0x616C4467
LP_HEADER_MAGIC = 0x414C5030
SPARSE_MAGIC = 0xED26FF3A

S_IFMT = 0xF000
S_IFLNK = 0xA000
S_IFREG = 0x8000
S_IFDIR = 0x4000

# Hardware-only init rc services pruned in `--minimal-daemons` mode to stay under Android 12+ child limits.
HARDWARE_DAEMON_PREFIXES = (
    "android.hardware.camera",
    "android.hardware.bluetooth",
    "android.hardware.gnss",
    "android.hardware.nfc",
    "android.hardware.radio",
    "android.hardware.sensors",
    "android.hardware.thermal",
    "android.hardware.vibrator",
    "rild",
    "ueventd",
    "watchdogd",
)


@dataclasses.dataclass
class PartitionSlice:
    name: str
    offset: int
    size: int


class OffsetReader:
    """Thread/slice wrapper around an underlying seekable binary stream (supports sparse images cleanly)."""

    def __init__(self, stream: BinaryIO, base_offset: int, length: int) -> None:
        self._stream = stream
        self._base = base_offset
        self._length = length

    def read_at(self, offset: int, size: int) -> bytes:
        if offset < 0 or offset >= self._length or size <= 0:
            return b""
        to_read = min(size, self._length - offset)
        self._stream.seek(self._base + offset)
        return self._stream.read(to_read)


def decode_sparse_to_temp(stream: BinaryIO, out_raw: BinaryIO) -> int:
    """Expands an Android sparse image (0xED26FF3A) into a raw seekable stream and returns raw size."""
    stream.seek(0)
    hdr = stream.read(28)
    if len(hdr) < 28:
        raise ValueError("Truncated sparse image header")
    magic, maj, minv, file_hdr_sz, chunk_hdr_sz, blk_sz, total_blks, total_chunks, _ = struct.unpack(
        "<IHHHHIIII", hdr
    )
    if magic != SPARSE_MAGIC:
        raise ValueError("Not an Android sparse image")
    stream.seek(file_hdr_sz)
    written_bytes = 0
    zero_block = b"\x00" * blk_sz
    for _ in range(total_chunks):
        ch = stream.read(12)
        if len(ch) < 12:
            break
        if chunk_hdr_sz > 12:
            stream.read(chunk_hdr_sz - 12)
        ctype, _, chunk_sz, total_sz = struct.unpack("<HHII", ch)
        data_sz = total_sz - chunk_hdr_sz
        if ctype == 0xCAC1:  # RAW
            remaining = chunk_sz * blk_sz
            while remaining > 0:
                buf = stream.read(min(65536, remaining))
                if not buf:
                    break
                out_raw.write(buf)
                remaining -= len(buf)
            written_bytes += chunk_sz * blk_sz
        elif ctype == 0xCAC2:  # FILL
            fill = stream.read(4)
            pat = fill * (blk_sz // 4)
            for _ in range(chunk_sz):
                out_raw.write(pat)
            written_bytes += chunk_sz * blk_sz
        elif ctype == 0xCAC3:  # DONT_CARE
            for _ in range(chunk_sz):
                out_raw.write(zero_block)
            written_bytes += chunk_sz * blk_sz
        else:
            if data_sz > 0:
                stream.seek(data_sz, os.SEEK_CUR)
    return written_bytes


def discover_partitions(stream: BinaryIO, total_size: int) -> List[PartitionSlice]:
    """Detects GPT -> liblp super partitions or raw ext4 partition."""
    # Check raw ext4 at offset 0
    stream.seek(1024)
    sb = stream.read(64)
    if len(sb) >= 0x3A and struct.unpack("<H", sb[0x38:0x3A])[0] == EXT4_SUPER_MAGIC:
        return [PartitionSlice("system", 0, total_size)]

    # Check GPT at LBA 1 (512)
    stream.seek(512)
    gpt = stream.read(512)
    if len(gpt) >= 92 and gpt[:8] == b"EFI PART":
        part_lba, num_parts, entry_sz = struct.unpack("<QII", gpt[72:88])
        stream.seek(part_lba * 512)
        gpt_parts: List[PartitionSlice] = []
        for _ in range(min(num_parts, 128)):
            ent = stream.read(entry_sz)
            if len(ent) < 128:
                break
            first_lba, last_lba = struct.unpack("<QQ", ent[32:48])
            if first_lba == 0:
                continue
            pname = ent[56:128].decode("utf-16-le", "ignore").rstrip("\x00")
            gpt_parts.append(PartitionSlice(pname, first_lba * 512, (last_lba - first_lba + 1) * 512))

        for part in gpt_parts:
            if part.name == "super":
                lp_slices = parse_liblp_super(stream, part.offset)
                if lp_slices:
                    return lp_slices
        return [p for p in gpt_parts if p.name in ("system", "vendor", "system_ext", "product")]

    # Check direct super image
    lp_slices = parse_liblp_super(stream, 0)
    if lp_slices:
        return lp_slices
    return []


def parse_liblp_super(stream: BinaryIO, super_offset: int) -> List[PartitionSlice]:
    """Parses Android dynamic `super` metadata (`0x616c4467` geometry + `0x414C5030` header)."""
    stream.seek(super_offset + 4096)
    geom = stream.read(4)
    if len(geom) < 4 or struct.unpack("<I", geom)[0] != LP_GEOMETRY_MAGIC:
        return []

    hdr_offset = super_offset + 12288
    stream.seek(hdr_offset)
    hdr = stream.read(256)
    if len(hdr) < 128:
        return []
    magic, _, _, hdr_sz = struct.unpack("<IHHI", hdr[:12])
    if magic != LP_HEADER_MAGIC:
        return []

    part_off, part_cnt, part_esz = struct.unpack("<III", hdr[80:92])
    ext_off, ext_cnt, ext_esz = struct.unpack("<III", hdr[92:104])
    tables_base = hdr_offset + hdr_sz

    stream.seek(tables_base + ext_off)
    extents = []
    for _ in range(ext_cnt):
        ext_data = stream.read(ext_esz)
        extents.append(struct.unpack("<QIIQ", ext_data[:24]))

    slices: List[PartitionSlice] = []
    stream.seek(tables_base + part_off)
    for _ in range(part_cnt):
        ent = stream.read(part_esz)
        pname = ent[:36].split(b"\x00", 1)[0].decode("utf-8", "replace")
        _, first_ext, num_ext = struct.unpack("<III", ent[36:48])
        if num_ext == 0:
            continue
        num_secs, target_type, target_data, _ = extents[first_ext]
        if target_type != 0:  # LP_TARGET_TYPE_LINEAR
            continue
        part_off_bytes = super_offset + target_data * 512
        # Confirm ext4 filesystem magic before adding (skips erofs system_dlkm cleanly if unsupported)
        cur = stream.tell()
        stream.seek(part_off_bytes + 1024)
        sb = stream.read(64)
        stream.seek(cur)
        if len(sb) >= 0x3A and struct.unpack("<H", sb[0x38:0x3A])[0] == EXT4_SUPER_MAGIC:
            clean_name = pname[:-2] if pname.endswith(("_a", "_b")) else pname
            slices.append(PartitionSlice(clean_name, part_off_bytes, num_secs * 512))
    return slices


@dataclasses.dataclass
class Ext4Inode:
    ino: int
    mode: int
    uid: int
    gid: int
    size: int
    flags: int
    raw_blocks: bytes


class Ext4Volume:
    """Read-only pure-Python ext4 volume reader (handles extents, direct/indirect blocks, symlinks, dirs)."""

    def __init__(self, reader: OffsetReader) -> None:
        self._r = reader
        sb = self._r.read_at(1024, 1024)
        if len(sb) < 256:
            raise ValueError("Truncated ext4 superblock")
        magic = struct.unpack("<H", sb[0x38:0x3A])[0]
        if magic != EXT4_SUPER_MAGIC:
            raise ValueError(f"Invalid ext4 magic: {hex(magic)}")

        self.inodes_count, self.blocks_count_lo = struct.unpack("<II", sb[0:8])
        log_bsz = struct.unpack("<I", sb[0x18:0x1C])[0]
        self.block_size = 1024 << log_bsz
        self.blocks_per_group = struct.unpack("<I", sb[0x20:0x24])[0]
        self.inodes_per_group = struct.unpack("<I", sb[0x28:0x2C])[0]
        self.inode_size = struct.unpack("<H", sb[0x58:0x5A])[0]
        self.feature_incompat = struct.unpack("<I", sb[0x60:0x64])[0]
        self.desc_size = struct.unpack("<H", sb[0xFE:0x100])[0] if (self.feature_incompat & 0x80) else 32
        if self.desc_size < 32:
            self.desc_size = 32
        self._bgd_base = self.block_size if self.block_size > 1024 else 2048

    def _read_block(self, blk: int) -> bytes:
        if blk == 0:
            return b"\x00" * self.block_size
        return self._r.read_at(blk * self.block_size, self.block_size)

    def _inode_table_block(self, group: int) -> int:
        bgd = self._r.read_at(self._bgd_base + group * self.desc_size, self.desc_size)
        lo = struct.unpack("<I", bgd[8:12])[0]
        hi = struct.unpack("<I", bgd[40:44])[0] if self.desc_size >= 64 else 0
        return (hi << 32) | lo

    def read_inode(self, ino: int) -> Ext4Inode:
        group = (ino - 1) // self.inodes_per_group
        index = (ino - 1) % self.inodes_per_group
        tbl_blk = self._inode_table_block(group)
        off = tbl_blk * self.block_size + index * self.inode_size
        raw = self._r.read_at(off, self.inode_size)
        mode, uid_lo, size_lo = struct.unpack("<HHI", raw[0:8])
        gid_lo = struct.unpack("<H", raw[0x18:0x1A])[0]
        flags = struct.unpack("<I", raw[0x20:0x24])[0]
        blocks_bytes = raw[0x28:0x64]  # 60 bytes = 15 uint32
        size_hi = struct.unpack("<I", raw[0x6C:0x70])[0]
        uid_hi, gid_hi = struct.unpack("<HH", raw[0x78:0x7C]) if len(raw) >= 0x7C else (0, 0)
        full_size = size_lo if (mode & S_IFMT) == S_IFDIR else (size_lo | (size_hi << 32))
        return Ext4Inode(
            ino=ino,
            mode=mode,
            uid=uid_lo | (uid_hi << 16),
            gid=gid_lo | (gid_hi << 16),
            size=full_size,
            flags=flags,
            raw_blocks=blocks_bytes,
        )

    def _iter_extent_blocks(self, node_bytes: bytes, file_blocks_needed: int) -> Dict[int, int]:
        out: Dict[int, int] = {}
        magic, entries, _, depth, _ = struct.unpack("<HHHHI", node_bytes[:12])
        if magic != EXT4_EXTENT_MAGIC:
            return out
        if depth == 0:
            for i in range(entries):
                ext = node_bytes[12 + i * 12 : 24 + i * 12]
                ee_block, ee_len, ee_start_hi, ee_start_lo = struct.unpack("<IHHI", ext)
                # Uninitialized extent flag uses bit 15 (`ee_len > 32768`) -> treat as zero sparse holes
                unwritten = ee_len > 0x8000
                actual_len = ee_len - 0x8000 if unwritten else ee_len
                phys_start = (ee_start_hi << 32) | ee_start_lo
                for j in range(actual_len):
                    lblk = ee_block + j
                    if lblk >= file_blocks_needed:
                        break
                    out[lblk] = 0 if unwritten else (phys_start + j)
        else:
            for i in range(entries):
                idx = node_bytes[12 + i * 12 : 24 + i * 12]
                _, ei_leaf_lo, ei_leaf_hi, _ = struct.unpack("<IIHH", idx)
                child_blk = (ei_leaf_hi << 32) | ei_leaf_lo
                out.update(self._iter_extent_blocks(self._read_block(child_blk), file_blocks_needed))
        return out

    def read_inode_bytes(self, inode: Ext4Inode) -> bytes:
        if inode.size == 0:
            return b""
        if (inode.mode & S_IFMT) == S_IFLNK and inode.size < 60 and not (inode.flags & EXT4_EXTENTS_FL):
            return inode.raw_blocks[: inode.size]

        total_blocks = (inode.size + self.block_size - 1) // self.block_size
        chunks: List[bytes] = []
        if inode.flags & EXT4_EXTENTS_FL:
            blk_map = self._iter_extent_blocks(inode.raw_blocks, total_blocks)
            for lblk in range(total_blocks):
                chunks.append(self._read_block(blk_map.get(lblk, 0)))
        else:
            ptrs = list(struct.unpack("<15I", inode.raw_blocks))
            direct = ptrs[:12]
            for lblk in range(min(total_blocks, 12)):
                chunks.append(self._read_block(direct[lblk]))
            if total_blocks > 12 and ptrs[12]:
                ind = self._read_block(ptrs[12])
                per_blk = self.block_size // 4
                ind_ptrs = struct.unpack(f"<{per_blk}I", ind)
                for lblk in range(12, min(total_blocks, 12 + per_blk)):
                    chunks.append(self._read_block(ind_ptrs[lblk - 12]))
        return b"".join(chunks)[: inode.size]

    def read_dir(self, inode: Ext4Inode) -> List[Tuple[int, str, int]]:
        data = self.read_inode_bytes(inode)
        entries: List[Tuple[int, str, int]] = []
        pos = 0
        while pos + 8 <= len(data):
            d_ino, rec_len, name_len, ftype = struct.unpack("<IHBB", data[pos : pos + 8])
            if rec_len < 8 or pos + rec_len > len(data):
                break
            if d_ino != 0 and name_len > 0 and pos + 8 + name_len <= len(data):
                name = data[pos + 8 : pos + 8 + name_len].decode("utf-8", "replace")
                if name not in (".", ".."):
                    entries.append((d_ino, name, ftype))
            pos += rec_len
        return entries

    def walk(self, root_prefix: str) -> Iterator[Tuple[str, Ext4Inode]]:
        stack: List[Tuple[int, str]] = [(2, root_prefix)]
        visited = set()
        while stack:
            ino, prefix = stack.pop()
            if ino in visited:
                continue
            visited.add(ino)
            inode = self.read_inode(ino)
            if prefix:
                yield (prefix, inode)
            if (inode.mode & S_IFMT) == S_IFDIR:
                children = self.read_dir(inode)
                for c_ino, cname, _ in reversed(children):
                    child_rel = f"{prefix}/{cname}" if prefix else cname
                    child_inode = self.read_inode(c_ino)
                    if (child_inode.mode & S_IFMT) == S_IFDIR:
                        stack.append((c_ino, child_rel))
                    else:
                        yield (child_rel, child_inode)


def should_prune_for_minimal_daemons(rel_path: str) -> bool:
    base = rel_path.rsplit("/", 1)[-1]
    if "/etc/init/" in rel_path and base.endswith(".rc"):
        return any(base.startswith(p) for p in HARDWARE_DAEMON_PREFIXES)
    return False


def add_bytes_to_tar(tf: tarfile.TarFile, tar_path: str, payload: bytes, mode: int = 0o644) -> None:
    info = tarfile.TarInfo(tar_path)
    info.size = len(payload)
    info.mode = mode
    tf.addfile(info, io.BytesIO(payload))


def flatten_apex_into_tar(tf: tarfile.TarFile, apex_name: str, apex_bytes: bytes, depth: int = 0) -> int:
    """Extracts an `.apex` or compressed `.capex` (`original_apex`) ext4 payload into `apex/<module>/...`."""
    if depth > 2:
        return 0
    try:
        zf = zipfile.ZipFile(io.BytesIO(apex_bytes))
    except zipfile.BadZipFile:
        return 0
    names = zf.namelist()
    if "apex_payload.img" not in names:
        if "original_apex" in names:
            return flatten_apex_into_tar(tf, apex_name, zf.read("original_apex"), depth + 1)
        return 0
    payload = zf.read("apex_payload.img")
    if len(payload) < 2048:
        return 0
    buf = io.BytesIO(payload)
    vol = Ext4Volume(OffsetReader(buf, 0, len(payload)))
    mod_name = apex_name.rsplit("/", 1)[-1]
    for suf in (".apex", ".capex"):
        if mod_name.endswith(suf):
            mod_name = mod_name[: -len(suf)]
    count = 0
    for rel, inode in vol.walk(f"apex/{mod_name}"):
        if rel == "lost+found" or "/lost+found" in rel:
            continue
        ftype = inode.mode & S_IFMT
        info = tarfile.TarInfo(rel)
        info.mode = inode.mode & 0o777
        if ftype == S_IFDIR:
            info.type = tarfile.DIRTYPE
            tf.addfile(info)
        elif ftype == S_IFLNK:
            info.type = tarfile.SYMTYPE
            info.linkname = vol.read_inode_bytes(inode).decode("utf-8", "replace")
            tf.addfile(info)
        elif ftype == S_IFREG:
            data = vol.read_inode_bytes(inode)
            info.size = len(data)
            tf.addfile(info, io.BytesIO(data))
            count += 1
    return count


def inspect_image(img_path: str) -> Dict[str, object]:
    with open(img_path, "rb") as raw_f:
        head = raw_f.read(4)
        if len(head) == 4 and struct.unpack("<I", head)[0] == SPARSE_MAGIC:
            tmp = io.BytesIO()
            total_sz = decode_sparse_to_temp(raw_f, tmp)
            stream: BinaryIO = tmp
        else:
            raw_f.seek(0, os.SEEK_END)
            total_sz = raw_f.tell()
            stream = raw_f

        slices = discover_partitions(stream, total_sz)
        part_reports = []
        for s in slices:
            vol = Ext4Volume(OffsetReader(stream, s.offset, s.size))
            files = 0
            dirs = 0
            links = 0
            has_init = False
            has_services = False
            has_gms = False
            has_phonesky = False
            apex_count = 0
            for rel, inode in vol.walk(s.name):
                ftype = inode.mode & S_IFMT
                if ftype == S_IFDIR:
                    dirs += 1
                elif ftype == S_IFLNK:
                    links += 1
                elif ftype == S_IFREG:
                    files += 1
                    if rel.endswith("/bin/init") or rel == "system/system/bin/init":
                        has_init = True
                    if rel.endswith("/framework/services.jar"):
                        has_services = True
                    if "gmscore" in rel.lower() or "prebuiltgms" in rel.lower():
                        has_gms = True
                    if "phonesky" in rel.lower():
                        has_phonesky = True
                    if rel.endswith(".apex"):
                        apex_count += 1
            part_reports.append(
                {
                    "name": s.name,
                    "size_mb": round(s.size / (1024 * 1024), 1),
                    "files": files,
                    "dirs": dirs,
                    "symlinks": links,
                    "apex_files": apex_count,
                    "has_init": has_init,
                    "has_services_jar": has_services,
                    "has_gapps_gms": has_gms,
                    "has_play_store": has_phonesky,
                }
            )
        return {"image": img_path, "partitions": part_reports}


def should_include_for_core_profile(rel_path: str) -> bool:
    """Keeps lightweight core userland binaries, libs, framework jars, and runtime APEXes."""
    core_prefixes = (
        "system/bin/",
        "system/lib64/",
        "system/etc/",
        "system/framework/",
        "system/build.prop",
        "system/apex/com.android.runtime",
        "system/apex/com.android.art",
        "system/apex/com.android.i18n",
    )
    return rel_path in ("system", "system/bin", "system/lib64", "system/etc", "system/framework", "system/apex") or rel_path.startswith(core_prefixes)


def pack_rootfs_tar(
    img_path: str,
    out_tar: str,
    minimal_daemons: bool = True,
    flatten_apex: bool = True,
    profile: str = "full",
) -> Dict[str, int]:
    tar_mode = "w:gz" if out_tar.endswith((".tar.gz", ".tgz")) else "w"
    stats = {"files": 0, "dirs": 0, "symlinks": 0, "pruned_rc": 0, "apex_files": 0}

    with open(img_path, "rb") as raw_f:
        head = raw_f.read(4)
        if len(head) == 4 and struct.unpack("<I", head)[0] == SPARSE_MAGIC:
            tmp = io.BytesIO()
            total_sz = decode_sparse_to_temp(raw_f, tmp)
            stream: BinaryIO = tmp
        else:
            raw_f.seek(0, os.SEEK_END)
            total_sz = raw_f.tell()
            stream = raw_f

        slices = discover_partitions(stream, total_sz)
        if not slices:
            raise RuntimeError(f"No readable ext4/super partitions found in {img_path}")

        if profile == "core":
            slices = [s for s in slices if s.name == "system"]

        with tarfile.open(out_tar, tar_mode, format=tarfile.GNU_FORMAT) as tf:
            for s in slices:
                vol = Ext4Volume(OffsetReader(stream, s.offset, s.size))
                normalize_sar = False
                if s.name == "system":
                    top_modes = {name: (vol.read_inode(ino).mode & S_IFMT) for ino, name, _ in vol.read_dir(vol.read_inode(2))}
                    normalize_sar = (top_modes.get("system") == S_IFDIR and top_modes.get("bin") != S_IFDIR)

                walk_prefix = "" if normalize_sar else s.name
                for raw_rel, inode in vol.walk(walk_prefix):
                    rel = raw_rel
                    if not rel or rel == "lost+found" or "/lost+found" in rel:
                        continue
                    if profile == "core" and not should_include_for_core_profile(rel):
                        continue
                    if minimal_daemons and should_prune_for_minimal_daemons(rel):
                        stats["pruned_rc"] += 1
                        continue

                    ftype = inode.mode & S_IFMT
                    if ftype == S_IFDIR:
                        info = tarfile.TarInfo(rel)
                        info.type = tarfile.DIRTYPE
                        info.mode = inode.mode & 0o777
                        tf.addfile(info)
                        stats["dirs"] += 1
                    elif ftype == S_IFLNK:
                        info = tarfile.TarInfo(rel)
                        info.type = tarfile.SYMTYPE
                        info.mode = 0o777
                        info.linkname = vol.read_inode_bytes(inode).decode("utf-8", "replace")
                        tf.addfile(info)
                        stats["symlinks"] += 1
                    elif ftype == S_IFREG:
                        data = vol.read_inode_bytes(inode)
                        if flatten_apex and (rel.endswith(".apex") or rel.endswith(".capex")) and "/apex/" in rel:
                            unpacked = flatten_apex_into_tar(tf, rel, data)
                            stats["apex_files"] += unpacked
                            if profile == "core" and unpacked > 0:
                                continue
                        info = tarfile.TarInfo(rel)
                        info.size = len(data)
                        info.mode = inode.mode & 0o777
                        tf.addfile(info, io.BytesIO(data))
                        stats["files"] += 1

            meta = json.dumps(
                {
                    "format": "karyon-prebuilt-rootfs-v1",
                    "profile": profile,
                    "minimal_daemons": minimal_daemons,
                    "flattened_apex": flatten_apex,
                    "stats": stats,
                },
                indent=2,
            ).encode("utf-8")
            add_bytes_to_tar(tf, "system/etc/karyon_prebuilt.json", meta, 0o644)

    return stats


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Inspect or pack prebuilt AOSP 14 disk images for Karyon")
    parser.add_argument("image", help="Path to prebuilt AOSP system.img / super.img")
    parser.add_argument("--inspect", action="store_true", help="Inspect partitions without creating a TAR")
    parser.add_argument("--out", help="Output .tar or .tar.gz path")
    parser.add_argument("--profile", choices=["full", "core"], default="full", help="Extraction profile")
    parser.add_argument("--no-minimal-daemons", action="store_true", help="Do not prune hardware vendor .rc files")
    parser.add_argument("--no-flatten-apex", action="store_true", help="Do not unpack .apex payloads into apex/")
    args = parser.parse_args(argv)

    if args.inspect or not args.out:
        report = inspect_image(args.image)
        print(json.dumps(report, indent=2))
        return 0

    stats = pack_rootfs_tar(
        img_path=args.image,
        out_tar=args.out,
        minimal_daemons=not args.no_minimal_daemons,
        flatten_apex=not args.no_flatten_apex,
        profile=args.profile,
    )
    print(json.dumps({"out": args.out, "stats": stats}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

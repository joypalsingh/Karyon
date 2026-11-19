#!/usr/bin/env python3
"""Unit tests for the pure-Python prebuilt AOSP ext4/sparse/TAR extractor."""

from __future__ import annotations

import io
import os
import struct
import tarfile
import tempfile
import unittest

import extract_prebuilt_aosp as ex


def build_synthetic_ext4_image() -> bytes:
    """Builds a minimal valid 4K-block ext4 image containing /bin/toybox, a symlink /bin/sh -> toybox, and an .rc file."""
    bsz = 4096
    num_blocks = 32
    img = bytearray(bsz * num_blocks)

    # Superblock at offset 1024: magic 0xEF53, 4K blocks, 32 inodes, 256-byte inodes, EXTENTS flag
    sb_off = 1024
    struct.pack_into("<II", img, sb_off + 0x00, 32, num_blocks)
    struct.pack_into("<I", img, sb_off + 0x18, 2)       # log_block_size = 2 -> 4096
    struct.pack_into("<I", img, sb_off + 0x20, num_blocks)
    struct.pack_into("<I", img, sb_off + 0x28, 32)
    struct.pack_into("<H", img, sb_off + 0x38, ex.EXT4_SUPER_MAGIC)
    struct.pack_into("<H", img, sb_off + 0x58, 256)     # inode_size
    struct.pack_into("<I", img, sb_off + 0x60, 0x42)    # FILETYPE | EXTENTS

    # BGD at block 1 (offset 4096): inode table starts at block 2 (offset 8192)
    struct.pack_into("<I", img, 4096 + 8, 2)

    inode_tbl = 2 * bsz

    def write_extent_inode(ino: int, mode: int, size: int, phys_block: int) -> None:
        off = inode_tbl + (ino - 1) * 256
        struct.pack_into("<HHI", img, off, mode, 0, size)
        struct.pack_into("<I", img, off + 0x20, ex.EXT4_EXTENTS_FL)
        # Leaf extent header + 1 extent
        ib = off + 0x28
        struct.pack_into("<HHHHI", img, ib, ex.EXT4_EXTENT_MAGIC, 1, 4, 0, 0)
        struct.pack_into("<IHHI", img, ib + 12, 0, 1, 0, phys_block)

    def write_fast_symlink_inode(ino: int, target: bytes) -> None:
        off = inode_tbl + (ino - 1) * 256
        struct.pack_into("<HHI", img, off, ex.S_IFLNK | 0o777, 0, len(target))
        img[off + 0x28 : off + 0x28 + len(target)] = target

    def pack_dir_block(items):
        blk = bytearray(bsz)
        pos = 0
        for idx, (ino, name_str, ftype) in enumerate(items):
            nb = name_str.encode("utf-8")
            rec_len = (bsz - pos) if idx == len(items) - 1 else ((8 + len(nb) + 3) & ~3)
            struct.pack_into("<IHBB", blk, pos, ino, rec_len, len(nb), ftype)
            blk[pos + 8 : pos + 8 + len(nb)] = nb
            pos += rec_len
        return bytes(blk)

    # Blocks: 10=root dir, 11=bin dir, 12=toybox payload, 13=etc dir, 14=init dir
    write_extent_inode(2, ex.S_IFDIR | 0o755, bsz, 10)
    write_extent_inode(3, ex.S_IFDIR | 0o755, bsz, 11)
    write_extent_inode(4, ex.S_IFREG | 0o755, 11, 12)
    write_fast_symlink_inode(5, b"toybox")
    write_extent_inode(6, ex.S_IFDIR | 0o755, bsz, 13)
    write_extent_inode(7, ex.S_IFDIR | 0o755, bsz, 14)
    write_extent_inode(8, ex.S_IFREG | 0o644, 15, 15)

    img[10 * bsz : 11 * bsz] = pack_dir_block([(2, ".", 2), (2, "..", 2), (3, "bin", 2), (6, "etc", 2)])
    img[11 * bsz : 12 * bsz] = pack_dir_block([(3, ".", 2), (2, "..", 2), (4, "toybox", 1), (5, "sh", 7)])
    img[12 * bsz : 12 * bsz + 11] = b"\x7fELF_TOYBOX"
    img[13 * bsz : 14 * bsz] = pack_dir_block([(6, ".", 2), (2, "..", 2), (7, "init", 2)])
    img[14 * bsz : 15 * bsz] = pack_dir_block([(7, ".", 2), (6, "..", 2), (8, "android.hardware.bluetooth.rc", 1)])
    img[15 * bsz : 15 * bsz + 15] = b"service bt /nop"
    return bytes(img)


def build_synthetic_sar_system_image() -> bytes:
    """Builds a minimal system-as-root ext4 image with top-level `/bin -> /system/bin` and `/system/bin/toybox`."""
    bsz = 4096
    num_blocks = 32
    img = bytearray(bsz * num_blocks)
    sb_off = 1024
    struct.pack_into("<II", img, sb_off + 0x00, 32, num_blocks)
    struct.pack_into("<I", img, sb_off + 0x18, 2)
    struct.pack_into("<I", img, sb_off + 0x20, num_blocks)
    struct.pack_into("<I", img, sb_off + 0x28, 32)
    struct.pack_into("<H", img, sb_off + 0x38, ex.EXT4_SUPER_MAGIC)
    struct.pack_into("<H", img, sb_off + 0x58, 256)
    struct.pack_into("<I", img, sb_off + 0x60, 0x42)
    struct.pack_into("<I", img, 4096 + 8, 2)
    inode_tbl = 2 * bsz

    def write_ext(ino: int, mode: int, size: int, blk: int) -> None:
        off = inode_tbl + (ino - 1) * 256
        struct.pack_into("<HHI", img, off, mode, 0, size)
        struct.pack_into("<I", img, off + 0x20, ex.EXT4_EXTENTS_FL)
        ib = off + 0x28
        struct.pack_into("<HHHHI", img, ib, ex.EXT4_EXTENT_MAGIC, 1, 4, 0, 0)
        struct.pack_into("<IHHI", img, ib + 12, 0, 1, 0, blk)

    def write_sym(ino: int, target: bytes) -> None:
        off = inode_tbl + (ino - 1) * 256
        struct.pack_into("<HHI", img, off, ex.S_IFLNK | 0o777, 0, len(target))
        img[off + 0x28 : off + 0x28 + len(target)] = target

    def pack_dir(items):
        blk = bytearray(bsz)
        pos = 0
        for idx, (ino, name_str, ftype) in enumerate(items):
            nb = name_str.encode("utf-8")
            rec_len = (bsz - pos) if idx == len(items) - 1 else ((8 + len(nb) + 3) & ~3)
            struct.pack_into("<IHBB", blk, pos, ino, rec_len, len(nb), ftype)
            blk[pos + 8 : pos + 8 + len(nb)] = nb
            pos += rec_len
        return bytes(blk)

    # ino 2=root (/bin -> /system/bin, /system dir), 3=bin symlink, 4=system dir, 5=system/bin dir, 6=toybox
    write_ext(2, ex.S_IFDIR | 0o755, bsz, 10)
    write_sym(3, b"/system/bin")
    write_ext(4, ex.S_IFDIR | 0o755, bsz, 11)
    write_ext(5, ex.S_IFDIR | 0o755, bsz, 12)
    write_ext(6, ex.S_IFREG | 0o755, 7, 13)

    img[10 * bsz : 11 * bsz] = pack_dir([(2, ".", 2), (2, "..", 2), (3, "bin", 7), (4, "system", 2)])
    img[11 * bsz : 12 * bsz] = pack_dir([(4, ".", 2), (2, "..", 2), (5, "bin", 2)])
    img[12 * bsz : 13 * bsz] = pack_dir([(5, ".", 2), (4, "..", 2), (6, "toybox", 1)])
    img[13 * bsz : 13 * bsz + 7] = b"\x7fELF_TB"
    return bytes(img)


class ExtractorTests(unittest.TestCase):
    def test_reads_ext4_directories_files_symlinks_and_packs_tar(self) -> None:
        raw_img = build_synthetic_ext4_image()
        with tempfile.TemporaryDirectory() as tmp:
            img_path = os.path.join(tmp, "system.img")
            tar_path = os.path.join(tmp, "out.tar")
            with open(img_path, "wb") as f:
                f.write(raw_img)

            report = ex.inspect_image(img_path)
            self.assertEqual(len(report["partitions"]), 1)
            part = report["partitions"][0]
            self.assertEqual(part["files"], 2)
            self.assertEqual(part["symlinks"], 1)

            stats = ex.pack_rootfs_tar(img_path, tar_path, minimal_daemons=True, flatten_apex=False)
            self.assertEqual(stats["pruned_rc"], 1)
            self.assertEqual(stats["symlinks"], 1)

            with tarfile.open(tar_path, "r") as tf:
                names = tf.getnames()
                self.assertIn("system/bin/toybox", names)
                self.assertIn("system/bin/sh", names)
                self.assertNotIn("system/etc/init/android.hardware.bluetooth.rc", names)
                sh_info = tf.getmember("system/bin/sh")
                self.assertTrue(sh_info.issym())
                self.assertEqual(sh_info.linkname, "toybox")
                tb_data = tf.extractfile("system/bin/toybox").read()
                self.assertEqual(tb_data, b"\x7fELF_TOYBOX")

    def test_decodes_android_sparse_image(self) -> None:
        raw_img = build_synthetic_ext4_image()
        blk_sz = 4096
        chunk_count = len(raw_img) // blk_sz
        hdr = struct.pack("<IHHHHIIII", ex.SPARSE_MAGIC, 1, 0, 28, 12, blk_sz, chunk_count, 1, 0)
        chunk_hdr = struct.pack("<HHII", 0xCAC1, 0, chunk_count, 12 + len(raw_img))
        sparse_stream = io.BytesIO(hdr + chunk_hdr + raw_img)
        decoded = io.BytesIO()
        written = ex.decode_sparse_to_temp(sparse_stream, decoded)
        self.assertEqual(written, len(raw_img))
        self.assertEqual(decoded.getvalue(), raw_img)

    def test_system_as_root_preserves_system_bin_without_root_symlink_collision(self) -> None:
        raw_img = build_synthetic_sar_system_image()
        with tempfile.TemporaryDirectory() as tmp:
            img_path = os.path.join(tmp, "system.img")
            tar_path = os.path.join(tmp, "sar.tar")
            with open(img_path, "wb") as f:
                f.write(raw_img)

            ex.pack_rootfs_tar(img_path, tar_path, profile="core")
            with tarfile.open(tar_path, "r") as tf:
                names = tf.getnames()
                self.assertIn("system/bin/toybox", names)
                self.assertNotIn("system/bin/bin", names)
                sb_info = tf.getmember("system/bin")
                self.assertTrue(sb_info.isdir())


if __name__ == "__main__":
    unittest.main()

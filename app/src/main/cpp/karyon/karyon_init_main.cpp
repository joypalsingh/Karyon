#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct FrameHeader {
    uint32_t width;
    uint32_t height;
    uint32_t payload_bytes;
};

// Compact 5x7 bitmap glyphs for ASCII 0x20..0x5F (uppercase/digits/symbols) stored as 5 columns of 7 bits.
struct Glyph {
    char ch;
    uint8_t cols[5];
};

constexpr Glyph kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
    {'#', {0x14, 0x7F, 0x14, 0x7F, 0x14}},
    {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
    {'(', {0x00, 0x1C, 0x22, 0x41, 0x00}},
    {')', {0x00, 0x41, 0x22, 0x1C, 0x00}},
    {'*', {0x14, 0x08, 0x3E, 0x08, 0x14}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {',', {0x00, 0x50, 0x30, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
    {'>', {0x41, 0x22, 0x14, 0x08, 0x00}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'[', {0x00, 0x7F, 0x41, 0x41, 0x00}},
    {']', {0x00, 0x41, 0x41, 0x7F, 0x00}},
    {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
};

const uint8_t* find_glyph(char raw) {
    char c = raw;
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    for (const auto& g : kGlyphs) {
        if (g.ch == c) return g.cols;
    }
    return kGlyphs[0].cols;
}

void put_pixel(std::vector<uint8_t>& fb, uint32_t w, uint32_t h, int x, int y, uint32_t rgba) {
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= w || static_cast<uint32_t>(y) >= h) return;
    const size_t off = (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4;
    fb[off + 0] = static_cast<uint8_t>(rgba & 0xFF);
    fb[off + 1] = static_cast<uint8_t>((rgba >> 8) & 0xFF);
    fb[off + 2] = static_cast<uint8_t>((rgba >> 16) & 0xFF);
    fb[off + 3] = static_cast<uint8_t>((rgba >> 24) & 0xFF);
}

void fill_rect(std::vector<uint8_t>& fb, uint32_t w, uint32_t h, int x, int y, int rw, int rh, uint32_t rgba) {
    for (int py = y; py < y + rh; ++py) {
        for (int px = x; px < x + rw; ++px) {
            put_pixel(fb, w, h, px, py, rgba);
        }
    }
}

void draw_text(std::vector<uint8_t>& fb, uint32_t w, uint32_t h, int x, int y, int scale, uint32_t rgba, const std::string& text) {
    int cx = x;
    for (char ch : text) {
        const uint8_t* cols = find_glyph(ch);
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = cols[col];
            for (int row = 0; row < 7; ++row) {
                if ((bits >> row) & 1) {
                    fill_rect(fb, w, h, cx + col * scale, y + row * scale, scale, scale, rgba);
                }
            }
        }
        cx += 6 * scale;
    }
}

bool write_all(int fd, const void* data, size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = write(fd, ptr + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool has_file(const fs::path& base, const char* rel) {
    std::error_code ec;
    return fs::exists(base / rel, ec);
}

bool has_named_entry(const fs::path& dir, const char* needle1, const char* needle2) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return false;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        for (char& ch : name) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        if ((needle1 && name.find(needle1) != std::string::npos) ||
            (needle2 && name.find(needle2) != std::string::npos)) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::string instance_id = "default";
    std::string instance_dir;
    int ctrl_fd = -1;
    int fb_fd = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--instance" && i + 1 < argc) {
            instance_id = argv[++i];
        } else if (arg == "--instance-dir" && i + 1 < argc) {
            instance_dir = argv[++i];
        } else if (arg == "--ctrl-fd" && i + 1 < argc) {
            ctrl_fd = std::atoi(argv[++i]);
        } else if (arg == "--fb-fd" && i + 1 < argc) {
            fb_fd = std::atoi(argv[++i]);
        }
    }

    const fs::path rfs = fs::path(instance_dir) / "rootfs";
    const fs::path ovl = fs::path(instance_dir) / "overlay";

    const bool has_init = has_file(rfs, "system/bin/init") || has_file(ovl, "system/bin/init");
    const bool has_services = has_file(rfs, "system/framework/services.jar") || has_file(ovl, "system/framework/services.jar");
    const bool has_gms = has_named_entry(rfs / "system/priv-app", "gmscore", "prebuiltgms") ||
                         has_named_entry(ovl / "system/priv-app", "gmscore", "prebuiltgms");
    const bool has_su = has_file(rfs, "system/xbin/su") || has_file(ovl, "system/xbin/su");

    std::printf("KARYON_INIT: started pid=%d uid=%d instance=%s\n", getpid(), getuid(), instance_id.c_str());
    std::printf("KARYON_INIT: rootfs init=%d services=%d gapps=%d su=%d\n",
                has_init ? 1 : 0, has_services ? 1 : 0, has_gms ? 1 : 0, has_su ? 1 : 0);
    std::fflush(stdout);

    if (ctrl_fd >= 0) {
        const std::string hello = "READY pid=" + std::to_string(getpid()) +
                                  " rootfs_bootable=" + std::string((has_init && has_services) ? "1" : "0") + "\n";
        (void)write_all(ctrl_fd, hello.data(), hello.size());
    }

    constexpr uint32_t kW = 540;
    constexpr uint32_t kH = 960;
    std::vector<uint8_t> fb(static_cast<size_t>(kW) * kH * 4, 0);

    std::string last_event = "NONE";
    uint64_t tick = 0;

    while (true) {
        if (ctrl_fd >= 0) {
            struct pollfd pfd { ctrl_fd, POLLIN, 0 };
            const int pr = poll(&pfd, 1, 250);
            if (pr > 0 && (pfd.revents & POLLIN)) {
                char buf[512];
                const ssize_t n = read(ctrl_fd, buf, sizeof(buf) - 1);
                if (n <= 0) break;
                buf[n] = '\0';
                std::string cmd(buf);
                while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
                if (cmd == "STOP") {
                    std::printf("KARYON_INIT: received STOP command, exiting.\n");
                    std::fflush(stdout);
                    break;
                }
                last_event = cmd;
                std::printf("KARYON_INIT: ctrl cmd=%s\n", cmd.c_str());
                std::fflush(stdout);
                const std::string ack = "ACK " + cmd + "\n";
                (void)write_all(ctrl_fd, ack.data(), ack.size());
            } else if (pr > 0 && (pfd.revents & (POLLHUP | POLLERR))) {
                break;
            }
        } else {
            usleep(250000);
        }

        // Render live diagnostic framebuffer every 2 ticks (~0.5s) onto ANativeWindow
        if (fb_fd >= 0 && (tick % 2 == 0)) {
            // RGBA little-endian: 0xAABBGGRR
            constexpr uint32_t kCard = 0xFF141816;      // #161814
            constexpr uint32_t kBorder = 0xFF29332E;    // #2E3329
            constexpr uint32_t kLime = 0xFF32F4C6;      // #C6F432
            constexpr uint32_t kCream = 0xFFECF3F2;     // #F2F3EC
            constexpr uint32_t kMuted = 0xFF9AA9A4;     // #A4A99A
            constexpr uint32_t kCoral = 0xFF4A6BFF;     // #FF6B4A

            for (size_t i = 0; i < static_cast<size_t>(kW) * kH; ++i) {
                fb[i * 4 + 0] = 0x0E;
                fb[i * 4 + 1] = 0x0F;
                fb[i * 4 + 2] = 0x0C;
                fb[i * 4 + 3] = 0xFF;
            }

            fill_rect(fb, kW, kH, 24, 32, kW - 48, 86, kCard);
            fill_rect(fb, kW, kH, 24, 32, kW - 48, 2, kLime);
            draw_text(fb, kW, kH, 42, 52, 2, kLime, "KARYON. SUPERVISOR");
            draw_text(fb, kW, kH, 42, 82, 2, kMuted, "ROOTLESS CONTAINER BRIDGE");

            fill_rect(fb, kW, kH, 24, 140, kW - 48, 230, kCard);
            fill_rect(fb, kW, kH, 24, 140, kW - 48, 1, kBorder);
            draw_text(fb, kW, kH, 42, 162, 2, kCream, "VM: " + instance_id);
            draw_text(fb, kW, kH, 42, 194, 2, kCream, "CHILD PID: " + std::to_string(getpid()));
            draw_text(fb, kW, kH, 42, 226, 2, kCream, "HOST UID:  " + std::to_string(getuid()));
            draw_text(fb, kW, kH, 42, 258, 2, kLime, "AF_UNIX:   CONNECTED");
            draw_text(fb, kW, kH, 42, 290, 2, kMuted, "LAST CMD:  " + last_event);
            draw_text(fb, kW, kH, 42, 322, 2, kMuted, "FRAME:     " + std::to_string(tick / 2));

            fill_rect(fb, kW, kH, 24, 396, kW - 48, 250, kCard);
            fill_rect(fb, kW, kH, 24, 396, kW - 48, 1, kBorder);
            draw_text(fb, kW, kH, 42, 420, 2, kCream, "AOSP 14 ROOTFS CHECKLIST");
            draw_text(fb, kW, kH, 42, 456, 2, has_init ? kLime : kCoral,
                      std::string("INIT ELF:     ") + (has_init ? "PRESENT" : "MISSING"));
            draw_text(fb, kW, kH, 42, 488, 2, has_services ? kLime : kCoral,
                      std::string("SERVICES.JAR: ") + (has_services ? "PRESENT" : "MISSING"));
            draw_text(fb, kW, kH, 42, 520, 2, has_gms ? kLime : kMuted,
                      std::string("GAPPS/GMS:    ") + (has_gms ? "PRESENT" : "NOT PACKED"));
            draw_text(fb, kW, kH, 42, 552, 2, has_su ? kLime : kMuted,
                      std::string("CONTAINER SU: ") + (has_su ? "PRESENT" : "NOT PACKED"));

            if (!has_init || !has_services) {
                draw_text(fb, kW, kH, 42, 600, 2, kCoral, "IMPORT AOSP ROOTFS TAR");
            } else {
                draw_text(fb, kW, kH, 42, 600, 2, kLime, "ROOTFS VERIFIED FOR BOOT");
            }

            // Heartbeat indicator bar
            const int bar_x = 42 + static_cast<int>((tick * 18) % (kW - 140));
            fill_rect(fb, kW, kH, 42, 678, kW - 84, 8, kBorder);
            fill_rect(fb, kW, kH, bar_x, 678, 56, 8, kLime);

            const FrameHeader hdr{kW, kH, static_cast<uint32_t>(fb.size())};
            if (!write_all(fb_fd, &hdr, sizeof(hdr)) || !write_all(fb_fd, fb.data(), fb.size())) {
                break;
            }
        }
        ++tick;
    }

    return 0;
}

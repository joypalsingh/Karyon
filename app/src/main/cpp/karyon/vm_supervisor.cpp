#include "vm_supervisor.h"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace karyon::supervisor {

namespace {

constexpr const char* kTag = "KaryonSupervisor";

struct FrameHeader {
    uint32_t width;
    uint32_t height;
    uint32_t payload_bytes;
};

bool read_exact(int fd, void* dst, size_t len) {
    auto* p = static_cast<uint8_t*>(dst);
    size_t got = 0;
    while (got < len) {
        const ssize_t n = read(fd, p + got, len - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

void clear_cloexec(int fd) {
    const int flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
    }
}

void close_if_valid(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

}  // namespace

VmSupervisor& global_supervisor() {
    static VmSupervisor instance;
    return instance;
}

VmSupervisor::~VmSupervisor() {
    stop_vm();
    detach_surface();
}

void VmSupervisor::append_log(const std::string& line) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", line.c_str());
    std::lock_guard<std::mutex> lock(state_mu_);
    logs_.push_back(line);
    while (logs_.size() > 250) {
        logs_.pop_front();
    }
}

bool VmSupervisor::start_vm(
    const std::string& instance_id,
    const std::string& instance_dir,
    const std::string& init_binary_path) {
    stop_vm();

    if (access(init_binary_path.c_str(), X_OK) != 0) {
        append_log("ERROR: child binary not executable: " + init_binary_path + " errno=" + std::to_string(errno));
        return false;
    }

    int ctrl_pair[2] = {-1, -1};
    int fb_pair[2] = {-1, -1};
    int log_pipe[2] = {-1, -1};

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, ctrl_pair) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, fb_pair) != 0 ||
        pipe(log_pipe) != 0) {
        append_log("ERROR: failed to allocate IPC sockets/pipes");
        close_if_valid(ctrl_pair[0]);
        close_if_valid(ctrl_pair[1]);
        close_if_valid(fb_pair[0]);
        close_if_valid(fb_pair[1]);
        close_if_valid(log_pipe[0]);
        close_if_valid(log_pipe[1]);
        return false;
    }

    const size_t slash_pos = init_binary_path.find_last_of('/');
    const std::string native_dir = (slash_pos != std::string::npos) ? init_binary_path.substr(0, slash_pos) : ".";
    const pid_t pid = fork();
    if (pid < 0) {
        append_log("ERROR: fork() failed errno=" + std::to_string(errno));
        close_if_valid(ctrl_pair[0]);
        close_if_valid(ctrl_pair[1]);
        close_if_valid(fb_pair[0]);
        close_if_valid(fb_pair[1]);
        close_if_valid(log_pipe[0]);
        close_if_valid(log_pipe[1]);
        return false;
    }

    if (pid == 0) {
        // Child process
        close(ctrl_pair[0]);
        close(fb_pair[0]);
        close(log_pipe[0]);

        dup2(log_pipe[1], STDOUT_FILENO);
        dup2(log_pipe[1], STDERR_FILENO);
        if (log_pipe[1] > STDERR_FILENO) close(log_pipe[1]);

        clear_cloexec(ctrl_pair[1]);
        clear_cloexec(fb_pair[1]);

        setenv("LD_LIBRARY_PATH", native_dir.c_str(), 1);

        const std::string ctrl_fd_str = std::to_string(ctrl_pair[1]);
        const std::string fb_fd_str = std::to_string(fb_pair[1]);

        const char* argv[] = {
            init_binary_path.c_str(),
            "--instance", instance_id.c_str(),
            "--instance-dir", instance_dir.c_str(),
            "--ctrl-fd", ctrl_fd_str.c_str(),
            "--fb-fd", fb_fd_str.c_str(),
            nullptr
        };
        execv(init_binary_path.c_str(), const_cast<char* const*>(argv));
        _exit(127);
    }

    // Parent process
    close(ctrl_pair[1]);
    close(fb_pair[1]);
    close(log_pipe[1]);

    {
        std::lock_guard<std::mutex> lock(state_mu_);
        ctrl_fd_ = ctrl_pair[0];
        fb_fd_ = fb_pair[0];
        stdout_fd_ = log_pipe[0];
        active_instance_id_ = instance_id;
        last_ctrl_reply_.clear();
    }
    child_pid_.store(pid);
    frames_rendered_.store(0);
    running_.store(true);

    append_log("Supervised child launched: pid=" + std::to_string(pid) + " instance=" + instance_id);

    log_thread_ = std::thread([this, fd = log_pipe[0]]() {
        char buf[1024];
        std::string partial;
        while (true) {
            const ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            partial.append(buf, static_cast<size_t>(n));
            size_t nl = 0;
            while ((nl = partial.find('\n')) != std::string::npos) {
                std::string line = partial.substr(0, nl);
                partial.erase(0, nl + 1);
                if (!line.empty()) append_log("[child] " + line);
            }
        }
        if (!partial.empty()) append_log("[child] " + partial);
    });

    ctrl_thread_ = std::thread([this, fd = ctrl_pair[0]]() {
        char buf[512];
        std::string partial;
        while (true) {
            const ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            partial.append(buf, static_cast<size_t>(n));
            size_t nl = 0;
            while ((nl = partial.find('\n')) != std::string::npos) {
                std::string line = partial.substr(0, nl);
                partial.erase(0, nl + 1);
                if (!line.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(state_mu_);
                        last_ctrl_reply_ = line;
                    }
                    append_log("[ctrl] " + line);
                }
            }
        }
    });

    fb_thread_ = std::thread([this, fd = fb_pair[0]]() {
        FrameHeader hdr{};
        std::vector<uint8_t> rgba;
        while (read_exact(fd, &hdr, sizeof(hdr))) {
            if (hdr.width == 0 || hdr.height == 0 || hdr.width > 4096 || hdr.height > 4096) break;
            const size_t expected = static_cast<size_t>(hdr.width) * hdr.height * 4;
            if (hdr.payload_bytes != expected) break;
            rgba.resize(expected);
            if (!read_exact(fd, rgba.data(), expected)) break;
            present_rgba_frame(hdr.width, hdr.height, rgba);
        }
    });

    reap_thread_ = std::thread([this, pid]() {
        int st = 0;
        const pid_t r = waitpid(pid, &st, 0);
        if (r == pid) {
            if (WIFEXITED(st)) {
                append_log("Child exited code=" + std::to_string(WEXITSTATUS(st)));
            } else if (WIFSIGNALED(st)) {
                append_log("Child terminated by signal=" + std::to_string(WTERMSIG(st)));
            }
        }
        running_.store(false);
        child_pid_.store(-1);
    });

    return true;
}

void VmSupervisor::stop_vm() {
    const int32_t pid = child_pid_.load();
    if (ctrl_fd_ >= 0) {
        const char kStop[] = "STOP\n";
        (void)write(ctrl_fd_, kStop, sizeof(kStop) - 1);
    }

    if (pid > 0) {
        for (int i = 0; i < 10; ++i) {
            if (child_pid_.load() <= 0) break;
            usleep(30000);
        }
        if (child_pid_.load() > 0) {
            kill(pid, SIGTERM);
            usleep(50000);
        }
        if (child_pid_.load() > 0) {
            kill(pid, SIGKILL);
        }
    }

    {
        std::lock_guard<std::mutex> lock(state_mu_);
        if (ctrl_fd_ >= 0) shutdown(ctrl_fd_, SHUT_RDWR);
        if (fb_fd_ >= 0) shutdown(fb_fd_, SHUT_RDWR);
        close_if_valid(ctrl_fd_);
        close_if_valid(fb_fd_);
        close_if_valid(stdout_fd_);
    }

    if (log_thread_.joinable()) log_thread_.join();
    if (ctrl_thread_.joinable()) ctrl_thread_.join();
    if (fb_thread_.joinable()) fb_thread_.join();
    if (reap_thread_.joinable()) reap_thread_.join();

    running_.store(false);
    child_pid_.store(-1);
}

bool VmSupervisor::send_command(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(state_mu_);
    if (ctrl_fd_ < 0 || !running_.load()) return false;
    const std::string wire = cmd + "\n";
    const ssize_t n = write(ctrl_fd_, wire.data(), wire.size());
    return n == static_cast<ssize_t>(wire.size());
}

void VmSupervisor::attach_surface(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(window_mu_);
    if (window_ == window) return;
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
    window_ = window;
    if (window_) {
        ANativeWindow_acquire(window_);
    }
}

void VmSupervisor::detach_surface() {
    std::lock_guard<std::mutex> lock(window_mu_);
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void VmSupervisor::present_rgba_frame(uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
    std::lock_guard<std::mutex> lock(window_mu_);
    if (!window_) return;

    ANativeWindow_setBuffersGeometry(window_, static_cast<int32_t>(w), static_cast<int32_t>(h), WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer buf{};
    if (ANativeWindow_lock(window_, &buf, nullptr) != 0) return;

    const auto copy_w = std::min<int32_t>(buf.width, static_cast<int32_t>(w));
    const auto copy_h = std::min<int32_t>(buf.height, static_cast<int32_t>(h));
    auto* dst = static_cast<uint8_t*>(buf.bits);
    const size_t dst_stride = static_cast<size_t>(buf.stride) * 4;
    const size_t src_stride = static_cast<size_t>(w) * 4;

    for (int32_t y = 0; y < copy_h; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * dst_stride,
                    rgba.data() + static_cast<size_t>(y) * src_stride,
                    static_cast<size_t>(copy_w) * 4);
    }

    ANativeWindow_unlockAndPost(window_);
    frames_rendered_.fetch_add(1);
}

VmRuntimeStatus VmSupervisor::status() const {
    std::lock_guard<std::mutex> lock(state_mu_);
    VmRuntimeStatus st;
    st.running = running_.load();
    st.child_pid = child_pid_.load();
    st.frames_rendered = frames_rendered_.load();
    st.instance_id = active_instance_id_;
    st.last_ctrl_reply = last_ctrl_reply_;
    return st;
}

std::vector<std::string> VmSupervisor::drain_logs(size_t max_lines) {
    std::lock_guard<std::mutex> lock(state_mu_);
    std::vector<std::string> out;
    while (!logs_.empty() && out.size() < max_lines) {
        out.push_back(std::move(logs_.front()));
        logs_.pop_front();
    }
    return out;
}

}  // namespace karyon::supervisor

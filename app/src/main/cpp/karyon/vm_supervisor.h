#pragma once

#include <android/native_window.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace karyon::supervisor {

struct VmRuntimeStatus {
    bool running = false;
    int32_t child_pid = -1;
    uint64_t frames_rendered = 0;
    std::string instance_id;
    std::string last_ctrl_reply;
};

class VmSupervisor {
public:
    VmSupervisor() = default;
    ~VmSupervisor();

    bool start_vm(
        const std::string& instance_id,
        const std::string& instance_dir,
        const std::string& init_binary_path);

    void stop_vm();
    bool send_command(const std::string& cmd);

    void attach_surface(ANativeWindow* window);
    void detach_surface();

    VmRuntimeStatus status() const;
    std::vector<std::string> drain_logs(size_t max_lines = 100);

private:
    void append_log(const std::string& line);
    void present_rgba_frame(uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba);

    mutable std::mutex state_mu_;
    std::mutex window_mu_;
    ANativeWindow* window_ = nullptr;

    std::atomic<bool> running_{false};
    std::atomic<int32_t> child_pid_{-1};
    std::atomic<uint64_t> frames_rendered_{0};
    std::string active_instance_id_;
    std::string last_ctrl_reply_;

    int ctrl_fd_ = -1;
    int fb_fd_ = -1;
    int stdout_fd_ = -1;

    std::thread log_thread_;
    std::thread fb_thread_;
    std::thread ctrl_thread_;
    std::thread reap_thread_;
    std::deque<std::string> logs_;
};

VmSupervisor& global_supervisor();

}  // namespace karyon::supervisor

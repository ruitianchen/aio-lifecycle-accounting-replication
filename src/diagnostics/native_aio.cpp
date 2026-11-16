#include <libaio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static double raw_time() {
    timespec t{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) throw std::runtime_error("clock failed");
    return double(t.tv_sec) + double(t.tv_nsec) * 1e-9;
}
static void require(bool ok, const std::string& text) { if (!ok) throw std::runtime_error(text); }
static long quota() {
    std::ifstream f("/proc/sys/fs/aio-nr"); long value = -1; f >> value;
    require(bool(f) && value >= 0, "Cannot read AIO quota"); return value;
}
static bool write_all(int fd, const void* value, size_t n) {
    const auto* p = static_cast<const char*>(value);
    while (n) { ssize_t k = write(fd, p, n); if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false; p += k; n -= size_t(k); }
    return true;
}
static bool read_all(int fd, void* value, size_t n) {
    auto* p = static_cast<char*>(value);
    while (n) { ssize_t k = read(fd, p, n); if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false; p += k; n -= size_t(k); }
    return true;
}
struct Reply { int slot; int action; int rc; long pid; long tid; double begin; double end; };
struct Channel { int command[2]; int response[2]; };
static void worker(int slot, int input, int output) {
    io_context_t context = 0;
    char command = 0;
    while (read_all(input, &command, 1)) {
        Reply r{}; r.slot = slot; r.action = command; r.pid = getpid(); r.tid = syscall(SYS_gettid);
        if (command == 'Q') {
            if (context) r.rc = io_destroy(context);
            write_all(output, &r, sizeof(r));
            return;
        }
        r.begin = raw_time();
        if (command == 'S') r.rc = context ? -EBUSY : io_setup(1024, &context);
        else if (command == 'D') { r.rc = context ? io_destroy(context) : -EINVAL; if (!r.rc) context = 0; }
        else r.rc = -EINVAL;
        r.end = raw_time();
        if (!write_all(output, &r, sizeof(r))) break;
    }
    if (context) io_destroy(context);
}
class Workers {
    std::vector<Channel> channels;
    std::vector<std::thread> threads;
    std::vector<pid_t> children;
    bool processes;
public:
    Workers(int n, bool use_processes) : channels(n), processes(use_processes) {
        for (auto& c : channels) require(pipe(c.command) == 0 && pipe(c.response) == 0, "pipe failed");
        if (processes) {
            const pid_t parent = getpid();
            for (int i = 0; i < n; ++i) {
                pid_t p = fork(); require(p >= 0, "fork failed");
                if (!p) {
                    prctl(PR_SET_PDEATHSIG, SIGTERM);
                    if (getppid() != parent) _exit(3);
                    for (int j = 0; j < n; ++j) {
                        close(channels[j].command[1]); close(channels[j].response[0]);
                        if (j != i) { close(channels[j].command[0]); close(channels[j].response[1]); }
                    }
                    worker(i, channels[i].command[0], channels[i].response[1]);
                    _exit(0);
                }
                children.push_back(p);
            }
            for (auto& c : channels) { close(c.command[0]); close(c.response[1]); }
        } else {
            for (int i = 0; i < n; ++i)
                threads.emplace_back(worker, i, channels[i].command[0], channels[i].response[1]);
        }
    }
    void send(int i, char action) { require(write_all(channels[i].command[1], &action, 1), "command write failed"); }
    Reply receive(int i) {
        pollfd f{channels[i].response[0], POLLIN, 0};
        require(poll(&f, 1, 5000) == 1, "worker response timeout");
        Reply r{}; require(read_all(f.fd, &r, sizeof(r)), "worker response EOF");
        require(r.slot == i, "worker ownership mismatch"); return r;
    }
    void stop() {
        for (size_t i = 0; i < channels.size(); ++i) send(int(i), 'Q');
        for (size_t i = 0; i < channels.size(); ++i) require(receive(int(i)).rc == 0, "unexpected context at shutdown");
        if (processes) {
            for (pid_t p : children) { int status = 0; require(waitpid(p, &status, 0) == p && WIFEXITED(status) && WEXITSTATUS(status) == 0, "worker process failed"); }
        } else for (auto& t : threads) t.join();
        for (auto& c : channels) {
            close(c.command[1]); close(c.response[0]);
            if (!processes) { close(c.command[0]); close(c.response[1]); }
        }
    }
};
static double interval_union(std::vector<Reply> rows) {
    std::sort(rows.begin(), rows.end(), [](const Reply& a, const Reply& b) { return a.begin < b.begin; });
    double total = 0, begin = 0, end = 0; bool open = false;
    for (const auto& r : rows) {
        if (!open) { begin = r.begin; end = r.end; open = true; }
        else if (r.begin > end) { total += end - begin; begin = r.begin; end = r.end; }
        else end = std::max(end, r.end);
    }
    return total + (open ? end - begin : 0);
}
int main(int argc, char** argv) {
    try {
        require(argc == 7, "usage: mode warmup measured prefix group1 group2");
        const std::string mode = argv[1], prefix = argv[4];
        require(mode == "serial_threads" || mode == "parallel_threads" || mode == "parallel_processes", "unknown mode");
        int warmup = std::stoi(argv[2]), measured = std::stoi(argv[3]);
        int group1 = std::stoi(argv[5]), group2 = std::stoi(argv[6]);
        require(warmup >= 0 && measured > 0 && group1 > 0 && group1 <= 10 && group2 >= 0 && group2 <= 10, "invalid rounds/groups");
        require(quota() == 0, "nonzero starting global AIO quota");
        std::ofstream calls(prefix + ".calls.csv"), phases(prefix + ".phases.csv");
        require(calls.good() && phases.good(), "cannot open new outputs");
        calls << std::setprecision(17); phases << std::setprecision(17);
        calls << "warmup,round,group_size,operation,slot,pid,tid,start_raw,end_raw,elapsed_s,returncode\n";
        phases << "warmup,round,group_size,operation,start_raw,end_raw,wall_s,sum_api_s,union_api_s,aio_nr_after\n";
        Workers workers(std::max(group1, group2), mode == "parallel_processes");
        long setups = 0, destroys = 0, peak_quota = 0;
        for (int round = 0; round < warmup + measured; ++round) {
            for (int n : {group1, group2}) {
                if (!n) continue;
                require(quota() == 0, "quota not zero at group boundary");
                for (char action : {'S', 'D'}) {
                    std::vector<Reply> replies;
                    const double begin = raw_time();
                    if (mode == "serial_threads") {
                        for (int i = 0; i < n; ++i) { workers.send(i, action); replies.push_back(workers.receive(i)); }
                    } else {
                        for (int i = 0; i < n; ++i) workers.send(i, action);
                        for (int i = 0; i < n; ++i) replies.push_back(workers.receive(i));
                    }
                    const double end = raw_time();
                    long held = quota(); peak_quota = std::max(peak_quota, held);
                    require(held == (action == 'S' ? n * 1024L : 0L), "quota ledger mismatch");
                    double sum = 0;
                    const char* operation = action == 'S' ? "io_setup" : "io_destroy";
                    for (const auto& r : replies) {
                        require(r.action == action && r.rc == 0 && r.end >= r.begin, "AIO API failed");
                        require(r.begin >= begin && r.end <= end, "API timestamp outside phase");
                        sum += r.end - r.begin;
                        calls << (round < warmup) << ',' << round << ',' << n << ',' << operation << ',' << r.slot << ',' << r.pid << ',' << r.tid << ',' << r.begin << ',' << r.end << ',' << r.end-r.begin << ',' << r.rc << '\n';
                    }
                    phases << (round < warmup) << ',' << round << ',' << n << ',' << operation << ',' << begin << ',' << end << ',' << end-begin << ',' << sum << ',' << interval_union(replies) << ',' << held << '\n';
                    calls.flush(); phases.flush();
                    if (action == 'S') setups += n; else destroys += n;
                }
            }
        }
        workers.stop();
        require(quota() == 0 && setups == destroys, "final resource accounting failure");
        std::ofstream summary(prefix + ".summary.json");
        summary << "{\"status\":\"PASS\",\"mode\":\"" << mode << "\",\"warmup_rounds\":" << warmup
                << ",\"measured_rounds\":" << measured << ",\"group1\":" << group1 << ",\"group2\":" << group2
                << ",\"setup_successes\":" << setups << ",\"destroy_successes\":" << destroys
                << ",\"peak_aio_nr\":" << peak_quota << ",\"aio_nr_after\":0,\"io_requests\":0}\n";
        require(calls.good() && phases.good() && summary.good(), "output write failed");
        std::cout << "PASS " << mode << " setups=" << setups << " destroys=" << destroys << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "STOP " << e.what() << std::endl;
        std::_Exit(2);
    }
}

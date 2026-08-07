#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "xbox_proxy.h"

namespace {

constexpr const char* kInputClass = "/sys/class/input";
constexpr const char* kDevInput = "/dev/input";
constexpr unsigned int kInputMajor = 13;

std::atomic_bool gRunning{true};

void logLine(const char* level, const char* format, ...) {
    std::fprintf(stderr, "airxpadfixd[%s]: ", level);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

std::string trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool containsIgnoreCase(const std::string& value, const std::string& needle) {
    return lower(value).find(lower(needle)) != std::string::npos;
}

bool readText(const std::string& path, std::string* output) {
    std::ifstream stream(path);
    if (!stream) return false;
    std::ostringstream data;
    data << stream.rdbuf();
    *output = trim(data.str());
    return true;
}

bool writeTextAtomic(const std::string& path, const std::string& value) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::trunc);
        if (!stream) return false;
        stream << value;
        stream.flush();
        if (!stream) return false;
    }
    return ::rename(temporary.c_str(), path.c_str()) == 0;
}

struct Config {
    std::string realName = "AIR X Controller";
    std::string virtualName = "Xbox Wireless Controller";
    std::string stateDir = "/sdcard/Download/AirXPadFix/state";
    int pollMs = 500;
    int settleMs = 250;
    bool cycleReal = true;
    bool xboxProxy = false;
    std::string proxyName = "Microsoft X-Box 360 pad";
    bool preserveBackKey = true;
};

bool parseBool(const std::string& value, bool fallback) {
    const std::string normalized = lower(trim(value));
    if (normalized == "true" || normalized == "yes" || normalized == "1") return true;
    if (normalized == "false" || normalized == "no" || normalized == "0") return false;
    return fallback;
}

Config loadConfig(const std::string& path) {
    Config config;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        try {
            if (key == "real_name") config.realName = value;
            else if (key == "virtual_name") config.virtualName = value;
            else if (key == "state_dir") config.stateDir = value;
            else if (key == "poll_ms") config.pollMs = std::clamp(std::stoi(value), 100, 10000);
            else if (key == "settle_ms") config.settleMs = std::clamp(std::stoi(value), 50, 5000);
            else if (key == "cycle_real") config.cycleReal = parseBool(value, config.cycleReal);
            else if (key == "xbox_proxy") config.xboxProxy = parseBool(value, config.xboxProxy);
            else if (key == "proxy_name") config.proxyName = value;
            else if (key == "preserve_back_key") {
                config.preserveBackKey = parseBool(value, config.preserveBackKey);
            }
        } catch (const std::exception&) {
            logLine("WARN", "Ignoring invalid config entry: %s", line.c_str());
        }
    }
    return config;
}

struct Device {
    std::string sysname;
    std::string name;
    std::string devnode;
    unsigned int majorNumber = 0;
    unsigned int minorNumber = 0;
};

bool startsWithEvent(const char* value) {
    if (std::strncmp(value, "event", 5) != 0 || value[5] == '\0') return false;
    for (const char* cursor = value + 5; *cursor; ++cursor) {
        if (!std::isdigit(static_cast<unsigned char>(*cursor))) return false;
    }
    return true;
}

std::vector<Device> enumerateDevices() {
    std::vector<Device> devices;
    DIR* directory = ::opendir(kInputClass);
    if (!directory) return devices;

    while (dirent* entry = ::readdir(directory)) {
        if (!startsWithEvent(entry->d_name)) continue;
        Device device;
        device.sysname = entry->d_name;
        device.devnode = std::string(kDevInput) + "/" + device.sysname;
        const std::string root = std::string(kInputClass) + "/" + device.sysname + "/device";
        if (!readText(root + "/name", &device.name)) continue;
        std::string dev;
        if (!readText(std::string(kInputClass) + "/" + device.sysname + "/dev", &dev)) continue;
        unsigned int majorNumber = 0;
        unsigned int minorNumber = 0;
        if (std::sscanf(dev.c_str(), "%u:%u", &majorNumber, &minorNumber) != 2) continue;
        device.majorNumber = majorNumber;
        device.minorNumber = minorNumber;
        devices.push_back(device);
    }
    ::closedir(directory);
    std::sort(devices.begin(), devices.end(), [](const Device& a, const Device& b) {
        return a.sysname < b.sysname;
    });
    return devices;
}

const Device* findMatch(const std::vector<Device>& devices, const std::string& name) {
    const Device* match = nullptr;
    for (const Device& device : devices) {
        if (!containsIgnoreCase(device.name, name)) continue;
        if (match) {
            logLine("ERROR", "Name '%s' matches more than one device (%s and %s)", name.c_str(),
                    match->name.c_str(), device.name.c_str());
            return nullptr;
        }
        match = &device;
    }
    return match;
}

struct NodeSnapshot {
    std::string path;
    mode_t permissions = 0;
    uid_t uid = 0;
    gid_t gid = 0;
    unsigned int majorNumber = 0;
    unsigned int minorNumber = 0;
    std::string selinuxContext;
};

bool validEventPath(const std::string& path) {
    const std::string prefix = std::string(kDevInput) + "/event";
    if (path.rfind(prefix, 0) != 0 || path.size() == prefix.size()) return false;
    return std::all_of(path.begin() + static_cast<std::ptrdiff_t>(prefix.size()), path.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

bool captureNode(const Device& device, NodeSnapshot* snapshot) {
    struct stat info {};
    if (::lstat(device.devnode.c_str(), &info) != 0) return false;
    if (!S_ISCHR(info.st_mode) || major(info.st_rdev) != kInputMajor ||
        major(info.st_rdev) != device.majorNumber || minor(info.st_rdev) != device.minorNumber) {
        logLine("ERROR", "Refusing unexpected node %s", device.devnode.c_str());
        return false;
    }
    snapshot->path = device.devnode;
    snapshot->permissions = info.st_mode & 07777;
    snapshot->uid = info.st_uid;
    snapshot->gid = info.st_gid;
    snapshot->majorNumber = major(info.st_rdev);
    snapshot->minorNumber = minor(info.st_rdev);
    char context[256]{};
    const ssize_t size = ::getxattr(device.devnode.c_str(), "security.selinux", context,
                                    sizeof(context) - 1);
    if (size > 0) {
        snapshot->selinuxContext.assign(context, static_cast<size_t>(size));
        if (!snapshot->selinuxContext.empty() && snapshot->selinuxContext.back() == '\0') {
            snapshot->selinuxContext.pop_back();
        }
    }
    return true;
}

std::string serializeSnapshot(const NodeSnapshot& snapshot) {
    std::ostringstream output;
    output << snapshot.path << '\n' << static_cast<unsigned int>(snapshot.permissions) << '\n'
           << snapshot.uid << '\n' << snapshot.gid << '\n' << snapshot.majorNumber << '\n'
           << snapshot.minorNumber << '\n' << snapshot.selinuxContext << '\n';
    return output.str();
}

bool parseUnsigned(const std::string& value, unsigned long* output) {
    try {
        size_t consumed = 0;
        *output = std::stoul(value, &consumed, 10);
        return consumed == value.size();
    } catch (const std::exception&) {
        return false;
    }
}

bool deserializeSnapshot(const std::string& path, NodeSnapshot* snapshot) {
    std::ifstream stream(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    if (lines.size() < 6 || !validEventPath(lines[0])) return false;
    unsigned long values[5]{};
    for (size_t i = 0; i < 5; ++i) {
        if (!parseUnsigned(lines[i + 1], &values[i])) return false;
    }
    snapshot->path = lines[0];
    snapshot->permissions = static_cast<mode_t>(values[0]);
    snapshot->uid = static_cast<uid_t>(values[1]);
    snapshot->gid = static_cast<gid_t>(values[2]);
    snapshot->majorNumber = static_cast<unsigned int>(values[3]);
    snapshot->minorNumber = static_cast<unsigned int>(values[4]);
    if (lines.size() >= 7) snapshot->selinuxContext = lines[6];
    return snapshot->majorNumber == kInputMajor;
}

bool restoreNode(const NodeSnapshot& snapshot) {
    if (!validEventPath(snapshot.path) || snapshot.majorNumber != kInputMajor) return false;
    struct stat existing {};
    if (::lstat(snapshot.path.c_str(), &existing) == 0) {
        return S_ISCHR(existing.st_mode) && major(existing.st_rdev) == snapshot.majorNumber &&
               minor(existing.st_rdev) == snapshot.minorNumber;
    }
    const std::string stagingPath = std::string(kDevInput) + "/.airxpadfix-node";
    ::unlink(stagingPath.c_str());
    const dev_t deviceNumber = makedev(snapshot.majorNumber, snapshot.minorNumber);
    if (::mknod(stagingPath.c_str(), S_IFCHR | snapshot.permissions, deviceNumber) != 0) {
        logLine("ERROR", "mknod(%s) failed: %s", stagingPath.c_str(), std::strerror(errno));
        return false;
    }
    if (::chown(stagingPath.c_str(), snapshot.uid, snapshot.gid) != 0 ||
        ::chmod(stagingPath.c_str(), snapshot.permissions) != 0) {
        logLine("ERROR", "Could not restore ownership/mode for %s: %s", stagingPath.c_str(),
                std::strerror(errno));
        ::unlink(stagingPath.c_str());
        return false;
    }
    if (!snapshot.selinuxContext.empty() &&
        ::setxattr(stagingPath.c_str(), "security.selinux", snapshot.selinuxContext.c_str(),
                   snapshot.selinuxContext.size() + 1, 0) != 0) {
        logLine("ERROR", "Could not restore SELinux label on %s: %s", stagingPath.c_str(),
                std::strerror(errno));
        ::unlink(stagingPath.c_str());
        return false;
    }
    // Linking the fully-labelled staging inode creates the final name atomically. EventHub only
    // sees IN_CREATE after all security metadata is ready, avoiding a race with system_server.
    if (::link(stagingPath.c_str(), snapshot.path.c_str()) != 0) {
        logLine("ERROR", "link(%s) failed: %s", snapshot.path.c_str(), std::strerror(errno));
        ::unlink(stagingPath.c_str());
        return false;
    }
    ::unlink(stagingPath.c_str());
    logLine("INFO", "Restored %s (%u:%u)", snapshot.path.c_str(), snapshot.majorNumber,
            snapshot.minorNumber);
    return true;
}

bool hideNode(const Device& device, const std::string& recoveryFile, NodeSnapshot* snapshot) {
    if (!captureNode(device, snapshot)) {
        logLine("ERROR", "Cannot snapshot %s", device.devnode.c_str());
        return false;
    }
    if (!writeTextAtomic(recoveryFile, serializeSnapshot(*snapshot))) {
        logLine("ERROR", "Cannot write recovery file %s", recoveryFile.c_str());
        return false;
    }
    if (::unlink(device.devnode.c_str()) != 0) {
        logLine("ERROR", "unlink(%s) failed: %s", device.devnode.c_str(), std::strerror(errno));
        ::unlink(recoveryFile.c_str());
        return false;
    }
    logLine("INFO", "Disconnected Android node %s (%s)", device.devnode.c_str(),
            device.name.c_str());
    return true;
}

bool restoreRecoveryFile(const std::string& recoveryFile, const Device* expectedDevice) {
    NodeSnapshot snapshot;
    if (!deserializeSnapshot(recoveryFile, &snapshot)) return false;
    if (expectedDevice &&
        (snapshot.path != expectedDevice->devnode || snapshot.majorNumber != expectedDevice->majorNumber ||
         snapshot.minorNumber != expectedDevice->minorNumber)) {
        logLine("ERROR", "Recovery metadata does not match current %s; refusing restore",
                expectedDevice->name.c_str());
        return false;
    }
    if (!restoreNode(snapshot)) return false;
    ::unlink(recoveryFile.c_str());
    return true;
}

bool cycleNode(const Device& device, const std::string& recoveryFile, int settleMs) {
    NodeSnapshot snapshot;
    if (!hideNode(device, recoveryFile, &snapshot)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(settleMs));
    if (!restoreNode(snapshot)) return false;
    ::unlink(recoveryFile.c_str());
    return true;
}

class ControllerSwitcher {
  public:
    explicit ControllerSwitcher(Config config)
        : config_(std::move(config)), virtualRecovery_(config_.stateDir + "/virtual-node.state"),
          realRecovery_(config_.stateDir + "/real-node.state"),
          modeFile_(config_.stateDir + "/mode") {}

    bool initialize() {
        if (config_.realName.empty() || config_.virtualName.empty() ||
            lower(config_.realName) == lower(config_.virtualName)) {
            logLine("ERROR", "Controller names must be non-empty and different");
            return false;
        }
        if (::mkdir(config_.stateDir.c_str(), 0700) != 0 && errno != EEXIST) {
            logLine("ERROR", "mkdir(%s) failed: %s", config_.stateDir.c_str(), std::strerror(errno));
            return false;
        }
        readText(modeFile_, &mode_);
        return true;
    }

    void synchronize() {
        const std::vector<Device> devices = enumerateDevices();
        const Device* real = findMatch(devices, config_.realName);
        const Device* virtualDevice = findMatch(devices, config_.virtualName);

        if (::access(realRecovery_.c_str(), F_OK) == 0 && !proxy_.active()) {
            if (!real) {
                logLine("INFO", "Discarding stale real-controller recovery after hardware sleep");
                ::unlink(realRecovery_.c_str());
            } else {
                if (!restoreRecoveryFile(realRecovery_, real)) {
                    logLine("ERROR", "Real controller recovery is pending; will retry");
                    return;
                }
            }
        }

        if (real) {
            activateReal(*real, virtualDevice);
        } else {
            activateVirtual(virtualDevice);
        }
    }

    void cleanup() {
        proxy_.stop();
        const std::vector<Device> devices = enumerateDevices();
        const Device* real = findMatch(devices, config_.realName);
        const Device* virtualDevice = findMatch(devices, config_.virtualName);
        // The device's default awake order is the persistent virtual controller followed by AIR X.
        // Restore in that order so stopping/uninstalling returns to the stock controller numbering.
        if (::access(virtualRecovery_.c_str(), F_OK) == 0) {
            restoreRecoveryFile(virtualRecovery_, virtualDevice);
        }
        if (::access(realRecovery_.c_str(), F_OK) == 0) {
            if (real) {
                restoreRecoveryFile(realRecovery_, real);
            } else {
                ::unlink(realRecovery_.c_str());
            }
        }
        writeTextAtomic(modeFile_, "stopped\n");
    }

    int physicalFd() const { return proxy_.physicalFd(); }

    bool processPhysicalEvents() { return proxy_.processEvents(); }

    void handlePhysicalInputLoss() {
        logLine("INFO", "Stopping Xbox proxy after physical input loss");
        proxy_.stop();
    }

  private:
    void activateReal(const Device& real, const Device* virtualDevice) {
        if (config_.xboxProxy) {
            activateXboxProxy(real, virtualDevice);
            return;
        }
        const bool virtualHidden = ::access(virtualRecovery_.c_str(), F_OK) == 0;
        if (!virtualHidden && virtualDevice && ::access(virtualDevice->devnode.c_str(), F_OK) == 0) {
            NodeSnapshot ignored;
            if (!hideNode(*virtualDevice, virtualRecovery_, &ignored)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.settleMs));
        }

        const bool needsPromotion = mode_ != "real";
        if (needsPromotion && config_.cycleReal && ::access(real.devnode.c_str(), F_OK) == 0) {
            logLine("INFO", "Cycling %s so Android assigns controller number 1", real.name.c_str());
            if (!cycleNode(real, realRecovery_, config_.settleMs)) return;
        }
        if (mode_ != "real") {
            mode_ = "real";
            writeTextAtomic(modeFile_, mode_ + "\n");
            logLine("INFO", "Real controller is active");
        }
    }

    void activateVirtual(const Device* virtualDevice) {
        proxy_.stop();
        bool restoredFromRecovery = false;
        if (::access(virtualRecovery_.c_str(), F_OK) == 0) {
            if (!virtualDevice) {
                logLine("WARN", "Virtual controller is absent from sysfs; deferring node restore");
                return;
            }
            if (!restoreRecoveryFile(virtualRecovery_, virtualDevice)) {
                logLine("ERROR", "Virtual controller recovery is pending; will retry");
                return;
            }
            restoredFromRecovery = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.settleMs));
        }
        if (!virtualDevice) {
            logLine("WARN", "Virtual controller is absent from sysfs");
            return;
        }
        if (mode_ != "virtual" && !restoredFromRecovery &&
            ::access(virtualDevice->devnode.c_str(), F_OK) == 0) {
            logLine("INFO", "Cycling %s so Android assigns controller number 1",
                    virtualDevice->name.c_str());
            if (!cycleNode(*virtualDevice, virtualRecovery_, config_.settleMs)) return;
        }
        if (mode_ != "virtual") {
            mode_ = "virtual";
            writeTextAtomic(modeFile_, mode_ + "\n");
            logLine("INFO", "Virtual controller is active");
        }
    }

    void restoreAfterProxyFailure(const Device& real, const Device* virtualDevice) {
        proxy_.stop();
        // Recreate the stock virtual controller before the real controller, matching boot order.
        if (::access(virtualRecovery_.c_str(), F_OK) == 0 && virtualDevice) {
            restoreRecoveryFile(virtualRecovery_, virtualDevice);
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.settleMs));
        }
        if (::access(realRecovery_.c_str(), F_OK) == 0) {
            restoreRecoveryFile(realRecovery_, &real);
        }
        mode_ = "stopped";
        writeTextAtomic(modeFile_, mode_ + "\n");
    }

    void activateXboxProxy(const Device& real, const Device* virtualDevice) {
        if (proxy_.active()) {
            if (mode_ != "proxy") {
                mode_ = "proxy";
                writeTextAtomic(modeFile_, mode_ + "\n");
            }
            return;
        }
        if (::access(real.devnode.c_str(), F_OK) != 0) {
            logLine("WARN", "Real controller node is unavailable for Xbox proxy");
            return;
        }
        if (!proxy_.openPhysical(real.devnode)) return;

        if (::access(virtualRecovery_.c_str(), F_OK) != 0 && virtualDevice &&
            ::access(virtualDevice->devnode.c_str(), F_OK) == 0) {
            NodeSnapshot ignored;
            if (!hideNode(*virtualDevice, virtualRecovery_, &ignored)) {
                proxy_.stop();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.settleMs));
        }

        if (::access(realRecovery_.c_str(), F_OK) != 0) {
            NodeSnapshot ignored;
            if (!hideNode(real, realRecovery_, &ignored)) {
                restoreAfterProxyFailure(real, virtualDevice);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.settleMs));
        }

        if (!proxy_.createVirtualDevices(config_.proxyName, config_.preserveBackKey)) {
            logLine("ERROR", "Could not create Xbox 360 proxy; restoring stock input nodes");
            restoreAfterProxyFailure(real, virtualDevice);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.settleMs));
        mode_ = "proxy";
        writeTextAtomic(modeFile_, mode_ + "\n");
        logLine("INFO", "AIR X hardware is active through the Xbox 360 proxy");
    }

    Config config_;
    std::string virtualRecovery_;
    std::string realRecovery_;
    std::string modeFile_;
    std::string mode_;
    XboxProxy proxy_;
};

void signalHandler(int) { gRunning = false; }

void usage(const char* executable) {
    std::fprintf(stderr, "Usage: %s [--config PATH] [--once]\n", executable);
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = "/sdcard/Download/AirXPadFix/config.conf";
    bool once = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--config" && i + 1 < argc) configPath = argv[++i];
        else if (argument == "--once") once = true;
        else {
            usage(argv[0]);
            return 2;
        }
    }

    Config config = loadConfig(configPath);
    const int pollMs = config.pollMs;
    ControllerSwitcher switcher(std::move(config));
    if (!switcher.initialize()) return 1;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGHUP, SIG_IGN);
    switcher.synchronize();
    if (once) return 0;

    const int inotifyFd = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (inotifyFd >= 0) {
        if (::inotify_add_watch(inotifyFd, kDevInput, IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM) < 0) {
            logLine("WARN", "Cannot watch %s; using polling only", kDevInput);
        }
    } else {
        logLine("WARN", "inotify unavailable; using polling only");
    }

    while (gRunning) {
        pollfd descriptors[2]{};
        nfds_t descriptorCount = 0;
        int inotifyIndex = -1;
        int physicalIndex = -1;
        if (inotifyFd >= 0) {
            inotifyIndex = static_cast<int>(descriptorCount);
            descriptors[descriptorCount++] = pollfd{inotifyFd, POLLIN, 0};
        }
        const int physicalFd = switcher.physicalFd();
        if (physicalFd >= 0) {
            physicalIndex = static_cast<int>(descriptorCount);
            descriptors[descriptorCount++] = pollfd{physicalFd, POLLIN, 0};
        }

        const int pollResult = ::poll(descriptors, descriptorCount, pollMs);
        bool shouldSynchronize = pollResult == 0;
        if (pollResult < 0 && errno != EINTR) {
            logLine("WARN", "poll failed: %s", std::strerror(errno));
            shouldSynchronize = true;
        }

        if (inotifyIndex >= 0 && (descriptors[inotifyIndex].revents & POLLIN) != 0) {
            char events[4096];
            while (::read(inotifyFd, events, sizeof(events)) > 0) {}
            shouldSynchronize = true;
        }
        if (physicalIndex >= 0) {
            const short events = descriptors[physicalIndex].revents;
            if ((events & POLLIN) != 0 && !switcher.processPhysicalEvents()) {
                switcher.handlePhysicalInputLoss();
                shouldSynchronize = true;
            } else if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                switcher.handlePhysicalInputLoss();
                shouldSynchronize = true;
            }
        }
        if (shouldSynchronize) switcher.synchronize();
    }

    if (inotifyFd >= 0) ::close(inotifyFd);
    switcher.cleanup();
    return 0;
}

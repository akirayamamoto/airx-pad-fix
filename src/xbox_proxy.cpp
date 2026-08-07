#include "xbox_proxy.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr const char* kUinputPath = "/dev/uinput";

void proxyLog(const char* level, const char* message) {
    std::fprintf(stderr, "airxpadfixd[%s]: %s\n", level, message);
    std::fflush(stderr);
}

void proxyLogError(const char* operation) {
    std::fprintf(stderr, "airxpadfixd[ERROR]: %s failed: %s\n", operation,
                 std::strerror(errno));
    std::fflush(stderr);
}

bool setEventBit(int fd, unsigned long request, int value, const char* operation) {
    if (::ioctl(fd, request, value) == 0) return true;
    proxyLogError(operation);
    return false;
}

bool configureAbsAxis(int fd, unsigned short code, int minimum, int maximum) {
    if (!setEventBit(fd, UI_SET_ABSBIT, code, "UI_SET_ABSBIT")) return false;
    uinput_abs_setup setup{};
    setup.code = code;
    setup.absinfo.minimum = minimum;
    setup.absinfo.maximum = maximum;
    setup.absinfo.value = 0;
    if (::ioctl(fd, UI_ABS_SETUP, &setup) == 0) return true;
    proxyLogError("UI_ABS_SETUP");
    return false;
}

bool configureIdentity(int fd, const std::string& name, unsigned short vendor,
                       unsigned short product, unsigned short version) {
    uinput_setup setup{};
    std::snprintf(setup.name, sizeof(setup.name), "%s", name.c_str());
    setup.id.bustype = BUS_USB;
    setup.id.vendor = vendor;
    setup.id.product = product;
    setup.id.version = version;
    if (::ioctl(fd, UI_DEV_SETUP, &setup) != 0) {
        proxyLogError("UI_DEV_SETUP");
        return false;
    }
    if (::ioctl(fd, UI_DEV_CREATE) != 0) {
        proxyLogError("UI_DEV_CREATE");
        return false;
    }
    return true;
}

bool isControllerKey(unsigned short code) {
    switch (code) {
        case BTN_SOUTH:
        case BTN_EAST:
        case BTN_NORTH:
        case BTN_WEST:
        case BTN_TL:
        case BTN_TR:
        case BTN_SELECT:
        case BTN_START:
        case BTN_MODE:
        case BTN_THUMBL:
        case BTN_THUMBR:
            return true;
        default:
            return false;
    }
}

bool mapAxis(unsigned short inputCode, unsigned short* outputCode) {
    switch (inputCode) {
        case ABS_X:
        case ABS_Y:
        case ABS_HAT0X:
        case ABS_HAT0Y:
            *outputCode = inputCode;
            return true;
        case ABS_Z:
            *outputCode = ABS_RX;
            return true;
        case ABS_RZ:
            *outputCode = ABS_RY;
            return true;
        case ABS_BRAKE:
            *outputCode = ABS_Z;
            return true;
        case ABS_GAS:
            *outputCode = ABS_RZ;
            return true;
        default:
            return false;
    }
}

}  // namespace

XboxProxy::~XboxProxy() { stop(); }

bool XboxProxy::openPhysical(const std::string& devnode) {
    stop();
    physicalFd_ = ::open(devnode.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (physicalFd_ < 0) {
        proxyLogError("open physical controller");
        return false;
    }
    if (::ioctl(physicalFd_, EVIOCGRAB, 1) != 0) {
        proxyLogError("EVIOCGRAB");
        stop();
        return false;
    }
    grabbed_ = true;
    return true;
}

bool XboxProxy::createVirtualDevices(const std::string& controllerName, bool preserveBackKey) {
    if (physicalFd_ < 0 || controllerCreated_) return false;

    controllerFd_ = ::open(kUinputPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (controllerFd_ < 0) {
        proxyLogError("open /dev/uinput");
        return false;
    }
    if (!setEventBit(controllerFd_, UI_SET_EVBIT, EV_KEY, "UI_SET_EVBIT(EV_KEY)") ||
        !setEventBit(controllerFd_, UI_SET_EVBIT, EV_ABS, "UI_SET_EVBIT(EV_ABS)")) {
        stop();
        return false;
    }

    constexpr std::array<unsigned short, 11> keys = {
        BTN_SOUTH, BTN_EAST,   BTN_NORTH, BTN_WEST,  BTN_TL,    BTN_TR,
        BTN_SELECT, BTN_START, BTN_MODE,  BTN_THUMBL, BTN_THUMBR,
    };
    for (const unsigned short key : keys) {
        if (!setEventBit(controllerFd_, UI_SET_KEYBIT, key, "UI_SET_KEYBIT")) {
            stop();
            return false;
        }
    }
    if (!configureAbsAxis(controllerFd_, ABS_X, -32768, 32767) ||
        !configureAbsAxis(controllerFd_, ABS_Y, -32768, 32767) ||
        !configureAbsAxis(controllerFd_, ABS_RX, -32768, 32767) ||
        !configureAbsAxis(controllerFd_, ABS_RY, -32768, 32767) ||
        !configureAbsAxis(controllerFd_, ABS_Z, 0, 255) ||
        !configureAbsAxis(controllerFd_, ABS_RZ, 0, 255) ||
        !configureAbsAxis(controllerFd_, ABS_HAT0X, -1, 1) ||
        !configureAbsAxis(controllerFd_, ABS_HAT0Y, -1, 1) ||
        !configureIdentity(controllerFd_, controllerName, 0x045e, 0x028e, 0x0114)) {
        stop();
        return false;
    }
    controllerCreated_ = true;

    if (preserveBackKey) {
        extraKeysFd_ = ::open(kUinputPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (extraKeysFd_ < 0 ||
            !setEventBit(extraKeysFd_, UI_SET_EVBIT, EV_KEY, "UI_SET_EVBIT(EV_KEY)") ||
            !setEventBit(extraKeysFd_, UI_SET_KEYBIT, KEY_BACK, "UI_SET_KEYBIT(KEY_BACK)") ||
            !configureIdentity(extraKeysFd_, "AIR X Extra Keys", 0x4001, 0x0429, 0x0100)) {
            if (extraKeysFd_ < 0) proxyLogError("open /dev/uinput for extra keys");
            stop();
            return false;
        }
        extraKeysCreated_ = true;
    }

    std::fprintf(stderr,
                 "airxpadfixd[INFO]: Created Xbox 360 proxy '%s' (USB 045e:028e)%s\n",
                 controllerName.c_str(), preserveBackKey ? " with extra-key companion" : "");
    std::fflush(stderr);
    return true;
}

bool XboxProxy::emitEvent(int fd, unsigned short type, unsigned short code, int value) {
    input_event event{};
    event.type = type;
    event.code = code;
    event.value = value;
    const ssize_t written = ::write(fd, &event, sizeof(event));
    if (written == static_cast<ssize_t>(sizeof(event))) return true;
    proxyLogError("write uinput event");
    return false;
}

bool XboxProxy::forwardEvent(unsigned short type, unsigned short code, int value) {
    if (type == EV_KEY) {
        if (code == KEY_BACK && extraKeysCreated_) {
            if (!emitEvent(extraKeysFd_, type, code, value)) return false;
            extraKeysPendingSync_ = true;
        } else if (isControllerKey(code)) {
            if (!emitEvent(controllerFd_, type, code, value)) return false;
            controllerPendingSync_ = true;
        }
        return true;
    }
    if (type == EV_ABS) {
        unsigned short outputCode = 0;
        if (mapAxis(code, &outputCode)) {
            if (!emitEvent(controllerFd_, type, outputCode, value)) return false;
            controllerPendingSync_ = true;
        }
        return true;
    }
    if (type == EV_SYN && code == SYN_REPORT) {
        if (controllerPendingSync_) {
            if (!emitEvent(controllerFd_, EV_SYN, SYN_REPORT, 0)) return false;
            controllerPendingSync_ = false;
        }
        if (extraKeysPendingSync_) {
            if (!emitEvent(extraKeysFd_, EV_SYN, SYN_REPORT, 0)) return false;
            extraKeysPendingSync_ = false;
        }
    }
    return true;
}

bool XboxProxy::processEvents() {
    if (!active() || physicalFd_ < 0) return false;
    std::array<input_event, 64> events{};
    while (true) {
        const ssize_t size = ::read(physicalFd_, events.data(), sizeof(events));
        if (size > 0) {
            if (size % static_cast<ssize_t>(sizeof(input_event)) != 0) {
                proxyLog("ERROR", "Physical controller returned a partial input event");
                return false;
            }
            const size_t count = static_cast<size_t>(size) / sizeof(input_event);
            for (size_t i = 0; i < count; ++i) {
                if (!forwardEvent(events[i].type, events[i].code, events[i].value)) return false;
            }
            continue;
        }
        if (size == 0) {
            proxyLog("WARN", "Physical controller input stream closed");
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        if (errno == EINTR) continue;
        proxyLogError("read physical controller");
        return false;
    }
}

void XboxProxy::stop() {
    if (extraKeysFd_ >= 0) {
        if (extraKeysCreated_) ::ioctl(extraKeysFd_, UI_DEV_DESTROY);
        ::close(extraKeysFd_);
    }
    if (controllerFd_ >= 0) {
        if (controllerCreated_) ::ioctl(controllerFd_, UI_DEV_DESTROY);
        ::close(controllerFd_);
    }
    if (physicalFd_ >= 0) {
        if (grabbed_) ::ioctl(physicalFd_, EVIOCGRAB, 0);
        ::close(physicalFd_);
    }
    physicalFd_ = -1;
    controllerFd_ = -1;
    extraKeysFd_ = -1;
    grabbed_ = false;
    controllerCreated_ = false;
    extraKeysCreated_ = false;
    controllerPendingSync_ = false;
    extraKeysPendingSync_ = false;
}

#pragma once

#include <string>

class XboxProxy {
  public:
    XboxProxy() = default;
    ~XboxProxy();

    XboxProxy(const XboxProxy&) = delete;
    XboxProxy& operator=(const XboxProxy&) = delete;

    bool openPhysical(const std::string& devnode);
    bool createVirtualDevices(const std::string& controllerName, bool preserveBackKey);
    bool processEvents();
    void stop();

    int physicalFd() const { return physicalFd_; }
    bool active() const { return controllerCreated_; }
    bool hasPhysical() const { return physicalFd_ >= 0; }

  private:
    bool emitEvent(int fd, unsigned short type, unsigned short code, int value);
    bool forwardEvent(unsigned short type, unsigned short code, int value);

    int physicalFd_ = -1;
    int controllerFd_ = -1;
    int extraKeysFd_ = -1;
    bool grabbed_ = false;
    bool controllerCreated_ = false;
    bool extraKeysCreated_ = false;
    bool controllerPendingSync_ = false;
    bool extraKeysPendingSync_ = false;
};

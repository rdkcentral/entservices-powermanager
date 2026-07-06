#!/bin/sh

set -eu

HEADER_ROOT="${1:?usage: generate_aidl_stub_headers.sh <header-root>}"

mkdir -p \
  "${HEADER_ROOT}/binder" \
  "${HEADER_ROOT}/utils" \
  "${HEADER_ROOT}/linux/android" \
  "${HEADER_ROOT}/com/rdk/hal/deepsleep" \
  "${HEADER_ROOT}/com/rdk/hal/boot"

cat <<'EOF' > "${HEADER_ROOT}/utils/String16.h"
#ifndef STUB_UTILS_STRING16_H
#define STUB_UTILS_STRING16_H

#include <string>

namespace android {
class String16 {
public:
    String16() = default;
    explicit String16(const char* value) : mValue(value ? value : "") {}
    explicit String16(const std::string& value) : mValue(value) {}

    const std::string& str() const { return mValue; }

    friend bool operator==(const String16& lhs, const String16& rhs) { return lhs.mValue == rhs.mValue; }
    friend bool operator!=(const String16& lhs, const String16& rhs) { return !(lhs == rhs); }

private:
    std::string mValue;
};
}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/utils/String8.h"
#ifndef STUB_UTILS_STRING8_H
#define STUB_UTILS_STRING8_H

#include <string>

#include "String16.h"

namespace android {
class String8 {
public:
    String8() = default;
    explicit String8(const char* value) : mValue(value ? value : "") {}
    explicit String8(const String16& value) : mValue(value.str()) {}

    const char* c_str() const { return mValue.c_str(); }
    const char* string() const { return mValue.c_str(); }

private:
    std::string mValue;
};
}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/utils/Vector.h"
#ifndef STUB_UTILS_VECTOR_H
#define STUB_UTILS_VECTOR_H

#include <vector>

namespace android {
template <typename T>
using Vector = std::vector<T>;
}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/utils/StrongPointer.h"
#ifndef STUB_UTILS_STRONGPOINTER_H
#define STUB_UTILS_STRONGPOINTER_H

#include <memory>

namespace android {
template <typename T>
class sp {
public:
    sp() = default;
    sp(std::nullptr_t) : mPtr(nullptr) {}
    sp(T* ptr) : mPtr(ptr) {}
    sp(const std::shared_ptr<T>& ptr) : mPtr(ptr) {}

    T* get() const { return mPtr.get(); }
    T* operator->() const { return mPtr.get(); }
    operator bool() const { return static_cast<bool>(mPtr); }
    bool operator==(std::nullptr_t) const { return mPtr == nullptr; }
    bool operator!=(std::nullptr_t) const { return mPtr != nullptr; }
    sp& operator=(T* ptr) {
        mPtr.reset(ptr);
        return *this;
    }

private:
    std::shared_ptr<T> mPtr;
};
}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/binder/Status.h"
#ifndef STUB_BINDER_STATUS_H
#define STUB_BINDER_STATUS_H

#include <string>
#include <utility>

#include "utils/String8.h"

namespace android { namespace binder {
class Status {
public:
    Status() : mOk(true), mMessage("OK") {}
    explicit Status(bool ok, std::string message = "OK") : mOk(ok), mMessage(std::move(message)) {}

    static Status ok() { return Status(true, "OK"); }
    bool isOk() const { return mOk; }
    android::String8 toString8() const { return android::String8(mMessage.c_str()); }

private:
    bool mOk;
    std::string mMessage;
};
}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/binder/IServiceManager.h"
#ifndef STUB_BINDER_ISERVICEMANAGER_H
#define STUB_BINDER_ISERVICEMANAGER_H

#include "binder/Status.h"
#include "utils/StrongPointer.h"
#include "utils/String16.h"
#include "utils/Vector.h"

namespace android {
class IBinder {
public:
    virtual ~IBinder() = default;
};

class IServiceManager {
public:
    virtual ~IServiceManager() = default;
    virtual sp<IBinder> getService(const String16&) { return sp<IBinder>(nullptr); }
    virtual Vector<String16> listServices() { return {}; }
};

class StubServiceManager : public IServiceManager {};

inline sp<IServiceManager> defaultServiceManager()
{
    static sp<IServiceManager> manager(new StubServiceManager());
    return manager;
}

template <typename T>
sp<T> interface_cast(const sp<IBinder>&)
{
    return sp<T>(new T());
}
}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/binder/ProcessState.h"
#ifndef STUB_BINDER_PROCESSSTATE_H
#define STUB_BINDER_PROCESSSTATE_H

namespace android {
class ProcessState {
public:
    static ProcessState* self()
    {
        static ProcessState instance;
        return &instance;
    }

    void startThreadPool() {}
};
}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/linux/android/binder.h"
#ifndef STUB_LINUX_ANDROID_BINDER_H
#define STUB_LINUX_ANDROID_BINDER_H

#include <cstdint>
#include <sys/ioctl.h>

typedef uintptr_t binder_uintptr_t;

struct binder_version {
    int32_t protocol_version;
};

struct binder_write_read {
    uint64_t write_size;
    uint64_t write_consumed;
    binder_uintptr_t write_buffer;
    uint64_t read_size;
    uint64_t read_consumed;
    binder_uintptr_t read_buffer;
};

struct binder_transaction_data {
    union {
        uint32_t handle;
        binder_uintptr_t ptr;
    } target;
    binder_uintptr_t cookie;
    uint32_t code;
    uint32_t flags;
    int32_t sender_pid;
    int32_t sender_euid;
    uint64_t data_size;
    uint64_t offsets_size;
    union {
        struct {
            binder_uintptr_t buffer;
            binder_uintptr_t offsets;
        } ptr;
        uint8_t buf[8];
    } data;
};

#define BINDER_WRITE_READ _IOWR('b', 1, struct binder_write_read)
#define BC_TRANSACTION 0x0
#define TF_ACCEPT_FDS 0x10
#define BR_REPLY 0x1
#define BR_DEAD_REPLY 0x2
#define BR_FAILED_REPLY 0x3
#define BR_TRANSACTION_COMPLETE 0x4
#define BR_NOOP 0x5
#define BR_OK 0x6

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/deepsleep/WakeUpTrigger.h"
#ifndef STUB_COM_RDK_HAL_DEEPSLEEP_WAKEUPTRIGGER_H
#define STUB_COM_RDK_HAL_DEEPSLEEP_WAKEUPTRIGGER_H

namespace com { namespace rdk { namespace hal { namespace deepsleep {
enum class WakeUpTrigger {
    ERROR_UNKNOWN = 0,
    TIMER,
    LAN,
    WLAN,
    VOICE,
    PRESENCE,
    RCU_BT,
    RCU_IR,
    FRONT_PANEL,
    CEC,
    RCU_RF4CE
};
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/deepsleep/KeyCode.h"
#ifndef STUB_COM_RDK_HAL_DEEPSLEEP_KEYCODE_H
#define STUB_COM_RDK_HAL_DEEPSLEEP_KEYCODE_H

#include <cstdint>

namespace com { namespace rdk { namespace hal { namespace deepsleep {
struct KeyCode {
    int32_t keyCode = 0;
};
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/deepsleep/Capabilities.h"
#ifndef STUB_COM_RDK_HAL_DEEPSLEEP_CAPABILITIES_H
#define STUB_COM_RDK_HAL_DEEPSLEEP_CAPABILITIES_H

namespace com { namespace rdk { namespace hal { namespace deepsleep {
struct Capabilities {};
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/deepsleep/IDeepSleep.h"
#ifndef STUB_COM_RDK_HAL_DEEPSLEEP_IDEEPSLEEP_H
#define STUB_COM_RDK_HAL_DEEPSLEEP_IDEEPSLEEP_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "binder/IServiceManager.h"
#include "com/rdk/hal/deepsleep/KeyCode.h"
#include "com/rdk/hal/deepsleep/WakeUpTrigger.h"

namespace com { namespace rdk { namespace hal { namespace deepsleep {
class IDeepSleep : public android::IBinder {
public:
    virtual ~IDeepSleep() = default;

    static std::string serviceName() { return "com.rdk.hal.deepsleep.IDeepSleep/default"; }

    virtual android::binder::Status setWakeUpTimer(int32_t, bool* result)
    {
        if (result) {
            *result = true;
        }
        return android::binder::Status::ok();
    }

    virtual android::binder::Status enterDeepSleep(const std::vector<WakeUpTrigger>& triggers, std::vector<WakeUpTrigger>* wokeUpByTriggers, std::optional<KeyCode>* keyCode, bool* success)
    {
        if (wokeUpByTriggers) {
            *wokeUpByTriggers = triggers;
        }
        if (keyCode) {
            *keyCode = KeyCode{};
        }
        if (success) {
            *success = true;
        }
        return android::binder::Status::ok();
    }
};
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/boot/BootReason.h"
#ifndef STUB_COM_RDK_HAL_BOOT_BOOTREASON_H
#define STUB_COM_RDK_HAL_BOOT_BOOTREASON_H

#include <string>

namespace com { namespace rdk { namespace hal { namespace boot {
enum class BootReason {
    ERROR_UNKNOWN = 0,
    WATCHDOG,
    MAINTENANCE_REBOOT,
    THERMAL_RESET,
    WARM_RESET,
    COLD_BOOT,
    STR_AUTH_FAILURE
};

inline std::string toString(BootReason reason)
{
    switch (reason) {
    case BootReason::WATCHDOG:
        return "WATCHDOG";
    case BootReason::MAINTENANCE_REBOOT:
        return "MAINTENANCE_REBOOT";
    case BootReason::THERMAL_RESET:
        return "THERMAL_RESET";
    case BootReason::WARM_RESET:
        return "WARM_RESET";
    case BootReason::COLD_BOOT:
        return "COLD_BOOT";
    case BootReason::STR_AUTH_FAILURE:
        return "STR_AUTH_FAILURE";
    case BootReason::ERROR_UNKNOWN:
    default:
        return "ERROR_UNKNOWN";
    }
}
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/boot/ResetType.h"
#ifndef STUB_COM_RDK_HAL_BOOT_RESETTYPE_H
#define STUB_COM_RDK_HAL_BOOT_RESETTYPE_H

namespace com { namespace rdk { namespace hal { namespace boot {
enum class ResetType {
    SOFTWARE_REBOOT = 0,
    MAINTENANCE_REBOOT = 1
};
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/boot/Capabilities.h"
#ifndef STUB_COM_RDK_HAL_BOOT_CAPABILITIES_H
#define STUB_COM_RDK_HAL_BOOT_CAPABILITIES_H

namespace com { namespace rdk { namespace hal { namespace boot {
struct Capabilities {};
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/boot/PowerSource.h"
#ifndef STUB_COM_RDK_HAL_BOOT_POWERSOURCE_H
#define STUB_COM_RDK_HAL_BOOT_POWERSOURCE_H

namespace com { namespace rdk { namespace hal { namespace boot {
enum class PowerSource {
    UNKNOWN = 0
};
}}}}

#endif
EOF

cat <<'EOF' > "${HEADER_ROOT}/com/rdk/hal/boot/IBoot.h"
#ifndef STUB_COM_RDK_HAL_BOOT_IBOOT_H
#define STUB_COM_RDK_HAL_BOOT_IBOOT_H

#include <string>

#include "binder/IServiceManager.h"
#include "com/rdk/hal/boot/BootReason.h"
#include "com/rdk/hal/boot/ResetType.h"

namespace com { namespace rdk { namespace hal { namespace boot {
class IBoot : public android::IBinder {
public:
    virtual ~IBoot() = default;

    static std::string serviceName() { return "com.rdk.hal.boot.IBoot/default"; }

    virtual android::binder::Status getBootReason(BootReason* bootReason)
    {
        if (bootReason) {
            *bootReason = BootReason::ERROR_UNKNOWN;
        }
        return android::binder::Status::ok();
    }

    virtual android::binder::Status reboot(ResetType, const android::String16&)
    {
        return android::binder::Status::ok();
    }
};
}}}}

#endif
EOF

/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

#include <linux/android/binder.h>
#include <binder/IServiceManager.h>
#include <com/rdk/hal/deepsleep/IDeepSleep.h>
#include <com/rdk/hal/boot/IBoot.h>

#include "Module.h"
#include <core/Portability.h>

#include "PowerManagerFactory.h"
#include "ServiceManagerCheck.h"

#include "UtilsLogging.h"

#include "DeepSleepImpl.h"
#include "PowerImpl.h"

#include "DeepSleepAidlImpl.h"
#include "PowerAidlImpl.h"

using namespace com::rdk::hal::boot;
using namespace com::rdk::hal::deepsleep;
static const android::String16 mServiceManagerName("manager");

namespace {
class HalFactoryUtility {
    public:
        enum class BackendType {
            UNKNOWN,
            LEGACY,
            AIDL
        };

        static std::unordered_map<std::string, BackendType> mBackendType;

        static bool isAidlServiceAvailable(const android::String16 &expectedServiceName)
        {
            LOGINFO("isAidlServiceAvailable invoked\r\n");

            const std::string expectedServiceNameString = android::String8(expectedServiceName).string();
            auto backendTypeIt = mBackendType.find(expectedServiceNameString);

            if (backendTypeIt != mBackendType.end()) {
                if (backendTypeIt->second == BackendType::AIDL) {
                    return true;
                } else if (backendTypeIt->second == BackendType::LEGACY) {
                    return false;
                }
            } else {
                backendTypeIt = mBackendType.emplace(expectedServiceNameString, BackendType::UNKNOWN).first;
            }

            if (!isServiceManagerAvailable()) {
                LOGINFO("Binder driver not available; falling back to legacy HAL\r\n");
                backendTypeIt->second = BackendType::LEGACY;
                return false;
            }

            android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
            if (serviceManager == nullptr) {
                LOGERR("isAidlServiceAvailable failed: IServiceManager unavailable\r\n");
                backendTypeIt->second = BackendType::LEGACY;
                return false;
            }

            LOGINFO("Successfully obtained IServiceManager\r\n");

            android::Vector<android::String16> services = serviceManager->listServices();
            size_t discoveredServiceCount = 0;
            bool matched = false;

            for (size_t index = 0; index < services.size(); ++index) {
                if (services[index] != mServiceManagerName) {
                    ++discoveredServiceCount;
                }
            }

            LOGINFO("isAidlServiceAvailable discovered %zu binder services\r\n", discoveredServiceCount);
            if (discoveredServiceCount == 0) {
                LOGINFO(
                    "isAidlServiceAvailable found no binder services beyond the ServiceManager entry while searching for '%s'\r\n",
                    android::String8(expectedServiceName).string());
                backendTypeIt->second = BackendType::LEGACY;
                return false;
            }

            LOGINFO(
                "isAidlServiceAvailable inspecting %zu registered binder services for '%s'\r\n",
                discoveredServiceCount, android::String8(expectedServiceName).string());

            for (size_t index = 0; index < services.size(); ++index) {
                if (services[index] == mServiceManagerName) {
                    continue;
                }

                const android::String8 discoveredServiceName(services[index]);
                if (services[index] == expectedServiceName) {
                    matched = true;
                }

                LOGINFO(
                    "isAidlServiceAvailable discovered binder service[%zu]='%s'\r\n",
                    index,
                    discoveredServiceName.string());
            }

            if (matched) {
                LOGINFO(
                    "isAidlServiceAvailable found AIDL service '%s'\r\n",
                    android::String8(expectedServiceName).string());
                backendTypeIt->second = BackendType::AIDL;
                return true;
            }

            LOGINFO(
                "isAidlServiceAvailable did not find AIDL service '%s'\r\n",
                android::String8(expectedServiceName).string());
            backendTypeIt->second = BackendType::LEGACY;
            return false;
        }
    };
    std::unordered_map<std::string, HalFactoryUtility::BackendType> HalFactoryUtility::mBackendType;
}

std::shared_ptr<hal::deepsleep::IPlatform> PowerManagerFactory::CreateDeepSleepPlatform()
{
    if (HalFactoryUtility::isAidlServiceAvailable(android::String16(IDeepSleep::serviceName().c_str()))) {
        LOGINFO("Using AIDL backend for DeepSleep HAL");
        return std::make_shared<DeepSleepAidlImpl>();
    }

    LOGINFO("Using RDKV backend for DeepSleep HAL");
    return std::make_shared<DeepSleepImpl>();
}

std::unique_ptr<hal::power::IPlatform> PowerManagerFactory::CreatePowerPlatform()
{
    if (HalFactoryUtility::isAidlServiceAvailable(android::String16(IBoot::serviceName().c_str()))) {
        LOGINFO("Using AIDL backend for Power HAL");
        return std::unique_ptr<PowerAidlImpl>(new PowerAidlImpl());
    }
    LOGINFO("Using RDKV backend for Power HAL");
    return std::unique_ptr<hal::power::IPlatform>(new PowerImpl());
}


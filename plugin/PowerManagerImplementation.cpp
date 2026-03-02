/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
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

#include <chrono>
#include <memory>

#include "PowerManagerImplementation.h"

#include "PowerUtils.h"
#include "UtilsIarm.h"
#include "UtilsLogging.h"

#include <core/Portability.h>
#include <interfaces/IPowerManager.h>

#define STANDBY_REASON_FILE "/opt/standbyReason.txt"

using util                           = PowerUtils;
using WakeupSourceConfig             = WPEFramework::Exchange::IPowerManager::WakeupSourceConfig;
using IWakeupSourceConfigIterator    = WPEFramework::Exchange::IPowerManager::IWakeupSourceConfigIterator;
using WakeupSourceConfigIteratorImpl = WPEFramework::Core::Service<WPEFramework::RPC::IteratorType<IWakeupSourceConfigIterator>>;

int WPEFramework::Plugin::PowerManagerImplementation::PreModeChangeController::_nextTransactionId = 0;
uint32_t WPEFramework::Plugin::PowerManagerImplementation::_nextClientId                          = 0;

#ifndef POWER_MODE_PRECHANGE_TIMEOUT_SEC
#define POWER_MODE_PRECHANGE_TIMEOUT_SEC 1
#endif

// Device is considered to be in transient deep sleep state if
// 1. As per PowerManager PowerState is DEEP_SLEEP, but then SoC is not in deepsleep
// 2. SoC woke-up from deep sleep even before schedule timeout
static constexpr int kTransientDeepsleepThresholdSec = 5;

using namespace std;

namespace WPEFramework {
namespace Plugin {

    SERVICE_REGISTRATION(PowerManagerImplementation, 1, 0);
    PowerManagerImplementation* PowerManagerImplementation::_instance = nullptr;

    PowerManagerImplementation::PowerManagerImplementation()
        : m_powerStateBeforeReboot(POWER_STATE_UNKNOWN)
        , m_networkStandbyMode(false)
        , m_networkStandbyModeValid(false)
        , m_powerStateBeforeRebootValid(false)
        , _modeChangeController(nullptr)
        , _deepSleepController(DeepSleepController::Create(*this))
        , _powerController(PowerController::Create(_deepSleepController))
        , _thermalController(ThermalController::Create(*this))
    {
        PowerManagerImplementation::_instance = this;
        Utils::IARM::init();
        LOGINFO(">> CTOR <<");
    }

    PowerManagerImplementation::~PowerManagerImplementation()
    {
        LOGINFO(">> DTOR <<");
    }

    void PowerManagerImplementation::dispatchPowerModeChangedEvent(const PowerState& prevState, const PowerState& newState)
    {
        LOGINFO(">>");
        _callbackLock.Lock();
        for (auto& notification : _modeChangedNotifications) {
            auto start = std::chrono::steady_clock::now();
            notification->OnPowerModeChanged(prevState, newState);
            auto elapsed = std::chrono::steady_clock::now() - start;
            LOGINFO("client %p took %" PRId64 "ms to process IModeChanged event", notification, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        }
        _callbackLock.Unlock();
        LOGINFO("<<");
    }

    void PowerManagerImplementation::dispatchDeepSleepTimeoutEvent(const uint32_t& timeout)
    {
        LOGINFO(">>");
        _callbackLock.Lock();
        for (auto& notification : _deepSleepTimeoutNotifications) {
            auto start = std::chrono::steady_clock::now();
            notification->OnDeepSleepTimeout(timeout);
            auto elapsed = std::chrono::steady_clock::now() - start;
            LOGINFO("client %p took %" PRId64 "ms to process IDeepSleepTimeout event", notification, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        }
        _callbackLock.Unlock();
        LOGINFO("<<");
    }

    void PowerManagerImplementation::dispatchRebootBeginEvent(const string& rebootRequestor, const std::string& rebootReasonCustom, const string& rebootReasonOther)
    {
        LOGINFO(">>");
        _callbackLock.Lock();
        for (auto& notification : _rebootNotifications) {
            auto start = std::chrono::steady_clock::now();
            notification->OnRebootBegin(rebootReasonCustom, rebootReasonOther, rebootRequestor);
            auto elapsed = std::chrono::steady_clock::now() - start;
            LOGINFO("client %p took %" PRId64 "ms to process IReboot event", notification, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        }
        _callbackLock.Unlock();
        LOGINFO("<<");
    }

    void PowerManagerImplementation::dispatchThermalModeChangedEvent(const ThermalTemperature& currentThermalLevel, const ThermalTemperature& newThermalLevel, const float& currentTemperature)
    {
        LOGINFO(">>");
        _callbackLock.Lock();
        for (auto& notification : _thermalModeChangedNotifications) {
            auto start = std::chrono::steady_clock::now();
            notification->OnThermalModeChanged(currentThermalLevel, newThermalLevel, currentTemperature);
            auto elapsed = std::chrono::steady_clock::now() - start;
            LOGINFO("client %p took %" PRId64 "ms to process IThermalModeChanged event", notification, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        }
        _callbackLock.Unlock();
        LOGINFO("<<");
    }

    void PowerManagerImplementation::dispatchNetworkStandbyModeChangedEvent(const bool& enabled)
    {
        LOGINFO(">>");
        _callbackLock.Lock();
        for (auto& notification : _networkStandbyModeChangedNotifications) {
            auto start = std::chrono::steady_clock::now();
            notification->OnNetworkStandbyModeChanged(enabled);
            auto elapsed = std::chrono::steady_clock::now() - start;
            LOGINFO("client %p took %" PRId64 "ms to process INetworkStandbyModeChanged event", notification, std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
        }
        _callbackLock.Unlock();
        LOGINFO("<<");
    }

    template <typename T>
    Core::hresult PowerManagerImplementation::Register(std::list<T*>& list, T* notification)
    {
        uint32_t status = Core::ERROR_GENERAL;

        ASSERT(nullptr != notification);
        _callbackLock.Lock();

        // Make sure we can't register the same notification callback multiple times
        if (std::find(list.begin(), list.end(), notification) == list.end()) {
            list.push_back(notification);
            notification->AddRef();
            status = Core::ERROR_NONE;
        }

        _callbackLock.Unlock();
        return status;
    }

    template <typename T>
    Core::hresult PowerManagerImplementation::Unregister(std::list<T*>& list, const T* notification)
    {
        uint32_t status = Core::ERROR_GENERAL;

        ASSERT(nullptr != notification);
        _callbackLock.Lock();

        // Make sure we can't unregister the same notification callback multiple times
        auto itr = std::find(list.begin(), list.end(), notification);
        if (itr != list.end()) {
            (*itr)->Release();
            list.erase(itr);
            status = Core::ERROR_NONE;
        }

        _callbackLock.Unlock();
        return status;
    }

    Core::hresult PowerManagerImplementation::Register(Exchange::IPowerManager::IRebootNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Register(_rebootNotifications, notification);
        LOGINFO("<< IReboot %p, errorCode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Unregister(const Exchange::IPowerManager::IRebootNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Unregister(_rebootNotifications, notification);
        LOGINFO("<< IRebootNotification %p, errorCode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Register(Exchange::IPowerManager::IModePreChangeNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Register(_preModeChangeNotifications, notification);
        LOGINFO("<< IModePreChange %p, errorCode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Unregister(const Exchange::IPowerManager::IModePreChangeNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Unregister(_preModeChangeNotifications, notification);
        LOGINFO("<< IModePreChange %p, errorCode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Register(Exchange::IPowerManager::IModeChangedNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Register(_modeChangedNotifications, notification);
        LOGINFO("<< IModeChanged %p, errorCode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Unregister(const Exchange::IPowerManager::IModeChangedNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Unregister(_modeChangedNotifications, notification);
        LOGINFO("<< IModeChanged %p, errorcode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Register(Exchange::IPowerManager::IDeepSleepTimeoutNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Register(_deepSleepTimeoutNotifications, notification);
        LOGINFO("<< IDeepSleepTimeout %p, errorcode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Unregister(const Exchange::IPowerManager::IDeepSleepTimeoutNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Unregister(_deepSleepTimeoutNotifications, notification);
        LOGINFO("<< IDeepSleepTimeout %p, errorcode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Register(Exchange::IPowerManager::INetworkStandbyModeChangedNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Register(_networkStandbyModeChangedNotifications, notification);
        LOGINFO("<< INetworkStandbyModeChanged %p, errorcode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Unregister(const Exchange::IPowerManager::INetworkStandbyModeChangedNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Unregister(_networkStandbyModeChangedNotifications, notification);
        LOGINFO("<< INetworkStandbyModeChanged %p, errorcode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Register(Exchange::IPowerManager::IThermalModeChangedNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Register(_thermalModeChangedNotifications, notification);
        LOGINFO("<< IThermalModeChanged %p, errorcode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Unregister(const Exchange::IPowerManager::IThermalModeChangedNotification* notification)
    {
        LOGINFO(">>");
        Core::hresult errorCode = Unregister(_thermalModeChangedNotifications, notification);
        LOGINFO("<< IThermalModeChanged %p, errorcode: %u", notification, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetPowerState(PowerState& currentState, PowerState& prevState) const
    {
        LOGINFO(">>");

        _apiLock.Lock();

        uint32_t errorCode = _powerController.GetPowerState(currentState, prevState);

        _apiLock.Unlock();

        LOGINFO("<< currentState : %s, prevState : %s, errorCode = %d", util::str(currentState), util::str(prevState), errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::setDevicePowerState(const int& keyCode, PowerState prevState, PowerState newState, const std::string& reason)
    {
        uint32_t errorCode = _powerController.SetPowerState(keyCode, newState, reason);

        if (Core::ERROR_NONE != errorCode) {
            LOGERR("Failed to set power state, errorCode: %d", errorCode);
            return errorCode;
        }

        // We don't do a thread switching here, as it may move device to deep sleep mode
        // even before client receiving the event
        dispatchPowerModeChangedEvent(prevState, newState);

        LOGINFO("keyCode: %d, prevState: %s, newState: %s, reason: %s, errorcode: %u", keyCode, util::str(prevState), util::str(newState), reason.c_str(), errorCode);

        if (PowerState::POWER_STATE_STANDBY_DEEP_SLEEP == newState) {
            LOGINFO("newState is DEEP SLEEP, activating deep sleep mode");
            _powerController.ActivateDeepSleep();
        }

        return errorCode;
    }

    // state change is sync only if transitioning from DEEP_SLEEP => LIGHT_SLEEP
    bool PowerManagerImplementation::isSyncStateChange(PowerState currState, PowerState newState) const
    {
        return (currState == PowerState::POWER_STATE_STANDBY_DEEP_SLEEP
            && newState == PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP);
    }

    // SetPowerState takes in a request to change PowerState, notify PowerModePreChange all reqistered clients
    // waits for Acknowledgment from clients using PreModeChangeController and runs Completion handler
    // after either receiving all `PowerModePreChangeComplete` acknowledgements or after timeout.
    // In Completion handler actual PowerState change is performed followed by notification to all clients
    // with IModeChanged notification event
    //
    // This API is async and has to takes care of many transient usecases hence complexity
    // 1. Straight fwd Power State change request
    // 2. Nested Power State change requests (where old request gets canceled)
    //    - weak_ptr is used to avoid race conditions between old request deletion and running of Completion handler
    //    - Nested state change requests for same state change requests (ex ON over ON) is silently ignored (i,e old state change request is not cancelled)
    // 3. To enforce state change, sync run model is intrduced where selfLock is held until state change is complete.
    //    - This was introduced because immerse ui was not launching if there is a direct transition from DEEP_SLEEP => ON (see RDKEMW-5633)
    Core::hresult PowerManagerImplementation::SetPowerState(const int keyCode, const PowerState newState, const string& reason)
    {
        static WPEFramework::Core::BinairySemaphore selfLock{ 1, 1 };

        PowerState currState = POWER_STATE_UNKNOWN;
        PowerState prevState = POWER_STATE_UNKNOWN;
        bool isSync          = false; // Perform state change in sync manner or async manner.
                                      // If isSync is `true` nested state change requests are blocked using `selfLock`
                                      // until previous state change is complete (i,e nested state change requests will be blocked)

        LOGINFO(">> newState: %s, reason %s", util::str(newState), reason.c_str());

        selfLock.Lock();

        LOGINFO("selfLock Acquired");

        uint32_t errorCode = GetPowerState(currState, prevState);

        // Cannot determine current state, won't be able to process request
        if (Core::ERROR_NONE != errorCode) {
            LOGERR("Failed to get current power state, errorCode: %d", errorCode);
            selfLock.Unlock();
            LOGINFO("selfLock Released isSync: na");
            return errorCode;
        }

        // Process request only if requested state is not same as current state
        if (currState != newState) {

            // Check if sync state change required
            isSync = isSyncStateChange(currState, newState);

            if (POWER_STATE_STANDBY_DEEP_SLEEP == currState) {
                if (_deepSleepController.IsDeepSleepInProgress()
                    && (_deepSleepController.Elapsed() < std::chrono::seconds(kTransientDeepsleepThresholdSec))) {

                    LOGINFO("deepsleep in  progress  ignoring %s request, elapsed: %" PRId64 " sec",
                            util::str(newState), std::chrono::duration_cast<std::chrono::seconds>(_deepSleepController.Elapsed()).count());

                    selfLock.Unlock();
                    LOGINFO("selfLock Released isSync: na");
                    return Core::ERROR_NONE;
                }
                // Deepsleep not in progress, so wakeup from deep sleep
                LOGINFO("Device wakeup from DEEP_SLEEP to %s", util::str(newState));
                _deepSleepController.Deactivate();
            }

            _apiLock.Lock();

            if (_modeChangeController) {
                LOGINFO("There is a state change request in progress for %s state.", util::str(_modeChangeController->powerState()));
                if (_modeChangeController->powerState() == newState) {
                    LOGINFO("Ignore (redundant) repeated transition request to %s state.", util::str(newState));
                    _apiLock.Unlock();
                    selfLock.Unlock();
                    return Core::ERROR_NONE;
                } else {
                    LOGWARN("Power state change is already in progress, cancel old request");
                    _modeChangeController.reset();
                }
            }

            _modeChangeController   = std::shared_ptr<PreModeChangeController>(new PreModeChangeController(newState));
            const int transactionId = _modeChangeController->TransactionId(); // transactionId is unique per request

            // Add all clients to ack await list (who we expect `PreChangeComplete` ack from)
            for (const auto& client : _modeChangeClients) {
                _modeChangeController->AckAwait(client.first);
            }

            // For sync state change requests timeout is `0`
            const uint32_t timeOut = isSync ? 0 : POWER_MODE_PRECHANGE_TIMEOUT_SEC;

            // Like in `Job` class we avoid impl destruction before handler is invoked
            this->AddRef();

            _apiLock.Unlock();

            // Dispatch pre power mode change notifications, we cannot take in apiLock here
            // as clients could call any PowerManager plugin APIs
            submitPowerModePreChangeEvent(currState, newState, transactionId, timeOut);

            // Starts pre modeChange timer, and waits for Ack from clients for given timeOut duration
            // On all clients acknowledging or upon timeout (whichever happens first), Completion handler gets triggered
            // The thread context in which completion handler is triggered could be
            //  1. Caller thread if timeout `0`
            //  2. Caller thread if all clients have acknowledged for power state transition even before `Schedule` gets called
            //  3. ACK TIMER thread if `Schedule` timed-out
            //     - To avoid race conditions in this usecase, take `_apiLock` to run completion handler
            //  4. Caller thread of last acknowledging client
            _modeChangeController->Schedule(timeOut * 1000,
                [this, keyCode, currState, newState, reason, isSync](bool isTimedout, bool isAborted) mutable {
                    LOGINFO(">> CompletionHandler isTimedout: %d, isAborted: %d", isTimedout, isAborted);

                    if (!isAborted) {
                        powerModePreChangeCompletionHandler(keyCode, currState, newState, reason);
                    } else {
                        LOGWARN("modeChangeController was already deleted, do not process CompletionHandler");
                    }

                    // Release the refCount taken just before _modeChangeController->Schedule
                    this->Release();

                    // For sync state change requests, the selfLock is held until this point. Release it now.
                    if (isSync) {
                        selfLock.Unlock();
                        LOGINFO("selfLock Released isSync: true");
                    }

                    LOGINFO("<< CompletionHandler");
                });
        } else {
            LOGINFO("Requested power state is same as current power state, no action required");
        }

        // For Async state change requests, release the lock immediately, allowing nested state changes if required
        // Example: DEEP_SLEEP is in transition (mediarite has delayed PowerState by 15 seconds), and user attemps to turn ON the device
        if (!isSync) {
            selfLock.Unlock();
            LOGINFO("selfLock Released isSync: false");
        }

        LOGINFO("<< keyCode: %d, newState: %s, errorCode: %d", keyCode, util::str(newState), Core::ERROR_NONE);

        return Core::ERROR_NONE;
    }

    void PowerManagerImplementation::submitPowerModePreChangeEvent(const PowerState currentState, const PowerState newState, const int transactionId, const int timeOut)
    {
        LOGINFO(">> currentState : %s, newState : %s, transactionId : %d", util::str(currentState), util::str(newState), transactionId);
        for (auto& notification : _preModeChangeNotifications) {
            Core::IWorkerPool::Instance().Submit(
                PowerManagerImplementation::LambdaJob::Create(this,
                    [notification, currentState, newState, transactionId, timeOut]() {
                        notification->OnPowerModePreChange(currentState, newState, transactionId, timeOut);
                    }));
        }

        LOGINFO("<< currentState : %s, newState : %s, transactionId : %d", util::str(currentState), util::str(newState), transactionId);
    }

    Core::hresult PowerManagerImplementation::GetTemperatureThresholds(float& high, float& critical) const
    {
        LOGINFO(">>");

        _apiLock.Lock();

        Core::hresult errorCode = _thermalController.GetTemperatureThresholds(high, critical);

        _apiLock.Unlock();

        LOGINFO("high: %f, critical: %f, errorCode: %u", high, critical, errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::SetTemperatureThresholds(const float high, const float critical)
    {
        LOGINFO(">> high: %f, critical: %f", high, critical);

        _apiLock.Lock();

        Core::hresult errorCode = _thermalController.SetTemperatureThresholds(high, critical);

        _apiLock.Unlock();

        LOGINFO("<< errorCode: %u", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetOvertempGraceInterval(int& graceInterval) const
    {
        LOGINFO(">>");

        _apiLock.Lock();

        Core::hresult errorCode = _thermalController.GetOvertempGraceInterval(graceInterval);

        _apiLock.Unlock();

        LOGINFO("<< graceInterval: %d, errorCode: %u", graceInterval, errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::SetOvertempGraceInterval(const int graceInterval)
    {
        LOGINFO(">> graceInterval: %d", graceInterval);

        _apiLock.Lock();

        Core::hresult errorCode = _thermalController.SetOvertempGraceInterval(graceInterval);

        _apiLock.Unlock();

        LOGINFO("<< errorCode: %u", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetThermalState(float& temperature) const
    {
        Core::hresult errorCode = Core::ERROR_GENERAL;

        LOGINFO(">>");

#ifdef ENABLE_THERMAL_PROTECTION
        _apiLock.Lock();

        ThermalTemperature curLevel = THERMAL_TEMPERATURE_UNKNOWN;
        float curTemperature        = 0;

        errorCode   = _thermalController.GetThermalState(curLevel, curTemperature);
        temperature = curTemperature;
        _apiLock.Unlock();
#else
        temperature = -1;
        errorCode   = Core::ERROR_GENERAL;
        LOGWARN("<< Thermal Protection disabled for this platform");
#endif
        LOGINFO("<< Current core temperature is : %f, errorCode: %u", temperature, errorCode);
        return errorCode;
    }

    Core::hresult PowerManagerImplementation::SetDeepSleepTimer(const int timeOut)
    {
        LOGINFO(">> timeOut: %d", timeOut);

        int timeOutVal = timeOut > 86400 ? 0 : timeOut;

        _apiLock.Lock();

        uint32_t errorCode = _powerController.SetDeepSleepTimer(timeOutVal);

        _apiLock.Unlock();

        LOGINFO("<< timeOutVal: %d, errorCode: %u", timeOutVal, errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetLastWakeupReason(WakeupReason& wakeupReason) const
    {
        LOGINFO(">>");

        _apiLock.Lock();

        uint32_t errorCode = _deepSleepController.GetLastWakeupReason(wakeupReason);

        _apiLock.Unlock();

        LOGINFO("<< wakeupReason: %u, errorCode: %u", wakeupReason, errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetLastWakeupKeyCode(int& keycode) const
    {
        LOGINFO(">>");

        _apiLock.Lock();

        uint32_t errorCode = _deepSleepController.GetLastWakeupKeyCode(keycode);

        _apiLock.Unlock();

        LOGINFO("<< Wakeup keycode: %d, errorCode: %u", keycode, errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetTimeSinceWakeup(TimeSinceWakeup& timeSinceWakeup)
    {
        LOGINFO(">>");

        _apiLock.Lock();

        uint32_t errorCode = _powerController.GetTimeSinceWakeup(timeSinceWakeup.secondsSinceWakeup);

        _apiLock.Unlock();

        LOGINFO("<< secondsSinceWakeup: %u, errorCode: %u", timeSinceWakeup.secondsSinceWakeup, errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::Reboot(const string& rebootRequestor, const string& rebootReasonCustom, const string& rebootReasonOther)
    {
        const string defaultArg   = "Unknown";
        const string requestor    = rebootRequestor.empty() ? defaultArg : rebootRequestor;
        const string customReason = rebootReasonCustom.empty() ? defaultArg : rebootReasonCustom;
        const string otherReason  = rebootReasonOther.empty() ? defaultArg : rebootReasonOther;

        LOGINFO(">> requestor %s, custom reason: %s, other reason: %s", requestor.c_str(), customReason.c_str(), otherReason.c_str());

        dispatchRebootBeginEvent(requestor, customReason, otherReason);

        _apiLock.Lock();

        uint32_t errorCode = _powerController.Reboot(rebootRequestor, rebootReasonCustom, rebootReasonOther);

        _apiLock.Unlock();

        LOGINFO("<< errorcode: %u", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::SetNetworkStandbyMode(const bool standbyMode)
    {
        LOGINFO(">> nwStandbyMode: %s", (standbyMode ? "enabled" : "disabled"));

        _apiLock.Lock();

        uint32_t errorCode = _powerController.SetNetworkStandbyMode(standbyMode);

        _apiLock.Unlock();

        if (Core::ERROR_NONE == errorCode) {
            // In the original IARM Power Manager implementation, notifications were always sent
            // out regardless of whether the network standby mode had changed. This behavior is
            // preserved in the new code for consistency.
            dispatchNetworkStandbyModeChangedEvent(standbyMode);
        }

        LOGINFO("<< errorcode: %u", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetNetworkStandbyMode(bool& standbyMode)
    {
        LOGINFO(">>");

        _apiLock.Lock();

        uint32_t errorCode = _powerController.GetNetworkStandbyMode(standbyMode);

        _apiLock.Unlock();

        LOGINFO("<< NwStandbyMode: %s, errorCode: %d",
            (standbyMode ? ("Enabled") : ("Disabled")), errorCode);

        return errorCode;
    }

    bool PowerManagerImplementation::isWakeupSrcEnabled(const std::list<WakeupSourceConfig>& configs, WakeupSrcType src) const
    {
        auto it = std::find_if(configs.begin(), configs.end(),
            [&](const WakeupSourceConfig& config) { return config.wakeupSource == src; });

        if (it == configs.end()) {
            // not found
            return false;
        }
        return it->enabled;
    }

    Core::hresult PowerManagerImplementation::setWakeupSourceConfig(const std::list<WakeupSourceConfig>& configs)
    {
        LOGINFO(">>");

        _apiLock.Lock();

        uint32_t errorCode = _powerController.SetWakeupSourceConfig(configs);

        _apiLock.Unlock();

        do {
            if (Core::ERROR_NONE != errorCode) {
                LOGERR("Failed to SetWakeupSourceConfig");
                break;
            }

            std::list<WakeupSourceConfig> currConfigs;

            errorCode = getWakeupSourceConfig(currConfigs);

            if (Core::ERROR_NONE != errorCode) {
                LOGERR("Failed to getWakeupSourceConfig");
                break;
            }

            bool isWiFiEnabled =  isWakeupSrcEnabled(currConfigs, WakeupSrcType::WAKEUP_SRC_WIFI);
            bool isLanEnabled  =  isWakeupSrcEnabled(currConfigs, WakeupSrcType::WAKEUP_SRC_LAN);

            // Update nwStandbyMode only if Wi-Fi and LAN are set to same state (both ON or both OFF).
            if (isWiFiEnabled != isLanEnabled) {
                LOGINFO("WakeupSrc WIFI: %d, LAN: %d", isWiFiEnabled, isLanEnabled);
                break;
            }

            bool currNwStandbyMode = false;

            errorCode = GetNetworkStandbyMode(currNwStandbyMode);

            if (Core::ERROR_NONE != errorCode) {
                LOGERR("Failed to fetch current nwStandbymode");
                break;
            }

            bool nwStandbyMode = isWiFiEnabled && isLanEnabled;

            if (nwStandbyMode == currNwStandbyMode) {
                LOGINFO("nwStandbyMode is already set to %d", nwStandbyMode);
                break;
            }

            errorCode = SetNetworkStandbyMode(nwStandbyMode);
        } while (false);

        LOGINFO("<< errorCode: %d", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::SetWakeupSourceConfig(IWakeupSourceConfigIterator* wakeupSources)
    {
        uint32_t errorCode = Core::ERROR_NONE;
        WakeupSourceConfig config{ WakeupSrcType::WAKEUP_SRC_UNKNOWN, false };

        std::list<WakeupSourceConfig> configs;

        LOGINFO(">>");

        // create std::list from Thunder iterator
        while (wakeupSources->Next(config)) {
            if (WakeupSrcType::WAKEUP_SRC_UNKNOWN == config.wakeupSource) {
                errorCode = Core::ERROR_INVALID_PARAMETER;
                break;
            }
            configs.push_back(config);
        }

        if (Core::ERROR_NONE == errorCode) {
            errorCode = setWakeupSourceConfig(configs);
        }

        LOGINFO("<< errorCode: %d", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::getWakeupSourceConfig(std::list<WakeupSourceConfig>& configs) const
    {
        _apiLock.Lock();

        uint32_t errorCode = _powerController.GetWakeupSourceConfig(configs);

        _apiLock.Unlock();

        LOGINFO("<< errorCode: %d", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetWakeupSourceConfig(IWakeupSourceConfigIterator*& wakeupSources) const
    {
        std::list<WakeupSourceConfig> configs;

        LOGINFO(">>");

        uint32_t errorCode = getWakeupSourceConfig(configs);

        if (Core::ERROR_NONE == errorCode) {
            wakeupSources = WakeupSourceConfigIteratorImpl::Create<IWakeupSourceConfigIterator>(configs);
        }

        LOGINFO("<< errorCode: %d", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::GetPowerStateBeforeReboot(PowerState& powerStateBeforeReboot)
    {
        LOGINFO(">>");

        _apiLock.Lock();

        uint32_t errorCode = _powerController.GetPowerStateBeforeReboot(powerStateBeforeReboot);

        _apiLock.Unlock();

        LOGINFO("<< powerStateBeforeReboot: %s, errorCode: %d", util::str(powerStateBeforeReboot), errorCode);

        return errorCode;
    }

    void PowerManagerImplementation::powerModePreChangeCompletionHandler(const int keyCode, PowerState currentState, PowerState newState, const std::string& reason)
    {
        LOGINFO(">> keyCode: %d, powerState: %s", keyCode, util::str(newState));

        setDevicePowerState(keyCode, currentState, newState, reason);

        LOGINFO("<<");
    }

    Core::hresult PowerManagerImplementation::PowerModePreChangeComplete(const uint32_t clientId, const int transactionId)
    {
        uint32_t errorCode = Core::ERROR_INVALID_PARAMETER;

        LOGINFO(">> clientId: %u, transactionId: %d", clientId, transactionId);

        _apiLock.Lock();

        if (_modeChangeController) {
            errorCode = _modeChangeController->Ack(clientId, transactionId);
        }

        _apiLock.Unlock();

        LOGINFO("<< errorcode: %u", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::DelayPowerModeChangeBy(const uint32_t clientId, const int transactionId, const int delayPeriod)
    {
        uint32_t errorCode = Core::ERROR_INVALID_PARAMETER;

        LOGINFO(">> clientId: %u, transactionId: %d, delayPeriod: %d", clientId, transactionId, delayPeriod);
        _apiLock.Lock();

        if (_modeChangeController) {
            errorCode = _modeChangeController->Reschedule(clientId, transactionId, delayPeriod * 1000);
        }

        _apiLock.Unlock();

        LOGINFO("<< errorcode: %u", errorCode);

        return errorCode;
    }

    Core::hresult PowerManagerImplementation::AddPowerModePreChangeClient(const string& clientName, uint32_t& clientId)
    {
        LOGINFO(">> client: %s, clientId: %u", clientName.c_str(), clientId);

        if (clientName.empty()) {
            LOGERR("AddPowerModePreChangeClient called with empty clientName");
            return Core::ERROR_INVALID_PARAMETER;
        }

        _apiLock.Lock();

        auto it = std::find_if(_modeChangeClients.cbegin(), _modeChangeClients.cend(),
            [&clientName](const std::pair<uint32_t, string>& client) {
                return client.second == clientName;
            });

        if (it == _modeChangeClients.end()) {
            clientId                     = ++_nextClientId;
            _modeChangeClients[clientId] = clientName;
        } else {
            // client is already registered, return the clientId
            clientId = it->first;
        }

        _apiLock.Unlock();

        for (auto& clients : _modeChangeClients) {
            LOGINFO("Registered client: %s, clientId: %u", clients.second.c_str(), clients.first);
        }

        LOGINFO("<< errorCode: 0");

        return Core::ERROR_NONE;
    }

    Core::hresult PowerManagerImplementation::RemovePowerModePreChangeClient(const uint32_t clientId)
    {
        uint32_t errorCode = Core::ERROR_INVALID_PARAMETER;
        std::string clientName;

        LOGINFO(">> clientId: %d", clientId);

        _apiLock.Lock();

        auto it = _modeChangeClients.find(clientId);

        if (it != _modeChangeClients.end()) {
            clientName = it->second;
            _modeChangeClients.erase(it);

            // self-ack if called while power mode change is in progress
            if (_modeChangeController) {
                _modeChangeController->Ack(clientId);
            }
            errorCode = Core::ERROR_NONE;
        }

        _apiLock.Unlock();

        LOGINFO("<< client: %s, clientId: %u, errorcode: %u", clientName.c_str(), clientId, errorCode);

        return errorCode;
    }

    void PowerManagerImplementation::onDeepSleepTimerWakeup(const int wakeupTimeout)
    {
        LOGINFO(">> DeepSleep timedout: %d", wakeupTimeout);
        dispatchDeepSleepTimeoutEvent(wakeupTimeout);

        /*Scheduled maintanace reboot is disabled. Instead state will change to LIGHT_SLEEP*/
        LOGINFO("Set Device to light sleep on Deep Sleep timer expiry");
        SetPowerState(0, PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP, "DeepSleep timedout");
        LOGINFO("<<");
    }

    void PowerManagerImplementation::onDeepSleepUserWakeup(const bool userWakeup)
    {
        PowerState newState = PowerState::POWER_STATE_ON;

#ifdef PLATCO_BOOTTO_STANDBY
        newState = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP;
#endif
        LOGINFO(">> User triggered wakeup from DEEP_SLEEP, moving to powerState: %s", util::str(newState));
        SetPowerState(0, newState, "DeepSleep userwakeup");
        LOGINFO("<<");
    }

    void PowerManagerImplementation::onDeepSleepFailed()
    {
        PowerState newState = PowerState::POWER_STATE_ON;

#ifdef PLATCO_BOOTTO_STANDBY
        newState = PowerState::POWER_STATE_STANDBY_LIGHT_SLEEP;
#endif
        LOGINFO(">> Failed to enter DeepSleep, moving to powerState: %s", util::str(newState));
        SetPowerState(0, newState, "DeepSleep failed");
        LOGINFO("<<");
    }

    void PowerManagerImplementation::onThermalTemperatureChanged(const ThermalTemperature cur_Thermal_Level,
        const ThermalTemperature new_Thermal_Level, const float current_Temp)
    {
        LOGINFO("THERMAL_MODECHANGED event received, curLevel: %u, newLevel: %u, curTemperature: %f",
            cur_Thermal_Level, new_Thermal_Level, current_Temp);

        dispatchThermalModeChangedEvent(cur_Thermal_Level, new_Thermal_Level, current_Temp);
        LOGINFO("<<");
    }

    void PowerManagerImplementation::onDeepSleepForThermalChange()
    {
        /*Scheduled maintanace reboot is disabled. Instead state will change to LIGHT_SLEEP*/
        LOGINFO(">> Set device to deepsleep on ThermalChange");
        SetPowerState(0, PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, "DeepSleep on Thermal change");
        LOGINFO("<<");
    }

}
}

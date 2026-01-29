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

#pragma once

#include "Module.h"

#include <memory>
#include <unordered_map>

#include <com/com.h>
#include <core/core.h>
#include <plugins/plugins.h>

#include <interfaces/IPowerManager.h>

#include "AckController.h"

// controllers
#include "DeepSleepController.h"
#include "PowerController.h"
#include "ThermalController.h"

using PowerState         = WPEFramework::Exchange::IPowerManager::PowerState;
using WakeupReason       = WPEFramework::Exchange::IPowerManager::WakeupReason;
using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;

namespace WPEFramework {
namespace Plugin {
    class PowerManagerImplementation : public Exchange::IPowerManager, public DeepSleepController::INotification, public ThermalController::INotification {
    public:
        using PreModeChangeController = AckController;

        // We do not allow this plugin to be copied !!
        PowerManagerImplementation();
        ~PowerManagerImplementation() override;

        static PowerManagerImplementation* instance(PowerManagerImplementation* PowerManagerImpl = nullptr);

        // We do not allow this plugin to be copied !!
        PowerManagerImplementation(const PowerManagerImplementation&)            = delete;
        PowerManagerImplementation& operator=(const PowerManagerImplementation&) = delete;

        BEGIN_INTERFACE_MAP(PowerManagerImplementation)
        INTERFACE_ENTRY(Exchange::IPowerManager)
        END_INTERFACE_MAP

    public:
        enum Event {
            PWRMGR_EVENT_POWERMODE_CHANGED,
            PWRMGR_EVENT_DEEPSLEEP_TIMEOUT,
            PWRMGR_EVENT_REBOOTING,
            PWRMGR_EVENT_THERMAL_MODECHANGED,
            PWRMGR_EVENT_NETWORK_STANDBYMODECHANGED,
        };

        class EXTERNAL LambdaJob : public Core::IDispatch {
        protected:
            LambdaJob(PowerManagerImplementation* impl, std::function<void()> lambda)
                : _impl(impl)
                , _lambda(std::move(lambda))
            {
                if (_impl != nullptr) {
                    _impl->AddRef();
                }
            }

        public:
            LambdaJob()                            = delete;
            LambdaJob(const LambdaJob&)            = delete;
            LambdaJob& operator=(const LambdaJob&) = delete;
            ~LambdaJob()
            {
                if (_impl != nullptr) {
                    _impl->Release();
                }
            }

            static Core::ProxyType<Core::IDispatch> Create(PowerManagerImplementation* impl, std::function<void()> lambda)
            {
                return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<LambdaJob>::Create(impl, std::move(lambda))));
            }

            virtual void Dispatch()
            {
                _lambda();
            }

        private:
            PowerManagerImplementation* _impl;
            std::function<void()> _lambda;
        };

    public:
        virtual Core::hresult Register(Exchange::IPowerManager::IRebootNotification* notification) override;
        virtual Core::hresult Unregister(const Exchange::IPowerManager::IRebootNotification* notification) override;
        virtual Core::hresult Register(Exchange::IPowerManager::IModePreChangeNotification* notification) override;
        virtual Core::hresult Unregister(const Exchange::IPowerManager::IModePreChangeNotification* notification) override;
        virtual Core::hresult Register(Exchange::IPowerManager::IModeChangedNotification* notification) override;
        virtual Core::hresult Unregister(const Exchange::IPowerManager::IModeChangedNotification* notification) override;
        virtual Core::hresult Register(Exchange::IPowerManager::IDeepSleepTimeoutNotification* notification) override;
        virtual Core::hresult Unregister(const Exchange::IPowerManager::IDeepSleepTimeoutNotification* notification) override;
        virtual Core::hresult Register(Exchange::IPowerManager::INetworkStandbyModeChangedNotification* notification) override;
        virtual Core::hresult Unregister(const Exchange::IPowerManager::INetworkStandbyModeChangedNotification* notification) override;
        virtual Core::hresult Register(Exchange::IPowerManager::IThermalModeChangedNotification* notification) override;
        virtual Core::hresult Unregister(const Exchange::IPowerManager::IThermalModeChangedNotification* notification) override;

        Core::hresult GetPowerState(PowerState& currentState, PowerState& prevState) const override;
        Core::hresult SetPowerState(const int keyCode, const PowerState powerState, const string& standbyReason) override;
        Core::hresult GetTemperatureThresholds(float& high, float& critical) const override;
        Core::hresult SetTemperatureThresholds(const float high, const float critical) override;
        Core::hresult GetOvertempGraceInterval(int& graceInterval) const override;
        Core::hresult SetOvertempGraceInterval(const int graceInterval) override;
        Core::hresult GetThermalState(float& temperature) const override;
        Core::hresult SetDeepSleepTimer(const int timeOut) override;
        Core::hresult GetLastWakeupReason(WakeupReason& wakeupReason) const override;
        Core::hresult GetLastWakeupKeyCode(int& keycode) const override;
        Core::hresult Reboot(const string& rebootRequestor, const string& rebootReasonCustom, const string& rebootReasonOther) override;
        Core::hresult SetNetworkStandbyMode(const bool standbyMode) override;
        Core::hresult GetNetworkStandbyMode(bool& standbyMode) override;
        Core::hresult SetWakeupSourceConfig(IWakeupSourceConfigIterator* wakeupSources) override;
        Core::hresult GetWakeupSourceConfig(IWakeupSourceConfigIterator*& wakeupSources) const override;
        Core::hresult GetPowerStateBeforeReboot(PowerState& powerStateBeforeReboot) override;
        Core::hresult PowerModePreChangeComplete(const uint32_t clientId, const int transactionId) override;
        Core::hresult DelayPowerModeChangeBy(const uint32_t clientId, const int transactionId, const int delayPeriod) override;
        Core::hresult AddPowerModePreChangeClient(const string& clientName, uint32_t& clientId) override;
        Core::hresult RemovePowerModePreChangeClient(const uint32_t clientId) override;

        static PowerManagerImplementation* _instance;

    private:
        PowerState m_powerStateBeforeReboot;
        bool m_networkStandbyMode;
        bool m_networkStandbyModeValid;
        bool m_powerStateBeforeRebootValid;

        // lock to guard all apis of PowerManager
        mutable Core::CriticalSection _apiLock;
        // lock to guard all notification from PowerManager to clients and also their callback register & unregister
        mutable Core::CriticalSection _callbackLock;

        Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> _engine;
        Core::ProxyType<RPC::CommunicatorClient> _communicatorClient;
        PluginHost::IShell* _controller;
        std::list<Exchange::IPowerManager::IRebootNotification*> _rebootNotifications;
        std::list<Exchange::IPowerManager::IModePreChangeNotification*> _preModeChangeNotifications;
        std::list<Exchange::IPowerManager::IModeChangedNotification*> _modeChangedNotifications;
        std::list<Exchange::IPowerManager::IDeepSleepTimeoutNotification*> _deepSleepTimeoutNotifications;
        std::list<Exchange::IPowerManager::INetworkStandbyModeChangedNotification*> _networkStandbyModeChangedNotifications;
        std::list<Exchange::IPowerManager::IThermalModeChangedNotification*> _thermalModeChangedNotifications;
        std::shared_ptr<PreModeChangeController> _modeChangeController;
        std::unordered_map<uint32_t, std::string> _modeChangeClients;

        void dispatchPowerModeChangedEvent(const PowerState& currentState, const PowerState& newState);
        void dispatchDeepSleepTimeoutEvent(const uint32_t& timeout);
        void dispatchRebootBeginEvent(const string& rebootReasonCustom, const string& rebootReasonOther, const string& rebootRequestor);
        void dispatchThermalModeChangedEvent(const ThermalTemperature& currentThermalLevel, const ThermalTemperature& newThermalLevel, const float& currentTemperature);
        void dispatchNetworkStandbyModeChangedEvent(const bool& enabled);

        void submitPowerModePreChangeEvent(const PowerState currentState, const PowerState newState, const int transactionId, const int timeOut);
        void powerModePreChangeCompletionHandler(const int keyCode, PowerState currentState, PowerState powerState, const std::string& reason);
        Core::hresult setDevicePowerState(const int& keyCode, PowerState currentState, PowerState powerState, const std::string& reason);
        inline bool isSyncStateChange(PowerState currState, PowerState newState) const;

        // DeepSleepController::INotification
        virtual void onDeepSleepTimerWakeup(const int wakeupTimeout) override;
        virtual void onDeepSleepUserWakeup(const bool userWakeup) override;
        virtual void onDeepSleepFailed() override;
        virtual void onThermalTemperatureChanged(const ThermalTemperature cur_Thermal_Level, const ThermalTemperature new_Thermal_Level, const float current_Temp) override;
        virtual void onDeepSleepForThermalChange() override;

        template <typename T>
        Core::hresult Register(std::list<T*>& list, T* notification);
        template <typename T>
        Core::hresult Unregister(std::list<T*>& list, const T* notification);

        bool isWakeupSrcEnabled(const std::list<WakeupSourceConfig>& configs, WakeupSrcType src) const;
        Core::hresult setWakeupSourceConfig(const std::list<WakeupSourceConfig>& configs);
        Core::hresult getWakeupSourceConfig(std::list<WakeupSourceConfig>& configs) const;

        static uint32_t _nextClientId; // static counter for unique client ID generation.

        // maintain this last
        DeepSleepController _deepSleepController;
        PowerController _powerController;
        ThermalController _thermalController;

        friend class Job;
    };
} // namespace Plugin
} // namespace WPEFramework

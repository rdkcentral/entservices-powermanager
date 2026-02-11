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

#include <interfaces/IPowerManager.h>
#include <interfaces/json/JPowerManager.h>

#include "UtilsLogging.h"

using PowerState         = WPEFramework::Exchange::IPowerManager::PowerState;
using ThermalTemperature = WPEFramework::Exchange::IPowerManager::ThermalTemperature;

namespace WPEFramework {
namespace Plugin {

    class PowerManager : public PluginHost::IPlugin, public PluginHost::JSONRPC {
    private:
        class Notification : public RPC::IRemoteConnection::INotification,
                             public PluginHost::IShell::ICOMLink::INotification,
                             public Exchange::IPowerManager::IRebootNotification,
                             public Exchange::IPowerManager::IModePreChangeNotification,
                             public Exchange::IPowerManager::IModeChangedNotification,
                             public Exchange::IPowerManager::IDeepSleepTimeoutNotification,
                             public Exchange::IPowerManager::INetworkStandbyModeChangedNotification,
                             public Exchange::IPowerManager::IThermalModeChangedNotification {
        private:
            Notification()                               = delete;
            Notification(const Notification&)            = delete;
            Notification& operator=(const Notification&) = delete;

        public:
            explicit Notification(PowerManager* parent)
                : _parent(*parent)
            {
                ASSERT(parent != nullptr);
            }

            virtual ~Notification()
            {
            }

            template <typename T>
            T* baseInterface()
            {
                static_assert(std::is_base_of<T, Notification>(), "base type mismatch");
                return static_cast<T*>(this);
            }

            BEGIN_INTERFACE_MAP(Notification)
            INTERFACE_ENTRY(Exchange::IPowerManager::IRebootNotification)
            INTERFACE_ENTRY(Exchange::IPowerManager::IModePreChangeNotification)
            INTERFACE_ENTRY(Exchange::IPowerManager::IModeChangedNotification)
            INTERFACE_ENTRY(Exchange::IPowerManager::IDeepSleepTimeoutNotification)
            INTERFACE_ENTRY(Exchange::IPowerManager::INetworkStandbyModeChangedNotification)
            INTERFACE_ENTRY(Exchange::IPowerManager::IThermalModeChangedNotification)
            INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
            END_INTERFACE_MAP

            void Activated(RPC::IRemoteConnection*) override
            {
            }

            void Deactivated(RPC::IRemoteConnection* connection) override
            {
                _parent.Deactivated(connection);
            }

            void Dangling(const Core::IUnknown* remote, const uint32_t interfaceId) override
            {
                ASSERT(remote != nullptr);
                _parent.CallbackRevoked(remote, interfaceId);
            }

            void Revoked(const Core::IUnknown* remote, const uint32_t interfaceId) override
            {
                ASSERT(remote != nullptr);
                _parent.CallbackRevoked(remote, interfaceId);
            }

            void OnPowerModeChanged(const PowerState currentState, const PowerState newState) override
            {
                LOGINFO("currentState %u, newState %u", currentState, newState);
                Exchange::JPowerManager::Event::OnPowerModeChanged(_parent, currentState, newState);
            }

            void OnPowerModePreChange(const PowerState currentState, const PowerState newState, const int trxnId, const int stateChangeAfter) override
            {
                LOGINFO("currentState %u, newState %u, transactionId %d, stateChangeAfter %d sec", currentState, newState, trxnId, stateChangeAfter);
                Exchange::JPowerManager::Event::OnPowerModePreChange(_parent, currentState, newState, trxnId, stateChangeAfter);
            }

            void OnDeepSleepTimeout(const int wakeupTimeout) override
            {
                LOGINFO("wakeupTimeout %d", wakeupTimeout);
                Exchange::JPowerManager::Event::OnDeepSleepTimeout(_parent, wakeupTimeout);
            }

            void OnNetworkStandbyModeChanged(const bool enabled) override
            {
                LOGINFO("enabled %d", enabled);
                Exchange::JPowerManager::Event::OnNetworkStandbyModeChanged(_parent, enabled);
            }

            void OnThermalModeChanged(const ThermalTemperature currentThermalLevel, const ThermalTemperature newThermalLevel, const float currentTemperature) override
            {
                LOGINFO("currentThermalLevel %d, newThermalLevel %d, currentTemperature %f", currentThermalLevel, newThermalLevel, currentTemperature);
                Exchange::JPowerManager::Event::OnThermalModeChanged(_parent, currentThermalLevel, newThermalLevel, currentTemperature);
            }

            void OnRebootBegin(const string& rebootReasonCustom, const string& rebootReasonOther, const string& rebootRequestor) override
            {
                LOGINFO("rebootReasonCustom %s, rebootReasonOther %s, rebootRequestor %s", rebootReasonCustom.c_str(), rebootReasonOther.c_str(), rebootRequestor.c_str());
                Exchange::JPowerManager::Event::OnRebootBegin(_parent, rebootReasonCustom, rebootReasonOther, rebootRequestor);
            }

        private:
            PowerManager& _parent;
        };

    public:
        // We do not allow this plugin to be copied !!
        PowerManager(const PowerManager&)            = delete;
        PowerManager& operator=(const PowerManager&) = delete;

        PowerManager();
        virtual ~PowerManager();

        BEGIN_INTERFACE_MAP(PowerManager)
        INTERFACE_ENTRY(PluginHost::IPlugin)
        INTERFACE_ENTRY(PluginHost::IDispatcher)
        INTERFACE_AGGREGATE(Exchange::IPowerManager, _powerManager)
        END_INTERFACE_MAP

        //  IPlugin methods
        // -------------------------------------------------------------------------------------------------------
        const string Initialize(PluginHost::IShell* service) override;
        void Deinitialize(PluginHost::IShell* service) override;
        string Information() const override;

    private:
        void Deactivated(RPC::IRemoteConnection* connection);
        void CallbackRevoked(const Core::IUnknown* remote, const uint32_t interfaceId);

    private:
        PluginHost::IShell* _service {};
        uint32_t _connectionId {};
        Exchange::IPowerManager* _powerManager {};
        Core::Sink<Notification> _powermanagersNotification;
    };

} // namespace Plugin
} // namespace WPEFramework

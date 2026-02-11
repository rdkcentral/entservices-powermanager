/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
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
#include <cstdint>
#include <memory>
#include <unordered_set>

#include <core/Portability.h>
#include <core/Timer.h>
#include <core/WorkerPool.h>

#include "UtilsLogging.h"
#include "interfaces/IPowerManager.h"

#include "LambdaJob.h"

/**
 * @class AckController
 * @brief Controls Awaits acknowledgement operations, timer timeouts & completion handler.
 *        The completion handler is triggered in one of these scenarios:
 *        - Acknowledgement received from all clients.
 *        - Scheduled timer times out.
 *        - Scheduled without any clients awaiting.
 *
 * IMPORTANT: This class is not thread-safe. It expects thread safety
 *            from the instantiating class.
 */
class AckController : public std::enable_shared_from_this<AckController> {
    using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;

public:
    /**
     * @brief Constructs an AckController instance for given `powerState` transition
     *        The TransactionId is unique for each instance.
     */
    AckController(PowerState powerState)
        : _workerPool(WPEFramework::Core::WorkerPool::Instance())
        , _powerState(powerState)
        , _transactionId(++_nextTransactionId)
        , _timeout(WPEFramework::Core::Time::Now())
        , _handler(nullptr)
        , _running(false)
    {
    }

    /**
     * @brief Destroys an AckController instance.
     *        If running, the controller will be revoked.
     *        The completion handler will not be called.
     */
    ~AckController()
    {
        revoke();
    }

    AckController(const AckController& o)            = delete;
    AckController& operator=(const AckController& o) = delete;
    AckController(AckController&& o)                 = delete;
    AckController& operator=(AckController&& o)      = delete;

    /**
     * @brief target power state for current state transition session
     */
    inline PowerState powerState() const
    {
        return _powerState;
    }

    /**
     * @brief Adds an expectation to await an acknowledgement from the given client.
     * @param clientId The ID of the client to await acknowledgement from.
     */
    void AckAwait(const uint32_t clientId)
    {
        _pending.insert(clientId);
        LOGINFO("Append clientId: %u, transactionId: %d, pending %d", clientId, _transactionId, int(_pending.size()));
    }

    /**
     * @brief Removes the expectation to await an acknowledgement from the given client associated with the given transaction ID.
     *        On acknowledgement from the last client, the completion handler will be triggered.
     * @param clientId The ID of the client.
     * @param transactionId The transaction ID associated with the client.
     * @return status - ERROR_INVALID_PARAMETER if the transaction ID is invalid.
     *                  ERROR_NONE on success.
     */
    uint32_t Ack(const uint32_t clientId, const int transactionId)
    {
        uint32_t status = WPEFramework::Core::ERROR_NONE;

        do {

            if (transactionId != _transactionId) {
                LOGERR("Invalid transactionId: %d", transactionId);
                status = WPEFramework::Core::ERROR_INVALID_PARAMETER;
                break;
            }

            const auto it = _pending.find(clientId);
            if (it == _pending.cend()) {
                LOGERR("Invalid clientId: %u", clientId);
                status = WPEFramework::Core::ERROR_INVALID_PARAMETER;
                break;
            }

            _pending.erase(clientId);

            if (_pending.empty() && _running) {
                _running = false;
                runHandler(false);
            }
        } while (false);

        LOGINFO("AckController::Ack: clientId: %u, transactionId: %d, status: %d, pending %d",
            clientId, transactionId, status, int(_pending.size()));
        return status;
    }

    /**
     * @brief Removes the expectation to await an acknowledgement from the given client.
     * @param clientId The ID of the client.
     * @return status - ERROR_INVALID_PARAMETER if the client ID is invalid.
     *                  ERROR_NONE on success.
     */
    uint32_t Ack(const uint32_t clientId)
    {
        return Ack(clientId, _transactionId);
    }

    /**
     * @brief Gets the current transaction ID.
     * @return The current transaction ID.
     */
    int TransactionId() const
    {
        return _transactionId;
    }

    /**
     * @brief Checks if the controller is running.
     * @return True if the controller is running, otherwise false.
     */
    bool IsRunning() const
    {
        return _running && _timerJob.IsValid();
    }

    /**
     * @brief Gets the set of pending client IDs.
     * @return A set of pending client IDs.
     */
    const std::unordered_set<uint32_t>& Pending() const
    {
        return _pending;
    }

    /**
     * @brief Schedules a completion handler trigger with a timeout.
     *
     * @param offsetInMilliseconds The timeout offset in milliseconds.
     * @param handler The completion handler to be invoke.
     *        The completion handler is triggered in one of these scenarios:
     *        - Acknowledgement received from all clients (Triggered from the last Ack caller thread).
     *        - Scheduled timer times out (Triggered from Thunder workerpool thread).
     *        - Scheduled without any clients awaiting (Triggered in the caller thread).
     *
     *  handler args: isTimedOut true   => handler is invoked because operation timedout
     *                isRevoked true    => handler is invoked because operation was cancelled (obj destroyed)
     *                if args are false => handler is invoked as acknowledgement is received from all clients
     *
     */
    void Schedule(const uint64_t offsetInMilliseconds, std::function<void(bool, bool)> handler)
    {
        ASSERT(false == _running);
        ASSERT(nullptr == _handler);

        LOGINFO("time offset: %" PRIu64 "ms, pending: %d", offsetInMilliseconds, int(_pending.size()));

        if (_pending.empty() || 0 == offsetInMilliseconds) {
            // no clients acks to wait for, trigger completion handler immediately
            handler(false, false);
        } else {
            std::weak_ptr<AckController> wPtr = shared_from_this();
            _running                          = true;
            _handler                          = std::move(handler);

            // If timeout is already set (via Reschedule), use max of offset or timeout
            auto newTimeout = WPEFramework::Core::Time::Now().Add(offsetInMilliseconds);
            _timeout        = std::max(newTimeout, _timeout);

            _timerJob = LambdaJob::Create([wPtr]() {
                std::shared_ptr<AckController> self = wPtr.lock();

                bool isRevoked  = self ? false : true;
                bool isTimedout = true;

                LOGINFO("AckTimer handler isTimedout: 1, isRevoked: %d", isRevoked);

                if (!isRevoked) {
                    if (self->_running) {
                        self->_running = false;
                        self->_handler(isTimedout, isRevoked);
                    } else {
                        LOGERR("FATAL not expected to reach timeout, without timer running");
                    }
                } else {
                    LOGERR("FATAL AckController is already revoked\n\trevoke operation should have triggered completion handler");
                }
            });
            _workerPool.Schedule(_timeout, _timerJob);
        }
    }

    /**
     * @brief Advances the timeout of the completion handler.
     * @param clientId The ID of the client.
     * @param transactionId The transaction ID associated with the client.
     * @param offsetInMilliseconds The new timeout offset in milliseconds from the current time.
     * @return status - ERROR_INVALID_PARAMETER if the client ID or transaction ID is invalid.
     *                  ERROR_ILLEGAL_STATE if the controller is not running.
     *                  ERROR_NONE on success.
     */
    uint32_t Reschedule(const uint32_t clientId, const int transactionId, const int offsetInMilliseconds)
    {
        uint32_t status = WPEFramework::Core::ERROR_NONE;
        ASSERT(nullptr != _handler);

        do {
            auto newTimeout = WPEFramework::Core::Time::Now().Add(offsetInMilliseconds);

            // If Reschedule is called even before Schedule, cache the timeout value
            // use timeout value later when Schedule gets called
            if (!_running) {
                _timeout = std::max(_timeout, newTimeout);
                status = WPEFramework::Core::ERROR_NONE;
                break;
            }

            if (transactionId != _transactionId) {
                LOGERR("Invalid transactionId: %d", transactionId);
                status = WPEFramework::Core::ERROR_INVALID_PARAMETER;
                break;
            }

            const auto it = _pending.find(clientId);
            if (it == _pending.cend()) {
                LOGERR("Invalid clientId: %u", clientId);
                status = WPEFramework::Core::ERROR_INVALID_PARAMETER;
                break;
            }

            // set new timeout only if it's greater than previous timeout, else fail silently
            if (newTimeout > _timeout) {
                _timeout = newTimeout;
                _workerPool.Reschedule(newTimeout, _timerJob);
            } else {
                LOGWARN("Skipping new timeout %" PRIu64 " is less than previous timeout %" PRIu64,
                    newTimeout.Ticks(), _timeout.Ticks());
            }
        } while (false);

        LOGINFO("clientId: %u, transactionId: %d, offset: %d, status: %d",
            clientId, transactionId, offsetInMilliseconds, status);

        return status;
    }

private:
    /**
     * @brief Executes the completion handler.
     * @param isTimedout Indicates whether the handler is triggered due to timeout.
     */
    void runHandler(bool isTimedout)
    {
        LOGINFO("isTimedout: %d, pending: %d", isTimedout, int(_pending.size()));
        if (!isTimedout) {
            _workerPool.Revoke(_timerJob);
        }
        bool isRevoked = false;
        _handler(isTimedout, isRevoked);
    }

    /**
     * @brief Stops or revokes the AckController if it is already running.
     *        After revoke the completion handler will not be called.
     *        This method is deliberately void; use `IsRunning` to check the status.
     */
    void revoke()
    {
        if (_running) {
            _running = false;
            if (_timerJob.IsValid()) {
                _workerPool.Revoke(_timerJob);
                bool isTimedout = false;
                bool isRevoked  = true;
                _handler(isTimedout, isRevoked);
            }
        }
        if (_timerJob.IsValid()) {
            _timerJob.Release();
        }
    }

private:
    using TimerJob = WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch>;

    WPEFramework::Core::IWorkerPool& _workerPool; // Thunder worker pool
    PowerState _powerState;                       // target / next powerState to change
    std::unordered_set<uint32_t> _pending;        // Set of pending acknowledgements.
    int _transactionId;                           // Unique transaction ID for each AckController instance.
    WPEFramework::Core::Time _timeout;            // Absolute timeout value for _timerJob (not duration)
    TimerJob _timerJob;                           // job scheduler to timeout
    std::function<void(bool, bool)> _handler;     // Completion handler to be called on timeout or all acknowledgements.
    std::atomic<bool> _running;                   // Flag to synchronize timer timeout callback and Ack* APIs.

    static int _nextTransactionId; // static counter for unique transaction ID generation.
};

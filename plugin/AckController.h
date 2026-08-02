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
#include <atomic>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

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
 * IMPORTANT: This class provides internal thread safety for Schedule, Reschedule,
 *            Ack, and recalculateTimeout operations using a mutex.
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
        , _defaultTimeoutMs(0)
        , _transactionId(++_nextTransactionId)
        , _timeout(WPEFramework::Core::Time::Now())
        , _handler(nullptr)
        , _state(State::IDLE)
        , _mutex()
    {
        LOGINFO("PowerManager: Created for powerState: %d, transactionId: %d", powerState, _transactionId);
    }

    /**
     * @brief Destroys an AckController instance.
     *        If running, the controller will be revoked.
     *        The completion handler will be called with isRevoked=true.
     */
    ~AckController()
    {
        LOGINFO("PowerManager: Destroyed, transactionId: %d, pending: %d", _transactionId, int(_pending.size()));
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
        LOGINFO("PowerManager: Append clientId: %u, transactionId: %d, pending: %d", clientId, _transactionId, int(_pending.size()));
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
        bool shouldRunHandler = false;
        bool isTimedout = false;
        
        {
            std::lock_guard<std::mutex> lock(_mutex);
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
                _clientExpiries.erase(clientId);  // Remove client's expiry
                LOGINFO("PowerManager: Client %u acknowledged, remaining pending: %d", clientId, int(_pending.size()));

                if (_pending.empty()) {
                    if (updateTimerState(State::DONE)) {
                        LOGINFO("PowerManager: All clients acknowledged, will trigger completion handler");
                        shouldRunHandler = true;
                        isTimedout = false;
                    }
                } else if (_state == State::RUNNING) {
                    // Recalculate timeout; if expired-client removal empties _pending, trigger handler
                    LOGINFO("PowerManager: Recalculating timeout for remaining %d clients", int(_pending.size()));
                    bool allDone = recalculateTimeout();
                    if (allDone) {
                        if (updateTimerState(State::DONE)) {
                            LOGINFO("PowerManager: All clients expired/removed, will trigger completion handler");
                            shouldRunHandler = true;
                            isTimedout = false;
                        }
                    }
                }
            } while (false);

            LOGINFO("PowerManager: clientId: %u, transactionId: %d, status: %d, pending: %d",
                clientId, transactionId, status, int(_pending.size()));
                
            // Mutex will be released here when lock goes out of scope
            if (status != WPEFramework::Core::ERROR_NONE) {
                return status;
            }
        }
        
        // Call handler outside mutex to prevent deadlock
        if (shouldRunHandler) {
            runHandler(isTimedout);
        }
        
        return WPEFramework::Core::ERROR_NONE;
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
        return _state == State::RUNNING && _timerJob.IsValid();
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
        bool shouldCallImmediately = false;
        
        {
            std::lock_guard<std::mutex> lock(_mutex);
            ASSERT(nullptr == _handler);

            LOGINFO("PowerManager: transactionId: %d, timeout: %" PRIu64 "ms, pending: %d", _transactionId, offsetInMilliseconds, int(_pending.size()));

            if (_pending.empty() || 0 == offsetInMilliseconds) {
                // no clients acks to wait for, trigger completion handler immediately
                LOGINFO("PowerManager: No pending clients or zero timeout, will trigger handler immediately");
                shouldCallImmediately = true;
            } else {
                std::weak_ptr<AckController> wPtr = shared_from_this();
                _state                            = State::RUNNING;
                _handler                          = std::move(handler);
                _defaultTimeoutMs                 = offsetInMilliseconds;

                // If timeout is already set (via Reschedule), use max of offset or timeout
                auto newTimeout = WPEFramework::Core::Time::Now().Add(offsetInMilliseconds);
                _timeout        = std::max(newTimeout, _timeout);

                _timerJob = LambdaJob::Create([wPtr]() {
                std::shared_ptr<AckController> self = wPtr.lock();

                bool isRevoked  = self ? false : true;
                bool isTimedout = true;

                LOGINFO("PowerManager: Timer handler isTimedout: 1, isRevoked: %d", isRevoked);

                if (!isRevoked) {
                    if (self->updateTimerState(State::DONE)) {
                        self->_handler(isTimedout, isRevoked);
                    } else {
                        LOGINFO("PowerManager: Timer fired but handler already invoked by last ACK or revoked");
                    }
                } else {
                    LOGINFO("PowerManager: Timer fired but AckController was already destroyed (revoked)");
                }
            });
                _workerPool.Schedule(_timeout, _timerJob);
            }
        }
        
        // Call handler outside mutex to prevent deadlock
        if (shouldCallImmediately) {
            handler(false, false);
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
        std::lock_guard<std::mutex> lock(_mutex);
        uint32_t status = WPEFramework::Core::ERROR_NONE;

        do {
            auto now = WPEFramework::Core::Time::Now();
            auto clientExpiry = now.Add(offsetInMilliseconds);

            // If Reschedule is called even before Schedule, cache the expiry time
            // use expiry value later when Schedule gets called
            if (_state == State::IDLE) {
                _timeout = std::max(_timeout, clientExpiry);
                _clientExpiries[clientId] = clientExpiry;
                status = WPEFramework::Core::ERROR_NONE;
                break;
            }
            if (_state == State::DONE) {
                LOGERR("PowerManager: Timer already expired or completed, cannot reschedule");
                status = WPEFramework::Core::ERROR_ILLEGAL_STATE;
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

            // Store the client's absolute expiry time
            _clientExpiries[clientId] = clientExpiry;
            LOGINFO("PowerManager: Stored expiry for client %u: %d ms from now", clientId, offsetInMilliseconds);

            // Calculate remaining time on current timer
            uint64_t currentRemainingMs = (now < _timeout) ? 
                (_timeout.Ticks() - now.Ticks()) / WPEFramework::Core::Time::TicksPerMillisecond : 0;

            // Only reschedule if client's absolute expiry is greater than current timer's absolute expiry
            if (clientExpiry > _timeout) {
                _timeout = clientExpiry;
                _workerPool.Reschedule(clientExpiry, _timerJob);
                uint64_t newRemainingMs = (now < clientExpiry) ? 
                    (clientExpiry.Ticks() - now.Ticks()) / WPEFramework::Core::Time::TicksPerMillisecond : 0;
                LOGINFO("PowerManager: Extended timeout: %" PRIu64 " ms → %" PRIu64 " ms (client %u requested %d ms)", 
                    currentRemainingMs, newRemainingMs, clientId, offsetInMilliseconds);
            } else {
                LOGINFO("PowerManager: Client %u delay (%d ms) within current timeout (%" PRIu64 " ms remaining), no reschedule needed",
                    clientId, offsetInMilliseconds, currentRemainingMs);
            }
        } while (false);

        LOGINFO("PowerManager: clientId: %u, transactionId: %d, offset: %d, status: %d",
            clientId, transactionId, offsetInMilliseconds, status);

        return status;
    }

private:
    /**
     * @brief Updates the timer state atomically.
     * @param desiredState The desired state to transition to.
     * @return True if the state was successfully updated, false otherwise.
     */
    bool updateTimerState(State desiredState)
    {
        State expected = State::RUNNING;
        return _state.compare_exchange_strong(expected, desiredState);
    }

    /**
     * @brief Recalculates the timeout based on remaining clients' delays.
     *        Uses the maximum delay among remaining clients, or default timeout if no delays set.
     *        IMPORTANT: Must be called while holding _mutex lock.
     *        @return true if all pending clients were removed (expired), false otherwise.
     */
    bool recalculateTimeout()
    {
        auto now = WPEFramework::Core::Time::Now();
        
        // Remove expired clients first
        removeExpiredClients();
        
        if (_pending.empty()) {
            LOGINFO("PowerManager: No pending clients after removing expired ones");
            return true;  // Signal caller to trigger completion handler
        }

        // Find the maximum expiry time among remaining pending clients
        WPEFramework::Core::Time maxExpiry = now.Add(_defaultTimeoutMs);
        uint32_t maxExpiryClientId = 0;

        for (const auto& clientId : _pending) {
            auto it = _clientExpiries.find(clientId);
            if (it != _clientExpiries.end()) {
                if (it->second > maxExpiry) {
                    maxExpiry = it->second;
                    maxExpiryClientId = clientId;
                }
            }
        }

        uint64_t currentRemainingMs = (now < _timeout) ? 
            (_timeout.Ticks() - now.Ticks()) / WPEFramework::Core::Time::TicksPerMillisecond : 0;
        uint64_t newRemainingMs = (now < maxExpiry) ? 
            (maxExpiry.Ticks() - now.Ticks()) / WPEFramework::Core::Time::TicksPerMillisecond : 0;

        // Only reschedule if new timeout is less than current timeout (optimization)
        if (maxExpiry < _timeout) {
            _timeout = maxExpiry;
            _workerPool.Reschedule(maxExpiry, _timerJob);
            LOGINFO("PowerManager: Timeout reduced: %" PRIu64 " ms → %" PRIu64 " ms (client %u has max expiry), pending: %d",
                currentRemainingMs, newRemainingMs, maxExpiryClientId, int(_pending.size()));
        } else {
            LOGINFO("PowerManager: Timeout unchanged: current %" PRIu64 " ms <= new %" PRIu64 " ms, pending: %d",
                currentRemainingMs, newRemainingMs, int(_pending.size()));
        }
        return false;
    }

    /**
     * @brief Removes clients whose expiry time has already passed.
     *        IMPORTANT: Must be called while holding _mutex lock.
     */
    void removeExpiredClients()
    {
        auto now = WPEFramework::Core::Time::Now();
        std::vector<uint32_t> expiredClients;

        for (const auto& clientId : _pending) {
            auto it = _clientExpiries.find(clientId);
            if (it != _clientExpiries.end() && it->second <= now) {
                expiredClients.push_back(clientId);
            }
        }

        for (const auto& clientId : expiredClients) {
            LOGWARN("PowerManager: Removing expired client %u from pending list", clientId);
            _pending.erase(clientId);
            _clientExpiries.erase(clientId);
        }

        if (!expiredClients.empty()) {
            LOGINFO("PowerManager: Removed %zu expired client(s), remaining pending: %d", 
                expiredClients.size(), int(_pending.size()));
        }
    }

    /**
     * @brief Executes the completion handler.
     * @param isTimedout Indicates whether the handler is triggered due to timeout.
     */
    void runHandler(bool isTimedout)
    {
        LOGINFO("PowerManager: transactionId: %d, isTimedout: %d, pending: %d", _transactionId, isTimedout, int(_pending.size()));
        if (!isTimedout) {
            LOGINFO("PowerManager: All clients acknowledged, revoking timer");
            _workerPool.Revoke(_timerJob);
        } else {
            LOGWARN("PowerManager: Timeout occurred with %d clients still pending", int(_pending.size()));
        }
        bool isRevoked = false;
        _handler(isTimedout, isRevoked);
    }

    /**
     * @brief Stops or revokes the AckController if it is already running.
     *        The completion handler will be called with isRevoked=true if running.
     *        This method is deliberately void; use `IsRunning` to check the status.
     *        Thread-safe: Protected by mutex to prevent race with timer callback.
     */
    void revoke()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (updateTimerState(State::DONE)) {
            LOGINFO("PowerManager: Revoking, transactionId: %d, pending: %d", _transactionId, int(_pending.size()));
            if (_timerJob.IsValid()) {
                _workerPool.Revoke(_timerJob);
                bool isTimedout = false;
                bool isRevoked  = true;
                // Call handler while holding mutex (same as old behavior)
                // This minimizes the race window but has potential deadlock risk
                _handler(isTimedout, isRevoked);
            }
        }
        if (_timerJob.IsValid()) {
            _timerJob.Release();
        }
    }

private:
    using TimerJob = WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch>;
    enum class State : uint8_t { IDLE, RUNNING, DONE };

    WPEFramework::Core::IWorkerPool& _workerPool;          // Thunder worker pool
    PowerState _powerState;                                // target / next powerState to change
    std::unordered_set<uint32_t> _pending;                 // Set of pending acknowledgements.
    std::unordered_map<uint32_t, WPEFramework::Core::Time> _clientExpiries;  // Map of clientId to absolute expiry time
    uint64_t _defaultTimeoutMs;                            // Default timeout in milliseconds
    int _transactionId;                                    // Unique transaction ID for each AckController instance.
    WPEFramework::Core::Time _timeout;                     // Absolute timeout value for _timerJob (not duration)
    TimerJob _timerJob;                                    // job scheduler to timeout
    std::function<void(bool, bool)> _handler;              // Completion handler to be called on timeout or all acknowledgements.
    std::atomic<State> _state;                             // IDLE → RUNNING → DONE; guards handler invocation race.
    mutable std::mutex _mutex;                             // Mutex for thread safety

    static int _nextTransactionId; // static counter for unique transaction ID generation.
};

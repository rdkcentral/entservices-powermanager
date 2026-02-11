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
#include <functional>

#include <core/IAction.h>
#include <core/Portability.h>
#include <core/Proxy.h>

class EXTERNAL LambdaJob : public WPEFramework::Core::IDispatch {
protected:
    LambdaJob(std::function<void()> lambda)
        : _lambda(std::move(lambda))
    {
    }

public:
    LambdaJob() = delete;
    LambdaJob(const LambdaJob&) = delete;
    LambdaJob& operator=(const LambdaJob&) = delete;
    ~LambdaJob() = default;

    static WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch> Create(std::function<void()> lambda)
    {
        return (WPEFramework::Core::ProxyType<WPEFramework::Core::IDispatch>(WPEFramework::Core::ProxyType<LambdaJob>::Create(std::move(lambda))));
    }

    virtual void Dispatch()
    {
        _lambda();
    }

private:
    std::function<void()> _lambda;
};

// MIT License
//
// Copyright (c) 2023 Egor Tsvetkov
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef SAFECALLBACK_HPP
#define SAFECALLBACK_HPP

#include<memory>
#include<functional>

namespace tea::asiocommunicator {



template<typename Function> struct SafeCallback;
template<typename Function> struct CallbackProtector;

template<typename ClassType, typename ReturnType, typename ... Args>
struct SafeCallback<ReturnType(ClassType::*)(Args...)>
{
public:
    using FunctionType = ReturnType(Args...);
    using MemberFunctionType = ReturnType(ClassType::*)(Args...);
    SafeCallback(const MemberFunctionType &func, ClassType* obj, std::shared_ptr<bool> is_alive):
        func_(func), obj_(obj), is_alive_(std::move(is_alive))
    {
    }

    //SafeCallback(const SafeCallback<ReturnType(ClassType::*)(Args...)>& other) = default;

    void invalidate() { *is_alive_ = false;}

    ReturnType operator() (Args... args) {
        if(*is_alive_) {
            return (obj_->*func_)(std::forward<Args>(args)...);
        }
        return ReturnType();
    }
private:
    MemberFunctionType func_;
    ClassType* obj_;
    std::shared_ptr<bool> is_alive_;
};


template<typename ClassType, typename ReturnType, typename ... Args>
struct CallbackProtector<ReturnType(ClassType::*)(Args...)>
{
public:
    using SafeCallbackT = SafeCallback<ReturnType(ClassType::*)(Args...)>;
    ~CallbackProtector()
    {
        *is_alive_ = false;
    }
    CallbackProtector(const typename SafeCallbackT::MemberFunctionType &func, ClassType* obj) :
        is_alive_(std::make_shared<bool>(true)),
        func_(func, obj, is_alive_)
        {}

    CallbackProtector() = delete;
    CallbackProtector(const CallbackProtector&) = delete;
    CallbackProtector(CallbackProtector&&) = delete;
    CallbackProtector& operator=(const CallbackProtector&) = delete;
    void operator=(CallbackProtector&&) = delete;

    //converter to std::function<ReturnType(ClassType::*)(Args...)>
    operator std::function<typename SafeCallbackT::FunctionType>() //NOLINT 
    {
        return std::function<typename SafeCallbackT::FunctionType>(func_);
    }
private:
    std::shared_ptr<bool> is_alive_;
    SafeCallbackT func_;
};

} /* namespace tea::asiocommunicator */

#endif // SAFECALLBACK_HPP

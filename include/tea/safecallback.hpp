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

namespace tea {

namespace asiocommunicator {


template<typename Function> struct SafeCallback;
template<typename Function> struct CallbackProtector;

template<typename ClassType, typename ReturnType, typename ... Args>
struct SafeCallback<ReturnType(ClassType::*)(Args...)>
{
public:
    using FunctionType = ReturnType(Args...);
    typedef ReturnType(ClassType::*MemberFunctionType)(Args...);
    SafeCallback(const MemberFunctionType &f_, ClassType* obj_, std::shared_ptr<bool> isAlive_):
        f(f_), obj(obj_), isAlive(isAlive_)
    {
    }

    //SafeCallback(const SafeCallback<ReturnType(ClassType::*)(Args...)>& other) = default;

    void invalidate() { *isAlive = false;}

    ReturnType operator() (Args... args) {
        if(*isAlive) {
            return (obj->*f)(std::forward<Args>(args)...);
        } else {
            return ReturnType();
        };
    }
private:
    MemberFunctionType f;
    ClassType* obj;
    std::shared_ptr<bool> isAlive;
};


template<typename ClassType, typename ReturnType, typename ... Args>
struct CallbackProtector<ReturnType(ClassType::*)(Args...)>
{
private:
    std::shared_ptr<bool> isAlive;
public:
    using SafeCallbackT = SafeCallback<ReturnType(ClassType::*)(Args...)>;
    ~CallbackProtector()
    {
        *isAlive = false;
    }
    CallbackProtector(const typename SafeCallbackT::MemberFunctionType &f_, ClassType* obj_) :
        isAlive(std::make_shared<bool>(true)),
        f(f_, obj_, isAlive)
        {}
    SafeCallbackT f;

    //converter to std::function<ReturnType(ClassType::*)(Args...)>
    operator std::function<typename SafeCallbackT::FunctionType>()
    {
        return std::function<typename SafeCallbackT::FunctionType>(f);
    }
};

} /* namespace asiocommunicator */
} /* namespace tea */

#endif // SAFECALLBACK_HPP

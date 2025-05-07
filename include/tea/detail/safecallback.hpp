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

#ifndef _TEA_ASIO_DETAIL_SAFECALLBACK_HPP_
#define _TEA_ASIO_DETAIL_SAFECALLBACK_HPP_

#include<memory>
#include<functional>

namespace tea::asiocommunicator {



template<typename Function> struct SafeMemberFcnCallback;

/*!
 * @brief The wrapper for any member functions which prevents call 
 * of the wrapped function if the object for which this member function is called is already destroyed.
 * 
 * @details The pervention of member function call for destroyed object
 * is based on checking the value of shared boolean variable `is_alive`. When this variable
 * is true the call of `this` object is translated to call of the wrapped function. When the object
 * for which wrapped function is called is destroyed this variable should be set to false. The convinient
 * way to do this is to use IsAliveTracker class
 * 
 * 
 * @tparam ClassType class which contains wrapped member function
 * @tparam ReturnType return type of the wrapped member function
 * @tparam ...Args  args of the wrapped member function
 */
template<typename ClassType, typename ReturnType, typename ... Args>
struct SafeMemberFcnCallback<ReturnType(ClassType::*)(Args...)>
{
public:
    using FunctionType = ReturnType(Args...);
    using MemberFunctionType = ReturnType(ClassType::*)(Args...);
    SafeMemberFcnCallback(const MemberFunctionType &func, ClassType* obj):
        func_(func), obj_(obj), is_alive_(obj->getIsAlivePtr())
    {
    }

    SafeMemberFcnCallback(SafeMemberFcnCallback&&) = delete;

    SafeMemberFcnCallback(const SafeMemberFcnCallback<ReturnType(ClassType::*)(Args...)>& other) = default;

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

/**
 * \brief The lifetime tracker 
 * \details When the object of this class is created, the shared_ptr to bool flag `is_alive_` is set to true. When the object
 * is destroyed the flag is automatically set to false by the destructor of this class, which prevents call
 * of the member function callback for destroyed object. This class should be used in pair with SafeMemberFcnCallback.
 */
class IsAliveTracker {
    public:
    IsAliveTracker() : is_alive_(std::make_shared<bool>(true)) {}
    ~IsAliveTracker() {
        *is_alive_ = false;
    }
    std::shared_ptr<bool> getIsAlivePtr() {
        return is_alive_;
    }
    private:
    std::shared_ptr<bool> is_alive_;
};

} /* namespace tea::asiocommunicator */

#endif // _TEA_ASIO_DETAIL_SAFECALLBACK_HPP_

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



template<typename Function> struct SafeMemberFcnCallback;
template<typename Function> struct MemberFcnCallbackProtector;


/*!
 * @brief The wrapper for any other class member function which prevents call 
 * of the wrapped function if the object for which this member function is called is already destroyed.
 * 
 * @details The mechanism of preventing member function call for destroyed object
 * is based on the checking the value of shared boolean variable `is_alive`. When this variable
 * is true the call to this->operator() is translated to call of the wrapped function. When the object
 * for which wrapped function is called is destroyed this variable should be set to false. The convinient
 * way to do this is to use class MemberFcnCallbackProtector
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
    SafeMemberFcnCallback(const MemberFunctionType &func, ClassType* obj, std::shared_ptr<bool> is_alive):
        func_(func), obj_(obj), is_alive_(std::move(is_alive))
    {
    }

    //SafeMemberFcnCallback(SafeMemberFcnCallback&&) = delete;

    //SafeMemberFcnCallback(const SafeMemberFcnCallback<ReturnType(ClassType::*)(Args...)>& other) = default;

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
 * \brief Observe the lifetime of the parent object.
 * \details When the object of this class is created, the shared bool flag `is_alive_` is set to true. When the object
 * is destroyed the flag is automatically set to false by the destructor of this class, which prevents call
 * of the member function callback for destroyed object. This class should be used in pair with SafeMemberFcnCallback.
 */
template<typename ClassType, typename ReturnType, typename ... Args>
struct MemberFcnCallbackProtector<ReturnType(ClassType::*)(Args...)>
{
public:
    using SafeCallbackT = SafeMemberFcnCallback<ReturnType(ClassType::*)(Args...)>;
    ~MemberFcnCallbackProtector()
    {
        *is_alive_ = false;
    }
    MemberFcnCallbackProtector(const typename SafeCallbackT::MemberFunctionType &func, ClassType* obj) :
        is_alive_(std::make_shared<bool>(true)),
        func_(func, obj, is_alive_)
        {}

    MemberFcnCallbackProtector() = delete;
    
    /*!
     * @name MemberFcnCallbackProtector_GroupOfFive
     * @brief Copy and move constructos and operators
     * @details Despite the default move constructor could be automatically generated it is still 
     * deleted for the following reason. 
     * When the parent object is created and some network operations are submitted, we can't change
     * callback function passed to boost::asio. So if data from the original object is moved away we
     * should inform boost::asio about this
     * (at least, we should change `this` pointer for all member functions passed as callbacks to boost::asio).
     * 
     * The next problem during moving or copying is using of the shared_ptr is_alive_. If new object is copied
     * to a new one, this variable should not be copied in a usual way (because in this case it will point
     * to the same location for both objects).
     * 
     * For the same reason copy constructor, copy and move operator are disabled.
     * @param  
     */
    ///@{
    MemberFcnCallbackProtector(const MemberFcnCallbackProtector&) = delete;
    MemberFcnCallbackProtector(MemberFcnCallbackProtector&&) = delete;
    MemberFcnCallbackProtector& operator=(const MemberFcnCallbackProtector&) = delete;
    void operator=(MemberFcnCallbackProtector&&) = delete;
    ///@}

    ///converter to std::function<ReturnType(ClassType::*)(Args...)>
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

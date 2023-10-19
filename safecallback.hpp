#ifndef SAFECALLBACK_HPP
#define SAFECALLBACK_HPP

#include<memory>
#include<functional>

template<typename Function> struct SafeCallback;
template<typename Function> struct SafeCallbackHolder;

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
struct SafeCallbackHolder<ReturnType(ClassType::*)(Args...)>
{
private:
    std::shared_ptr<bool> isAlive;
public:
    using SafeCallbackT = SafeCallback<ReturnType(ClassType::*)(Args...)>;
    ~SafeCallbackHolder()
    {
        *isAlive = false;
    }
    SafeCallbackHolder(const typename SafeCallbackT::MemberFunctionType &f_, ClassType* obj_) :
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


#endif // SAFECALLBACK_HPP

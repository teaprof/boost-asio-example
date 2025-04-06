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

#ifndef _TEA_ASIOCOMMUNICATOR_DETAIL_H
#define _TEA_ASIOCOMMUNICATOR_DETAIL_H

#include "safecallback.hpp"
#include "message.hpp"
#include "asyncoperations.hpp"
#include <boost/asio.hpp>
#include <boost/smart_ptr.hpp>
#include <memory>
#include <queue>
#include <map>
#include <type_traits>
#include <functional>
#include <iostream>
#include <optional>

namespace tea::asiocommunicator {

namespace detail {

struct GetContext
{
    /*!
    * \brief returns reference to the static context that is used by all objects in this library
    */
    static boost::asio::io_context& get()
    {
        static boost::asio::io_context context;
        return context;
    }
};

//ASIOtalker is a class that implements sending and receiving messages.
/// \todo: may be split into Reader and Writers
class ASIOBufferedTalker : public IsAliveTracker //todo: maybe rename to QueuedTalker?
{
public:
    //Possible values of the connection status
    enum class Status {
        notStarted, working, closed, failed
    };

    enum class BufferPolicy
    {
        allocNewBuffers, collectAndUsePreallocatedBuffers
    };

    explicit ASIOBufferedTalker(std::shared_ptr<boost::asio::ip::tcp::socket> socket):
        on_read_finished_safe_(&ASIOBufferedTalker::onReadFinished, this),
        on_write_finished_safe_(&ASIOBufferedTalker::onWriteFinished, this),
        on_network_failed_safe_(&ASIOBufferedTalker::onNetworkFailed, this),
        socket_(std::move(socket)),
        writer_(Writer::create_shared_ptr(socket_, on_write_finished_safe_, on_network_failed_safe_)), //todo: socket could be destroyed before Writer (or Reader) destoroyed
        reader_(Reader::create_shared_ptr(socket_, on_read_finished_safe_, on_network_failed_safe_))
        { }

    explicit ASIOBufferedTalker(boost::asio::io_context &context):
        on_read_finished_safe_(&ASIOBufferedTalker::onReadFinished, this),
        on_write_finished_safe_(&ASIOBufferedTalker::onWriteFinished, this),
        on_network_failed_safe_(&ASIOBufferedTalker::onNetworkFailed, this),
        socket_(std::make_shared<boost::asio::ip::tcp::socket>(context)),
        writer_(Writer::create_shared_ptr(socket_, on_write_finished_safe_, on_network_failed_safe_)),
        reader_(Reader::create_shared_ptr(socket_, on_read_finished_safe_, on_network_failed_safe_))
        { }

    ~ASIOBufferedTalker() = default;
    ASIOBufferedTalker(const ASIOBufferedTalker&) = delete;
    ASIOBufferedTalker(ASIOBufferedTalker&& other) = delete;
    ASIOBufferedTalker& operator=(const ASIOBufferedTalker&) = delete;
    ASIOBufferedTalker& operator=(ASIOBufferedTalker&&) = delete; 


    //What should we do if move or copy constructor is called when async operation in progress?
    //Despite we implement a special mechanism of safe callbacks, we delete this constructors
    //because of the following reasons:
    //1. If move constructor is called when an async operation in progress, it may indicate the logical
    //error in the code (what shoud we do with pending async operation called for source object?
    //Abandon it? Or redirect callback to a new object (there is no way to do this in the current project)?
    //2. If copy constructor is called when an async operation in progress, what should we do with
    //this operation callbacks? How to copy the pending async operation to a new object?

    void start()
    {
        reader_->startReadAsync();
        status_ = Status::working;
    }

    void send(const MsgBody& buf)
    {
        MsgBody copy_of_buf(buf);
        send(std::move(copy_of_buf));
    }
    void send(MsgBody&& buf)
    {
        if(writer_->writeInProgress())
        {
            write_queue_.push(buf);
        } else {
            if(write_queue_.empty())
            {
                writer_->send(std::move(buf));
            } else {
                writeFromBuffer();
            }
        }
    }

    bool receive(MsgBody& buf)
    {
        if(read_queue_.empty()) {
            return false;
        }
        buf = std::move(read_queue_.front());
        read_queue_.pop();
        return true;
    }

    void close()
    {
        reader_->close();
        writer_->close();
        status_ = Status::closed;
    }

    //Status tracking routines:
    [[nodiscard]] Status getStatus() const { return status_; }

    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
    {
        socket_ = std::move(socket);
        reader_->setSocket(socket_);
        writer_->setSocket(socket_);
    }

    [[nodiscard]] bool isFinished() const
    {
        return status_ == Status::closed || status_ == Status::failed;
    }
    [[nodiscard]] bool isActive() const
    {
        return status_ == Status::working;
    }
private:
    void onReadFinished([[maybe_unused]] MsgHeader& header, MsgBody&& buf)
    {
       read_queue_.emplace(std::move(buf));
       reader_->startReadAsync();
    }
    void onWriteFinished([[maybe_unused]] MsgBody&& buf)
    {
        writeFromBuffer();
    }
    void writeFromBuffer()
    {
       if(write_queue_.empty()) {
           return;
       }
       MsgBody msg(std::move(write_queue_.front()));
       write_queue_.pop();
       writer_->send(std::move(msg));
    }
    void onNetworkFailed(boost::system::error_code& ecode, [[maybe_unused]] size_t len)
    {
       std::cout<<"ASIOtalker: "<<ecode.message()<<std::endl; /// \todo: use Log object
       close();
       status_ = Status::failed;
    }
    SafeMemberFcnCallback<decltype(&ASIOBufferedTalker::onReadFinished)> on_read_finished_safe_;
    SafeMemberFcnCallback<decltype(&ASIOBufferedTalker::onWriteFinished)> on_write_finished_safe_;
    SafeMemberFcnCallback<decltype(&ASIOBufferedTalker::onNetworkFailed)> on_network_failed_safe_;

    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    std::shared_ptr<Writer> writer_;
    std::shared_ptr<Reader> reader_;
    Status status_ = Status::notStarted;
    std::queue<MsgBody> read_queue_;
    std::queue<MsgBody> write_queue_;
};

class ASIOServer : public IsAliveTracker
{
public:
    using error_code = boost::system::error_code;

    explicit ASIOServer(boost::asio::io_context &context = GetContext::get()):
        context_(context),
        on_accepted_safe_(&ASIOServer::onAccepted, this),
        on_accept_failed_safe_(&ASIOServer::onAcceptFailed, this)
    { }
    virtual ~ASIOServer() = default;

    ASIOServer(const ASIOServer&) = delete;
    ASIOServer(ASIOServer&&) = delete;
    ASIOServer& operator=(const ASIOServer&) = delete;
    ASIOServer& operator=(ASIOServer&&) = delete;

    enum class Status {notStarted, Listening, notListening, closed, terminated};

    void startAccept(port_t port)
    {
        acceptor_ = Acceptor::create_shared_ptr(context_, port, on_accepted_safe_, on_accept_failed_safe_);
        acceptor_->start_accept();
    }

    void send(size_t session_id, const MsgBody& msg)
    {
        sessions_.at(session_id).send(msg);
    }

    void send(size_t session_id, MsgBody&& msg)
    {
        sessions_.at(session_id).send(std::move(msg));
    }

    bool receiveAnySession(size_t &session_id, MsgBody& buf)
    {
        for(auto& [session, talker]: sessions_)
        {
            if(talker.receive(buf))
            {
                session_id = session;
                return true;
            }
        }
        return false;
    }
    bool receiveBySession_id(size_t session_id, MsgBody& buf)
    {
        return sessions_.at(session_id).receive(buf);
    }
    std::vector<size_t> getActiveSessions()
    {
        std::vector<size_t> activeSessions;
        activeSessions.reserve(sessions_.size());
        for(const auto &it : sessions_) {
            activeSessions.push_back(it.first);
        }
        return activeSessions;
    }
    std::vector<size_t> getNewSessions()
    {
        std::vector<size_t> res(std::move(new_sessions_));
        new_sessions_.clear();
        return res;
    }
    [[nodiscard]] size_t getActiveSessionsCount() const
    {
        size_t count = 0;
        for(const auto &it : sessions_) {
            if(it.second.isActive()) {
                count++;
            }
        }
        return count;
    }
    void close()
    {
        for(auto &it : sessions_) {
            it.second.close();
        }
        sessions_.clear();
    }
    void close(size_t session_id)
    {
        auto it = sessions_.find(session_id); //we assume that such element exists
        it->second.close();
        sessions_.erase(it);
    }
    void poll()
    {
        context_.get().poll();
    }

    void removeFinishedSessions()
    {
        std::vector<size_t> sessionsToDel;
        for(const auto &it : sessions_) {
            if(it.second.getStatus() > ASIOBufferedTalker::Status::working) {
                sessionsToDel.push_back(it.first);
            }
        }
        for(const size_t session : sessionsToDel) {
            sessions_.erase(session);
        }
    }

private:
    std::vector<size_t> new_sessions_;

    size_t session_counter_{0};



    void onAccepted(boost::asio::ip::tcp::socket &&socket)
    {
        std::cout<<"Accepted connection"<<std::endl;
        sessions_.emplace(session_counter_, std::make_shared<boost::asio::ip::tcp::socket>(std::move(socket)));
        new_sessions_.push_back(session_counter_);
        sessions_.at(session_counter_).start();
        session_counter_++;

    }
    void onAcceptFailed(const boost::system::error_code& ecode) // NOLINT
    {
        std::cout<<ecode.message()<<std::endl;
    }

    std::reference_wrapper<boost::asio::io_context> context_;

    SafeMemberFcnCallback<decltype(&ASIOServer::onAccepted)> on_accepted_safe_;
    SafeMemberFcnCallback<decltype(&ASIOServer::onAcceptFailed)> on_accept_failed_safe_; 

    std::map<size_t, ASIOBufferedTalker> sessions_;
    std::shared_ptr<Acceptor> acceptor_;

};

class ASIOClient : public IsAliveTracker
{
public:
    enum class Status
    {
        notinitialized, resolving, connecting, connectedOk, disconnected,
        resolvingFailed, connectingFailed
    };

    explicit ASIOClient(boost::asio::io_context &context = GetContext::get()) :
        context_(context),
        on_resolved_safe_(&ASIOClient::onResolved, this), on_resolve_failed_safe_(&ASIOClient::onResolveFailed, this),
        on_connected_safe_(&ASIOClient::onConnected, this), on_connect_failed_safe_(&ASIOClient::onConnectFailed, this),
        resolver_(Resolver::create_shared_ptr(context, on_resolved_safe_, on_resolve_failed_safe_)),
        connecter_(Connecter::create_shared_ptr(context, on_connected_safe_, on_connect_failed_safe_)),
        talker_(context) { }

    ASIOClient(const ASIOClient&) = delete;
    ASIOClient(ASIOClient&&) = delete;
    ASIOClient& operator=(const ASIOClient&) = delete;
    void operator=(ASIOClient&&) = delete;
    ~ASIOClient() = default;

    void connect(const std::string &address, uint16_t port)
    {
        port_ = port;
        address_ = address;        
        doResolveAndConnect();
    }

    void sync_connect(const std::string &address, uint16_t port)
    {
        connect(address, port);
        while(status_ >= Status::notinitialized && status_ <= Status::connecting) {
            poll();
        }
    }

    void send(const MsgBody& msg)
    {
        talker_.send(msg);
    }

    void send(MsgBody&& msg)
    {
        talker_.send(std::move(msg));
    }

    bool receive(MsgBody& buf)
    {
        return talker_.receive(buf);
    }

    void close() { talker_.close(); }
    void poll() { context_.poll(); }

    Status getStatus() { return status_; }
private:
    Status status_{Status::notinitialized};

    void updateStatus();
    boost::asio::io_context &context_;
    uint16_t port_{8082}; // NOLINT magic number
    std::string address_;

    void doResolveAndConnect()
    {
        status_ = Status::notinitialized;
        resolver_->resolve(address_, std::to_string(port_));
    }

    void onResolved(const boost::asio::ip::tcp::resolver::results_type& endpoints)
    {
        status_ = Status::connecting;
        connecter_->connect(endpoints);
    }

    void onResolveFailed([[maybe_unused]] const boost::system::error_code &ecode)
    {
        status_ = Status::resolvingFailed;
    }

    void onConnected([[maybe_unused]] const typename boost::asio::ip::tcp::endpoint &ep)
    {
        auto s = connecter_->socket();
        status_ = Status::connectedOk;
        talker_.setSocket(s);
        talker_.start();
    }
    void onConnectFailed([[maybe_unused]] const boost::system::error_code &ecode)
    {
        std::cout<<"Connect failed: "<<ecode.message()<<std::endl;
        status_ = Status::connectingFailed;
    }

    SafeMemberFcnCallback<decltype(&ASIOClient::onResolved)> on_resolved_safe_;
    SafeMemberFcnCallback<decltype(&ASIOClient::onResolveFailed)> on_resolve_failed_safe_;
    SafeMemberFcnCallback<decltype(&ASIOClient::onConnected)> on_connected_safe_;
    SafeMemberFcnCallback<decltype(&ASIOClient::onConnectFailed)> on_connect_failed_safe_;
    std::shared_ptr<Resolver> resolver_;
    std::shared_ptr<Connecter> connecter_;
    ASIOBufferedTalker talker_;
};

} /* namespace detail */

} /* namespace tea::asiocommunicator */

#endif // _TEA_ASIOCOMMUNICATOR_DETAIL_H

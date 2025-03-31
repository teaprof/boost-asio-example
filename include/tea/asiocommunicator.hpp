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

#ifndef ASIOCOMMUNICATOR_H
#define ASIOCOMMUNICATOR_H

#include "safecallback.hpp"
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

using byte = uint8_t;
using   Buffer = std::vector<byte>;

using port_t = uint16_t;
static_assert(sizeof(port_t) >= 2, "size of port_t should be atleat 2 bytes");

namespace detail {

struct Header final
{
    //Header should start with the following code:
    static constexpr uint32_t header_code_begin = 0x1234;

    uint32_t code; //should be equal to header_code_begin
    uint32_t msglen{0};  //len of the message in bytes
    Header() : code(header_code_begin) {}
    void* data()
    {
        return &code;
    }
    static size_t headerLen()
    {
        return sizeof(Header);
    }
    void initialize(const Buffer& body)
    {
        code = header_code_begin;
        msglen = body.size();
    }
};

//Declare types for various callback functions
using onReadFinishedT = void(Header&, Buffer&&);
using onWriteFinishedT = void(Buffer&&);
using onNetworkFailedT =void(boost::system::error_code&, size_t len);
using onAcceptedT = void(boost::asio::ip::tcp::socket&&);
using onAcceptFailedT = void(const boost::system::error_code&);
using onResolvedT = void(const boost::asio::ip::tcp::resolver::results_type& endpoints);
using onResolveFailedT = void(const boost::system::error_code&);
using onConnectedT = void(const typename boost::asio::ip::tcp::endpoint&);
using onConnectFailedT = void(const boost::system::error_code&);


struct GetContext
{
    static boost::asio::io_context& get()
    {
        static boost::asio::io_context context;
        return context;
    }
};

template<class T>
class AsyncOperationWaiter: public std::enable_shared_from_this<T>
{
public:
    AsyncOperationWaiter() = default;

    //we should not copy or move this object because we don't know
    //what we should do with pending io operations.
    AsyncOperationWaiter(const AsyncOperationWaiter&) = delete;
    AsyncOperationWaiter(AsyncOperationWaiter&&) = delete;
    void operator=(const AsyncOperationWaiter&) = delete;
    void operator=(AsyncOperationWaiter&&) = delete;
    ~AsyncOperationWaiter() = default;


    //All derived classes should hide their constructors because only shared_ptrs
    //to such objects are allowed. Instead of constructor, call this function
    //to create a shared_ptr to the derived object.
    template<typename ... Args>
    static std::shared_ptr<T> create_shared_ptr(Args&&...args)
    {
        struct MakeSharedEnabler : public T
        {
            explicit MakeSharedEnabler(Args...args) : T(std::forward<Args>(args)...) { }
        };
        return std::make_shared<MakeSharedEnabler>(std::forward<Args>(args)...);
    }
};


class Writer: public AsyncOperationWaiter<Writer>
{
    friend class AsyncOperationWaiter<Writer>;
public:
    // \todo: is this function needed?
    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        socket_ = std::move(socket);
    }

    void send(Buffer&& buffer)
    {
        assert(write_in_progress_ == false);
        body_ = std::move(buffer);
        doWriteHeader();
    }

    void close() {}
    bool writeInProgress() const {return write_in_progress_;}
private:
    Writer(std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::function<onWriteFinishedT> on_write_finished, std::function<onNetworkFailedT> on_network_failed) :
        on_write_finished_(std::move(on_write_finished)), on_network_failed_(std::move(on_network_failed)), socket_(std::move(socket)) {}


    bool write_in_progress_ = false;
    std::function<onWriteFinishedT> on_write_finished_;
    std::function<onNetworkFailedT> on_network_failed_;
    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;

    Header header_;
    Buffer body_;

    void doWriteHeader()
    {
        write_in_progress_ = true;
        auto self(shared_from_this());
        header_.initialize(body_);
        boost::asio::async_write(*socket_,
                                 boost::asio::buffer(header_.data(), Header::headerLen()),
                                 std::bind(&Writer::onWriteHeaderFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onWriteHeaderFinished(boost::system::error_code err_code, size_t len)
    {
        if(err_code.failed())
        {
            write_in_progress_ = true;
            if(on_network_failed_) {
                on_network_failed_(err_code, len);
            } else {
                throw std::runtime_error("tea::asiocommunicator::Reader::onWriteHeaderFinished: async_write returned non-zero code");
            }
        } else {
            doWriteBody();
        }
    }

    void doWriteBody()
    {        
        auto self(shared_from_this());
        boost::asio::async_write(*socket_, boost::asio::buffer(body_.data(), body_.size()),
                                 std::bind(&Writer::onWriteBodyFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onWriteBodyFinished(boost::system::error_code ecode, size_t len)
    {
        write_in_progress_ = false;
        if(ecode.failed())
        {
            if(on_network_failed_) {
                on_network_failed_(ecode, len);
            } else {
                throw std::runtime_error("tea::asiocommunicator::Reader::onWriteHeaderFinished: async_write returned non-zero code");
            }
        } else {
            on_write_finished_(std::move(body_));
        }
    }
};

class Reader: public AsyncOperationWaiter<Reader>
{
    friend class AsyncOperationWaiter<Reader>;
public:
    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> sock)
    {
        socket_ = std::move(sock);
    }
    void startReadAsync()
    {
        assert(read_in_progress_ == false);
        doReadHeader();
    }
    boost::system::error_code errorCode() { return error_code_; }
    bool readInProgress() const {return read_in_progress_; }
    void close() {}
private:
    Reader(std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::function<onReadFinishedT> on_read_finished, std::function<onNetworkFailedT> on_network_failed) :
        on_read_finished_(std::move(on_read_finished)), on_network_failed_(std::move(on_network_failed)), socket_(std::move(socket)) {}


    bool read_in_progress_ = false;
    boost::system::error_code error_code_;
    std::function<onReadFinishedT> on_read_finished_;
    std::function<onNetworkFailedT> on_network_failed_;
    Header header_;
    Buffer body_;
    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;


    void doReadHeader()
    {
        //at least one shared_ptr to 'this' pointer in your program should exists
        //before this line, otherwise bad_weak_ptr exception will be thrown
        auto self(shared_from_this());
        read_in_progress_ = true;

        boost::asio::async_read(*socket_, boost::asio::buffer(header_.data(), Header::headerLen()),
                                std::bind(&Reader::onReadHeaderFinished, self, std::placeholders::_1, std::placeholders::_2));
    }
    void onReadHeaderFinished(boost::system::error_code ecode, size_t len)
    {
        error_code_ = ecode;
        if(header_.code != Header::header_code_begin) {
            throw std::runtime_error("tea::asiocommunicator::Reader::onReadHeaderFinished: unexpected value, can't parse message header");
        }
        if(ecode.failed())
        {
            read_in_progress_ = false;
            if(on_network_failed_) {
                on_network_failed_(ecode, len);
            } else {
                throw std::runtime_error("tea::asiocommunicator::Reader::onReadHeaderFinished: async_read returned non-zero code");
            }
        } else {
            doReadBody();
        }
    }

    void doReadBody()
    {
        auto self(shared_from_this());        
        body_.resize(header_.msglen);
        boost::asio::async_read(*socket_, boost::asio::buffer(body_.data(), body_.size()),
                                std::bind(&Reader::onReadBodyFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onReadBodyFinished(boost::system::error_code ecode, size_t len)
    {
        read_in_progress_ = false;
        error_code_ = ecode;
        if(ecode.failed())
        {
            if(on_network_failed_) {
                on_network_failed_(ecode, len);
            } else {
                throw std::runtime_error("tea::asiocommunicator::Reader::onReadHeaderFinished: async_read returned non-zero code");
            }
        } else {
            if(on_read_finished_) {
                on_read_finished_(header_, std::move(body_));
            }
        }
    }
};


//ASIOtalker is a class that implements sending and receiving messages.
class ASIOBufferedTalker //todo: maybe rename to QueuedTalker?
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
    ASIOBufferedTalker(ASIOBufferedTalker&&) = delete;
    ASIOBufferedTalker& operator=(const ASIOBufferedTalker&) = delete;
    void operator=(ASIOBufferedTalker&&) = delete;
    


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

    void send(const Buffer& buf)
    {
        Buffer copy_of_buf(buf);
        send(std::move(copy_of_buf));
    }
    void send(Buffer&& buf)
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

    bool receive(Buffer& buf)
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
    void onReadFinished([[maybe_unused]] Header& header, Buffer&& buf)
    {
       read_queue_.emplace(std::move(buf));
       reader_->startReadAsync();
    }
    void onWriteFinished([[maybe_unused]] Buffer&& buf)
    {
        writeFromBuffer();
    }
    void writeFromBuffer()
    {
       if(write_queue_.empty()) {
           return;
       }
       Buffer msg(std::move(write_queue_.front()));
       write_queue_.pop();
       writer_->send(std::move(msg));
    }
    void onNetworkFailed(boost::system::error_code& ecode, [[maybe_unused]] size_t len)
    {
       std::cout<<"ASIOtalker: "<<ecode.message()<<std::endl;
       close();
       status_ = Status::failed;
    }
    CallbackProtector<decltype(&ASIOBufferedTalker::onReadFinished)> on_read_finished_safe_;
    CallbackProtector<decltype(&ASIOBufferedTalker::onWriteFinished)> on_write_finished_safe_;
    CallbackProtector<decltype(&ASIOBufferedTalker::onNetworkFailed)> on_network_failed_safe_;

    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    std::shared_ptr<Writer> writer_;
    std::shared_ptr<Reader> reader_;
    Status status_ = Status::notStarted;
    std::queue<Buffer> read_queue_;
    std::queue<Buffer> write_queue_;
};

class ASIOAcceptor : public AsyncOperationWaiter<ASIOAcceptor>
{
    friend class AsyncOperationWaiter<ASIOAcceptor>;
public:
    ASIOAcceptor() = delete;
    ASIOAcceptor(const ASIOAcceptor&) = delete;
    ASIOAcceptor(ASIOAcceptor&&) = delete;
    ASIOAcceptor operator=(const ASIOAcceptor&) = delete;
    ASIOAcceptor operator=(ASIOAcceptor&&) = delete;

    ~ASIOAcceptor()
    {
        try {
            acceptor_.close();
        } catch (...) {
            //nothing to do
        }
    }

    void start_accept(port_t port)
    //This function can be used to stop current acceptor operation and start accepting on a new port
    {
        //First, we should to close current operation
        acceptor_.close();
        //Then we have two alternatives:
        //Alternative 1: create new acceptor object (acceptor constructor will call open, bind and listen by itself), like in the following code
        //acceptor = std::move(boost::asio::ip::tcp::acceptor(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), mport)));
        //
        //Alternative 2: create new endpoint object like in the following code
        const boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));
        acceptor_.open(endpoint.protocol());
        acceptor_.bind(endpoint);
        acceptor_.listen();
        doAccept();
    }
    void start_accept()
    {
        doAccept();
    }

    bool acceptInProgress() const { return accept_in_progress_;}
private:
    ASIOAcceptor(boost::asio::io_context& context, port_t port,
                 std::function<onAcceptedT> on_new_connection,
                 std::function<onAcceptFailedT> on_accept_failed)
        : on_new_connection_(std::move(on_new_connection)),
        on_accept_failed_(std::move(on_accept_failed)),
        context_(context), acceptor_(context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {}

    void doAccept()
    {
        if(accept_in_progress_) {
           return;
        }
        accept_in_progress_ = true;
        auto self(shared_from_this());
        msocket_ = std::make_shared<boost::asio::ip::tcp::socket>(context_);
        //endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), mport);
        acceptor_.async_accept(*msocket_, std::bind(&ASIOAcceptor::onAcceptNewConnection, self, std::placeholders::_1)); //, std::placeholders::_2));
    }

    void onAcceptNewConnection(const boost::system::error_code& ecode) //, boost::asio::ip::tcp::socket socket)
    {
        accept_in_progress_ = false;
        if(ecode.failed())
        {
            if(on_accept_failed_) {
                on_accept_failed_(ecode);
            } else {
                throw std::runtime_error("tea::asiocommunicator::ASIOacceptor::onAcceptNewConnection: async_accept returned non-zero code");
            }
        } else {
            on_new_connection_(std::move(*msocket_));
            msocket_ = nullptr;
            doAccept();
        }
    }

    std::shared_ptr<boost::asio::ip::tcp::socket> msocket_;

    std::function<void(boost::asio::ip::tcp::socket&&)> on_new_connection_;
    std::function<void(const boost::system::error_code& ecode)> on_accept_failed_;
    boost::asio::io_context& context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    bool accept_in_progress_{false};
};

class ASIOresolver : public AsyncOperationWaiter<ASIOresolver>
{
    friend class AsyncOperationWaiter<ASIOresolver>;
public:

    void resolve(const std::string& address, const std::string& port)
    {
        doResolve(address, port);
    }
private:

    ASIOresolver(boost::asio::io_context& io_context, std::function<onResolvedT> on_resolved, std::function<onResolveFailedT> on_resolve_failed)
        : on_resolved_(std::move(on_resolved)), on_resolve_failed_(std::move(on_resolve_failed)), resolver_(io_context)
    { }

    void doResolve(const std::string& address, const std::string& port)
    {
        auto self(shared_from_this());
        resolver_.async_resolve(address, port, std::bind(&ASIOresolver::onResolveFinished, self, std::placeholders::_1, std::placeholders::_2));
    }
    void onResolveFinished(const boost::system::error_code& ecode, const boost::asio::ip::tcp::resolver::results_type& results)
    {
        if(ecode.failed())
        {
            if(on_resolve_failed_)
            {
                on_resolve_failed_(ecode);
            } else {
                throw std::runtime_error(std::string("Can't resolve host, error = ") + ecode.message());
            }
        } else {
            on_resolved_(results);
        }
    }
    std::function<onResolvedT> on_resolved_;
    std::function<onResolveFailedT> on_resolve_failed_;
    boost::asio::ip::tcp::resolver resolver_;
};

class ASIOconnecter : public AsyncOperationWaiter<ASIOconnecter>
{
    friend class AsyncOperationWaiter<ASIOconnecter>;
public:
    void connect(const boost::asio::ip::tcp::resolver::results_type& endpoints)
    {
        end_points_ = endpoints;
        doConnect();
    }
    std::shared_ptr<boost::asio::ip::tcp::socket> socket()
    {
        return msocket_;
    }
private:
    ASIOconnecter(boost::asio::io_context& context, std::function<onConnectedT> on_connected, std::function<onConnectFailedT> on_connect_failed)
        : context_(context), msocket_(std::make_shared<boost::asio::ip::tcp::socket>(context)),
        on_connected_(std::move(on_connected)), on_connect_failed_(std::move(on_connect_failed)) {}

    void doConnect()
    {
        auto self(shared_from_this());
        msocket_ = std::make_shared<boost::asio::ip::tcp::socket>(context_);
        boost::asio::async_connect(*msocket_, end_points_, std::bind(&ASIOconnecter::onConnectFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onConnectFinished(const boost::system::error_code &ecode, const typename boost::asio::ip::tcp::endpoint &ep)
    {        
        if(ecode.failed())
        {
            if(on_connect_failed_) {
                on_connect_failed_(ecode);
            } else {
                throw std::runtime_error(std::string("Can't connect to host, error = ") + ecode.message());
            }
        } else {
            on_connected_(ep);
        }
    }

    boost::asio::ip::tcp::resolver::results_type end_points_;
    boost::asio::io_context& context_;
    std::shared_ptr<boost::asio::ip::tcp::socket> msocket_;
    std::function<onConnectedT> on_connected_;
    std::function<onConnectFailedT> on_connect_failed_;
};

class ASIOServer
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
    void operator=(ASIOServer&&) = delete;

    enum class Status {notStarted, Listening, notListening, closed, terminated};

    void startAccept(port_t port)
    {
        acceptor_ = ASIOAcceptor::create_shared_ptr(context_, port, on_accepted_safe_, on_accept_failed_safe_);
        acceptor_->start_accept();
    }

    void send(size_t session_id, const Buffer& msg)
    {
        sessions_.at(session_id).send(msg);
    }

    void send(size_t session_id, Buffer&& msg)
    {
        sessions_.at(session_id).send(std::move(msg));
    }

    bool receiveAnySession(size_t &session_id, Buffer& buf)
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
    bool receiveBySession_id(size_t session_id, Buffer& buf)
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
        context_.poll();
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

    boost::asio::io_context& context_;

    CallbackProtector<decltype(&ASIOServer::onAccepted)> on_accepted_safe_;
    CallbackProtector<decltype(&ASIOServer::onAcceptFailed)> on_accept_failed_safe_; 

    std::map<size_t, ASIOBufferedTalker> sessions_;
    std::shared_ptr<ASIOAcceptor> acceptor_;

};

class ASIOClient
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
        resolver_(ASIOresolver::create_shared_ptr(context, on_resolved_safe_, on_resolve_failed_safe_)),
        connecter_(ASIOconnecter::create_shared_ptr(context, on_connected_safe_, on_connect_failed_safe_)),
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

    void send(const Buffer& msg)
    {
        talker_.send(msg);
    }

    void send(Buffer&& msg)
    {
        talker_.send(std::move(msg));
    }

    bool receive(Buffer& buf)
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

    CallbackProtector<decltype(&ASIOClient::onResolved)> on_resolved_safe_;
    CallbackProtector<decltype(&ASIOClient::onResolveFailed)> on_resolve_failed_safe_;
    CallbackProtector<decltype(&ASIOClient::onConnected)> on_connected_safe_;
    CallbackProtector<decltype(&ASIOClient::onConnectFailed)> on_connect_failed_safe_;
    std::shared_ptr<ASIOresolver> resolver_;
    std::shared_ptr<ASIOconnecter> connecter_;
    ASIOBufferedTalker talker_;
};

} /* namespace detail */

template<class T>
concept IsBuffer = std::same_as<Buffer, T>;

class Server : public detail::ASIOServer
{
public:
    template<typename ... Args>
    explicit Server(Args&& ... args) : detail::ASIOServer(std::forward<Args>(args)...) {}

    template<typename MsgType> requires (not IsBuffer<MsgType>)
    void send(size_t session_id, const MsgType& msg)
    {
        Buffer buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        detail::ASIOServer::send(session_id, std::move(buf));
    }

    template<typename MsgType> requires (not IsBuffer<MsgType>)
    bool receiveAnySession(size_t &session_id, MsgType& msg)
    {
        Buffer buf;
        if(detail::ASIOServer::receiveAnySession(session_id, buf))
        {
            if(buf.size() != sizeof(msg)) {
                throw std::runtime_error("incorrect message size");
            }
            std::memcpy(&msg, buf.data(), buf.size());
            return true;
        };
        return false;
    }

    template<typename MsgType> requires (not IsBuffer<MsgType>)
    bool receiveBySession_id(size_t session_id, MsgType& msg)
    {
        Buffer buf;
        if(detail::ASIOServer::receiveBySession_id(session_id, buf))
        {
            if(buf.size() != sizeof(msg)) {
                throw std::runtime_error("incorrect message size");
            }
            std::memcpy(&msg, buf.data(), buf.size());
            return true;
        };
        return false;
    }

};


class Client : public detail::ASIOClient
{
public:
    template<typename ... Args>
    explicit Client(Args&& ... args) : detail::ASIOClient(std::forward<Args>(args)...) {}


    template<typename MsgType> requires (not IsBuffer<MsgType>)
    void send(const MsgType& msg)
    {
        Buffer buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        detail::ASIOClient::send(std::move(buf));
    }
    template<typename MsgType> requires (not IsBuffer<MsgType>)
    bool receive(MsgType& msg)
    {
        Buffer buf;
        if(detail::ASIOClient::receive(buf))
        {
            if(buf.size() != sizeof(msg)) {
                throw std::runtime_error("incorrect message size");
            }
            std::memcpy(&msg, buf.data(), buf.size());
            return true;
        };
        return false;
    }
};
} /* namespace tea::asiocommunicator */

#endif // ASIOCOMMUNICATOR_H

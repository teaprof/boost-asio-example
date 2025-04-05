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

#ifndef _TEA_ASIOCOMMUNICATOR_DETAIL_ASYNCOPERATIONS_H
#define _TEA_ASIOCOMMUNICATOR_DETAIL_ASYNCOPERATIONS_H

#include "safecallback.hpp"
#include "message.hpp"
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

using port_t = uint16_t;
static_assert(sizeof(port_t) >= 2, "size of port_t should be atleat 2 bytes");

//Declare types for various callback functions
using onReadFinishedT = void(MsgHeader&, MsgBody&&);
using onWriteFinishedT = void(MsgBody&&);
using onNetworkFailedT =void(boost::system::error_code&, size_t len);
using onAcceptedT = void(boost::asio::ip::tcp::socket&&);
using onAcceptFailedT = void(const boost::system::error_code&);
using onResolvedT = void(const boost::asio::ip::tcp::resolver::results_type& endpoints);
using onResolveFailedT = void(const boost::system::error_code&);
using onConnectedT = void(const typename boost::asio::ip::tcp::endpoint&);
using onConnectFailedT = void(const boost::system::error_code&);

/*!
 * @brief The base class for any async operation classes
 * @details The only function of this class is to make the objects of the derived type T 
 * safe in terms of the asyncronous operations. It mean that object of type T should not be
 * destroyed before asyncronous operation is completed or cancelled. To control the life-time
 * of such objects we use shared pointers. It means that
 * - such objects should not be allocated on stack
 * @tparam T is the type of the derived concrete async operation class. 
 */
template<class T>
class AsyncOperationBase: public std::enable_shared_from_this<T>
{
public:
    AsyncOperationBase() = default;

    //we should not copy or move this object because we don't know
    //what we should do with pending io operations.
    /// \todo: why move operations are prohibited too?
    AsyncOperationBase(const AsyncOperationBase&) = delete;
    //AsyncOperationBase(AsyncOperationBase&&) = delete;
    void operator=(const AsyncOperationBase&) = delete;
    void operator=(AsyncOperationBase&&) = delete;
    ~AsyncOperationBase() = default;


    //All derived classes should hide their constructors because only shared_ptrs
    //to such objects are allowed. Instead of calling the constructor, call this function
    //to create a shared_ptr to the derived object.
    template<typename ... Args>
    static std::shared_ptr<T> create_shared_ptr_fast(Args&&...args)
    /// requires only one memory allocation (control block and object of type T are allocated in the same time),
    /// but object's memory can be deallocated only with deallocation of control block (when last shared_ptr 
    /// to this object is destroyed)
    {
        // This is the way how to call private constructor of T. AsyncOperationBase should
        // be the friend of T.
        struct MakeSharedEnabler : public T
        {
            explicit MakeSharedEnabler(Args...args) : T(std::forward<Args>(args)...) { }
        };
        return std::make_shared<MakeSharedEnabler>(std::forward<Args>(args)...);
    }

    /// this version allocates two blocks of memory: one for control block and one for the object.
    template<typename ... Args>
    static std::shared_ptr<T> create_shared_ptr(Args&&...args)
    {
        // the following instructions don't lead to the memory leak
        return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
    }
};


class Writer: public AsyncOperationBase<Writer>
{
    // add AsyncOperationBase to friends to grant him access to the private constructor of this class
    friend class AsyncOperationBase<Writer>;
public:
    Writer(Writer&& other);
    // \todo: is this function needed?
    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        socket_ = std::move(socket);
    }

    void send(MsgBody&& buffer)
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

    MsgHeader header_;
    MsgBody body_;

    void doWriteHeader()
    {
        write_in_progress_ = true;
        auto self(shared_from_this()); /// \todo: here should be weak_from_this        
        header_.initialize(body_);
        boost::asio::async_write(*socket_,
                                 boost::asio::buffer(header_.data(), MsgHeader::headerLen()),
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

class Reader: public AsyncOperationBase<Reader>
{
    // add AsyncOperationBase to friends to grant him access to the private constructor of this class
    friend class AsyncOperationBase<Reader>;
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
    MsgHeader header_;
    MsgBody body_;
    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;


    void doReadHeader()
    {
        //at least one shared_ptr to 'this' pointer in your program should exists
        //before this line, otherwise bad_weak_ptr exception will be thrown
        auto self(shared_from_this());
        read_in_progress_ = true;

        boost::asio::async_read(*socket_, boost::asio::buffer(header_.data(), MsgHeader::headerLen()),
                                std::bind(&Reader::onReadHeaderFinished, self, std::placeholders::_1, std::placeholders::_2));
    }
    void onReadHeaderFinished(boost::system::error_code ecode, size_t len)
    {
        error_code_ = ecode;
        if(header_.signature() != MsgHeaderBuffer::header_signature) {
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
        body_.resize(header_.msglen());
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

class ASIOAcceptor : public AsyncOperationBase<ASIOAcceptor>
{
    friend class AsyncOperationBase<ASIOAcceptor>;
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

class ASIOresolver : public AsyncOperationBase<ASIOresolver>
{
    friend class AsyncOperationBase<ASIOresolver>;
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

class ASIOconnecter : public AsyncOperationBase<ASIOconnecter>
{
    friend class AsyncOperationBase<ASIOconnecter>;
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

} /* namespace detail */

} /* namespace tea::asiocommunicator */

#endif // _TEA_ASIOCOMMUNICATOR_DETAIL_ASYNCOPERATIONS_H

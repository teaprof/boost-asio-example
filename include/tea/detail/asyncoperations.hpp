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

//Signatures of different callback functions
using onReadFinishedT = void(MsgHeader&, MsgBody&&);
using onWriteFinishedT = void(MsgBody&&);
using onNetworkFailedT =void(boost::system::error_code&, size_t len);
using onAcceptedT = void(boost::asio::ip::tcp::socket&&);
using onAcceptFailedT = void(const boost::system::error_code&);
using onResolvedT = void(const boost::asio::ip::tcp::resolver::results_type& endpoints);
using onResolveFailedT = void(const boost::system::error_code&);
using onConnectedT = void(const typename boost::asio::ip::tcp::endpoint&);
using onConnectFailedT = void(const boost::system::error_code&);

//! The base class for any other classes wrapping boost::asio async operations.
/**
 * This class protects the derived classes from deletion during asyncronous 
 * operations. Since such objects incapsulate
 * buffers and callback functions for async io operations, it means that they
 * should not be destroyed before asyncronous operation is completed or cancelled.
 * For the same reason, they should not be copied and moved.
 * 
 * To prevent the deletion of such objects during async operation in progress 
 * the objects of derived types are managed by shared pointers. The direct creation
 * of such objects is prohibited (constructor is declared as private).
 * @tparam T is the type of the derived class
 */
template<class T>
class AsyncOperationBase: public std::enable_shared_from_this<T>
{    
public:  
    AsyncOperationBase() = default;

    /** @name Copy and move
     *  copy and move operations are prohibited for the reason described above
     *  @{
     */
    AsyncOperationBase(const AsyncOperationBase&) = delete;
    AsyncOperationBase(AsyncOperationBase&&) = delete;
    void operator=(const AsyncOperationBase&) = delete;
    void operator=(AsyncOperationBase&&) = delete;
    ///@}

    ~AsyncOperationBase() = default;

    /** @name Object creation
     * All derived classes should hide their constructors because only shared_ptrs
     * to such objects are allowed. Instead of calling the constructor, call these functions
     * to create a shared_ptr to the derived object.
     * 
     * This functions differ only by the way they create shared pointers. 
     * @{
     */    
    //! Create new object and return shared pointer to it
    /**
     * This function requires only one memory allocation (control block of shared_ptr and 
     * object itself of type T are allocated in the same time), but object's memory can be 
     * deallocated only with deallocation of control block (when all shared_ptrs and weak_ptrs 
     * to this object are destroyed)
     */
    template<typename ... Args>
    static std::shared_ptr<T> create_shared_ptr_fast(Args&&...args)
    {
        // This is the way how to call private constructor of T. AsyncOperationBase should
        // be the friend of T.
        struct MakeSharedEnabler : public T
        {
            explicit MakeSharedEnabler(Args...args) : T(std::forward<Args>(args)...) { }
        };
        return std::make_shared<MakeSharedEnabler>(std::forward<Args>(args)...);
    }

    /// Create new object and return shared pointer to it
    /**
     * This function allocatates two blocks in memory: one for shared_ptr control block and 
     * one for the managed object of type T. When the last shared_ptr to this object
     * is destroyed the object is destroyed immediately. The control block is destroyed
     * until last weak_ptr is destroyed.
     */
    template<typename ... Args>
    static std::shared_ptr<T> create_shared_ptr(Args&&...args)
    {
        // the following instructions don't lead to the memory leak in any case
        return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
    }
    /// @}
};


//! Object-oriented wrapper for async write operations incapsulating all required memory buffers and callback functions
/**
 * This class can send arbitrary message. The message consists of the header and the body,
 * Header contains the size of the body. The body part of the message is the message itself.
 * 
 * This object should not be created directly calling the constructor (see comments on constructor).
 */
class Writer: public AsyncOperationBase<Writer>
{
    // add AsyncOperationBase to friends to grant him access to the private constructor of this class
    friend class AsyncOperationBase<Writer>;

    //! private constructor
    /** The constructor is private to avoid creation objects of this type which are not managed by shared_ptrs.
     *  This object should be created using static function Writer::create_shared_ptr_fast or Writer::create_shared_ptr 
     *  inherited from AsyncOperationBase.    
     */
    Writer(std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::function<onWriteFinishedT> on_write_finished, std::function<onNetworkFailedT> on_network_failed) :
        on_write_finished_(std::move(on_write_finished)), on_network_failed_(std::move(on_network_failed)), socket_(std::move(socket)) {}
public:  
    //! set socket for communications
    /** When creating this object the socket could be unknown. This method should 
     * be called when the connection is established and the socket becomes known. */
    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        socket_ = std::move(socket);
    }

    //! send message
    void send(MsgBody&& buffer)
    {
        assert(write_in_progress_ == false);
        body_ = std::move(buffer);
        doWriteHeader();
    }

    /// Close all connections
    void close() { /* nothing to do */}

    /// return true if any async operation is in progress (submitted but not finished yet)
    bool writeInProgress() const {return write_in_progress_;}

    //! return error code of the last finished async operation
    /** This function is auxiliary because in case of error the callback is invoked or exception is thrown */
    boost::system::error_code errorCode() { return error_code_; }
private:
    bool write_in_progress_ = false;

    /// @name callback functions:
    /// callback functions that are passed as one of the arguments when any async operation is called
    ///@{    
    std::function<onWriteFinishedT> on_write_finished_;
    std::function<onNetworkFailedT> on_network_failed_;
    ///@}

    /// @name message to transmit:
    ///@{
    /// header and body part of the message being transmitted
    MsgHeader header_;
    MsgBody body_;
    ///@}

    /// socket where messages are written to
    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    
    /// error code of the last finished async operation
    boost::system::error_code error_code_;


    /**
     * @name functions for sending message and callbacks
     * @{
     */
    /// initialize `header_` and initiate async write of the `header_`
    void doWriteHeader()
    {
        // change status.
        write_in_progress_ = true;
        // create shared ptr from this using `enable_shared_from_this`
        auto self(shared_from_this());
        // initialize header that should be sent to counter party
        header_.initialize(body_);
        // submit async operation. We pass shared_ptr to `*this` inplace of raw ptr to ensure that `this` will not be destroyed before
        // async operation is completed. 
        boost::asio::async_write(*socket_,
                                 boost::asio::buffer(header_.data(), MsgHeader::headerLen()),
                                 std::bind(&Writer::onWriteHeaderFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    /// Callback function which is invoked when async write of the header is finished. It 
    /// initiates transmitting of the message body if no error during transmitting the header has occured. In case
    /// of error, the `on_network_failed` is called (or exception is thrown, if std::function `on_network_failed`    
    /// set by constructor is empty)
    void onWriteHeaderFinished(boost::system::error_code err_code, size_t len)
    {
        error_code_ = err_code;
        if(err_code.failed())
        {
            // error has occured during async network operation
            write_in_progress_ = false;
            if(on_network_failed_) {
                // if callback function is set, call it
                on_network_failed_(err_code, len);
            } else {
                // otherwise throw an exception
                std::stringstream str;
                str<<"tea::asiocommunicator::Writer::onWriteHeaderFinished: async_write returned non-zero code: "<<err_code;
                throw std::runtime_error(str.str());
            }
        } else {
            // async network operation finished successfully,
            // proceed to the next step:            
            doWriteBody();
        }
    }

    /// initiate async write of the message body
    void doWriteBody()
    {        
        assert(write_in_progress_);
        auto self(shared_from_this());
        boost::asio::async_write(*socket_, boost::asio::buffer(body_.data(), body_.size()),
                                 std::bind(&Writer::onWriteBodyFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    /// Callback function which is invoked when async write of the message body is finished. 
    /// It invokes `on_write_finished` if no error during transmitting the message body has occured.
    /// In case of error, the `on_network_failed` is called (or exception is thrown, if std::function `on_network_failed`
    /// is empty)
    void onWriteBodyFinished(boost::system::error_code err_code, size_t len)
    {
        error_code_ = err_code;
        write_in_progress_ = false;
        if(err_code.failed())
        {
            if(on_network_failed_) {
                on_network_failed_(err_code, len);
            } else {
                std::stringstream str;
                str<<"tea::asiocommunicator::Writer::onWriteBodyFinished: async_write returned non-zero code: "<<err_code;
                throw std::runtime_error(str.str());
            }
        } else {            
            on_write_finished_(std::move(body_));
        }
    }
    /// @}
};

class Reader: public AsyncOperationBase<Reader>
{
    // add AsyncOperationBase to friends to grant him access to the private constructor of this class
    friend class AsyncOperationBase<Reader>;

    Reader(std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::function<onReadFinishedT> on_read_finished, std::function<onNetworkFailedT> on_network_failed) :
        on_read_finished_(std::move(on_read_finished)), on_network_failed_(std::move(on_network_failed)), socket_(std::move(socket)) {}
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
    bool readInProgress() const {return read_in_progress_; }
    boost::system::error_code errorCode() { return error_code_; }
    void close() {}    
private:
    bool read_in_progress_ = false;

    std::function<onReadFinishedT> on_read_finished_;
    std::function<onNetworkFailedT> on_network_failed_;

    MsgHeader header_;
    MsgBody body_;

    std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
    boost::system::error_code error_code_;


    void doReadHeader()
    {
        read_in_progress_ = true;
        auto self(shared_from_this());
        boost::asio::async_read(*socket_, boost::asio::buffer(header_.data(), MsgHeader::headerLen()),
                                std::bind(&Reader::onReadHeaderFinished, self, std::placeholders::_1, std::placeholders::_2));
    }
    void onReadHeaderFinished(boost::system::error_code err_code, size_t len)
    {
        error_code_ = err_code;
        if(header_.signature != MsgHeader::header_signature) {
            throw std::runtime_error("tea::asiocommunicator::Reader::onReadHeaderFinished: unexpected value, can't parse message header");
        }
        if(err_code.failed())
        {
            read_in_progress_ = false;
            if(on_network_failed_) {
                on_network_failed_(err_code, len);
            } else {
                std::stringstream str;
                str<<"tea::asiocommunicator::Reader::onReadHeaderFinished: async_read returned non-zero code: "<<err_code;
                throw std::runtime_error(str.str());
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

    void onReadBodyFinished(boost::system::error_code err_code, size_t len)
    {
        read_in_progress_ = false;
        error_code_ = err_code;
        if(err_code.failed())
        {
            if(on_network_failed_) {
                on_network_failed_(err_code, len);
            } else {
                std::stringstream str;
                str<<"tea::asiocommunicator::Reader::onReadBodyFinished: async_read returned non-zero code: "<<err_code;
                throw std::runtime_error(str.str());
            }
        } else {
            if(on_read_finished_) {
                on_read_finished_(header_, std::move(body_));
            }
        }
    }
};

class Acceptor : public AsyncOperationBase<Acceptor>
{
    friend class AsyncOperationBase<Acceptor>;

    Acceptor(boost::asio::io_context& context, port_t port, std::function<onAcceptedT> on_new_connection, std::function<onAcceptFailedT> on_accept_failed)
    : on_new_connection_(std::move(on_new_connection)), on_accept_failed_(std::move(on_accept_failed)), context_(context), acceptor_(context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) 
    {}
public:
    Acceptor() = delete;
    Acceptor(const Acceptor&) = delete;
    Acceptor(Acceptor&&) = delete;
    Acceptor operator=(const Acceptor&) = delete;
    Acceptor operator=(Acceptor&&) = delete;

    ~Acceptor()
    {
        try {
            close();
        } catch (...) {
            //nothing to do
        }
    }

    void start_accept(port_t port)
    //This function can be used to stop current acceptor operation and start accepting on a new port
    {
        //First, we should to close current operation
        close();
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
    boost::system::error_code errorCode() { return error_code_; }
    void close() { acceptor_.close(); }
private:
    boost::system::error_code error_code_;


    void doAccept()
    {
        if(accept_in_progress_) {
           return;
        }
        accept_in_progress_ = true;
        auto self(shared_from_this());
        msocket_ = std::make_shared<boost::asio::ip::tcp::socket>(context_);
        //endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), mport);
        acceptor_.async_accept(*msocket_, std::bind(&Acceptor::onAcceptNewConnection, self, std::placeholders::_1)); //, std::placeholders::_2));
    }

    void onAcceptNewConnection(const boost::system::error_code& err_code) //, boost::asio::ip::tcp::socket socket)
    {
        accept_in_progress_ = false;
        error_code_ = err_code;
        if(err_code.failed())
        {
            if(on_accept_failed_) {
                on_accept_failed_(err_code);
            } else {
                std::stringstream str;
                str<<"tea::asiocommunicator::ASIOacceptor::onAcceptNewConnection: async_accept returned non-zero code: "<<err_code;
                throw std::runtime_error(str.str());
            }
        } else {
            on_new_connection_(std::move(*msocket_));
            msocket_ = nullptr;
            doAccept();
        }
    }

    std::shared_ptr<boost::asio::ip::tcp::socket> msocket_;

    std::function<void(boost::asio::ip::tcp::socket&&)> on_new_connection_;
    std::function<void(const boost::system::error_code& err_code)> on_accept_failed_;
    boost::asio::io_context& context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    bool accept_in_progress_{false};
};

class Resolver : public AsyncOperationBase<Resolver>
{
    friend class AsyncOperationBase<Resolver>;

    Resolver(boost::asio::io_context& io_context, std::function<onResolvedT> on_resolved, std::function<onResolveFailedT> on_resolve_failed)
    : on_resolved_(std::move(on_resolved)), on_resolve_failed_(std::move(on_resolve_failed)), resolver_(io_context) { }

public:

    void resolve(const std::string& address, const std::string& port)
    {
        doResolve(address, port);
    }
    boost::system::error_code errorCode() { return error_code_; }
    void close() { }
private:
    boost::system::error_code error_code_;

    void doResolve(const std::string& address, const std::string& port)
    {
        auto self(shared_from_this());
        resolver_.async_resolve(address, port, std::bind(&Resolver::onResolveFinished, self, std::placeholders::_1, std::placeholders::_2));
    }
    void onResolveFinished(const boost::system::error_code& err_code, const boost::asio::ip::tcp::resolver::results_type& results)
    {
        error_code_ = err_code;
        if(err_code.failed())
        {
            if(on_resolve_failed_)
            {
                on_resolve_failed_(err_code);
            } else {
                throw std::runtime_error(std::string("Can't resolve host, error = ") + err_code.message());
            }
        } else {
            on_resolved_(results);
        }
    }
    std::function<onResolvedT> on_resolved_;
    std::function<onResolveFailedT> on_resolve_failed_;
    boost::asio::ip::tcp::resolver resolver_;
};

class Connector : public AsyncOperationBase<Connector>
{
    friend class AsyncOperationBase<Connector>;
    Connector(boost::asio::io_context& context, std::function<onConnectedT> on_connected, std::function<onConnectFailedT> on_connect_failed)
        : context_(context), msocket_(std::make_shared<boost::asio::ip::tcp::socket>(context)),
        on_connected_(std::move(on_connected)), on_connect_failed_(std::move(on_connect_failed)) {}    
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
    boost::system::error_code errorCode() { return error_code_; }
    void close() {}
private:
    boost::system::error_code error_code_;

    void doConnect()
    {
        auto self(shared_from_this());
        msocket_ = std::make_shared<boost::asio::ip::tcp::socket>(context_);
        boost::asio::async_connect(*msocket_, end_points_, std::bind(&Connector::onConnectFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onConnectFinished(const boost::system::error_code &err_code, const typename boost::asio::ip::tcp::endpoint &ep)
    {
        error_code_ = err_code;
        if(err_code.failed())
        {
            if(on_connect_failed_) {
                on_connect_failed_(err_code);
            } else {
                std::stringstream str;
                str<<"tea::asiocommunicator::Connector::onConnectFinished: Can't connect to host: "<<err_code;
                throw std::runtime_error(str.str());
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

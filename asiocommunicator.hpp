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
#include <thread>

namespace tea {

namespace asiocommunicator {

using byte = uint8_t;
using Buffer = std::vector<byte>;

using port_t = unsigned short; //C++ standard requires that it is at least 16 bits
static_assert(sizeof(port_t) >= 2, "size of port_t should be atleat 2 bytes");

namespace detail {

struct Header final
{
    //Header should start with the following code:
    static constexpr uint32_t headerCodeBegin = 0x1234;

    uint32_t code; //should be equal to headerCodeBegin
    uint32_t msglen;  //len of the message in bytes
    Header() : code(headerCodeBegin), msglen(0) {}
    void* data()
    {
        return &code;
    }
    size_t headerLen()
    {
        return sizeof(Header);
    }
    void initialize(const Buffer& body)
    {
        code = headerCodeBegin;
        msglen = body.size();
    }
};


//Declare types for various callback functions
using onReadFinishedT = void(Header&&, Buffer&&);
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

//boost::asio::ip::tcp::socket sss(GetContext::get());

template<class T>
class AsyncOperationWaiter: public std::enable_shared_from_this<T>
{
public:
    AsyncOperationWaiter() = default;
    //AsyncOperationCaller(boost::asio::ip::tcp::socket&) = default;

    //we should not copy or move this object because we don't know
    //what we should do with pending io operations.
    AsyncOperationWaiter(const AsyncOperationWaiter&) = delete;
    AsyncOperationWaiter(AsyncOperationWaiter&&) = delete;


    //All derived classes should hide their constructors because only shared_ptrs
    //to such objects are allowed. Instead of constructor, call this function
    //to create a shared_ptr to the derived object.
    template<typename ... Args>
    static std::shared_ptr<T> create_shared_ptr(Args&&...args)
    {
        struct make_shared_enabler : public T
        {
            make_shared_enabler(Args...args) : T(std::forward<Args>(args)...) { }
        };
        return std::make_shared<make_shared_enabler>(std::forward<Args>(args)...);
    }
protected:
    //boost::asio::ip::tcp::socket& socket; //some of derived classes requires context, not socket
};


class Writer: public AsyncOperationWaiter<Writer>
{
    friend class AsyncOperationWaiter<Writer>;
public:
    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> sock)
    {
        socket = sock;
    }

    void send(Buffer&& Buf)
    {
        assert(write_in_progress == false);
        std::swap(Buf, body);
        doWriteHeader();
    }

    void close() {}
    bool writeInProgress() {return write_in_progress;}
private:
    Writer(std::shared_ptr<boost::asio::ip::tcp::socket> socket_, std::function<onWriteFinishedT> onWriteFinished_, std::function<onNetworkFailedT> onNetworkFailed_) :
        onWriteFinished(onWriteFinished_), onNetworkFailed(onNetworkFailed_), socket(socket_) {}


    bool write_in_progress = false;
    std::function<onWriteFinishedT> onWriteFinished;
    std::function<onNetworkFailedT> onNetworkFailed;
    std::shared_ptr<boost::asio::ip::tcp::socket> socket;

    Header header;
    Buffer body;

    void doWriteHeader()
    {
        write_in_progress = true;
        auto self(shared_from_this());
        header.initialize(body);
        //std::cout<<"doWriteHeader::headerlen = "<<header.headerLen()<<std::endl;
        boost::asio::async_write(*socket,
                                 boost::asio::buffer(header.data(), header.headerLen()),
                                 std::bind(&Writer::onWriteHeaderFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onWriteHeaderFinished(boost::system::error_code ec, size_t len)
    {
        if(ec.failed())
        {
            write_in_progress = true;
            if(onNetworkFailed)
                onNetworkFailed(ec, len);
            else
                throw std::runtime_error("tea::asiocommunicator::Reader::onWriteHeaderFinished: async_write returned non-zero code");
        } else {
            doWriteBody();
        }
    }

    void doWriteBody()
    {        
        auto self(shared_from_this());
        boost::asio::async_write(*socket, boost::asio::buffer(body.data(), body.size()),
                                 std::bind(&Writer::onWriteBodyFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onWriteBodyFinished(boost::system::error_code ec, size_t len)
    {
        std::cout<<"onWriteBodyFinished"<<std::endl;
        write_in_progress = false;
        if(ec.failed())
        {
            if(onNetworkFailed)
                onNetworkFailed(ec, len);
            else
                throw std::runtime_error("tea::asiocommunicator::Reader::onWriteHeaderFinished: async_write returned non-zero code");
        } else {
            onWriteFinished(std::move(body));
        }
    }
};

class Reader: public AsyncOperationWaiter<Reader>
{
    friend class AsyncOperationWaiter<Reader>;
public:
    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> sock)
    {
        socket = sock;
    }
    void startReadAsync()
    {
        assert(read_in_progress == false);
        doReadHeader();
        //doReadBody();
    }
    boost::system::error_code errorCode() { return error_code; }
    bool readInProgress() {return read_in_progress; }
    void close() {}
private:
    Reader(std::shared_ptr<boost::asio::ip::tcp::socket> socket_, std::function<onReadFinishedT> onReadFinished_, std::function<onNetworkFailedT> onNetworkFailed_) :
        onReadFinished(onReadFinished_), onNetworkFailed(onNetworkFailed_), socket(socket_) {}


    bool read_in_progress = false;
    boost::system::error_code error_code;
    std::function<onReadFinishedT> onReadFinished;
    std::function<onNetworkFailedT> onNetworkFailed;
    Header header;
    Buffer body;
    std::shared_ptr<boost::asio::ip::tcp::socket> socket;


    void doReadHeader()
    {
        //at least one shared_ptr to 'this' pointer in your program should exists
        //before this line, otherwise bad_weak_ptr exception will be thrown
        auto self(shared_from_this());
        read_in_progress = true;

        boost::asio::async_read(*socket, boost::asio::buffer(header.data(), header.headerLen()),
                                std::bind(&Reader::onReadHeaderFinished, self, std::placeholders::_1, std::placeholders::_2));
    }
    void onReadHeaderFinished(boost::system::error_code ec, size_t len)
    {
        error_code = ec;
        if(header.code != Header::headerCodeBegin)
            throw std::runtime_error("tea::asiocommunicator::Reader::onReadHeaderFinished: unexpected value, can't parse message header");
        if(ec.failed())
        {
            read_in_progress = false;
            if(onNetworkFailed)
                onNetworkFailed(ec, len);
            else
                throw std::runtime_error("tea::asiocommunicator::Reader::onReadHeaderFinished: async_read returned non-zero code");
        } else {
            doReadBody();
        }
    }

    void doReadBody()
    {
        auto self(shared_from_this());        
        body.resize(header.msglen);
        boost::asio::async_read(*socket, boost::asio::buffer(body.data(), body.size()),
                                std::bind(&Reader::onReadBodyFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onReadBodyFinished(boost::system::error_code ec, size_t len)
    {
        read_in_progress = false;
        error_code = ec;
        if(ec.failed())
        {
            if(onNetworkFailed)
                onNetworkFailed(ec, len);
            else
                throw std::runtime_error("tea::asiocommunicator::Reader::onReadHeaderFinished: async_read returned non-zero code");
        } else {
            //call callback function
            if(onReadFinished)
                onReadFinished(std::move(header), std::move(body));
        }
        std::cout<<"Readbody finished"<<std::endl;
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

    ASIOBufferedTalker(std::shared_ptr<boost::asio::ip::tcp::socket> socket_) :
        onReadFinishedSafe(&ASIOBufferedTalker::onReadFinished, this),
        onWriteFinishedSafe(&ASIOBufferedTalker::onWriteFinished, this),
        onNetworkFailedSafe(&ASIOBufferedTalker::onNetworkFailed, this),
        socket(socket_),
        writer(Writer::create_shared_ptr(socket, onWriteFinishedSafe, onNetworkFailedSafe)), //todo: socket could be destroyed before Writer (or Reader) destoroyed
        reader(Reader::create_shared_ptr(socket, onReadFinishedSafe, onNetworkFailedSafe))
        {
            std::cout<<"ASIOBufferedTalker(&&socket_)"<<std::endl;
        }

    ASIOBufferedTalker(boost::asio::io_context &context) :
        onReadFinishedSafe(&ASIOBufferedTalker::onReadFinished, this),
        onWriteFinishedSafe(&ASIOBufferedTalker::onWriteFinished, this),
        onNetworkFailedSafe(&ASIOBufferedTalker::onNetworkFailed, this),
        socket(std::make_shared<boost::asio::ip::tcp::socket>(context)),
        writer(Writer::create_shared_ptr(socket, onWriteFinishedSafe, onNetworkFailedSafe)),
        reader(Reader::create_shared_ptr(socket, onReadFinishedSafe, onNetworkFailedSafe))
        {
            std::cout<<"ASIOBufferedTalker(&context)"<<std::endl;
        }


    //What should we do if move or copy constructor is called when async operation in progress?
    //Despite we implement a special mechanism of safe callbacks, we delete this constructors
    //because of the following reasons:
    //1. If move constructor is called when an async operation in progress, it may indicate the logical
    //error in the code (what shoud we do with pending async operation called for source object?
    //Abandon it? Or redirect callback to a new object (there is no way to do this in the current project)?
    //2. If copy constructor is called when an async operation in progress, what should we do with
    //this operation callbacks? How to copy the pernding async operation to a new object?
    ASIOBufferedTalker(const ASIOBufferedTalker&) = delete;
    ASIOBufferedTalker(ASIOBufferedTalker&&) = delete;

    ~ASIOBufferedTalker() {}

    //boost::asio::ip::tcp::socket& sock();
    void start()
    {
        reader->startReadAsync();
        status = Status::working;
    }

    void send(const Buffer& buf)
    {
        Buffer copy_of_buf(buf);
        send(std::move(copy_of_buf));
    }
    void send(Buffer&& buf)
    {
        if(writer->writeInProgress())
        {
            writeQueue.push(buf);
        } else {
            if(writeQueue.empty())
            {
                writer->send(std::move(buf));
            } else {
                writeFromBuffer();
            }
        }
    }

    bool receive(Buffer& buf)
    {
        if(readQueue.empty())
            return false;
        buf = std::move(readQueue.front());
        readQueue.pop();
        return true;
    }

    void close()
    {
        reader->close();
        writer->close();
        status = Status::closed;
    }

    //Status tracking routines:
    Status getStatus() const { return status; }

    void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> sock)
    {
        socket = sock;
        reader->setSocket(socket);
        writer->setSocket(socket);
    }

    bool isFinished() const
    {
        return status == Status::closed || status == Status::failed;
    }
    bool isActive() const
    {
        return status == Status::working;
    }
private:
    void onReadFinished(Header&& header, Buffer&& buf)
    {
       readQueue.emplace(std::move(buf));
       reader->startReadAsync();
    }
    void onWriteFinished(Buffer&& buf)
    {
        writeFromBuffer();
    }
    void writeFromBuffer()
    {
       if(writeQueue.empty())
           return;
       Buffer msg(std::move(writeQueue.front()));
       writeQueue.pop();
       writer->send(std::move(msg));
    }
    void onNetworkFailed(boost::system::error_code& ec, size_t len)
    {
       std::cout<<"ASIOtalker: "<<ec.message()<<std::endl;
       close();
       status = Status::failed;
    }
    CallbackProtector<decltype(&ASIOBufferedTalker::onReadFinished)> onReadFinishedSafe;
    CallbackProtector<decltype(&ASIOBufferedTalker::onWriteFinished)> onWriteFinishedSafe;
    CallbackProtector<decltype(&ASIOBufferedTalker::onNetworkFailed)> onNetworkFailedSafe;

    std::shared_ptr<boost::asio::ip::tcp::socket> socket;
    std::shared_ptr<Writer> writer;
    std::shared_ptr<Reader> reader;
    Status status = Status::notStarted;
    //PreallocatedBuffers<Buffer> preallocatedBuffers;
    std::queue<Buffer> readQueue;
    std::queue<Buffer> writeQueue;
};

class ASIOacceptor : public AsyncOperationWaiter<ASIOacceptor>
{
    friend class AsyncOperationWaiter<ASIOacceptor>;
public:
    ~ASIOacceptor()
    {
        acceptor.close();
    }

    void start_accept(port_t port)
    //This function can be used to stop current acceptor operation and start accepting on a new port
    {
        //First, we should to close current operation
        acceptor.close();
        //Then we have two alternatives:
        //Alternative 1: create new acceptor object (acceptor constructor will call open, bind and listen by itself)
        //acceptor = std::move(boost::asio::ip::tcp::acceptor(context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), mport)));
        //Alternative 2: create new endpoint object
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));
        acceptor.open(endpoint.protocol());
        acceptor.bind(endpoint);
        acceptor.listen();
        doAccept();
    }

    void start_accept()
    {
        doAccept();
    }

    bool acceptInProgress() { return acceptInProgress_;}
private:
    ASIOacceptor(boost::asio::io_context& context_, port_t port,
                 std::function<onAcceptedT> onNewConnection_,
                 std::function<onAcceptFailedT> onAcceptFailed_)
        : onNewConnection(onNewConnection_),
        onAcceptFailed(onAcceptFailed_),
        context(context_), acceptor(context_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {}

    void doAccept()
    {
        if(acceptInProgress_)
           return;
        acceptInProgress_ = true;
        auto self(shared_from_this());
        msocket = std::make_shared<boost::asio::ip::tcp::socket>(context);
        //endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), mport);
        acceptor.async_accept(*msocket, std::bind(&ASIOacceptor::onAcceptNewConnection, self, std::placeholders::_1)); //, std::placeholders::_2));
    }

    void onAcceptNewConnection(const boost::system::error_code& ec) //, boost::asio::ip::tcp::socket socket)
    {
        acceptInProgress_ = false;
        if(ec.failed())
        {
            if(onAcceptFailed)
                onAcceptFailed(ec);
            else
                throw std::runtime_error("tea::asiocommunicator::ASIOacceptor::onAcceptNewConnection: async_accept returned non-zero code");
        } else {
            onNewConnection(std::move(*msocket));
            msocket = nullptr;
            doAccept();
        }
    }

    std::shared_ptr<boost::asio::ip::tcp::socket> msocket;

    std::function<void(boost::asio::ip::tcp::socket&&)> onNewConnection;
    std::function<void(const boost::system::error_code& ec)> onAcceptFailed;
    boost::asio::io_context& context;
    boost::asio::ip::tcp::acceptor acceptor;
    bool acceptInProgress_{false};    
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

    ASIOresolver(boost::asio::io_context& io_context, std::function<onResolvedT> onResolved_, std::function<onResolveFailedT> onResolveFailed_)
        : onResolved(onResolved_), onResolveFailed(onResolveFailed_), resolver(io_context)
    { }

    void doResolve(const std::string& address, const std::string& port)
    {
        auto self(shared_from_this());
        resolver.async_resolve(address, port, std::bind(&ASIOresolver::onResolveFinished, self, std::placeholders::_1, std::placeholders::_2));
    }
    void onResolveFinished(const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type results)
    {
        if(ec.failed())
        {
            if(onResolveFailed)
            {
                onResolveFailed(ec);
            } else {
                throw std::runtime_error(std::string("Can't resolve host, error = ") + ec.message());
            }
        } else {
            onResolved(results);
        }
    }
    std::function<onResolvedT> onResolved;
    std::function<onResolveFailedT> onResolveFailed;
    boost::asio::ip::tcp::resolver resolver;
};

class ASIOconnecter : public AsyncOperationWaiter<ASIOconnecter>
{
    friend class AsyncOperationWaiter<ASIOconnecter>;
public:
    void connect(const boost::asio::ip::tcp::resolver::results_type& endpoints)
    {
        mendpoints = endpoints;
        doConnect();
    }
    std::shared_ptr<boost::asio::ip::tcp::socket> socket()
    {
        //return std::make_shared<boost::asio::ip::tcp::socket>(std::move(msocket));
        return msocket;
    }
private:
    ASIOconnecter(boost::asio::io_context& context_, std::function<onConnectedT> onConnected_, std::function<onConnectFailedT> onConnectFailed_)
        : context(context_), msocket(std::make_shared<boost::asio::ip::tcp::socket>(context_)),
        onConnected(onConnected_), onConnectFailed(onConnectFailed_) {}

    void doConnect()
    {
        auto self(shared_from_this());
        msocket = std::make_shared<boost::asio::ip::tcp::socket>(context);
        boost::asio::async_connect(*msocket, mendpoints, std::bind(&ASIOconnecter::onConnectFinished, self, std::placeholders::_1, std::placeholders::_2));
    }

    void onConnectFinished(const boost::system::error_code &ec, const typename boost::asio::ip::tcp::endpoint &ep)
    {        
        if(ec.failed())
        {
            if(onConnectFailed)
                onConnectFailed(ec);
            else
                throw std::runtime_error(std::string("Can't connect to host, error = ") + ec.message());
        } else {
            onConnected(ep);
        }
    }

    boost::asio::ip::tcp::resolver::results_type mendpoints;
    boost::asio::io_context& context;
    std::shared_ptr<boost::asio::ip::tcp::socket> msocket;
    std::function<onConnectedT> onConnected;
    std::function<onConnectFailedT> onConnectFailed;
};

class ASIOserver
///\todo remove client session if connection terminated
{
public:
    using error_code = boost::system::error_code;

    ASIOserver(const ASIOserver&) = delete;
    ASIOserver(boost::asio::io_context &context_ = GetContext::get()):
        mcontext(context_),
        onAcceptedSafe(&ASIOserver::onAccepted, this),
        onAcceptFailedSafe(&ASIOserver::onAcceptFailed, this)
    { }
    virtual ~ASIOserver() {}

    enum class Status {notStarted, Listening, notListening, closed, terminated};

    void startAccept(port_t port)
    {
        acceptor = ASIOacceptor::create_shared_ptr(mcontext, port, onAcceptedSafe, onAcceptFailedSafe);
        acceptor->start_accept();
    }

    void send(size_t sessionID, const Buffer& msg)
    {
        sessions.at(sessionID).send(msg);
    }

    void send(size_t sessionID, Buffer&& msg)
    {
        sessions.at(sessionID).send(std::move(msg));
    }

    bool receiveAnySession(size_t &sessionID, Buffer& buf)
    {
        for(auto& [session, talker]: sessions)
        {
            if(talker.receive(buf))
            {
                sessionID = session;
                return true;
            }
        }
        return false;
    }
    bool receiveBySessionID(size_t sessionID, Buffer& buf)
    {
        return sessions.at(sessionID).receive(buf);
    }
    std::vector<size_t> getActiveSessions()
    {
        std::vector<size_t> activeSessions;
        activeSessions.reserve(sessions.size());
        for(const auto &it : sessions)
            activeSessions.push_back(it.first);
        return activeSessions;
    }
    std::vector<size_t> getNewSessions()
    {
        std::vector<size_t> res(std::move(newSessions));
        newSessions.clear();
        return res;
    }
    size_t getActiveSessionsCount() const
    {
        size_t count = 0;
        for(const auto &it : sessions)
            if(it.second.isActive())
                count++;
        return count;
    }
    void close()
    {
        for(auto &it : sessions)
            it.second.close();
        sessions.clear();
    }
    void close(size_t sessionID)
    {
        auto it = sessions.find(sessionID); //we assume that such element exists
        it->second.close();
        sessions.erase(it);
    }
    void poll()
    {
        mcontext.poll();
    }

    void removeFinishedSessions()
    {
        std::vector<size_t> sessionsToDel;
        for(const auto &it : sessions)
            if(it.second.getStatus() > ASIOBufferedTalker::Status::working)
                sessionsToDel.push_back(it.first);
        for(const size_t session : sessionsToDel)
        {
            std::cout<<"Removing session "<<session<<std::endl;
            sessions.erase(session);
        }
    }

private:
    std::vector<size_t> newSessions;    

    size_t sessionCounter{0};



    void onAccepted(boost::asio::ip::tcp::socket &&socket)
    {
        std::cout<<"Accepted connection"<<std::endl;
        sessions.emplace(sessionCounter, std::make_shared<boost::asio::ip::tcp::socket>(std::move(socket)));
        newSessions.push_back(sessionCounter);
        sessions.at(sessionCounter).start();
        sessionCounter++;

    }
    void onAcceptFailed(const boost::system::error_code& ec)
    {
        std::cout<<ec.message()<<std::endl;
        //nothing to do
    }

    boost::asio::io_context& mcontext;

    CallbackProtector<decltype(&ASIOserver::onAccepted)> onAcceptedSafe;
    CallbackProtector<decltype(&ASIOserver::onAcceptFailed)> onAcceptFailedSafe;    

    std::map<size_t, ASIOBufferedTalker> sessions;
    std::shared_ptr<ASIOacceptor> acceptor;

};

class ASIOclient
{
public:
    enum class Status
    {
        notinitialized, resolving, connecting, connectedOk, disconnected,
        resolvingFailed, connectingFailed
    };

    ASIOclient(boost::asio::io_context &context = GetContext::get()) :
        status(Status::notinitialized),
        mcontext(context),
        onResolvedSafe(&ASIOclient::onResolved, this), onResolveFailedSafe(&ASIOclient::onResolveFailed, this),
        onConnectedSafe(&ASIOclient::onConnected, this), onConnectFailedSafe(&ASIOclient::onConnectFailed, this),
        resolver(ASIOresolver::create_shared_ptr(context, onResolvedSafe, onResolveFailedSafe)),
        connecter(ASIOconnecter::create_shared_ptr(context, onConnectedSafe, onConnectFailedSafe)),
        talker(context) { }

    void connect(const std::string &address, uint16_t port)
    {
        mport = port;
        maddress = address;        
        doResolveAndConnect();
    }

    void sync_connect(const std::string &address, uint16_t port)
    {
        connect(address, port);
        while(status >= Status::notinitialized && status <= Status::connecting)
            poll();
    }

    void send(const Buffer& msg)
    {
        talker.send(msg);
    }

    void send(Buffer&& msg)
    {
        talker.send(std::move(msg));
    }

    bool receive(Buffer& buf)
    {
        return talker.receive(buf);
    }

    void close() { talker.close(); }
    void poll() { mcontext.poll(); }

    Status getStatus() { return status; }
private:
    Status status;

    void updateStatus();
    boost::asio::io_context &mcontext;
    uint16_t mport;
    std::string maddress;

    void doResolveAndConnect()
    {
        status = Status::notinitialized;
        resolver->resolve(maddress, std::to_string(mport));
    }

    void onResolved(const boost::asio::ip::tcp::resolver::results_type& endpoints)
    {
        status = Status::connecting;
        connecter->connect(endpoints);
    }

    void onResolveFailed([[maybe_unused]] const boost::system::error_code &ec)
    {
        status = Status::resolvingFailed;
    }

    void onConnected([[maybe_unused]] const typename boost::asio::ip::tcp::endpoint &ep)
    {
        auto s = connecter->socket();
        status = Status::connectedOk;
        talker.setSocket(s);
        talker.start();
    }
    void onConnectFailed([[maybe_unused]] const boost::system::error_code &ec)
    {
        std::cout<<"Connect failed: "<<ec.message()<<std::endl;
        status = Status::connectingFailed;
    }

    CallbackProtector<decltype(&ASIOclient::onResolved)> onResolvedSafe;
    CallbackProtector<decltype(&ASIOclient::onResolveFailed)> onResolveFailedSafe;
    CallbackProtector<decltype(&ASIOclient::onConnected)> onConnectedSafe;
    CallbackProtector<decltype(&ASIOclient::onConnectFailed)> onConnectFailedSafe;
    std::shared_ptr<ASIOresolver> resolver;
    std::shared_ptr<ASIOconnecter> connecter;
    ASIOBufferedTalker talker;
};

} /* namespace detail */

template<class T>
concept IsBuffer = std::same_as<Buffer, T>;

template<class T>
concept NotBuffer = !IsBuffer<T>;


class Server : public detail::ASIOserver
{
public:
    template<typename ... Args>
    Server(Args&& ... args) : detail::ASIOserver(std::forward<Args>(args)...) {}

    template<typename MsgType> requires NotBuffer<MsgType>
    void send(size_t sessionID, const MsgType& msg)
    {
        Buffer buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        detail::ASIOserver::send(sessionID, std::move(buf));
    }

    template<typename MsgType> requires NotBuffer<MsgType>
    bool receiveAnySession(size_t &sessionID, MsgType& msg)
    {
        Buffer buf;
        if(detail::ASIOserver::receiveAnySession(sessionID, buf))
        {
            if(buf.size() != sizeof(msg))
                throw "incorrect message size";
            std::memcpy(&msg, buf.data(), buf.size());
            return true;
        };
        return false;
    }

    template<typename MsgType> requires NotBuffer<MsgType>
    bool receiveBySessionID(size_t sessionID, MsgType& msg)
    {
        Buffer buf;
        if(detail::ASIOserver::receiveBySessionID(sessionID, buf))
        {
            if(buf.size() != sizeof(msg))
                throw "incorrect message size";
            std::memcpy(&msg, buf.data(), buf.size());
            return true;
        };
        return false;
    }

};


class Client : public detail::ASIOclient
{
public:
    template<typename ... Args>
    Client(Args&& ... args) : detail::ASIOclient(std::forward<Args>(args)...) {}


    template<typename MsgType> requires NotBuffer<MsgType>
    void send(const MsgType& msg)
    {
        Buffer buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        detail::ASIOclient::send(std::move(buf));
    }

    template<typename MsgType> requires NotBuffer<MsgType>
    bool receive(MsgType& msg)
    {
        Buffer buf;
        if(detail::ASIOclient::receive(buf))
        {
            if(buf.size() != sizeof(msg))
                throw "incorrect message size";
            std::memcpy(&msg, buf.data(), buf.size());
            return true;
        };
        return false;
    }
};

} /* namespace asiocommunicator */
} /* namespace tea */

#endif // ASIOCOMMUNICATOR_H

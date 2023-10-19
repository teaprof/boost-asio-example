#ifndef ASIOCOMMUNICATOR_H
#define ASIOCOMMUNICATOR_H

#include <boost/asio.hpp>
#include <boost/smart_ptr.hpp>
#include <memory>
#include <queue>
#include <map>
#include <type_traits>
#include <functional>

namespace tea {

namespace asiocommunicator {

using byte = uint8_t;
using Buffer = std::vector<byte>;

namespace detail {

struct Header
{
    //Header should start with the following code:
    static constexpr uint32_t headerCodeBegin = 0x1234;

    uint32_t code; //should be equal to headerCodeBegin
    uint32_t msglen;  //len of the message in bytes
    Header() : code(headerCodeBegin), msglen(0) {}
    void* data() { return &code; }
    size_t headerLen() { return sizeof(Header); }

    void initialize(const Buffer& body)
    {
        code = headerCodeBegin;
        msglen = body.size();
    }

};

class Writer: public std::enable_shared_from_this<Writer>
{
public:
    Writer(std::function<void(Buffer&&)> onWriteFinished, std::function<void(boost::system::error_code, size_t)> onNetworkFailed);

    void send(Buffer&& Buf) { std::swap(Buf, body); }

    boost::system::error_code errorCode() { return error_code; };
    bool writeInProgress() { return write_in_progress; };
private:
    bool write_in_progress = false;
    boost::system::error_code error_code;
    std::function<void(Buffer&&)> onWriteFinished;
    std::function<void(boost::system::error_code ec, size_t len)> onNetworkFailed;

    Header header;
    Buffer body;

    void doWriteHeader()
    {
        write_in_progress = true;
        auto self(shared_from_this());
        header.initialize(body);
        boost::asio::async_write(socket,
                                 boost::asio::buffer(header.data(), header.headerLen()),
                                 [this, self](boost::system::error_code ec, size_t len)
                                 {
                                     onWriteHeaderFinished(ec, len);
                                 });
    }

    void onWriteHeaderFinished(boost::system::error_code ec, size_t len)
    {
        error_code = ec;
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
        boost::asio::async_write(socket,
                                 boost::asio::buffer(body.data(), body.size()),
                                 [this, self](boost::system::error_code ec, size_t len)
                                    {
                                        onWriteBodyFinished(ec, len);
                                 });
    }

    void onWriteBodyFinished(boost::system::error_code ec, size_t len)
    {
        write_in_progress = false;
        error_code = ec;
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

class Reader: public std::enable_shared_from_this<Reader>
{
public:
    std::shared_ptr<Reader> createReader(std::function<void(Header&&, Buffer&&)> onReadFinished, std::function<void(boost::system::error_code, size_t len)> onNetworkFailed);
    void startReadAsync()
    {
        read_in_progress = true;
        doReadHeader();
    }
    boost::system::error_code errorCode() { return error_code; };
    bool readInProgress() {return read_in_progress; };
protected:
    Reader(std::function<void(Buffer&&)> onReadFinished, std::function<void()> onNetworkFailed);
private:
    bool read_in_progress = false;
    boost::system::error_code error_code;
    //callbacks:
    std::function<void(Header&&, Buffer&&)> onReadFinished;
    std::function<void(boost::system::error_code, size_t len)> onNetworkFailed;
    Header header;
    Buffer body;

    void doReadHeader()
    {
        //at least one shared_ptr to 'this' pointer in your program should exists
        //before this line, otherwise bad_weak_ptr exception will be thrown
        auto self(shared_from_this());

        boost::asio::async_read(socket,
                            boost::asio::buffer(header.data(), header.headerLen()),
                                [this, self](boost::system::error_code ec, size_t len)
                                {
                                    onReadHeaderFinished(ec, len);
                                });
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
        boost::asio::async_read(socket,
                                boost::asio::buffer(body.data(), body.size()),
                                [this, self](boost::system::error_code ec, size_t len)
                                {
                                    onReadBodyFinished(ec, len);
                                });

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
    }
};


//ASIOtalker is a class that implements sending and receiving messages.
class ASIOtalker : public std::enable_shared_from_this<ASIOtalker>
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

    //the only way to create such object is to call ASIOtalker::create, which creates a
    //shared pointer to ASIOtalker object
    ASIOtalker() = delete;
    ASIOtalker(const ASIOtalker&) = delete;
    static std::shared_ptr<ASIOtalker> create(boost::asio::io_context& context);

    ~ASIOtalker();


    boost::asio::ip::tcp::socket& sock();
    void start();

    void send(const Buffer& buf)
    {
        Buffer copy_of_buf(buf);
        send(std::move(buf));
    }
    void send(Buffer&& buf)
    {
        if(writer.writeInProgress())
        {
            writeQueue.push(buf);
        } else {
            if(writeQueue.empty())
            {
                writer.send(std::move(buf));
            } else {
                writeFromBuffer();
            }
        }
    }

    bool receive(Buffer& buf);


    void close();

    //Status tracking routines:
    Status getStatus() { return status; }
    void registerCallback(std::function<void(Status)> &&func); //func will be called when status is updated
protected:
    ASIOtalker(boost::asio::io_context& context) :
        writer(onWriteFinished, onNetworkFailed),
        reader(onReadFinished, onNetworkFailed) {};
private:
    static void onWriteFinished(std::weak_ptr<ASIOtalker> talker, Buffer&& buf)
    {
        if(talker.expired())
            return;
        talker.lock()->writeFromBuffer();
    }
    void writeFromBuffer()
    {
        if(writeQueue.empty())
            return;
        Buffer msg = writeQueue.front();
        writeQueue.pop();
        writer.send(std::move(msg));
    }
    static void onNetworkFailed(std::weak_ptr<ASIOtalker> talker, boost::system::error_code, size_t len)
    {
        if(talker.expired())
            return;
        talker.lock()->status = Status::failed;
        //do we need to close reader and writer?
    }
    static void onReadFinished(std::weak_ptr<ASIOtalker> talker, Buffer&& buf)
    {
        if(talker.expired())
            return;
        auto self = talker.lock();
        self->readQueue.emplace(std::move(buf));
        self->reader.startReadAsync();
    }
private:
    Reader reader;
    Writer writer;
    Status status = Status::notStarted;
    //PreallocatedBuffers<Buffer> preallocatedBuffers;
    std::queue<Buffer> readQueue;
    std::queue<Buffer> writeQueue;

    void setStatus(ASIOtalker::Status newStatus);
    std::function<void(Status)> onStatusUpdatedCallback;

    //potentially, this function can clear error_code.fail flag
    void onNetworkFail(boost::system::error_code& ec);

    boost::asio::ip::tcp::socket socket;
};



class ASIOserver : public std::enable_shared_from_this<ASIOserver>
///\todo remove client session if connection terminated
//You should create shared_ptr to this class before calling any method. In other way bad_weak_ptr exception
//will be raised.
{
public:
    using error_code = boost::system::error_code;

    ASIOserver(const ASIOserver&) = delete;
    static std::shared_ptr<ASIOserver> create(uint16_t port);
    virtual ~ASIOserver();

    void startAccept();

    void send(size_t sessionID, const Buffer& msg);
    void send(size_t sessionID, Buffer&& msg);

    bool receiveAnySession(size_t &sessionID, Buffer& buf); //pair.first = sessionID, pair.second = message
    bool receiveBySessionID(size_t sessionID, Buffer& buf); //pair.first = sessionID, pair.second = message
    std::vector<size_t> getActiveSessions();

    void close();
    void poll();
protected:
    ASIOserver(uint16_t port);
private:
    std::vector<byte> buf;

    void onTalkerStatusUpdated(size_t SessiodID, ASIOtalker::Status status);

    uint16_t mport;
    size_t sessionCounter;
    std::map<size_t, std::shared_ptr<ASIOtalker>> sessions;

    std::shared_ptr<ASIOtalker> listener;

    boost::asio::ip::tcp::endpoint ep;
    boost::asio::ip::tcp::acceptor acceptor;
    boost::asio::ip::tcp::socket sock;
};

class ASIOclient : public std::enable_shared_from_this<ASIOclient>
{
public:
    enum class Status
    {
        notinitialized, resolving, connecting, connected, disconnected,
        resolvingFailed, connectingFailed
    };

    static std::shared_ptr<ASIOclient> create();
    static std::shared_ptr<ASIOclient> create(const std::string &address, uint16_t port);
    ASIOclient(const ASIOclient&) = delete;
    ~ASIOclient();            

    void connect(const std::string &address, uint16_t port);

    void send(const Buffer& msg);
    void send(Buffer&& msg);

    bool receive(Buffer& buf);

    void close();
    void poll();

    Status getStatus();
protected:
    ASIOclient();
private:
    Status status;
    void doResolve();
    void doConnect();

    void updateStatus();
    uint16_t mport;
    std::string maddress;
    std::shared_ptr<ASIOtalker> talker;
    boost::asio::ip::tcp::resolver resolver;
    boost::asio::ip::tcp::resolver::results_type endpoints;    
};

} /* namespace detail */


class Client
{
public:
    Client()
    {
        mClient = detail::ASIOclient::create();
    }
    ~Client() { }

    void connect(const std::string &address, uint16_t port)
    {
        mClient->connect(address, port);
    }

    void send(const Buffer& msg)
    {
        mClient->send(msg);
    }

    bool receive(Buffer& buf)
    {
        return mClient->receive(buf);
    }

    bool isFailed();

    bool isConnected();

    void close()
    {
        mClient->close();
    }

    void poll()
    {
        mClient->poll();
    }

private:
    std::shared_ptr<detail::ASIOclient> mClient;
};

class Server
{
public:
    Server(uint16_t port)
    {
        mServer = detail::ASIOserver::create(port);
    }

    void startAccept()
    {
        mServer->startAccept();
    }

    void send(size_t sessionID, const Buffer& msg)
    {
        mServer->send(sessionID, msg);
    }
    void send(size_t sessionID, Buffer&& msg)
    {
        mServer->send(sessionID, std::move(msg));
    }

    template<class T>
    std::enable_if_t<std::is_pod_v<T>, void>
    sendcopy(size_t sessionID, const T& msg)
    {
        Buffer buf(sizeof(T));
        std::memcpy(buf.data(), &msg, sizeof(T));
        mServer->send(sessionID, std::move(buf));
    }

    bool receiveAnySession(size_t &sessionID, Buffer& buf)
    {
        return mServer->receiveAnySession(sessionID, buf);
    }

    template<class T>
    std::enable_if_t<std::is_pod_v<T>, bool>
    receiveAnySession(size_t &sessionID, T& msg)
    {
        Buffer buf;
        bool res = mServer->receiveAnySession(sessionID, buf);
        if(res)
            std::memcpy(&msg, buf.data(), sizeof(T));
        return res;
    }

    bool receiveBySessionID(size_t sessionID, Buffer& buf)
    {
        return mServer->receiveBySessionID(sessionID, buf);
    }

    template<class T>
    std::enable_if_t<std::is_pod_v<T>, bool>
    receiveBySessionID(size_t sessionID, T& msg)
    {
        Buffer buf;
        bool res = mServer->receiveBySessionID(sessionID, buf);
        if(res)
            std::memcpy(&msg, buf.data(), sizeof(T));
        return res;
    }

    std::vector<size_t> getActiveSessions()
    {
        return mServer->getActiveSessions();
    }

    void close()
    {
        mServer->close();
    }

    void poll()
    {
        mServer->poll();
    }
private:
    std::shared_ptr<detail::ASIOserver> mServer;
};



boost::asio::io_context& getContext();

} /* namespace asiocommunicator */
} /* namespace tea */


#endif // ASIOCOMMUNICATOR_H

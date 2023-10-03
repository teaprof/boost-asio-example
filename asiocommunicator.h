#ifndef ASIOCOMMUNICATOR_H
#define ASIOCOMMUNICATOR_H

#include <boost/asio.hpp>
#include <boost/smart_ptr.hpp>
#include <memory>
#include <queue>
#include <map>
#include <type_traits>

namespace tea {

namespace asiocommunicator {

using byte = uint8_t;
using Buffer = std::vector<byte>;

namespace detail {

//This class contains a list of Containers like std::vector and finds a
//container with most appropriate capacity on demand. In this way, using of this
//class can reduce the number of system calls such as malloc and free (new and delete).
//The internal logic is very simple, it works good when maxTotalCapacity is
//greater than average container capacity, and capacity is approximately
//the same for all containers.
template<class Container=Buffer>
class PreallocatedBuffers
{
public:
    PreallocatedBuffers(size_t _maxTotalCapacity = 1<<20) :
        maxTotalCapacity(_maxTotalCapacity), curTotalCapacity(0)
    {

    }

    void push(Container&& buf)
    {
        if(curTotalCapacity + buf.capacity() < maxTotalCapacity)
        {
            mBuffers.emplace_back(std::move(buf));
            curTotalCapacity += buf.capacity();
        }
    }
    void find(size_t minimalSize, Container& buf)
    {
        size_t bestIdx = 0, bestSize = 0;
        bool found = false;
        //find container with minimal size but not less than minimalSize
        for(size_t curIdx = 0; curIdx < mBuffers.size(); curIdx++)
        {
            size_t curSize = mBuffers[curIdx].size();
            if(curSize >= minimalSize)
            {
                if(found)
                {
                    if(curSize < bestSize)
                    {
                        bestIdx = curIdx;
                        bestSize = curSize;
                        if(bestSize == minimalSize)
                            break;
                    }
                } else {
                    found = true;
                    bestIdx = curIdx;
                    bestSize = curSize;
                    if(bestSize == minimalSize)
                        break;
                }
            }
        }
        if(found)
        {
            auto& mybuf = mBuffers[bestIdx];
            curTotalCapacity -= mybuf.capacity();
            if(curTotalCapacity + buf.capacity() <= maxTotalCapacity)
            {
                curTotalCapacity += buf.capacity();
                buf.swap(mybuf);
            } else {
                buf.swap(mybuf);
                mBuffers.erase(mBuffers.begin() + bestIdx);
            }
        } else {
            buf.reserve(minimalSize);
        }
    }
private:
    size_t maxTotalCapacity, curTotalCapacity;
    std::vector<Container> mBuffers;
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

    void send(const Buffer& buf);
    void send(Buffer&& buf);

    bool receive(Buffer& buf);


    void close();

    Status getStatus() { return status; }

    void registerCallback(std::function<void(Status)> &&func);
protected:
    ASIOtalker(boost::asio::io_context& context);
private:
    void doReadHeader();
    void doReadBody();
    void onReadBodyFinished();

    void doWriteHeader();
    void doWriteBody();
    void onWriteBodyFinished();
private:
    using msgHeaderType = uint32_t[2]; //msgHeaderType[0] is a special magic const to identify that header began, msgHeaderType[1] is a size of a following message
    Status status;
    boost::asio::ip::tcp::socket socket;
    bool writeInProgress;

    void initializeHeader(msgHeaderType& header, const Buffer& msg)
    {
        header[0] = 123; //magic constant identifying the beginning of the header
        header[1] = msg.size();
    }

    void allocateReadBuf()
    {
        if(currentReadHeader[0] != 123)
            throw std::runtime_error("ASIOtalker::validateHeader: unexpected value, can't parse header");
        currentReadbuf.resize(currentReadHeader[1]);
    }

    msgHeaderType currentReadHeader, currentWriteHeader;
    Buffer currentReadbuf; //buffer for an incoming message that is currently being received
    std::queue<Buffer> outgoingQueue; //buffer of outgoing messages that are ready to be sent
    std::queue<Buffer> incomingQueue; //buffer of incoming messages that are already received

    PreallocatedBuffers<Buffer> preallocatedBuffers;

    void setStatus(ASIOtalker::Status newStatus);
    std::function<void(Status)> onStatusUpdatedCallback;

    //potentially, this function can clear error_code.fail flag
    void onNetworkFail(boost::system::error_code& ec);
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

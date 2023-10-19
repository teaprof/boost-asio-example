/// \todo rename file to abstractTCPIPcommunicator
/// other files could be renamed to: abstractFileSystemCommunicator, GRIDbasecommunicator, GRIDTCPIPcommunicator, GRIDfilesystemcommunicator
#include<boost/bind/bind.hpp>
#include<iostream>
#include<vector>
#include<cstdlib>
#include<limits>
#include<string>
#include "asiocommunicator.hpp"

namespace tea {
namespace asiocommunicator {

auto &log_trace = std::cout;
auto &log_info = std::cout;
auto &log_error = std::cerr;
std::ostream& (&logendl)(std::ostream&) = std::endl;

boost::asio::io_context& getContext()
{
    static boost::asio::io_context context;
    return context;
}


namespace detail {

/*!
 * \brief This struct is used to check if async operation finished with error or not.
 * \details This struct contains method checkAndLock::check to check
 *
 * Use сheckAndLock::check method of this struct in the beggining of async operation
 * completion handler to do some job related to error analysis and weak_ptr of calling object lock.
 */
struct CheckError
{
    enum class opcode
    {
        accept = 0, resolve, connect, readHeader, readBody, writeHeader, writeBody
    };


    static const char* getopname(opcode code)
    {
        switch(code)
        {
        case opcode::accept: return "ACCEPT";
        case opcode::resolve: return "RESOLVE";
        case opcode::connect: return "CONNECT";
        case opcode::readHeader: return "READHEADER";
        case opcode::readBody: return "READBODY";
        case opcode::writeHeader: return "WRITEHEADER";
        case opcode::writeBody: return "WRITEBODY";
        }
        return nullptr;
    }


    /*! @brief Analyzes error code ec
    *   @details This function do following job:
    * - check if wptr to T is valid
    * - check error code in both cases (different cases for valid and invalid wptr)
    *   Type T could be ASIOtalker, ASIOserver or ASIOclient
    * @return return pair (ptr, status) ptr = wptr.lock() and status can be treated as
    * a new status for connection.
    * If all checks are succeeded then ptr is not null and status is ASIOtalker::Status::connectionOk.
    */
    template<class CommunticatorObjectType> //CommunicatorObjectType could be ASIOtalker, ASIOserver or ASIOclient
    static bool check(std::weak_ptr<CommunticatorObjectType> wptr, const boost::system::error_code &ec, opcode op)
    {
        if(wptr.expired())
        {
            //communicator object *wptr is destroyed already, but asynchronous operation have not been finished.
            if(ec.value() == boost::system::errc::success) //no matter which category the value ec.category() belongs to
            {
                //in mostly cases it is Ok: since the object is destroyed the results of asynchronous operation is not
                //interesting for caller and should be ignored.
                log_info<<"Object destroyed but connection was still alive, operation = "<<getopname(op)<<logendl;
            } else {
                //it's Ok, object is destroyed and some pending IO operations returns with error codes
                std::stringstream str;
                str<<"Network operation "<<getopname(op);
                str<<" terminated unexpectedly after object already destroyed, error code = "<<ec<<", msg = "<<ec.message();
                log_error<<str.str()<<logendl;
                throw std::runtime_error(str.str());
            }
            //return detail::ASIOtalker::Status::destroyed;
            return false;
        } else {
            auto status = detail::ASIOtalker::Status::working;
            //object is not destroyed, analyze the error
            if(ec.value() != boost::system::errc::success)
            {
                bool errorHandled = false;
                if(ec.category() == boost::asio::error::get_misc_category() && ec.value() == boost::asio::error::eof)
                {
                    log_info<<"Connection closed"<<logendl;
                    status = detail::ASIOtalker::Status::closed;
                    errorHandled = true;
                }
                //here other error handlers could be placed
                //...
                if(!errorHandled)
                {
                    status = detail::ASIOtalker::Status::failed;
                    log_info<<"Network operation "<<getopname(op)<<" terminated unexpectedly,"<<
                        "error category: "<<ec.category().name()<<", "<<
                        "error value: "<<ec.value()<<", "<<
                        "message: "<<ec.message()<<logendl;
                }
            }
            //return status;
            return true;
        }
    }
};

std::shared_ptr<ASIOtalker> ASIOtalker::create(boost::asio::io_context& context)
{
    //since ASIOtalker::ASIOtalker(context) is protected, we shoud use this work-around to access it:
    struct make_shared_enabler : public ASIOtalker
    {
        make_shared_enabler(boost::asio::io_context& context) : ASIOtalker(context) { }
    };
    return std::make_shared<make_shared_enabler>(context);
}

ASIOtalker::ASIOtalker(boost::asio::io_context& context) :
    status(ASIOtalker::Status::notStarted), socket(context), writeInProgress(false)
{
    //nothing to do
}
ASIOtalker::~ASIOtalker()
{
    //nothing to do
}

void ASIOtalker::start()
{
    setStatus(ASIOtalker::Status::working);
    doReadHeader();
    doWriteHeader();
}

bool ASIOtalker::receive(Buffer& buf)
{
    if(!incomingQueue.empty())
    {
        buf.swap(incomingQueue.front());
        incomingQueue.pop();
        return true;
    } else {
        return false;
    }
}

void ASIOtalker::send(const Buffer &buf)
{
    outgoingQueue.push(buf);
    if(!writeInProgress)
        doWriteHeader();
}

void ASIOtalker::send(Buffer &&buf)
{
    outgoingQueue.emplace(std::move(buf));
    if(!writeInProgress)
        doWriteHeader();
}

template<class T>
void callBack(boost::system::error_code ec, size_t len, std::shared_ptr<T> self,
              std::function<void(T&)> onSuccessCallBack,
              std::function<void(T&, boost::system::error_code& ec)> onFailCallBack)
{
    (void)len;
    if(self.expired() == false)
    {
        auto obj = self.lock();
        if(ec.failed())
            onFailCallBack(*obj, ec); //this function can clear 'fail' flag
        if(!ec.failed())
            onSuccessCallBack(*obj);
    } else {
        if(ec.failed())
            log_info<<"ec.failed"<<std::endl;
    }
}

void ASIOtalker::doWriteHeader()
{
    auto self(shared_from_this());

    if(status == ASIOtalker::Status::working && !outgoingQueue.empty())
    {
        writeInProgress = true;
        auto& msgBuffer = outgoingQueue.front();
        initializeHeader(currentWriteHeader, msgBuffer);
        auto callback = std::bind(callBack<ASIOtalker>, std::placeholders::_1, std::placeholders::_2, self,
                                  &ASIOtalker::doWriteBody, &ASIOtalker::onNetworkFail);
        boost::asio::async_write(socket,
                                 boost::asio::buffer(std::begin(currentWriteHeader), sizeof(currentWriteHeader)),
                                 callback);
    };
}

void ASIOtalker::doWriteBody()
{
    //at least one shared_ptr to 'this' pointer in your program should exists
    //before this line, otherwise bad_weak_ptr exception will be thrown
    auto self(shared_from_this());

    if(status == ASIOtalker::Status::working && !outgoingQueue.empty())
    {
        assert(writeInProgress);
        auto& msgBuffer = outgoingQueue.front();
        auto callback = std::bind(callBack<ASIOtalker>, std::placeholders::_1, std::placeholders::_2, self,
                                  &ASIOtalker::onWriteFinished, &ASIOtalker::onNetworkFail);
        boost::asio::async_write(socket,
                                 boost::asio::buffer(msgBuffer.data(), msgBuffer.size()),
                                 callback);
    }
}

void ASIOtalker::onWriteFinished()
{
    preallocatedBuffers.push(std::move(outgoingQueue.front()));
    outgoingQueue.pop();
    if(!outgoingQueue.empty())
        //writequeue is not empty, initiate sending of the next queued message
        doWriteHeader();
    else
        writeInProgress = false;
}

void ASIOtalker::doReadHeader()
{
    //at least one shared_ptr to 'this' pointer in your program should exists
    //before this line, otherwise bad_weak_ptr exception will be thrown
    auto self(shared_from_this());

    auto callback = std::bind(callBack<ASIOtalker>, std::placeholders::_1, std::placeholders::_2, self,
                              &ASIOtalker::doReadBody, &ASIOtalker::onNetworkFail);
    boost::asio::async_read(socket,
                            boost::asio::buffer(std::begin(currentReadHeader), sizeof(currentReadHeader)),
                            callback);
}

void ASIOtalker::doReadBody()
{
    //at least one shared_ptr to 'this' pointer in your program should exists
    //before this line, otherwise bad_weak_ptr exception will be thrown
    auto self(shared_from_this());

    allocateReadBuf();
    auto callback = std::bind(callBack<ASIOtalker>, std::placeholders::_1, std::placeholders::_2, self,
                              &ASIOtalker::onReadFinished, &ASIOtalker::onNetworkFail);
    boost::asio::async_read(socket,
                            boost::asio::buffer(currentReadbuf.data(), currentReadbuf.size()),
                            callback);
}

void ASIOtalker::onReadFinished()
{
    incomingQueue.emplace(std::move(currentReadbuf));
    doReadHeader();
}

void ASIOtalker::close()
{
    boost::system::error_code ec;
    if(socket.is_open())
    {
        socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if(ec != boost::system::errc::success)
            log_error<<"Error while shutdown socket: "<<ec.message()<<logendl;
    };
    socket.close(ec);
    if(ec != boost::system::errc::success)
        log_error<<"Error while shutdown socket: "<<ec.message()<<logendl;
    setStatus(Status::closed);
}

void ASIOtalker::onNetworkFail(boost::system::error_code& ec)
{
    if(ec.failed())
        setStatus(Status::failed;
}

boost::asio::ip::tcp::socket& ASIOtalker::sock()
{
    return socket;
}


void ASIOtalker::registerCallback(std::function<void(ASIOtalker::Status)> &&func)
{
    onStatusUpdatedCallback = std::move(func);
}

void ASIOtalker::setStatus(ASIOtalker::Status newStatus)
{
    if(newStatus != status)
    {
        status = newStatus;
        if(onStatusUpdatedCallback)
            onStatusUpdatedCallback(newStatus);
    }
}

//**********************  ASIOServer  ******************************************

std::shared_ptr<ASIOserver> ASIOserver::create(uint16_t port)
{
    struct make_shared_enabler : public ASIOserver
    {
        make_shared_enabler(uint16_t port) : ASIOserver(port) { }
    };
    return std::make_shared<make_shared_enabler>(port);
}

ASIOserver::ASIOserver(uint16_t port) :
    mport(port), ep(boost::asio::ip::tcp::v4(), port),
    acceptor(getContext(), ep), sock(getContext())
{
    sessionCounter = 0;
}

ASIOserver::~ASIOserver()
{
}

void ASIOserver::startAccept()
{
    log_trace<<"ASIOserver::start_accept";
    //at least one shared_ptr to 'this' pointer in your program should exists
    //before this line, otherwise bad_weak_ptr exception will be thrown
    auto self(shared_from_this());
    listener = ASIOtalker::create(getContext());

    auto curSessionID = sessionCounter++;
    acceptor.async_accept(listener->sock(),
        [this, self, curSessionID](const error_code &ec)
        {
            auto [ptr, newStatus] = CheckError::check(self, ec, CheckError::opcode::accept);
            if(newStatus == ASIOtalker::Status::working && ptr)
            {
                assert(listener != nullptr);
                listener->start();
                sessions.insert(std::make_pair(curSessionID, listener));
                std::function<void(ASIOtalker::Status)> fcn = std::bind(&ASIOserver::onTalkerStatusUpdated, this, curSessionID, std::placeholders::_1);
                listener->registerCallback(std::move(fcn));
                log_info<<"Server successfully accepted connection";
                startAccept(); //start accepting next client
            }
        });
}

void ASIOserver::send(size_t sessionID, const Buffer& msg)
{
    auto client = sessions.find(sessionID);
    if(client != sessions.end())
    {
        client->second->send(msg);
    } else {
        log_error<<"Unknown session ID"<<logendl;
    }
}

void ASIOserver::send(size_t sessionID, Buffer&& msg)
{
    auto client = sessions.find(sessionID);
    if(client != sessions.end())
    {
        client->second->send(msg);
    } else {
        std::stringstream str;
        str<<"ASIOserver::send: Unknown session ID "<<sessionID;
        throw std::runtime_error(str.str());
    }
}

bool ASIOserver::receiveAnySession(size_t &sessionID, Buffer& msg)
{
    for(const auto& p : sessions)
        if(p.second->receive(msg))
        {
            sessionID = p.first;
            return true;
        }
    return false;
}

bool ASIOserver::receiveBySessionID(size_t sessionID, Buffer& buf)
{
    auto client = sessions.find(sessionID);
    if(client != sessions.end())
    {
        return client->second->receive(buf);
    } else {
        std::stringstream str;
        str<<"ASIOserver::receiveBySessionID: Unknown session ID "<<sessionID;
        throw std::runtime_error(str.str());
    }
}


std::vector<size_t> ASIOserver::getActiveSessions()
{
std::vector<size_t> res;
    for(const auto &p : sessions)
        res.push_back(p.first);
    return res;
}

void ASIOserver::close()
{
    poll();
    if(listener)
        listener->close();
    for(const auto& it : sessions)
        it.second->close();
}

void ASIOserver::poll()
{
    getContext().poll();
}

void ASIOserver::onTalkerStatusUpdated(size_t sessionID, ASIOtalker::Status status)
{
    bool needToDelete = false;
    switch(status)
    {
        case ASIOtalker::Status::notStarted:
            log_info<<"Session #"<<sessionID<<" not started";
            break;
        case ASIOtalker::Status::working:
            log_info<<"Session #"<<sessionID<<" started and Ok";
            break;
        case ASIOtalker::Status::closed:
            log_info<<"Session #"<<sessionID<<" closed";
            needToDelete = true;
            break;
        case ASIOtalker::Status::destroyed:
            log_info<<"Session #"<<sessionID<<" destroyed";
            needToDelete = true;
            break;
        case ASIOtalker::Status::failed:
            log_info<<"Session #"<<sessionID<<" failed (peer broken?)";
            needToDelete = true;
            break;
    }
    if(needToDelete)
    {
        sessions.erase(sessionID);
        log_info<<"Session #"<<sessionID<<" closed";
    }
}

std::shared_ptr<ASIOclient> ASIOclient::create()
{
    struct make_shared_enabler : public ASIOclient
    { };
    return std::make_shared<make_shared_enabler>();
}

std::shared_ptr<ASIOclient> ASIOclient::create(const std::string& address, uint16_t port)
{
    struct make_shared_enabler : public ASIOclient
    { };
    auto res = std::make_shared<make_shared_enabler>();
    res->connect(address, port);
    return res;
}


ASIOclient::ASIOclient() : status(ASIOclient::Status::notinitialized), mport(0), talker(nullptr), resolver(getContext())
{
    log_trace<<"constructing ASIOclient";
}

ASIOclient::~ASIOclient()
{
    log_trace<<"destructing ASIOclient";
}


void ASIOclient::connect(const std::string &address, uint16_t port)
{   
    mport = port;
    maddress = address;
    talker = ASIOtalker::create(getContext());
    doResolve();
}

void ASIOclient::doResolve()
{
    //at least one shared_ptr to 'this' pointer in your program should exists
    //before this line, otherwise bad_weak_ptr exception will be thrown
    auto self(shared_from_this());

    status = ASIOclient::Status::resolving;

    static int i = 0;
    log_trace<<"Trying to resolve, attempt #"<<i;
    int j = i++;
    resolver.async_resolve(maddress, std::to_string(mport),
        [this, self, j](const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::results_type results)
        {
            log_trace<<"Remote host resolved, attemp #"<<j<<" finished";
            auto [ptr, newStatus] = CheckError::check(self, ec, CheckError::opcode::accept);
            if(newStatus == ASIOtalker::Status::working && ptr && results.size() > 0)
            {
                endpoints = results;
                doConnect();
            } else {
                status = ASIOclient::Status::resolvingFailed;
                throw std::runtime_error(std::string("Can't resolve host, error = ") + ec.message());
            }
        });
}

void ASIOclient::doConnect()
{
    //at least one shared_ptr to 'this' pointer in your program should exists
    //before this line, otherwise bad_weak_ptr exception will be thrown
    auto self(shared_from_this());

    assert(talker != nullptr);
    status = ASIOclient::Status::connecting;
    boost::asio::async_connect(talker->sock(), endpoints,
        [this, self](const boost::system::error_code &ec, const typename boost::asio::ip::tcp::endpoint &ep)
        {
            (void)ep;
            auto [ptr, newStatus] = CheckError::check(self, ec, CheckError::opcode::accept);
            if(newStatus == ASIOtalker::Status::working && ptr)
            {
                talker->start();
                status = ASIOclient::Status::connected;
            } else {
                status = ASIOclient::Status::connectingFailed;
            };
        });
}

void ASIOclient::send(const Buffer& buf)
{
    if(!talker)
        throw std::runtime_error("ASIOclient::send: not connected yet");
    talker->send(buf);
}

bool ASIOclient::receive(Buffer& buf)
{
    return talker->receive(buf);
}

/*ASIOclient::Status ASIOclient::getStatus()
{
    updateStatus();
    return status;
};*/
/*bool ASIOclient::isFailed()
{
    updateStatus();
    return status == Status::disconnected || status == Status::resolvingFailed || status == Status::connectingFailed;
}
bool ASIOclient::isConnected()
{
    updateStatus();
    return status == Status::connected;
};*/

void ASIOclient::updateStatus()
{
    if(talker)
        switch(talker->getStatus())
        {
            case ASIOtalker::Status::notStarted: /* in this case do not change this->status */ break;
            case ASIOtalker::Status::working: status = ASIOclient::Status::connected; break;
            case ASIOtalker::Status::closed:
            case ASIOtalker::Status::destroyed:
            case ASIOtalker::Status::failed: status = ASIOclient::Status::disconnected;
        }
}


void ASIOclient::close()
{
    if(talker)
    {
        poll();
        talker->close();
    }
}

void ASIOclient::poll()
{
    getContext().poll();
}

} /* namespace detail */
} /* namespace asiocommunicator */
} /* namespace tea */

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

#include "detail/asioserverclient.hpp"

namespace tea::asiocommunicator {

using byte = uint8_t;
using MsgBody = std::vector<byte>;

using port_t = uint16_t;
static_assert(sizeof(port_t) >= 2, "size of port_t should be atleat 2 bytes");

template<class T>
concept IsBuffer = std::same_as<MsgBody, T>;

class Server : public detail::ASIOServer
{
public:
    /*template<typename ... Args>
    explicit Server(Args&& ... args) : detail::ASIOServer(std::forward<Args>(args)...) {}*/

    explicit Server(boost::asio::io_context& context) : detail::ASIOServer(context) {}

    Server(Server&& other) = delete;
    Server(const Server& other) = delete;
    Server& operator=(Server&& other) = delete;
    Server& operator=(const Server& other) = delete;

    ~Server() override = default;


    template<typename MsgType> requires (not IsBuffer<MsgType>)
    void send(size_t session_id, const MsgType& msg)
    {
        MsgBody buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        detail::ASIOServer::send(session_id, std::move(buf));
    }

    template<typename MsgType> requires (not IsBuffer<MsgType>)
    bool receiveAnySession(size_t &session_id, MsgType& msg)
    {
        MsgBody buf;
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
        MsgBody buf;
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
        MsgBody buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        detail::ASIOClient::send(std::move(buf));
    }
    template<typename MsgType> requires (not IsBuffer<MsgType>)
    bool receive(MsgType& msg)
    {
        MsgBody buf;
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

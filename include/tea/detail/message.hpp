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

#ifndef _TEA_ASIOCOMMUNICATOR_DETAIL_MESSAGE_H
#define _TEA_ASIOCOMMUNICATOR_DETAIL_MESSAGE_H

#include <cstdint>
#include <vector>

namespace tea::asiocommunicator {

using byte = uint8_t;
using MsgBody = std::vector<byte>;


namespace detail {

/*!
* \brief Header part of every message send or received by this library
* \details The header contains two values:
* - the signature of the header
* - the length of the message body
*/
struct MsgHeader final
{
    /// Message header should start with the following signature:
    static constexpr uint32_t header_signature = 0x1234;

    uint32_t signature; //should be equal to header_signature
    uint32_t msglen{0};  //len of the message in bytes
    MsgHeader() : signature(header_signature) {}
    void* data()
    {
        return &signature;
    }
    static size_t headerLen()
    {
        return sizeof(MsgHeader);
    }
    void initialize(const MsgBody& body)
    {
        signature = header_signature;
        msglen = body.size();
    }
};


} /* namespace detail */

} /* namespace tea::asiocommunicator */

#endif // _TEA_ASIOCOMMUNICATOR_DETAIL_MESSAGE_H

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

#ifndef _TEA_ASIO_DETAIL_GLOBAL_CONTEXT
#define _TEA_ASIO_DETAIL_GLOBAL_CONTEXT

#include <boost/asio.hpp>

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

} /* namespace detail */

} /* namespace tea::asiocommunicator */

#endif // _TEA_ASIO_DETAIL_GLOBAL_CONTEXT

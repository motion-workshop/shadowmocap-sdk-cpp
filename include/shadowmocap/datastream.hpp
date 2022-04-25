#pragma once

#include <shadowmocap/config.hpp>
#include <shadowmocap/message.hpp>

#if SHADOWMOCAP_USE_BOOST_ASIO
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ts/net.hpp>
#include <boost/asio/ts/socket.hpp>
#include <boost/asio/use_awaitable.hpp>
#else
#include <asio/awaitable.hpp>
#include <asio/detached.hpp>
#include <asio/ts/net.hpp>
#include <asio/ts/socket.hpp>
#include <asio/use_awaitable.hpp>
#endif

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace shadowmocap {

#if SHADOWMOCAP_USE_BOOST_ASIO
namespace net = boost::asio;
#else
namespace net = asio;
#endif

using net::ip::tcp;

template <typename Protocol>
struct datastream {
    typename Protocol::socket socket_;
    std::vector<std::string> names_;
    std::chrono::steady_clock::time_point deadline_;
}; // struct datastream

template <typename Message, typename AsyncReadStream>
auto read_message(AsyncReadStream &s) -> net::awaitable<Message>
{
    constexpr auto MaxMessageLength = (1 << 16);
    static_assert(
        sizeof(typename Message::value_type) == sizeof(char),
        "message is not bytes");

    unsigned length = 0;
    co_await net::async_read(
        s, net::buffer(&length, sizeof(length)), net::use_awaitable);

    length = ntohl(length);
    if (length > MaxMessageLength) {
        throw std::runtime_error("message length is not valid");
    }

    Message message(length, 0);

    co_await net::async_read(s, net::buffer(message), net::use_awaitable);

    co_return message;
}

template <typename Message, typename Protocol>
auto read_message(datastream<Protocol> &stream) -> net::awaitable<Message>
{
    auto message = co_await read_message<Message>(stream.socket_);

    if (is_metadata(message)) {
        stream.names_ = parse_metadata(message);

        message = co_await read_message<Message>(stream.socket_);
    }

    co_return message;
}

/**
 * Write a binary message with its length header to the stream.
 */
template <typename AsyncWriteStream>
auto write_message(AsyncWriteStream &s, std::string_view message)
    -> net::awaitable<void>
{
    unsigned length = htonl(static_cast<unsigned>(std::size(message)));
    co_await net::async_write(
        s, net::buffer(&length, sizeof(length)), net::use_awaitable);

    co_await net::async_write(s, net::buffer(message), net::use_awaitable);
}

template <typename Protocol>
auto write_message(datastream<Protocol> &stream, std::string_view message)
    -> net::awaitable<void>
{
    co_await write_message(stream.socket_, message);
}

auto open_connection(const tcp::endpoint &endpoint)
    -> net::awaitable<datastream<tcp>>;

/**
 * Give the datastream more time to complete its next operation with respect to
 * the watchdog coroutine.
 */
template <class Rep, class Period>
void extend_deadline_for(
    datastream<tcp> &stream,
    const std::chrono::duration<Rep, Period> &timeout_duration)
{
    stream.deadline_ = std::max(
        stream.deadline_, std::chrono::steady_clock::now() + timeout_duration);
}

/**
 * From Chris Kohlhoff talk "Talking Async Ep1: Why C++20 is the Awesomest
 * Language for Network Programming".
 *
 * https://youtu.be/icgnqFM-aY4
 *
 * And here is the source code from the talk.
 *
 * https://github.com/chriskohlhoff/talking-async/blob/master/episode1/step_6.cpp
 *
 * This function is intended for use with awaitable operators in Asio.
 *
 * @code
 * co_await(async_read_loop(stream) || watchdog(stream.deadline_));
 * @endcode
 *
 * Where the async_read_loop function updates the deadline timer.
 *
 * @code
 * for (;;) {
 *   stream.deadline_ = now() + 1s;
 *   co_await net::async_read(stream.socket_, ...);
 * }
 * @endcode
 */
auto watchdog(const std::chrono::steady_clock::time_point &deadline)
    -> net::awaitable<void>;

/**
 * Close a socket that is reading in its own coroutine.
 *
 * Intended for use to handle a timeout for a datastream where the
 * async_read_loop function updates the deadline timer.
 *
 * @code
 * co_spawn(ctx, async_read_loop(stream), ...);
 * co_await watchdog(stream);
 * @endcode
 */
template <typename Protocol>
auto watchdog(datastream<tcp> &stream) -> net::awaitable<void>;

} // namespace shadowmocap

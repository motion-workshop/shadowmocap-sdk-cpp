#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include <shadowmocap.hpp>

#if SHADOWMOCAP_USE_BOOST_ASIO
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#else
#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/io_context.hpp>
#endif

#include <chrono>
#include <iostream>
#include <string>

namespace net = shadowmocap::net;
using net::ip::tcp;

net::awaitable<void>
read_shadowmocap_datastream_frames(shadowmocap::datastream<tcp> &stream)
{
    using namespace shadowmocap;
    using namespace std::chrono_literals;

    constexpr auto Mask = channel::Lq | channel::c;
    constexpr auto ItemSize = get_channel_mask_dimension(Mask);

    {
        // Create an XML string that lists the channels we want in order.
        // <configurable><Lq/><c/></configurable>
        const auto message = make_channel_message(Mask);

        co_await write_message(stream, message);
    }

    auto start = std::chrono::steady_clock::now();

    std::size_t num_bytes = 0;
    for (int i = 0; i < 100; ++i) {
        extend_deadline_for(stream, 1s);

        auto message = co_await read_message<std::string>(stream);
        num_bytes += std::size(message);

        const auto NumItem = std::size(stream.names_);

        REQUIRE(NumItem > 0);

        if (NumItem == 0) {
            throw std::runtime_error("name map must not be empty");
        }

        REQUIRE(std::size(message) == NumItem * (2 + ItemSize) * 4);

        if (std::size(message) != NumItem * (2 + ItemSize) * 4) {
            throw std::runtime_error("message size mismatch");
        }

        auto view = shadowmocap::make_message_view<ItemSize>(message);

        REQUIRE(std::size(view) == NumItem);

        if (std::size(view) != NumItem) {
            throw std::runtime_error("message item count mismatch");
        }

        for (auto &item : view) {
            REQUIRE(item.length == ItemSize);

            if (item.length != ItemSize) {
                throw std::runtime_error("message item channel size mismatch");
            }

            std::cout << item.key << " " << item.length << " = ";

            std::copy(
                std::begin(item.data), std::end(item.data),
                std::ostream_iterator<float>(std::cout, ", "));

            std::cout << "\n";
        }
    }

    auto end = std::chrono::steady_clock::now();

    std::cout << "read 100 samples (" << num_bytes << " bytes) in "
              << std::chrono::duration<double>(end - start).count() << "\n";
}

net::awaitable<void> read_shadowmocap_datastream(const tcp::endpoint &endpoint)
{
    using namespace net::experimental::awaitable_operators;
    using namespace shadowmocap;
    using namespace std::chrono_literals;

    auto stream = co_await open_connection(endpoint);

    extend_deadline_for(stream, 1s);

    co_await(
        read_shadowmocap_datastream_frames(stream) ||
        watchdog(stream.deadline_));
}

bool run()
{
    try {
        net::io_context ctx;

        constexpr std::string_view host = "127.0.0.1";
        constexpr std::string_view service = "32076";

        auto endpoint = *shadowmocap::tcp::resolver(ctx).resolve(host, service);

        co_spawn(ctx, read_shadowmocap_datastream(endpoint), [](auto ptr) {
            // Propagate exception from the coroutine
            if (ptr) {
                std::rethrow_exception(ptr);
            }
        });

        ctx.run();

        return true;
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
    }

    return false;
}

TEST_CASE("read_shadowmocap_datastream")
{
    REQUIRE(run());
}

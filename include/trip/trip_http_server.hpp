#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>

#include "trip_service.hpp"

namespace trip
{
    class TripHttpServer
    {
    public:
        TripHttpServer(const std::string &address, uint16_t port, std::size_t threads = std::thread::hardware_concurrency());
        ~TripHttpServer();

        TripHttpServer(const TripHttpServer &) = delete;
        TripHttpServer &operator=(const TripHttpServer &) = delete;

        void start();
        void stop();

        [[nodiscard]] uint16_t port() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}

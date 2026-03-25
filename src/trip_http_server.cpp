#include "trip/trip_http_server.hpp"

#include "trip_http_server_impl.hpp"

namespace trip
{
    TripHttpServer::TripHttpServer(const std::string &address, uint16_t port, std::size_t threads)
        : impl_(std::make_unique<Impl>(address, port, threads))
    {
    }

    TripHttpServer::~TripHttpServer() = default;

    void TripHttpServer::start()
    {
        impl_->start();
    }

    void TripHttpServer::stop()
    {
        impl_->stop();
    }

    uint16_t TripHttpServer::port() const
    {
        return impl_->port();
    }
}

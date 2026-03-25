#include "trip_http_server_impl.hpp"

#include <algorithm>
#include <stdexcept>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/strand.hpp>

namespace trip
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;

    TripHttpServer::Impl::Impl(const std::string &address, uint16_t port, std::size_t thread_count)
        : io_context_(1),
          acceptor_(asio::make_strand(io_context_)),
          endpoint_(asio::ip::make_address(address), port),
          thread_count_(std::max<std::size_t>(thread_count, 1U))
    {
    }

    TripHttpServer::Impl::~Impl()
    {
        stop();
    }

    void TripHttpServer::Impl::start()
    {
        if (started_)
        {
            return;
        }

        beast::error_code ec;
        acceptor_.open(endpoint_.protocol(), ec);
        if (ec)
        {
            throw std::runtime_error("acceptor open failed: " + ec.message());
        }

        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec)
        {
            throw std::runtime_error("set_option failed: " + ec.message());
        }

        acceptor_.bind(endpoint_, ec);
        if (ec)
        {
            throw std::runtime_error("acceptor bind failed: " + ec.message());
        }

        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            throw std::runtime_error("acceptor listen failed: " + ec.message());
        }

        doAccept();
        started_ = true;

        workers_.reserve(thread_count_);
        for (std::size_t i = 0; i < thread_count_; ++i)
        {
            workers_.emplace_back([this]()
                                  { io_context_.run(); });
        }
    }

    void TripHttpServer::Impl::stop()
    {
        if (!started_)
        {
            return;
        }

        beast::error_code ec;
        acceptor_.cancel(ec);
        acceptor_.close(ec);
        io_context_.stop();

        for (auto &worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        workers_.clear();
        started_ = false;
    }

    uint16_t TripHttpServer::Impl::port() const
    {
        beast::error_code ec;
        const auto local = acceptor_.local_endpoint(ec);
        if (ec)
        {
            return endpoint_.port();
        }
        return local.port();
    }

    TripService &TripHttpServer::Impl::service()
    {
        return service_;
    }

    void TripHttpServer::Impl::subscribe(const std::string &trip_id, std::weak_ptr<WsSession> session)
    {
        std::scoped_lock lock(subscribers_mutex_);
        auto &bucket = subscribers_by_trip_[trip_id];
        std::erase_if(bucket, [](const std::weak_ptr<WsSession> &entry)
                      { return entry.expired(); });
        bucket.push_back(std::move(session));
    }

    void TripHttpServer::Impl::publishLatestEvent(const std::string &token, const std::string &trip_id)
    {
        auto revision = service_.getTripRevision(token, trip_id);
        if (!revision.ok() || revision.value == 0)
        {
            return;
        }

        auto latest = service_.getEventsSince(token, trip_id, revision.value - 1);
        if (!latest.ok() || latest.value.empty())
        {
            return;
        }

        const std::string payload =
            "{\"type\":\"event\",\"trip_id\":\"" + detail::escapeJson(trip_id) +
            "\",\"event\":" + detail::eventToJson(latest.value.back()) + "}";

        std::vector<std::shared_ptr<WsSession>> recipients;
        {
            std::scoped_lock lock(subscribers_mutex_);
            auto it = subscribers_by_trip_.find(trip_id);
            if (it == subscribers_by_trip_.end())
            {
                return;
            }

            auto &bucket = it->second;
            std::erase_if(bucket, [](const std::weak_ptr<WsSession> &entry)
                          { return entry.expired(); });

            for (auto &entry : bucket)
            {
                if (auto session = entry.lock())
                {
                    recipients.push_back(std::move(session));
                }
            }
        }

        for (auto &session : recipients)
        {
            session->enqueue(payload);
        }
    }

    void TripHttpServer::Impl::doAccept()
    {
        acceptor_.async_accept(
            asio::make_strand(io_context_),
            [this](beast::error_code ec, asio::ip::tcp::socket socket)
            {
                if (!ec)
                {
                    std::make_shared<HttpSession>(std::move(socket), *this)->run();
                }
                if (acceptor_.is_open())
                {
                    doAccept();
                }
            });
    }
}

#include "trip_http_server_impl.hpp"

#include <boost/asio/post.hpp>

namespace trip
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;

    TripHttpServer::Impl::WsSession::WsSession(
        beast::tcp_stream stream,
        Impl &impl,
        std::string token,
        std::string trip_id,
        uint64_t since_revision)
        : ws_(std::move(stream)),
          impl_(impl),
          token_(std::move(token)),
          trip_id_(std::move(trip_id)),
          since_revision_(since_revision)
    {
    }

    void TripHttpServer::Impl::WsSession::run(http::request<http::string_body> req)
    {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        ws_.text(true);
        ws_.async_accept(req, beast::bind_front_handler(&WsSession::onAccept, shared_from_this()));
    }

    void TripHttpServer::Impl::WsSession::enqueue(const std::string &payload)
    {
        asio::post(
            ws_.get_executor(),
            [self = shared_from_this(), payload]()
            {
                const bool should_write = self->outbox_.empty();
                self->outbox_.push_back(payload);
                if (should_write)
                {
                    self->doWrite();
                }
            });
    }

    void TripHttpServer::Impl::WsSession::onAccept(beast::error_code ec)
    {
        if (ec)
        {
            return;
        }

        impl_.subscribe(trip_id_, weak_from_this());

        auto backlog = impl_.service().getEventsSince(token_, trip_id_, since_revision_);
        if (backlog.ok())
        {
            for (const auto &event : backlog.value)
            {
                enqueue("{\"type\":\"event\",\"trip_id\":\"" + detail::escapeJson(trip_id_) + "\",\"event\":" + detail::eventToJson(event) + "}");
            }
        }

        doRead();
    }

    void TripHttpServer::Impl::WsSession::doRead()
    {
        ws_.async_read(buffer_, beast::bind_front_handler(&WsSession::onRead, shared_from_this()));
    }

    void TripHttpServer::Impl::WsSession::onRead(beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            return;
        }
        buffer_.consume(buffer_.size());
        doRead();
    }

    void TripHttpServer::Impl::WsSession::doWrite()
    {
        ws_.async_write(
            asio::buffer(outbox_.front()),
            beast::bind_front_handler(&WsSession::onWrite, shared_from_this()));
    }

    void TripHttpServer::Impl::WsSession::onWrite(beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            return;
        }
        outbox_.pop_front();
        if (!outbox_.empty())
        {
            doWrite();
        }
    }
}

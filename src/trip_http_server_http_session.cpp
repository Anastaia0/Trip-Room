#include "trip_http_server_impl.hpp"

#include <boost/asio/dispatch.hpp>

namespace trip
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;

    TripHttpServer::Impl::HttpSession::HttpSession(asio::ip::tcp::socket socket, Impl &impl)
        : stream_(std::move(socket)),
          impl_(impl),
          service_(impl.service())
    {
    }

    void TripHttpServer::Impl::HttpSession::run()
    {
        asio::dispatch(stream_.get_executor(), beast::bind_front_handler(&HttpSession::doRead, shared_from_this()));
    }

    void TripHttpServer::Impl::HttpSession::doRead()
    {
        req_ = {};
        http::async_read(stream_, buffer_, req_, beast::bind_front_handler(&HttpSession::onRead, shared_from_this()));
    }

    void TripHttpServer::Impl::HttpSession::onRead(beast::error_code ec, std::size_t)
    {
        if (ec == http::error::end_of_stream)
        {
            doClose();
            return;
        }
        if (ec)
        {
            doClose();
            return;
        }

        if (websocket::is_upgrade(req_))
        {
            const auto [path, query] = detail::splitTarget(std::string(req_.target()));
            if (path != "/ws/updates")
            {
                auto not_found = detail::jsonResponse(http::status::not_found, "{\"status\":\"NotFound\",\"message\":\"Route not found\"}", req_.version(), req_.keep_alive());
                res_ = std::make_shared<http::response<http::string_body>>(std::move(not_found));
                http::async_write(stream_, *res_, beast::bind_front_handler(&HttpSession::onWrite, shared_from_this(), res_->need_eof()));
                return;
            }

            const auto trip_it = query.find("trip_id");
            if (query.find("token") != query.end())
            {
                auto invalid = detail::jsonResponse(http::status::bad_request, "{\"status\":\"InvalidArgument\",\"message\":\"Use Authorization header instead of token query parameter\"}", req_.version(), false);
                res_ = std::make_shared<http::response<http::string_body>>(std::move(invalid));
                http::async_write(stream_, *res_, beast::bind_front_handler(&HttpSession::onWrite, shared_from_this(), true));
                return;
            }

            const std::string token = detail::authorizationBearerToken(req_);
            if (token.empty() || trip_it == query.end())
            {
                auto invalid = detail::jsonResponse(http::status::bad_request, "{\"status\":\"InvalidArgument\",\"message\":\"Authorization header and trip_id are required\"}", req_.version(), false);
                res_ = std::make_shared<http::response<http::string_body>>(std::move(invalid));
                http::async_write(stream_, *res_, beast::bind_front_handler(&HttpSession::onWrite, shared_from_this(), true));
                return;
            }

            uint64_t since_revision = 0;
            const auto since_it = query.find("since_revision");
            if (since_it != query.end())
            {
                try
                {
                    since_revision = detail::parseUint64(since_it->second);
                }
                catch (const std::exception &)
                {
                    auto invalid = detail::jsonResponse(http::status::bad_request, "{\"status\":\"InvalidArgument\",\"message\":\"Invalid since_revision\"}", req_.version(), false);
                    res_ = std::make_shared<http::response<http::string_body>>(std::move(invalid));
                    http::async_write(stream_, *res_, beast::bind_front_handler(&HttpSession::onWrite, shared_from_this(), true));
                    return;
                }
            }

            auto access = service_.getTripRevision(token, trip_it->second);
            if (!access.ok())
            {
                auto forbidden = detail::jsonResponse(http::status::forbidden, "{\"status\":\"" + detail::escapeJson(detail::statusToString(access.status)) + "\",\"message\":\"" + detail::escapeJson(access.message) + "\"}", req_.version(), false);
                res_ = std::make_shared<http::response<http::string_body>>(std::move(forbidden));
                http::async_write(stream_, *res_, beast::bind_front_handler(&HttpSession::onWrite, shared_from_this(), true));
                return;
            }

            std::make_shared<WsSession>(std::move(stream_), impl_, token, trip_it->second, since_revision)->run(std::move(req_));
            return;
        }

        auto response = detail::handleApiRequest(
            service_,
            [this](const std::string &token, const std::string &trip_id)
            {
                impl_.publishLatestEvent(token, trip_id);
            },
            req_);
        res_ = std::make_shared<http::response<http::string_body>>(std::move(response));
        http::async_write(stream_, *res_, beast::bind_front_handler(&HttpSession::onWrite, shared_from_this(), res_->need_eof()));
    }

    void TripHttpServer::Impl::HttpSession::onWrite(bool close, beast::error_code ec, std::size_t)
    {
        if (ec)
        {
            doClose();
            return;
        }
        if (close)
        {
            doClose();
            return;
        }

        res_.reset();
        doRead();
    }

    void TripHttpServer::Impl::HttpSession::doClose()
    {
        beast::error_code ec;
        stream_.socket().shutdown(asio::ip::tcp::socket::shutdown_send, ec);
        stream_.socket().close(ec);
    }
}

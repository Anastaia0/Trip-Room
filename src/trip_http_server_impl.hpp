#pragma once

#include "trip/trip_http_server.hpp"
#include "trip_http_server_detail.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

namespace trip
{
    class TripHttpServer::Impl
    {
    public:
        class WsSession;
        class HttpSession;

        Impl(const std::string &address, uint16_t port, std::size_t thread_count);
        ~Impl();

        void start();
        void stop();
        [[nodiscard]] uint16_t port() const;

        void subscribe(const std::string &trip_id, std::weak_ptr<WsSession> session);
        void publishLatestEvent(const std::string &token, const std::string &trip_id);
        TripService &service();

        void doAccept();

        class WsSession : public std::enable_shared_from_this<WsSession>
        {
        public:
            WsSession(
                boost::beast::tcp_stream stream,
                Impl &impl,
                std::string token,
                std::string trip_id,
                uint64_t since_revision);

            void run(boost::beast::http::request<boost::beast::http::string_body> req);
            void enqueue(const std::string &payload);

        private:
            void onAccept(boost::beast::error_code ec);
            void doRead();
            void onRead(boost::beast::error_code ec, std::size_t);
            void doWrite();
            void onWrite(boost::beast::error_code ec, std::size_t);

            boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
            boost::beast::flat_buffer buffer_;
            std::deque<std::string> outbox_;
            Impl &impl_;
            std::string token_;
            std::string trip_id_;
            uint64_t since_revision_ = 0;
        };

        class HttpSession : public std::enable_shared_from_this<HttpSession>
        {
        public:
            HttpSession(boost::asio::ip::tcp::socket socket, Impl &impl);
            void run();

        private:
            void doRead();
            void onRead(boost::beast::error_code ec, std::size_t);
            void onWrite(bool close, boost::beast::error_code ec, std::size_t);
            void doClose();

            boost::beast::tcp_stream stream_;
            boost::beast::flat_buffer buffer_;
            boost::beast::http::request<boost::beast::http::string_body> req_;
            std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>> res_;
            Impl &impl_;
            TripService &service_;
        };

    private:
        using tcp = boost::asio::ip::tcp;

        boost::asio::io_context io_context_;
        tcp::acceptor acceptor_;
        tcp::endpoint endpoint_;
        std::size_t thread_count_;
        bool started_ = false;
        std::vector<std::thread> workers_;
        TripService service_;
        std::mutex subscribers_mutex_;
        std::unordered_map<std::string, std::vector<std::weak_ptr<WsSession>>> subscribers_by_trip_;
    };
}

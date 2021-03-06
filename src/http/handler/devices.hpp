/*
    twain-server: This project exposes the TWAIN DSM as a web API
    Copyright (C) 2019 "Diagnósticos da América S.A. <bruno.f@dasa.com.br>"

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "cors.hpp"
#include "handler.hpp"
#include "../../twain/device.hpp"
#include <boost/asio/coroutine.hpp>

namespace dasa::gliese::scanner::http::handler {
    class DevicesHandler : public kitsune::ioc::Service<DevicesHandler, GetMapping> {
    public:
        [[nodiscard]] boost::beast::string_view route() const override {
            return "/devices";
        }

        boost::beast::http::response<boost::beast::http::dynamic_body> operator()(boost::beast::http::request<boost::beast::http::string_body>&& request) override;
    };

    class DevicesCORSHandler : public CORSMapping<DevicesCORSHandler> {
    public:
        [[nodiscard]] boost::beast::string_view route() const override {
            return "/devices";
        }
    };

    class DevicesAsyncHandler : public kitsune::ioc::Service<DevicesAsyncHandler, GetMapping> {
    public:
        [[nodiscard]] boost::beast::string_view route() const override {
            return "/devices-async";
        }

        void handle(const boost::beast::http::request<boost::beast::http::string_body>& request, std::function<void(boost::beast::http::response<boost::beast::http::dynamic_body>)> send, boost::asio::coroutine co, std::error_code ec, std::list<twain::Device> devices);
        void operator()(boost::beast::http::request<boost::beast::http::string_body>&& request, std::function<void(boost::beast::http::response<boost::beast::http::dynamic_body>)> send) override;
    };

    class DevicesAsyncCORSHandler : public CORSMapping<DevicesAsyncCORSHandler> {
    public:
        [[nodiscard]] boost::beast::string_view route() const override {
            return "/devices-async";
        }
    };

    class DevicesDPIHandler : public kitsune::ioc::Service<DevicesHandler, GetMapping> {
    public:
        [[nodiscard]] boost::beast::string_view route() const override {
            return "/devices/dpi";
        }

        boost::beast::http::response<boost::beast::http::dynamic_body> operator()(boost::beast::http::request<boost::beast::http::string_body>&& request) override;
    };

    class DevicesDPICORSHandler : public CORSMapping<DevicesDPICORSHandler> {
    public:
        [[nodiscard]] boost::beast::string_view route() const override {
            return "/devices/dpi";
        }

    protected:
        [[nodiscard]] boost::beast::string_view headers() final {
            return "Server, Content-Type, X-Device";
        }
    };
}


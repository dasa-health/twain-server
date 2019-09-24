#include "handlers.hpp"
#include "../../application.hpp"

extern dasa::gliese::scanner::Application *application;

using dasa::gliese::scanner::http::handler::RouteHandler;
namespace http = boost::beast::http;

class StatusHandler : public RouteHandler {
    [[nodiscard]] http::verb method() const final {
        return http::verb::get;
    }

    [[nodiscard]] boost::beast::string_view route() const final {
        return "/status";
    }

	http::response<http::string_body> operator()(http::request<http::string_body>&& request) final {
        nlohmann::json response;
        response["status"] = "UP";
		response["details"] = {
			{"twain", application->getTwain().getState()}
		};

		http::response<http::string_body> res{ http::status::ok, request.version() };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "application/json");
		res.keep_alive(request.keep_alive());
		res.body() = response.dump();
		res.prepare_payload();
		return res;
    }
};

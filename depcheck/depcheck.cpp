// TEMPORARY: plan 0022 checkpoint A1 dependency compile/link check.
//
// Includes the Crow and cpp-jwt headers and exercises a trivial slice of each
// API to force a real compile + link of both libraries (and their transitive
// deps: standalone asio, OpenSSL, nlohmann_json). It is not part of the
// application and should be removed when the server/ daemon lands (milestone M-E).

#include <crow.h>

#include <iostream>
#include <jwt/jwt.hpp>
#include <string>

int main() {
  // Crow: construct an app and register a route. We never call run() — this is
  // purely a compile/link smoke check.
  crow::SimpleApp app;
  CROW_ROUTE(app, "/health")([]() { return std::string("ok"); });

  // cpp-jwt: build, sign, and decode a trivial HS256 token round-trip.
  using namespace jwt::params;
  jwt::jwt_object obj{algorithm("HS256"), secret("dep-check-secret"), payload({{"sub", "depcheck"}})};
  const std::string token = obj.signature();

  auto decoded = jwt::decode(token, algorithms({"HS256"}), secret("dep-check-secret"));
  const std::string sub = decoded.payload().get_claim_value<std::string>("sub");

  std::cout << "depcheck OK: crow+cpp-jwt linked; token sub=" << sub << "\n";
  return 0;
}

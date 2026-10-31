#include "UserRoutes.h"

#include <auth/PasswordHash.h>
#include <auth/UserStore.h>
#include <auth/types/AuthError.h>
#include <auth/types/User.h>
#include <database/Database.h>

#include <algorithm>
#include <string>
#include <vector>

#include "AuthRoutes.h"
#include "Json.h"

namespace server {

namespace {

constexpr size_t MAX_LOGIN_LEN = 64;
constexpr size_t MAX_FULLNAME_LEN = 255;
constexpr size_t MIN_PASSWORD_LEN = 8; // same as POST /auth/password
constexpr size_t MAX_PASSWORD_LEN = 1024;

/// Serializes a User. Never includes password material (User has none; keep it that way).
crow::json::wvalue userToJson(const auth::User& u) {
  crow::json::wvalue w;
  w["login"] = u.login;
  w["fullName"] = u.fullName;
  w["isAdmin"] = u.isAdmin;
  w["enabled"] = u.enabled;
  w["createdAt"] = u.createdAt;
  w["updatedAt"] = u.updatedAt;
  return w;
}

/// 1..64 chars of [A-Za-z0-9._@-].
bool validLogin(const std::string& s) {
  return !s.empty() && s.size() <= MAX_LOGIN_LEN && std::ranges::all_of(s, [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' ||
           c == '@' || c == '-';
  });
}

/// Returns an error message (empty if valid).
const char* checkFullName(const std::string& s) {
  return s.empty() || s.size() > MAX_FULLNAME_LEN ? "fullName must be 1..255 bytes" : "";
}

const char* checkPassword(const std::string& s) {
  return s.size() < MIN_PASSWORD_LEN || s.size() > MAX_PASSWORD_LEN ? "password must be 8..1024 bytes" : "";
}

bool isBool(const crow::json::rvalue& v) {
  return v.t() == crow::json::type::True || v.t() == crow::json::type::False;
}

/// Maps a store exception to a response.
crow::response storeError(const auth::AuthException& e) {
  switch (e.code()) {
    case auth::AuthErrorCode::NotFound:
      return notFound(e.what());
    case auth::AuthErrorCode::Duplicate:
      return conflict(e.what());
    case auth::AuthErrorCode::LastAdmin:
      return jsonError(409, "LastAdmin", e.what());
    default:
      return internalError();
  }
}

crow::response listUsers(auth::UserStore& store, const crow::request& req) {
  uint32_t offset = 0;
  uint32_t limit = DEFAULT_PAGE_LIMIT;
  crow::response err;
  if (!parsePage(req, offset, limit, err)) {
    return err;
  }

  std::vector<crow::json::wvalue> arr;
  for (const auto& u : store.list(db::Pagination{offset, limit})) {
    arr.push_back(userToJson(u));
  }
  crow::json::wvalue resp;
  resp["users"] = std::move(arr);
  return jsonOk(200, resp);
}

crow::response createUser(auth::UserStore& store, uint32_t iterations, const crow::request& req) {
  auto body = crow::json::load(req.body);
  if (!body || body.t() != crow::json::type::Object) {
    return badRequest("body must be a JSON object");
  }
  if (!body.has("login") || !body.has("fullName") || !body.has("password") || !body.has("isAdmin")) {
    return badRequest("body must contain login, fullName, password and isAdmin");
  }
  if (body["login"].t() != crow::json::type::String || body["fullName"].t() != crow::json::type::String ||
      body["password"].t() != crow::json::type::String || !isBool(body["isAdmin"])) {
    return badRequest("login, fullName, password must be strings and isAdmin a boolean");
  }
  const std::string login = body["login"].s();
  const std::string fullName = body["fullName"].s();
  const std::string password = body["password"].s();
  if (!validLogin(login)) {
    return badRequest("login must be 1..64 characters from [A-Za-z0-9._@-]");
  }
  if (const char* m = checkFullName(fullName); *m) {
    return badRequest(m);
  }
  if (const char* m = checkPassword(password); *m) {
    return badRequest(m);
  }
  const bool isAdmin = body["isAdmin"].t() == crow::json::type::True;

  try {
    store.create(login, fullName, auth::hashPassword(password, iterations), isAdmin);
    const auto user = store.get(login);
    return user ? jsonOk(201, userToJson(*user)) : internalError();
  } catch (const auth::AuthException& e) {
    return storeError(e);
  }
}

crow::response patchUser(
  auth::UserStore& store, uint32_t iterations, const std::string& login, const crow::request& req
) {
  auto body = crow::json::load(req.body);
  if (!body || body.t() != crow::json::type::Object) {
    return badRequest("body must be a JSON object");
  }
  const auto keys = body.keys();
  if (keys.empty()) {
    return badRequest("body must contain at least one of fullName, password, isAdmin, enabled");
  }
  auth::UserUpdate upd;
  std::string password;
  for (const auto& k : keys) {
    const auto& v = body[k];
    if (k == "fullName" && v.t() == crow::json::type::String) {
      upd.fullName = v.s();
      if (const char* m = checkFullName(*upd.fullName); *m) {
        return badRequest(m);
      }
    } else if (k == "password" && v.t() == crow::json::type::String) {
      password = v.s();
      if (const char* m = checkPassword(password); *m) {
        return badRequest(m);
      }
    } else if (k == "isAdmin" && isBool(v)) {
      upd.isAdmin = v.t() == crow::json::type::True;
    } else if (k == "enabled" && isBool(v)) {
      upd.enabled = v.t() == crow::json::type::True;
    } else {
      return badRequest("unknown field or wrong type: " + k);
    }
  }

  try {
    // Existence is checked first so an unknown login is a cheap 404 (no PBKDF2 work).
    if (!store.get(login)) {
      return notFound("user not found");
    }
    if (!password.empty()) {
      upd.password = auth::hashPassword(password, iterations);
    }
    store.update(login, upd);
    return crow::response{204};
  } catch (const auth::AuthException& e) {
    return storeError(e);
  }
}

} // namespace

void registerUserRoutes(crow::App<AuthMiddleware>& app, auth::UserStore& userStore, uint32_t pbkdf2Iterations) {
  CROW_ROUTE(app, "/users").methods(crow::HTTPMethod::Get)([&app, &userStore](const crow::request& req) {
    crow::response res;
    if (!requireAdmin(app, req, res)) {
      return res;
    }
    return listUsers(userStore, req);
  });

  CROW_ROUTE(app, "/users")
    .methods(crow::HTTPMethod::Post)([&app, &userStore, pbkdf2Iterations](const crow::request& req) {
      crow::response res;
      if (!requireAdmin(app, req, res)) {
        return res;
      }
      return createUser(userStore, pbkdf2Iterations, req);
    });

  CROW_ROUTE(app, "/users/<string>")
    .methods(crow::HTTPMethod::Get, crow::HTTPMethod::Patch, crow::HTTPMethod::Delete)(
      [&app, &userStore, pbkdf2Iterations](const crow::request& req, const std::string& rawLogin) {
        crow::response res;
        if (!requireAdmin(app, req, res)) {
          return res;
        }
        std::string login;
        if (!percentDecode(rawLogin, login)) {
          return badRequest("invalid percent-encoding in login");
        }
        try {
          if (req.method == crow::HTTPMethod::Get) {
            const auto user = userStore.get(login);
            return user ? jsonOk(200, userToJson(*user)) : notFound("user not found");
          }
          if (req.method == crow::HTTPMethod::Delete) {
            userStore.removeGuarded(login);
            return crow::response{204};
          }
          return patchUser(userStore, pbkdf2Iterations, login, req);
        } catch (const auth::AuthException& e) {
          return storeError(e);
        }
      }
    );
}

} // namespace server

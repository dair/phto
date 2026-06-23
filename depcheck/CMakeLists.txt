cmake_minimum_required(VERSION 3.28)

# TEMPORARY scaffolding for plan 0022 checkpoint A1.
# Proves the new Phase 2 dependencies (Crow + cpp-jwt, with their transitive
# asio / OpenSSL / nlohmann_json) resolve, compile, and link together.
# This target is NOT part of the application and should be removed (or replaced)
# once the real server/ daemon is introduced in milestone M-E.
add_executable(depcheck depcheck.cpp)
target_link_libraries(depcheck
    PRIVATE Crow::Crow cpp-jwt::cpp-jwt
)

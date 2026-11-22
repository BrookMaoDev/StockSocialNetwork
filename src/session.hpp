#pragma once

#include <string>

#include <pqxx/pqxx>

struct Session {
    pqxx::connection &conn;
    std::string username; // empty when logged out
};

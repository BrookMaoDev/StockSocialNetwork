#pragma once

#include <pqxx/pqxx>

// Creates the database if needed, applies schema.sql, loads historical data,
// and returns a connection to the app database. --reset drops it first.
pqxx::connection setup_database(bool reset);

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "db.hpp"

static const char SCHEMA_SQL[] = {
#embed "../sql/schema.sql"
    , '\0'
};

static const std::string APP_DB = "stocksocial";
static const char *HISTORY_CSV = "data/SP500History.csv";
static const long SETUP_LOCK = 7428301; // any fixed number, shared by all clients

static std::string env_or(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value ? value : fallback;
}

static std::string conninfo(const std::string &dbname) {
    return "host=" + env_or("PGHOST", "127.0.0.1") +
           " port=" + env_or("PGPORT", "5432") +
           " user=" + env_or("PGUSER", "postgres") +
           " password=" + env_or("PGPASSWORD", "postgres") +
           " dbname=" + dbname;
}

// CREATE/DROP DATABASE can't run in a transaction, so use the maintenance DB.
static void ensure_database(bool reset) {
    pqxx::connection admin(conninfo("postgres"));
    pqxx::nontransaction tx(admin);

    if (reset) {
        tx.exec("DROP DATABASE IF EXISTS " + tx.quote_name(APP_DB) + " WITH (FORCE)");
    }

    bool exists = tx.query_value<bool>(
        "SELECT EXISTS (SELECT 1 FROM pg_database WHERE datname = " + tx.quote(APP_DB) + ")");
    if (!exists) {
        try {
            tx.exec("CREATE DATABASE " + tx.quote_name(APP_DB));
        } catch (const pqxx::sql_error &e) {
            if (e.sqlstate() != "42P04") { // duplicate_database: another client just created it
                throw;
            }
        }
    }
}

// Applies schema.sql only if it changed since it was last applied (its MD5 is
// stored in schema_version), so normal startups don't redefine anything.
static void apply_schema(pqxx::work &tx) {
    tx.exec("CREATE TABLE IF NOT EXISTS schema_version (md5 TEXT NOT NULL)");
    std::string md5 = tx.query_value<std::string>("SELECT md5($1)", pqxx::params{SCHEMA_SQL});
    if (tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM schema_version WHERE md5 = $1)",
                             pqxx::params{md5})) {
        return;
    }
    tx.exec(SCHEMA_SQL);
    tx.exec("DELETE FROM schema_version");
    tx.exec("INSERT INTO schema_version (md5) VALUES ($1)", pqxx::params{md5});
}

// Loads the historical CSV once, while daily_prices is still empty.
static void load_history(pqxx::work &tx) {
    if (tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM daily_prices)")) {
        return;
    }

    std::ifstream csv(HISTORY_CSV);
    if (!csv) {
        std::cerr << "Warning: " << HISTORY_CSV << " not found, skipping history load" << std::endl;
        return;
    }

    // Stage raw text first, then convert to real types in SQL.
    tx.exec("CREATE TEMP TABLE staging (ts TEXT, open TEXT, high TEXT, low TEXT, "
            "close TEXT, volume TEXT, symbol TEXT, line BIGINT) ON COMMIT DROP");

    auto stream = pqxx::stream_to::table(tx, {"staging"});
    std::string line;
    long lines = 0;
    std::getline(csv, line); // skip header
    while (std::getline(csv, line)) {
        std::array<std::string, 8> fields;
        std::stringstream ss(line);
        for (int i = 0; i < 7; ++i) {
            std::getline(ss, fields[i], ',');
        }
        fields[7] = std::to_string(++lines);
        stream.write_row(fields);
    }
    stream.complete();

    long incomplete = tx.query_value<long>(
        "SELECT count(*) FROM staging WHERE '' IN (open, high, low)");
    if (incomplete > 0) {
        std::cerr << "Warning: " << incomplete << " rows missing open/high/low (stored as NULL)" << std::endl;
    }

    tx.exec("INSERT INTO stocks SELECT DISTINCT symbol FROM staging ON CONFLICT DO NOTHING");
    // ORDER BY line: for duplicate (symbol, date) rows, the first one in the file wins.
    auto inserted = tx.exec(R"(
        INSERT INTO daily_prices (symbol, "timestamp", open, high, low, close, volume)
        SELECT symbol, ts::date,
               NULLIF(open, '')::double precision,
               NULLIF(high, '')::double precision,
               NULLIF(low, '')::double precision,
               close::double precision,
               volume::bigint
        FROM staging
        ORDER BY line
        ON CONFLICT DO NOTHING
    )").affected_rows();

    if (inserted < lines) {
        std::cerr << "Warning: skipped " << lines - inserted << " duplicate (symbol, date) rows" << std::endl;
    }
    std::cout << "Loaded " << inserted << " historical prices" << std::endl;
}

pqxx::connection setup_database(bool reset) {
    ensure_database(reset);
    pqxx::connection conn(conninfo(APP_DB));

    // One transaction holding an advisory lock: clients starting at the same time
    // wait their turn here instead of deadlocking on each other's schema changes.
    pqxx::work tx(conn);
    tx.exec("SELECT pg_advisory_xact_lock(" + std::to_string(SETUP_LOCK) + ")");
    apply_schema(tx);
    load_history(tx);
    tx.commit();
    return conn;
}

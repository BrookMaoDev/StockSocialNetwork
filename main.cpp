#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <pqxx/pqxx>

static const char SCHEMA_SQL[] = {
#embed "schema.sql"
    , '\0'
};

static const std::string APP_DB = "stocksocial";
static const char *HISTORY_CSV = "SP500History.csv";

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
        tx.exec("CREATE DATABASE " + tx.quote_name(APP_DB));
    }
}

static void apply_schema(pqxx::connection &conn) {
    pqxx::work tx(conn);
    tx.exec(SCHEMA_SQL);
    tx.commit();
}

// Loads the historical CSV once, while daily_prices is still empty.
static void load_history(pqxx::connection &conn) {
    pqxx::work tx(conn);
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
    tx.commit();
}

int main(int argc, char *argv[]) {
    bool reset = argc > 1 && std::strcmp(argv[1], "--reset") == 0;

    try {
        ensure_database(reset);
        pqxx::connection conn(conninfo(APP_DB));
        apply_schema(conn);
        load_history(conn);
        std::cout << "Connected to " << conn.dbname() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

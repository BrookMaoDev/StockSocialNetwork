#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <pqxx/pqxx>

static const char SCHEMA_SQL[] = {
#embed "schema.sql"
    , '\0'
};

static const std::string APP_DB = "stocksocial";

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

int main(int argc, char *argv[]) {
    bool reset = argc > 1 && std::strcmp(argv[1], "--reset") == 0;

    try {
        ensure_database(reset);
        pqxx::connection conn(conninfo(APP_DB));
        apply_schema(conn);
        std::cout << "Connected to " << conn.dbname() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

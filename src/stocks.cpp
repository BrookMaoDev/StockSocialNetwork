#include "stocks.hpp"

#include <climits>
#include <iomanip>
#include <iostream>
#include <utility>
#include <vector>

#include "plot.hpp"
#include "ui.hpp"

// Menu label -> Postgres interval.
static const std::vector<std::pair<std::string, std::string>> INTERVALS = {
    {"1 week", "7 days"},
    {"1 month", "1 month"},
    {"3 months", "3 months"},
    {"1 year", "1 year"},
    {"5 years", "5 years"},
};

void show_history(Session &s, const std::string &symbol) {
    for (size_t i = 0; i < INTERVALS.size(); ++i) {
        std::cout << i + 1 << ". " << INTERVALS[i].first << "\n";
    }
    int choice = prompt_int("Interval", 1, INTERVALS.size());

    // Counted back from the symbol's latest date, not today: the data may be old.
    pqxx::nontransaction tx(s.conn);
    auto res = tx.exec(R"(
        SELECT "timestamp", open, high, low, close, volume FROM daily_prices
        WHERE symbol = $1
          AND "timestamp" > (SELECT max("timestamp") FROM daily_prices WHERE symbol = $1) - $2::interval
        ORDER BY "timestamp"
    )", pqxx::params{symbol, INTERVALS[choice - 1].second});
    if (res.empty()) {
        std::cout << "No price data for " << symbol << std::endl;
        return;
    }

    std::vector<double> closes;
    for (auto row : res) {
        closes.push_back(row["close"].as<double>());
    }
    std::string first = res.front()["timestamp"].c_str();
    std::string last = res.back()["timestamp"].c_str();

    std::cout << "\n" << symbol << " close, " << first << " to " << last
              << " (" << res.size() << " trading days)\n";
    plot(closes, first, last);
    double change = (closes.back() - closes.front()) / closes.front() * 100;
    std::cout << std::fixed << std::setprecision(2) << "Change: " << closes.front() << " -> "
              << closes.back() << " (" << std::showpos << change << std::noshowpos << "%)"
              << std::defaultfloat << std::endl;

    if (prompt("Show table? (y/N)") == "y") {
        std::cout << std::left << std::setw(12) << "Date" << std::setw(10) << "Open"
                  << std::setw(10) << "High" << std::setw(10) << "Low" << std::setw(10) << "Close"
                  << "Volume\n";
        for (auto row : res) {
            for (int col = 0; col < 5; ++col) {
                std::cout << std::setw(col == 0 ? 12 : 10) << row[col].c_str(); // NULL prints ""
            }
            std::cout << row[5].c_str() << "\n";
        }
        std::cout << std::right;
    }
}

static void add_price(Session &s) {
    std::string symbol = prompt_symbol("Symbol");
    if (symbol.empty() || symbol.size() > 8) {
        std::cout << "Symbol must be 1 to 8 characters" << std::endl;
        return;
    }
    {
        pqxx::nontransaction tx(s.conn);
        bool known = tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM stocks WHERE symbol = $1)",
                                          pqxx::params{symbol});
        if (!known && prompt(symbol + " is a new symbol, add it? (y/N)") != "y") {
            return;
        }
    }
    std::string date = prompt("Date (YYYY-MM-DD)");
    auto open = prompt_decimal("Open (blank if unknown)", true);
    auto high = prompt_decimal("High (blank if unknown)", true);
    auto low = prompt_decimal("Low (blank if unknown)", true);
    std::string close = *prompt_decimal("Close", false);
    int volume = prompt_int("Volume", 0, INT_MAX);

    try {
        pqxx::work tx(s.conn);
        tx.exec("INSERT INTO stocks (symbol) VALUES ($1) ON CONFLICT DO NOTHING", pqxx::params{symbol});
        tx.exec(R"(
            INSERT INTO daily_prices (symbol, "timestamp", open, high, low, close, volume)
            VALUES ($1, $2::date, $3::double precision, $4::double precision,
                    $5::double precision, $6::double precision, $7)
        )", pqxx::params{symbol, date, open, high, low, close, volume});
        tx.commit();
        std::cout << "Added " << symbol << " close " << close << " on " << date << std::endl;
    } catch (const pqxx::unique_violation &) {
        std::cout << symbol << " already has a price for " << date << std::endl;
    } catch (const pqxx::data_exception &) {
        std::cout << "Invalid date " << date << std::endl;
    }
}

void stocks_menu(Session &session) {
    run_menu("Stocks", {
        {"Price history", [&] { show_history(session, prompt_symbol("Symbol")); }},
        {"Add daily price", [&] { add_price(session); }},
    }, "Back");
}

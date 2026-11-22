#include "portfolio.hpp"

#include <climits>
#include <iomanip>
#include <iostream>
#include <optional>

#include "stocks.hpp"
#include "ui.hpp"

// Value of each holding = quantity * latest close, rounded to cents.
static const char *TOTAL_VALUE_SQL = R"(
    SELECT p.cash,
           p.cash + COALESCE(sum(round(h.quantity * latest_close(h.symbol)::numeric, 2)), 0)
    FROM portfolios p
    LEFT JOIN holds h ON h.username = p.username AND h.portfolio_name = p.name
    WHERE p.username = $1 AND p.name = $2
    GROUP BY p.username, p.name
)";

// Lists the user's portfolios and lets them pick one (0 cancels).
static std::optional<std::string> choose_portfolio(Session &s, const std::string &label) {
    pqxx::nontransaction tx(s.conn);
    auto res = tx.exec("SELECT name FROM portfolios WHERE username = $1 ORDER BY name",
                       pqxx::params{s.username});
    if (res.empty()) {
        std::cout << "You have no portfolios" << std::endl;
        return std::nullopt;
    }
    for (int i = 0; i < res.size(); ++i) {
        std::cout << i + 1 << ". " << res[i][0].c_str() << "\n";
    }
    int choice = prompt_int(label + " (0 to cancel)", 0, res.size());
    if (choice == 0) {
        return std::nullopt;
    }
    return res[choice - 1][0].as<std::string>();
}

static void list_portfolios(Session &s) {
    pqxx::nontransaction tx(s.conn);
    auto res = tx.exec("SELECT name FROM portfolios WHERE username = $1 ORDER BY name",
                       pqxx::params{s.username});
    if (res.empty()) {
        std::cout << "You have no portfolios" << std::endl;
        return;
    }
    std::cout << std::left << std::setw(20) << "Name" << std::setw(16) << "Cash" << "Total value\n";
    for (auto row : res) {
        auto name = row[0].as<std::string>();
        auto [cash, total] = tx.query1<std::string, std::string>(
            TOTAL_VALUE_SQL, pqxx::params{s.username, name});
        std::cout << std::setw(20) << name << std::setw(16) << cash << total << "\n";
    }
}

static void create_portfolio(Session &s) {
    std::string name = prompt("Portfolio name");
    if (name.empty() || name.size() > 64) {
        std::cout << "Name must be 1 to 64 characters" << std::endl;
        return;
    }
    try {
        pqxx::work tx(s.conn);
        tx.exec("INSERT INTO portfolios (username, name) VALUES ($1, $2)",
                pqxx::params{s.username, name});
        tx.commit();
        std::cout << "Created portfolio " << name << std::endl;
    } catch (const pqxx::unique_violation &) {
        std::cout << "You already have a portfolio named " << name << std::endl;
    }
}

static void view_portfolio(Session &s, const std::string &name) {
    pqxx::nontransaction tx(s.conn);
    auto holdings = tx.exec(R"(
        SELECT symbol, quantity, latest_close(symbol),
               round(quantity * latest_close(symbol)::numeric, 2)
        FROM holds WHERE username = $1 AND portfolio_name = $2 ORDER BY symbol
    )", pqxx::params{s.username, name});
    auto [cash, total] = tx.query1<std::string, std::string>(
        TOTAL_VALUE_SQL, pqxx::params{s.username, name});

    std::cout << "Cash: " << cash << "\n";
    if (holdings.empty()) {
        std::cout << "No stock holdings\n";
    } else {
        std::cout << std::left << std::setw(8) << "Symbol" << std::setw(10) << "Shares"
                  << std::setw(12) << "Last close" << "Value\n";
        for (auto row : holdings) {
            std::cout << std::setw(8) << row[0].c_str() << std::setw(10) << row[1].c_str()
                      << std::setw(12) << row[2].c_str() << row[3].c_str() << "\n";
        }
    }
    std::cout << "Total market value: " << total << std::endl;
}

static void deposit(Session &s, const std::string &name) {
    std::string amount = prompt_amount("Amount to deposit");
    try {
        pqxx::work tx(s.conn);
        tx.exec("UPDATE portfolios SET cash = cash + $3::numeric WHERE username = $1 AND name = $2",
                pqxx::params{s.username, name, amount});
        tx.commit();
        std::cout << "Deposited " << amount << std::endl;
    } catch (const pqxx::data_exception &) {
        std::cout << "Balance would be too large" << std::endl;
    }
}

static void withdraw(Session &s, const std::string &name) {
    std::string amount = prompt_amount("Amount to withdraw");
    try {
        pqxx::work tx(s.conn);
        tx.exec("UPDATE portfolios SET cash = cash - $3::numeric WHERE username = $1 AND name = $2",
                pqxx::params{s.username, name, amount});
        tx.commit();
        std::cout << "Withdrew " << amount << std::endl;
    } catch (const pqxx::check_violation &) {
        std::cout << "Not enough cash" << std::endl;
    }
}

static void transfer(Session &s, const std::string &from) {
    auto to = choose_portfolio(s, "Transfer to");
    if (!to) {
        return;
    }
    if (*to == from) {
        std::cout << "Choose a different portfolio" << std::endl;
        return;
    }
    std::string amount = prompt_amount("Amount to transfer");
    try {
        // One transaction: money never leaves one portfolio without arriving in the other.
        pqxx::work tx(s.conn);
        const char *sql = "UPDATE portfolios SET cash = cash + $3::numeric WHERE username = $1 AND name = $2";
        tx.exec(sql, pqxx::params{s.username, from, "-" + amount});
        tx.exec(sql, pqxx::params{s.username, *to, amount});
        tx.commit();
        std::cout << "Transferred " << amount << " to " << *to << std::endl;
    } catch (const pqxx::check_violation &) {
        std::cout << "Not enough cash" << std::endl;
    } catch (const pqxx::data_exception &) {
        std::cout << "Balance would be too large" << std::endl;
    }
}

static void buy(Session &s, const std::string &name) {
    std::string symbol = prompt_symbol("Symbol");
    int shares = prompt_int("Shares to buy", 1, INT_MAX);
    try {
        pqxx::work tx(s.conn);
        auto cost = tx.query1<std::optional<std::string>>(
            "SELECT round(latest_close($1)::numeric * $2, 2)", pqxx::params{symbol, shares});
        if (!std::get<0>(cost)) {
            std::cout << "Unknown symbol " << symbol << std::endl;
            return;
        }
        std::string total = *std::get<0>(cost);

        tx.exec("UPDATE portfolios SET cash = cash - $3::numeric WHERE username = $1 AND name = $2",
                pqxx::params{s.username, name, total});
        tx.exec(R"(
            INSERT INTO holds (username, portfolio_name, symbol, quantity) VALUES ($1, $2, $3, $4)
            ON CONFLICT (username, portfolio_name, symbol)
            DO UPDATE SET quantity = holds.quantity + EXCLUDED.quantity
        )", pqxx::params{s.username, name, symbol, shares});
        tx.commit();
        std::cout << "Bought " << shares << " " << symbol << " for " << total << std::endl;
    } catch (const pqxx::check_violation &) {
        std::cout << "Not enough cash" << std::endl;
    } catch (const pqxx::data_exception &) {
        std::cout << "Too many shares" << std::endl;
    }
}

static void sell(Session &s, const std::string &name) {
    std::string symbol = prompt_symbol("Symbol");
    int shares = prompt_int("Shares to sell", 1, INT_MAX);
    try {
        pqxx::work tx(s.conn);
        auto proceeds = tx.query1<std::optional<std::string>>(
            "SELECT round(latest_close($1)::numeric * $2, 2)", pqxx::params{symbol, shares});
        if (!std::get<0>(proceeds)) {
            std::cout << "Unknown symbol " << symbol << std::endl;
            return;
        }
        std::string total = *std::get<0>(proceeds);

        // FOR UPDATE locks the row until commit, so a concurrent sell can't
        // read the same quantity before we change it.
        pqxx::result res = tx.exec(
            "SELECT quantity FROM holds WHERE username = $1 AND portfolio_name = $2 AND symbol = $3 FOR UPDATE",
            pqxx::params{s.username, name, symbol});
        int held = res.empty() ? 0 : res[0][0].as<int>();
        if (held < shares) {
            std::cout << "You only own " << held << " shares of " << symbol << std::endl;
            return;
        }

        tx.exec("UPDATE portfolios SET cash = cash + $3::numeric WHERE username = $1 AND name = $2",
                pqxx::params{s.username, name, total});
        if (held == shares) {
            // Selling everything: delete, since holds only keeps quantities > 0.
            tx.exec("DELETE FROM holds WHERE username = $1 AND portfolio_name = $2 AND symbol = $3",
                    pqxx::params{s.username, name, symbol});
        } else {
            tx.exec("UPDATE holds SET quantity = quantity - $4 WHERE username = $1 AND portfolio_name = $2 AND symbol = $3",
                    pqxx::params{s.username, name, symbol, shares});
        }
        tx.commit();
        std::cout << "Sold " << shares << " " << symbol << " for " << total << std::endl;
    } catch (const pqxx::data_exception &) {
        std::cout << "Balance would be too large" << std::endl;
    }
}

// Lets the user pick one of the portfolio's holdings and shows its history.
static void holding_history(Session &s, const std::string &name) {
    pqxx::result res;
    {
        // Scoped: tx must end before show_history starts its own (one per connection).
        pqxx::nontransaction tx(s.conn);
        res = tx.exec("SELECT symbol FROM holds WHERE username = $1 AND portfolio_name = $2 ORDER BY symbol",
                      pqxx::params{s.username, name});
    }
    if (res.empty()) {
        std::cout << "No stock holdings" << std::endl;
        return;
    }
    for (int i = 0; i < res.size(); ++i) {
        std::cout << i + 1 << ". " << res[i][0].c_str() << "\n";
    }
    int choice = prompt_int("Holding (0 to cancel)", 0, res.size());
    if (choice != 0) {
        show_history(s, res[choice - 1][0].as<std::string>());
    }
}

static void portfolio_menu(Session &s, const std::string &name) {
    run_menu("Portfolio: " + name, {
        {"View", [&] { view_portfolio(s, name); }},
        {"Deposit cash", [&] { deposit(s, name); }},
        {"Withdraw cash", [&] { withdraw(s, name); }},
        {"Transfer cash to another portfolio", [&] { transfer(s, name); }},
        {"Buy stock", [&] { buy(s, name); }},
        {"Sell stock", [&] { sell(s, name); }},
        {"Price history of a holding", [&] { holding_history(s, name); }},
    }, "Back");
}

void portfolios_menu(Session &session) {
    run_menu("Portfolios", {
        {"List portfolios", [&] { list_portfolios(session); }},
        {"Create portfolio", [&] { create_portfolio(session); }},
        {"Open portfolio", [&] {
            if (auto name = choose_portfolio(session, "Open")) {
                portfolio_menu(session, *name);
            }
        }},
    }, "Back");
}

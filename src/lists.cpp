#include "lists.hpp"

#include <climits>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "friends.hpp"
#include "ui.hpp"

struct List {
    int id;
    std::string name, creator, visibility;
};

static std::string describe(const List &l) {
    return l.name + " [" + l.visibility + "] by " + l.creator;
}

// Every list the user can see (see can_access in schema.sql), their own first.
static std::vector<List> accessible_lists(Session &s) {
    pqxx::nontransaction tx(s.conn);
    std::vector<List> lists;
    for (auto [id, name, creator, visibility] : tx.query<int, std::string, std::string, std::string>(
             "SELECT id, name, creator, visibility FROM lists WHERE can_access(id, $1) "
             "ORDER BY creator <> $1, creator, name",
             pqxx::params{s.username})) {
        lists.push_back({id, name, creator, visibility});
    }
    return lists;
}

static std::vector<std::string> shared_with(Session &s, int list_id) {
    pqxx::nontransaction tx(s.conn);
    std::vector<std::string> names;
    for (auto [name] : tx.query<std::string>(
             "SELECT username FROM shared_with WHERE list_id = $1 ORDER BY username",
             pqxx::params{list_id})) {
        names.push_back(name);
    }
    return names;
}

static void view_lists(Session &s) {
    auto lists = accessible_lists(s);
    if (lists.empty()) {
        std::cout << "No stock lists" << std::endl;
    }
    for (const auto &l : lists) {
        std::cout << "  " << describe(l) << "\n";
    }
}

static void create_list(Session &s) {
    std::string name = prompt("List name");
    if (name.empty() || name.size() > 64) {
        std::cout << "Name must be 1 to 64 characters" << std::endl;
        return;
    }
    pqxx::work tx(s.conn);
    tx.exec("INSERT INTO lists (creator, name) VALUES ($1, $2)", pqxx::params{s.username, name});
    tx.commit();
    std::cout << "Created private list " << name << std::endl;
}

static void view_list(Session &s, const List &l) {
    std::vector<std::string> shares = shared_with(s, l.id);
    pqxx::nontransaction tx(s.conn);
    auto rows = tx.exec(R"(
        SELECT symbol, quantity, latest_close(symbol),
               round(quantity * latest_close(symbol)::numeric, 2)
        FROM contains WHERE list_id = $1 ORDER BY symbol
    )", pqxx::params{l.id});

    std::cout << describe(l) << "\n";
    if (l.creator == s.username && l.visibility == "shared") {
        std::cout << "Shared with: ";
        for (const auto &name : shares) {
            std::cout << name << " ";
        }
        std::cout << (shares.empty() ? "nobody yet" : "") << "\n";
    }
    if (rows.empty()) {
        std::cout << "No stocks in this list" << std::endl;
        return;
    }
    std::cout << std::left << std::setw(8) << "Symbol" << std::setw(10) << "Shares"
              << std::setw(12) << "Last close" << "Value\n";
    for (auto row : rows) {
        std::cout << std::setw(8) << row[0].c_str() << std::setw(10) << row[1].c_str()
                  << std::setw(12) << row[2].c_str() << row[3].c_str() << "\n";
    }
    std::cout << std::right << std::flush;
}

static void set_stock(Session &s, const List &l) {
    std::string symbol = prompt_symbol("Symbol");
    int shares = prompt_int("Shares", 1, INT_MAX);
    try {
        pqxx::work tx(s.conn);
        tx.exec(R"(
            INSERT INTO contains (list_id, symbol, quantity) VALUES ($1, $2, $3)
            ON CONFLICT (list_id, symbol) DO UPDATE SET quantity = EXCLUDED.quantity
        )", pqxx::params{l.id, symbol, shares});
        tx.commit();
        std::cout << l.name << " now has " << shares << " " << symbol << std::endl;
    } catch (const pqxx::foreign_key_violation &) {
        std::cout << "Unknown symbol " << symbol << std::endl;
    }
}

static void remove_stock(Session &s, const List &l) {
    std::vector<std::string> symbols;
    {
        pqxx::nontransaction tx(s.conn);
        for (auto [symbol] : tx.query<std::string>(
                 "SELECT symbol FROM contains WHERE list_id = $1 ORDER BY symbol", pqxx::params{l.id})) {
            symbols.push_back(symbol);
        }
    }
    auto choice = choose(symbols, "Remove", "No stocks in this list");
    if (!choice) {
        return;
    }
    pqxx::work tx(s.conn);
    tx.exec("DELETE FROM contains WHERE list_id = $1 AND symbol = $2", pqxx::params{l.id, symbols[*choice]});
    tx.commit();
    std::cout << "Removed " << symbols[*choice] << std::endl;
}

static void set_visibility(Session &s, List &l) {
    static const std::vector<std::string> options = {"private", "shared", "public"};
    auto choice = choose(options, "Visibility", "");
    if (!choice) {
        return;
    }
    pqxx::work tx(s.conn);
    tx.exec("UPDATE lists SET visibility = $3 WHERE id = $1 AND creator = $2",
            pqxx::params{l.id, s.username, options[*choice]});
    tx.commit();
    l.visibility = options[*choice];
    std::cout << l.name << " is now " << l.visibility << std::endl;
}

static void share(Session &s, List &l) {
    auto friends = friends_of(s);
    auto choice = choose(friends, "Share with", "You have no friends to share with");
    if (!choice) {
        return;
    }
    pqxx::work tx(s.conn);
    tx.exec("INSERT INTO shared_with (list_id, username) VALUES ($1, $2) ON CONFLICT DO NOTHING",
            pqxx::params{l.id, friends[*choice]});
    if (l.visibility == "private") {
        tx.exec("UPDATE lists SET visibility = 'shared' WHERE id = $1", pqxx::params{l.id});
        l.visibility = "shared";
    }
    tx.commit();
    std::cout << "Shared " << l.name << " with " << friends[*choice] << std::endl;
    if (l.visibility == "public") {
        std::cout << "(It's public, so everyone can see it anyway)" << std::endl;
    }
}

static void unshare(Session &s, const List &l) {
    auto names = shared_with(s, l.id);
    auto choice = choose(names, "Unshare with", "Not shared with anyone");
    if (!choice) {
        return;
    }
    pqxx::work tx(s.conn);
    tx.exec("DELETE FROM shared_with WHERE list_id = $1 AND username = $2",
            pqxx::params{l.id, names[*choice]});
    tx.commit();
    std::cout << "Unshared " << l.name << " with " << names[*choice] << std::endl;
}

// Returns true if the list was deleted.
static bool delete_list(Session &s, const List &l) {
    if (prompt("Delete " + l.name + " and its reviews? (y/N)") != "y") {
        return false;
    }
    pqxx::work tx(s.conn);
    tx.exec("DELETE FROM lists WHERE id = $1 AND creator = $2", pqxx::params{l.id, s.username});
    tx.commit();
    std::cout << "Deleted " << l.name << std::endl;
    return true;
}

static void list_menu(Session &s, List l) {
    bool deleted = false;
    std::vector<MenuItem> items = {
        {"View", [&] { view_list(s, l); }},
    };
    if (l.creator == s.username) {
        items.insert(items.end(), {
            {"Add or update stock", [&] { set_stock(s, l); }},
            {"Remove stock", [&] { remove_stock(s, l); }},
            {"Set visibility", [&] { set_visibility(s, l); }},
            {"Share with a friend", [&] { share(s, l); }},
            {"Unshare", [&] { unshare(s, l); }},
            {"Delete list", [&] { deleted = delete_list(s, l); }},
        });
    }
    run_menu("List: " + l.name, items, "Back", [&] { return deleted; });
}

void lists_menu(Session &session) {
    run_menu("Stock lists", {
        {"View accessible lists", [&] { view_lists(session); }},
        {"Create list", [&] { create_list(session); }},
        {"Open list", [&] {
            auto lists = accessible_lists(session);
            std::vector<std::string> labels;
            for (const auto &l : lists) {
                labels.push_back(describe(l));
            }
            if (auto choice = choose(labels, "Open", "No stock lists")) {
                list_menu(session, lists[*choice]);
            }
        }},
    }, "Back");
}

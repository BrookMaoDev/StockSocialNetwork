#include <cstring>
#include <iostream>

#include "auth.hpp"
#include "db.hpp"
#include "session.hpp"
#include "ui.hpp"

static void not_implemented() {
    std::cout << "Not implemented yet" << std::endl;
}

static void main_menu(Session &session) {
    run_menu("Logged in as " + session.username, {
        {"Portfolios", not_implemented},
        {"Stocks", not_implemented},
        {"Friends", not_implemented},
        {"Stock lists", not_implemented},
        {"Log out", [&] { session.username.clear(); }},
    }, "Quit", [&] { return session.username.empty(); });
}

int main(int argc, char *argv[]) {
    bool reset = argc > 1 && std::strcmp(argv[1], "--reset") == 0;

    try {
        pqxx::connection conn = setup_database(reset);
        Session session{conn, ""};

        run_menu("StockSocial", {
            {"Register", [&] { register_user(session); }},
            {"Log in", [&] {
                if (login(session)) {
                    main_menu(session);
                }
            }},
        }, "Quit", [&] { return !session.username.empty(); });
    } catch (const InputClosed &) {
        std::cout << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

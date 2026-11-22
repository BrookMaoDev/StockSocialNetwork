#include "friends.hpp"

#include <iostream>
#include <string>
#include <vector>

#include "ui.hpp"

// One friends row per pair, stored in either order:
//   pending            requester sent the request to recipient
//   accepted           they're friends
//   rejected/removed   recipient rejected/removed requester, so requester is
//                      the one who must wait COOLDOWN_SECONDS to request again

static const int COOLDOWN_SECONDS = 5 * 60;

// Matches the friends row for the pair ($1, $2) in either order, using the
// friends_unordered_pair index.
static const std::string PAIR = R"(
    LEAST(requester, recipient) = LEAST($1::varchar, $2::varchar)
    AND GREATEST(requester, recipient) = GREATEST($1::varchar, $2::varchar)
)";

// Runs a query whose first column is a username and returns those names.
static std::vector<std::string> names(Session &s, const std::string &sql) {
    pqxx::nontransaction tx(s.conn);
    std::vector<std::string> result;
    for (auto [name] : tx.query<std::string>(sql, pqxx::params{s.username})) {
        result.push_back(name);
    }
    return result;
}

static std::vector<std::string> friends_of(Session &s) {
    return names(s, R"(
        SELECT CASE WHEN requester = $1 THEN recipient ELSE requester END AS friend
        FROM friends WHERE $1 IN (requester, recipient) AND status = 'accepted'
        ORDER BY friend
    )");
}

static std::vector<std::string> incoming(Session &s) {
    return names(s, "SELECT requester FROM friends WHERE recipient = $1 AND status = 'pending' ORDER BY requester");
}

static std::vector<std::string> outgoing(Session &s) {
    return names(s, "SELECT recipient FROM friends WHERE requester = $1 AND status = 'pending' ORDER BY recipient");
}

static void print_names(const std::vector<std::string> &list, const std::string &empty_message) {
    if (list.empty()) {
        std::cout << empty_message << std::endl;
    }
    for (const auto &name : list) {
        std::cout << "  " << name << "\n";
    }
}

static void send_request(Session &s) {
    std::string other = prompt("Send request to");
    if (other == s.username) {
        std::cout << "You can't send a request to yourself" << std::endl;
        return;
    }
    try {
        pqxx::work tx(s.conn);
        if (!tx.query_value<bool>("SELECT EXISTS (SELECT 1 FROM users WHERE username = $1)",
                                  pqxx::params{other})) {
            std::cout << "No user named " << other << std::endl;
            return;
        }

        // Lock the pair's row (if any) so the checks below can't race.
        auto res = tx.exec("SELECT requester, status, extract(epoch FROM now() - last_changed)::int "
                           "FROM friends WHERE " + PAIR + " FOR UPDATE",
                           pqxx::params{s.username, other});
        if (res.empty()) {
            tx.exec("INSERT INTO friends (requester, recipient) VALUES ($1, $2)",
                    pqxx::params{s.username, other});
        } else {
            auto requester = res[0][0].as<std::string>();
            auto status = res[0][1].as<std::string>();
            int elapsed = res[0][2].as<int>();

            // Early returns without commit() roll back, which also releases the lock.
            if (status == "accepted") {
                std::cout << "You're already friends with " << other << std::endl;
                return;
            }
            if (status == "pending" && requester == s.username) {
                std::cout << "You've already sent " << other << " a friend request" << std::endl;
                return;
            }
            if (status == "pending") { // they already asked us: accept it
                tx.exec("UPDATE friends SET status = 'accepted', last_changed = now() WHERE " + PAIR,
                        pqxx::params{s.username, other});
                tx.commit();
                std::cout << other << " had already sent you a request, you are now friends" << std::endl;
                return;
            }
            if (requester == s.username && elapsed < COOLDOWN_SECONDS) { // we were rejected/removed
                std::cout << "You can send " << other << " another request in "
                          << COOLDOWN_SECONDS - elapsed << " seconds" << std::endl;
                return;
            }

            // Reuse the row as a new pending request from us.
            tx.exec("UPDATE friends SET requester = $1, recipient = $2, status = 'pending', "
                    "last_changed = now() WHERE " + PAIR,
                    pqxx::params{s.username, other});
        }
        tx.commit();
        std::cout << "Sent friend request to " << other << std::endl;
    } catch (const pqxx::unique_violation &) {
        // Both users sent a first request at the same moment.
        std::cout << other << " just sent you a request, check incoming requests" << std::endl;
    }
}

static void respond_to_request(Session &s) {
    auto requests = incoming(s);
    auto choice = choose(requests, "Request", "No incoming requests");
    if (!choice) {
        return;
    }
    const std::string &other = requests[*choice];
    int action = prompt_int("1. Accept  2. Reject  0. Cancel", 0, 2);
    if (action == 0) {
        return;
    }
    std::string status = action == 1 ? "accepted" : "rejected";

    pqxx::work tx(s.conn);
    tx.exec("UPDATE friends SET status = $3, last_changed = now() "
            "WHERE requester = $1 AND recipient = $2 AND status = 'pending'",
            pqxx::params{other, s.username, status});
    tx.commit();
    std::cout << (action == 1 ? "You are now friends with " : "Rejected request from ") << other << std::endl;
}

static void remove_friend(Session &s) {
    auto friends = friends_of(s);
    auto choice = choose(friends, "Remove", "You have no friends yet");
    if (!choice) {
        return;
    }
    const std::string &other = friends[*choice];

    // Rewrite the row so the removed friend is the requester (on cooldown).
    // The friends_unshare_on_unfriend trigger unshares lists between them.
    pqxx::work tx(s.conn);
    tx.exec("UPDATE friends SET requester = $2, recipient = $1, status = 'removed', "
            "last_changed = now() WHERE " + PAIR + " AND status = 'accepted'",
            pqxx::params{s.username, other});
    tx.commit();
    std::cout << "Removed " << other << " from your friends" << std::endl;
}

void friends_menu(Session &session) {
    run_menu("Friends", {
        {"View friends", [&] { print_names(friends_of(session), "You have no friends yet"); }},
        {"Incoming requests", [&] { respond_to_request(session); }},
        {"Outgoing requests", [&] { print_names(outgoing(session), "No outgoing requests"); }},
        {"Send friend request", [&] { send_request(session); }},
        {"Remove friend", [&] { remove_friend(session); }},
    }, "Back");
}

#include "auth.hpp"

#include <iostream>
#include <stdexcept>

#include <crypt.h>

#include "ui.hpp"

static const size_t MAX_USERNAME = 32; // matches users.username VARCHAR(32)

// Hashes with a fresh random salt using libxcrypt's default (yescrypt). The
// result embeds the algorithm, cost and salt, e.g. "$y$j9T$<salt>$<hash>".
static std::string hash_password(const std::string &password) {
    char salt[CRYPT_GENSALT_OUTPUT_SIZE];
    if (!crypt_gensalt_rn(nullptr, 0, nullptr, 0, salt, sizeof salt)) {
        throw std::runtime_error("crypt_gensalt failed");
    }
    crypt_data data{};
    const char *hash = crypt_rn(password.c_str(), salt, &data, sizeof data);
    if (!hash) {
        throw std::runtime_error("crypt failed");
    }
    return hash;
}

// Re-hashes the password using the stored hash as the settings (same algorithm
// and salt), then compares.
static bool verify_password(const std::string &password, const std::string &stored) {
    crypt_data data{};
    const char *hash = crypt_rn(password.c_str(), stored.c_str(), &data, sizeof data);
    return hash && stored == hash;
}

void register_user(Session &session) {
    std::string username = prompt("Username");
    if (username.empty() || username.size() > MAX_USERNAME) {
        std::cout << "Username must be 1 to " << MAX_USERNAME << " characters" << std::endl;
        return;
    }

    std::string password = prompt_password("Password");
    if (password.empty()) {
        std::cout << "Password can't be empty" << std::endl;
        return;
    }
    if (prompt_password("Confirm password") != password) {
        std::cout << "Passwords don't match" << std::endl;
        return;
    }

    try {
        pqxx::work tx(session.conn);
        tx.exec("INSERT INTO users (username, password) VALUES ($1, $2)",
                pqxx::params{username, hash_password(password)});
        tx.commit();
        std::cout << "Registered " << username << ", you can now log in" << std::endl;
    } catch (const pqxx::unique_violation &) {
        std::cout << "Username already taken" << std::endl;
    }
}

bool login(Session &session) {
    std::string username = prompt("Username");
    std::string password = prompt_password("Password");

    pqxx::nontransaction tx(session.conn);
    pqxx::result res = tx.exec("SELECT password FROM users WHERE username = $1",
                               pqxx::params{username});

    // Same message for an unknown username and a wrong password, so failed logins
    // don't hint at which one was wrong (registration still reveals taken names).
    if (res.empty() || !verify_password(password, res[0][0].as<std::string>())) {
        std::cout << "Invalid username or password" << std::endl;
        return false;
    }
    session.username = username;
    return true;
}

#include "reviews.hpp"

#include <iostream>
#include <string>
#include <vector>

#include "ui.hpp"

void view_reviews(Session &s, int list_id) {
    // Public lists: everyone sees all reviews. Otherwise: only the reviewer and the list's creator.
    pqxx::nontransaction tx(s.conn);
    auto rows = tx.exec(R"(
        SELECT r.username, to_char(r.last_updated, 'YYYY-MM-DD HH24:MI'), r.content
        FROM reviews r JOIN lists l ON l.id = r.list_id
        WHERE r.list_id = $1
          AND (l.visibility = 'public' OR r.username = $2 OR l.creator = $2)
        ORDER BY r.last_updated DESC
    )", pqxx::params{list_id, s.username});

    if (rows.empty()) {
        std::cout << "No reviews you can see" << std::endl;
        return;
    }
    for (auto row : rows) {
        std::cout << "\n" << row[0].c_str() << " (" << row[1].c_str() << "):\n";
        // Indent every line of the review.
        std::string content = row[2].as<std::string>();
        size_t start = 0;
        while (start <= content.size()) {
            size_t end = content.find('\n', start);
            if (end == std::string::npos) {
                end = content.size();
            }
            std::cout << "  " << content.substr(start, end - start) << "\n";
            start = end + 1;
        }
    }
    std::cout << std::flush;
}

void write_review(Session &s, int list_id) {
    {
        pqxx::nontransaction tx(s.conn);
        auto old = tx.exec("SELECT content FROM reviews WHERE list_id = $1 AND username = $2",
                           pqxx::params{list_id, s.username});
        if (!old.empty()) {
            std::cout << "Your current review:\n" << old[0][0].c_str() << "\n\n";
        }
    }
    std::string content = prompt_multiline("Your review");
    if (content.empty()) {
        std::cout << "Cancelled" << std::endl;
        return;
    }
    try {
        pqxx::work tx(s.conn);
        tx.exec(R"(
            INSERT INTO reviews (list_id, username, content) VALUES ($1, $2, $3)
            ON CONFLICT (list_id, username)
            DO UPDATE SET content = EXCLUDED.content, last_updated = now()
        )", pqxx::params{list_id, s.username, content});
        tx.commit();
        std::cout << "Saved your review" << std::endl;
    } catch (const pqxx::data_exception &) {
        std::cout << "Reviews can be at most 4000 characters" << std::endl;
    } catch (const pqxx::plpgsql_raise &) {
        std::cout << "You can only review lists you can see" << std::endl;
    }
}

void delete_review(Session &s, int list_id) {
    // Reviewers can delete their own review; the creator can delete any.
    const std::string can_delete = R"(
        list_id = $1 AND (username = $2 OR (SELECT creator FROM lists WHERE id = $1) = $2)
    )";
    std::vector<std::string> reviewers;
    {
        pqxx::nontransaction tx(s.conn);
        for (auto [name] : tx.query<std::string>(
                 "SELECT username FROM reviews WHERE " + can_delete + " ORDER BY username",
                 pqxx::params{list_id, s.username})) {
            reviewers.push_back(name);
        }
    }
    auto choice = choose(reviewers, "Delete review by", "No reviews you can delete");
    if (!choice || !confirm("Delete the review by " + reviewers[*choice] + "?")) {
        return;
    }
    pqxx::work tx(s.conn);
    tx.exec("DELETE FROM reviews WHERE " + can_delete + " AND username = $3",
            pqxx::params{list_id, s.username, reviewers[*choice]});
    tx.commit();
    std::cout << "Deleted review by " << reviewers[*choice] << std::endl;
}

void my_reviews(Session &s) {
    std::vector<int> list_ids;
    std::vector<std::string> labels;
    {
        pqxx::nontransaction tx(s.conn);
        for (auto [id, name, creator, content] : tx.query<int, std::string, std::string, std::string>(
                 "SELECT l.id, l.name, l.creator, r.content FROM reviews r JOIN lists l ON l.id = r.list_id "
                 "WHERE r.username = $1 ORDER BY l.name, l.creator",
                 pqxx::params{s.username})) {
            std::string preview = content.substr(0, content.find('\n')).substr(0, 40);
            list_ids.push_back(id);
            labels.push_back(name + " by " + creator + ": " + preview);
        }
    }
    auto choice = choose(labels, "Delete review on", "You haven't written any reviews");
    if (!choice || !confirm("Delete your review?")) {
        return;
    }
    pqxx::work tx(s.conn);
    tx.exec("DELETE FROM reviews WHERE list_id = $1 AND username = $2",
            pqxx::params{list_ids[*choice], s.username});
    tx.commit();
    std::cout << "Deleted your review" << std::endl;
}

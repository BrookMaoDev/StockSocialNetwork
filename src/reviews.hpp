#pragma once

#include "session.hpp"

// All three assume the user can already see the list (they're reached from it).
void view_reviews(Session &session, int list_id);
void write_review(Session &session, int list_id);
void delete_review(Session &session, int list_id);

// Lists the user's reviews on every list, including ones they can no longer
// open, and lets them delete one.
void my_reviews(Session &session);

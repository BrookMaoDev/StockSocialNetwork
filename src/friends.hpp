#pragma once

#include <string>
#include <vector>

#include "session.hpp"

void friends_menu(Session &session);

// Usernames of the user's accepted friends, sorted.
std::vector<std::string> friends_of(Session &session);

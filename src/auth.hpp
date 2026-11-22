#pragma once

#include "session.hpp"

void register_user(Session &session);

// On success, sets session.username and returns true.
bool login(Session &session);

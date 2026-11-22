#pragma once

#include <string>

#include "session.hpp"

void stocks_menu(Session &session);

// Asks for an interval, then plots (and optionally tables) the symbol's closes.
void show_history(Session &session, const std::string &symbol);

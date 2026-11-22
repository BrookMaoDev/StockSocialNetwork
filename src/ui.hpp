#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Thrown when stdin closes (Ctrl+D), so the app can exit cleanly from anywhere.
struct InputClosed : std::runtime_error {
    InputClosed() : std::runtime_error("input closed") {}
};

// Reads one line, with leading/trailing whitespace removed.
std::string prompt(const std::string &label);
// Reads one line without echoing it to the terminal. Not trimmed.
std::string prompt_password(const std::string &label);
// Reads a stock symbol, uppercased.
std::string prompt_symbol(const std::string &label);

// Reads a non-negative decimal number as text. If `optional`, a blank line
// returns nullopt (stored as NULL).
std::optional<std::string> prompt_decimal(const std::string &label, bool optional);

// Reads a positive amount of money (at most 2 decimals). Returned as text so it
// goes into NUMERIC columns exactly, without floating point rounding.
std::string prompt_amount(const std::string &label);
int prompt_int(const std::string &label, int min, int max);

// Prints `options` numbered from 1 and returns the index of the chosen one, or
// nullopt if the user enters 0. Prints `empty_message` if there are no options.
std::optional<int> choose(const std::vector<std::string> &options, const std::string &label,
                          const std::string &empty_message);

struct MenuItem {
    std::string label;
    std::function<void()> action;
};

// Shows the items numbered from 1, plus "0. <exit_label>". Repeats until 0 is
// chosen, or until `done` returns true (checked after each action).
void run_menu(const std::string &title, const std::vector<MenuItem> &items,
              const std::string &exit_label, const std::function<bool()> &done = nullptr);

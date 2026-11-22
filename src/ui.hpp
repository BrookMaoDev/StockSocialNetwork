#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

// Thrown when stdin closes (Ctrl+D), so the app can exit cleanly from anywhere.
struct InputClosed : std::runtime_error {
    InputClosed() : std::runtime_error("input closed") {}
};

// Reads one line, with leading/trailing whitespace removed.
std::string prompt(const std::string &label);
int prompt_int(const std::string &label, int min, int max);

struct MenuItem {
    std::string label;
    std::function<void()> action;
};

// Shows the items numbered from 1, plus "0. <exit_label>". Repeats until 0 is
// chosen, or until `done` returns true (checked after each action).
void run_menu(const std::string &title, const std::vector<MenuItem> &items,
              const std::string &exit_label, const std::function<bool()> &done = nullptr);

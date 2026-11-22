#include "ui.hpp"

#include <iostream>

#include <termios.h>
#include <unistd.h>

static std::string trim(const std::string &s) {
    const char *whitespace = " \t\r";
    size_t start = s.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(whitespace);
    return s.substr(start, end - start + 1);
}

std::string prompt(const std::string &label) {
    std::cout << label << ": " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        throw InputClosed();
    }
    return trim(line);
}

std::string prompt_password(const std::string &label) {
    std::cout << label << ": " << std::flush;

    // Turn off terminal echo (skipped when input is piped, e.g. in tests).
    termios old;
    bool tty = tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios quiet = old;
        quiet.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }

    std::string line;
    bool ok = static_cast<bool>(std::getline(std::cin, line));

    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        std::cout << std::endl; // the user's Enter wasn't echoed
    }
    if (!ok) {
        throw InputClosed();
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

// Keeps asking until the user enters a whole number in [min, max].
int prompt_int(const std::string &label, int min, int max) {
    while (true) {
        std::string raw = prompt(label); // outside try: InputClosed must propagate
        try {
            size_t pos;
            int value = std::stoi(raw, &pos);
            if (pos == raw.size() && value >= min && value <= max) {
                return value;
            }
        } catch (const std::exception &) {
            // not a number, or too large for int
        }
        std::cout << "Enter a whole number from " << min << " to " << max << std::endl;
    }
}

void run_menu(const std::string &title, const std::vector<MenuItem> &items,
              const std::string &exit_label, const std::function<bool()> &done) {
    while (true) {
        std::cout << "\n== " << title << " ==\n";
        for (size_t i = 0; i < items.size(); ++i) {
            std::cout << i + 1 << ". " << items[i].label << "\n";
        }
        std::cout << "0. " << exit_label << "\n";

        int choice = prompt_int("Choose", 0, items.size());
        if (choice == 0) {
            return;
        }
        items[choice - 1].action();
        if (done && done()) {
            return;
        }
    }
}

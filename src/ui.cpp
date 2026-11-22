#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>

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

std::string prompt_multiline(const std::string &label) {
    std::cout << label << " (end with an empty line):" << std::endl;
    std::string text, line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            throw InputClosed();
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            return text;
        }
        if (!text.empty()) {
            text += '\n';
        }
        text += line;
    }
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

std::string prompt_symbol(const std::string &label) {
    std::string s = prompt(label);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

bool confirm(const std::string &question) {
    std::string answer = prompt(question + " (y/N)");
    std::transform(answer.begin(), answer.end(), answer.begin(), ::tolower);
    return answer == "y" || answer == "yes";
}

std::optional<std::string> prompt_decimal(const std::string &label, bool optional) {
    static const std::regex decimal(R"(\d+(\.\d+)?)");
    while (true) {
        std::string raw = prompt(label);
        if (raw.empty() && optional) {
            return std::nullopt;
        }
        bool positive = raw.find_first_not_of("0.") != std::string::npos;
        if (std::regex_match(raw, decimal) && positive) {
            return raw;
        }
        std::cout << "Enter a positive number like 42 or 158.37" << std::endl;
    }
}

std::string prompt_amount(const std::string &label) {
    // Up to 13 digits before the point: fits NUMERIC(15,2).
    static const std::regex money(R"(\d{1,13}(\.\d{1,2})?)");
    while (true) {
        std::string raw = prompt(label);
        bool positive = raw.find_first_not_of("0.") != std::string::npos;
        if (std::regex_match(raw, money) && positive) {
            return raw;
        }
        std::cout << "Enter a positive amount like 100 or 25.50 (at most 9999999999999.99)" << std::endl;
    }
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

std::optional<int> choose(const std::vector<std::string> &options, const std::string &label,
                          const std::string &empty_message) {
    if (options.empty()) {
        std::cout << empty_message << std::endl;
        return std::nullopt;
    }
    for (size_t i = 0; i < options.size(); ++i) {
        std::cout << i + 1 << ". " << options[i] << "\n";
    }
    int choice = prompt_int(label + " (0 to cancel)", 0, options.size());
    if (choice == 0) {
        return std::nullopt;
    }
    return choice - 1;
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

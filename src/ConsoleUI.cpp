#include "ConsoleUI.h"
#include "Validation.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
bool consoleSupportsColour() {
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) {
        return false;
    }
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return SetConsoleMode(handle, mode) != 0;
}
#else
bool consoleSupportsColour() {
    const char* term = std::getenv("TERM");
    return term != nullptr && std::string(term) != "dumb";
}
#endif
}

std::string ConsoleUI::trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string ConsoleUI::nowTime() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

bool ConsoleUI::colourEnabled() const {
    static const bool enabled = consoleSupportsColour();
    return enabled;
}

void ConsoleUI::colour(Tone tone) const {
    if (!colourEnabled()) {
        return;
    }
    switch (tone) {
        case Tone::Success: std::cout << "\033[32m"; break;
        case Tone::Error:   std::cout << "\033[31m"; break;
        case Tone::Warning: std::cout << "\033[33m"; break;
        case Tone::Status:  std::cout << "\033[36m"; break;
        default: break;
    }
}

void ConsoleUI::resetColour() const {
    if (colourEnabled()) {
        std::cout << "\033[0m";
    }
}

void ConsoleUI::clearScreen() const {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void ConsoleUI::printBanner() const {
    std::cout << "============================================================\n";
    std::cout << "  SecureTrace  |  Digital Evidence & Case Management\n";
    std::cout << "  UTeM  |  Faculty of AI and Cyber Security (FAIX)\n";
    std::cout << "============================================================\n";
}

void ConsoleUI::printHeader(const std::string& title) const {
    std::cout << "\n--- " << title << " ---\n";
}

void ConsoleUI::printError(const std::string& message) const {
    colour(Tone::Error);
    std::cout << "[ERROR] " << message << "\n";
    resetColour();
}

void ConsoleUI::printSuccess(const std::string& message) const {
    colour(Tone::Success);
    std::cout << "[OK] " << message << "\n";
    resetColour();
}

void ConsoleUI::printWarning(const std::string& message) const {
    colour(Tone::Warning);
    std::cout << "[WARN] " << message << "\n";
    resetColour();
}

void ConsoleUI::printStatusBar(const std::string& username, const std::string& role) const {
    colour(Tone::Status);
    std::cout << "User: " << username << " | Role: " << role << " | " << nowTime() << "\n";
    resetColour();
}

void ConsoleUI::pause() const {
    std::cout << "\nPress Enter to continue...";
    std::cin.clear();
    std::string dummy;
    std::getline(std::cin, dummy);
}

std::string ConsoleUI::readLine(const std::string& prompt) const {
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return trim(value);
}

std::string ConsoleUI::readString(const std::string& prompt, std::size_t minLen, std::size_t maxLen) const {
    while (true) {
        const std::string value = readLine(prompt);
        if (value == "c" || value == "C") {
            return {};
        }
        if (value.size() < minLen || value.size() > maxLen) {
            printError("Length must be between " + std::to_string(minLen) +
                       " and " + std::to_string(maxLen) + ". Type c to cancel.");
            continue;
        }
        return value;
    }
}

int ConsoleUI::readChoice(const std::string& prompt, int minValue, int maxValue, bool allowZero) const {
    while (true) {
        const std::string raw = readLine(prompt);
        if (allowZero && (raw == "0" || raw == "b" || raw == "B")) {
            return 0;
        }
        try {
            std::size_t idx = 0;
            const int value = std::stoi(raw, &idx);
            if (idx != raw.size() || value < minValue || value > maxValue) {
                throw std::out_of_range("range");
            }
            return value;
        } catch (...) {
            printError("Enter a number between " + std::to_string(minValue) +
                       " and " + std::to_string(maxValue) +
                       (allowZero ? ", or 0/b to go back." : "."));
        }
    }
}

int ConsoleUI::readInt(const std::string& prompt, int minValue, int maxValue) const {
    return readChoice(prompt, minValue, maxValue, false);
}

std::string ConsoleUI::readOptional(const std::string& prompt) const {
    return readLine(prompt);
}

int ConsoleUI::readOptionalInt(const std::string& prompt, int minValue, int maxValue) const {
    while (true) {
        const std::string raw = readLine(prompt);
        if (raw.empty() || raw == "c" || raw == "C") {
            return -1;
        }
        try {
            std::size_t idx = 0;
            const int value = std::stoi(raw, &idx);
            if (idx != raw.size() || value < minValue || value > maxValue) {
                throw std::out_of_range("range");
            }
            return value;
        } catch (...) {
            printError("Enter a number between " + std::to_string(minValue) +
                       " and " + std::to_string(maxValue) + ", leave blank, or type c to cancel.");
        }
    }
}

double ConsoleUI::readDouble(const std::string& prompt, double minValue, double maxValue) const {
    while (true) {
        const std::string raw = readLine(prompt);
        if (raw == "c" || raw == "C") {
            return -1.0;
        }
        try {
            std::size_t idx = 0;
            const double value = std::stod(raw, &idx);
            if (idx != raw.size() || value < minValue || value > maxValue) {
                throw std::out_of_range("range");
            }
            return value;
        } catch (...) {
            printError("Enter a number between " + std::to_string(minValue) +
                       " and " + std::to_string(maxValue) + ". Type c to cancel.");
        }
    }
}

std::string ConsoleUI::readDate(const std::string& prompt, bool allowEmpty) const {
    while (true) {
        const std::string value = readLine(prompt);
        if ((allowEmpty && value.empty()) || value == "c" || value == "C") {
            return {};
        }
        if (Validation::isIsoDate(value)) {
            return value;
        }
        printError("Use YYYY-MM-DD. Type c to cancel.");
    }
}

std::string ConsoleUI::readDateTime(const std::string& prompt, bool allowEmpty) const {
    while (true) {
        const std::string value = readLine(prompt);
        if ((allowEmpty && value.empty()) || value == "c" || value == "C") {
            return {};
        }
        if (Validation::isDateTime(value)) {
            return value;
        }
        printError("Use YYYY-MM-DD HH:MM:SS. Type c to cancel.");
    }
}

int ConsoleUI::pick(const std::string& prompt, const std::vector<std::string>& options) const {
    for (std::size_t i = 0; i < options.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << options[i] << "\n";
    }
    std::cout << "  0. Back / cancel\n";
    return readChoice(prompt, 1, static_cast<int>(options.size()), true);
}

std::string ConsoleUI::readPassword(const std::string& prompt) const {
    std::cout << prompt;
    std::string password;
#ifdef _WIN32
    while (true) {
        const int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            break;
        }
        if (ch == 8 || ch == 127) {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b";
            }
            continue;
        }
        if (ch == 0 || ch == 224) {
            _getch();
            continue;
        }
        if (ch >= 32 && ch <= 126 && password.size() < 128) {
            password.push_back(static_cast<char>(ch));
            std::cout << '*';
        }
    }
#else
    termios oldt{};
    tcgetattr(STDIN_FILENO, &oldt);
    termios newt = oldt;
    newt.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON));
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    while (true) {
        const char ch = static_cast<char>(std::cin.get());
        if (ch == '\n' || ch == '\r') {
            break;
        }
        if ((ch == 8 || ch == 127) && !password.empty()) {
            password.pop_back();
            std::cout << "\b \b" << std::flush;
            continue;
        }
        if (ch >= 32 && ch <= 126 && password.size() < 128) {
            password.push_back(ch);
            std::cout << '*' << std::flush;
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    std::cout << '\n';
    return password;
}

bool ConsoleUI::confirm(const std::string& prompt) const {
    while (true) {
        const std::string answer = readLine(prompt + " (y/n): ");
        if (answer == "y" || answer == "Y") return true;
        if (answer == "n" || answer == "N" || answer == "c" || answer == "C") return false;
        printError("Enter y or n.");
    }
}

int ConsoleUI::showMenu(const std::string& title, const std::vector<std::string>& options) const {
    printHeader(title);
    for (std::size_t i = 0; i < options.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << options[i] << "\n";
    }
    std::cout << "  0. Back\n";
    const int choice = readChoice("Select (0/b back): ", 1, static_cast<int>(options.size()), true);
    if (choice == 0) {
        return static_cast<int>(options.size());
    }
    return choice;
}

std::string ConsoleUI::truncate(const std::string& value, std::size_t maxLen) const {
    if (value.size() <= maxLen) {
        return value;
    }
    if (maxLen <= 3) {
        return value.substr(0, maxLen);
    }
    return value.substr(0, maxLen - 3) + "...";
}

void ConsoleUI::printTable(const std::vector<std::string>& headers,
                           const std::vector<std::vector<std::string>>& rows) const {
    if (headers.empty()) {
        return;
    }
    const std::size_t maxCol = 28;
    std::vector<std::size_t> widths(headers.size(), 0);
    for (std::size_t i = 0; i < headers.size(); ++i) {
        widths[i] = std::min(maxCol, headers[i].size());
    }
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            widths[i] = std::min(maxCol, std::max(widths[i], row[i].size()));
        }
    }

    auto printSep = [&]() {
        std::cout << "+";
        for (std::size_t w : widths) {
            std::cout << std::string(w + 2, '-') << "+";
        }
        std::cout << "\n";
    };
    auto printRow = [&](const std::vector<std::string>& row) {
        std::cout << "|";
        for (std::size_t i = 0; i < headers.size(); ++i) {
            const std::string cell = truncate(i < row.size() ? row[i] : "", widths[i]);
            std::cout << " " << cell << std::string(widths[i] - cell.size(), ' ') << " |";
        }
        std::cout << "\n";
    };

    printSep();
    printRow(headers);
    printSep();
    if (rows.empty()) {
        std::cout << "(no rows)\n";
        printSep();
        return;
    }

    const std::size_t pageSize = 10;
    std::size_t page = 0;
    const std::size_t pages = (rows.size() + pageSize - 1) / pageSize;
    while (true) {
        const std::size_t start = page * pageSize;
        const std::size_t end = std::min(rows.size(), start + pageSize);
        for (std::size_t i = start; i < end; ++i) {
            printRow(rows[i]);
        }
        printSep();
        std::cout << "Page " << (page + 1) << "/" << pages
                  << "  (n next, p prev, Enter/0 done)\n";
        if (pages <= 1) {
            break;
        }
        const std::string cmd = readLine("Page: ");
        if (cmd.empty() || cmd == "0" || cmd == "b" || cmd == "B" || cmd == "q" || cmd == "Q") {
            break;
        }
        if (cmd == "n" || cmd == "N") {
            if (page + 1 < pages) {
                ++page;
            }
        } else if (cmd == "p" || cmd == "P") {
            if (page > 0) {
                --page;
            }
        }
    }
}

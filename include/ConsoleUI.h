#pragma once

#include <string>
#include <vector>

class ConsoleUI {
public:
    void clearScreen() const;
    void printBanner() const;
    void printHeader(const std::string& title) const;
    void printError(const std::string& message) const;
    void printSuccess(const std::string& message) const;
    void printWarning(const std::string& message) const;
    void printStatusBar(const std::string& username, const std::string& role) const;
    void pause() const;

    std::string readLine(const std::string& prompt) const;
    std::string readString(const std::string& prompt, std::size_t minLen, std::size_t maxLen) const;
    std::string readOptional(const std::string& prompt) const;
    int readInt(const std::string& prompt, int minValue, int maxValue) const;
    int readOptionalInt(const std::string& prompt, int minValue, int maxValue) const;
    double readDouble(const std::string& prompt, double minValue, double maxValue) const;
    std::string readDate(const std::string& prompt, bool allowEmpty) const;
    std::string readDateTime(const std::string& prompt, bool allowEmpty) const;
    std::string readPassword(const std::string& prompt) const;
    bool confirm(const std::string& prompt) const;
    int showMenu(const std::string& title, const std::vector<std::string>& options) const;
    int pick(const std::string& prompt, const std::vector<std::string>& options) const;

    void printTable(const std::vector<std::string>& headers,
                    const std::vector<std::vector<std::string>>& rows) const;

    static std::string trim(const std::string& value);
    static std::string nowTime();

private:
    enum class Tone { Normal, Success, Error, Warning, Status };

    void colour(Tone tone) const;
    void resetColour() const;
    bool colourEnabled() const;
    std::string truncate(const std::string& value, std::size_t maxLen) const;
    int readChoice(const std::string& prompt, int minValue, int maxValue, bool allowZero) const;
};

#pragma once

#include <cstdint>
#include <string>

namespace appstore_ui {

class ShareCodeState
{
public:
    void open(uint32_t now);
    bool append(char ch, uint32_t now, size_t max_length = 64);
    bool erase_last();

    std::string &input() { return input_; }
    const std::string &input() const { return input_; }
    std::string &message() { return message_; }
    const std::string &message() const { return message_; }

private:
    std::string input_;
    std::string message_ = "Enter a code from CardputerZero Hub.";
    uint32_t opened_tick_ = 0;
};

} // namespace appstore_ui

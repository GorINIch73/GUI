#pragma once

#include <string>

namespace vlk {

class Error {
public:
    Error() = default;

    explicit Error(std::string message)
        : message_(std::move(message))
    {
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !message_.empty();
    }

    [[nodiscard]] const std::string& message() const noexcept
    {
        return message_;
    }

private:
    std::string message_;
};

} // namespace vlk

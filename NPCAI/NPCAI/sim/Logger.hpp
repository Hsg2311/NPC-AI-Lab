#pragma once
#include <string>
#include <cstdint>

namespace sim {

class Logger {
public:
    static Logger& get();

    void setTick(uint32_t tick);
    void log(const std::string& category, const std::string& message);
    void logTransition(const std::string& actorName,
                       const std::string& fromState,
                       const std::string& toState,
                       const std::string& reason = "");

private:
    Logger() = default;
    uint32_t currentTick_{ 0 };
};

} // namespace sim

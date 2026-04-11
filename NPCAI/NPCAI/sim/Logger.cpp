#include "Logger.hpp"
#include <cstdio>
#include <iostream>

namespace sim {

Logger& Logger::get() {
    static Logger instance;
    return instance;
}

void Logger::setTick(uint32_t tick) {
    currentTick_ = tick;
}

void Logger::log(const std::string& category, const std::string& message) {
    char tickBuf[16];
    std::snprintf(tickBuf, sizeof(tickBuf), "[T:%04u]", currentTick_);
    std::cout << tickBuf << "[" << category << "] " << message << "\n";
}

void Logger::logTransition(const std::string& actorName,
                            const std::string& fromState,
                            const std::string& toState,
                            const std::string& reason)
{
    std::string msg = fromState + " -> " + toState;
    if (!reason.empty()) {
        msg += "  (" + reason + ")";
    }
    log("NPC:" + actorName, msg);
}

} // namespace sim

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace up::cli {

// Minimal argument parser: positional arguments plus --key value / --flag options.
class Args {
public:
    Args(int argc, char** argv, int first, const std::vector<std::string>& flags);

    const std::vector<std::string>& positional() const { return positional_; }
    std::optional<std::string> option(const std::string& name) const;
    bool flag(const std::string& name) const;
    const std::vector<std::string>& unknown() const { return unknown_; }

private:
    std::vector<std::string> positional_;
    std::map<std::string, std::string> options_;
    std::vector<std::string> flags_;
    std::vector<std::string> unknown_;
};

}  // namespace up::cli

#include "cli/Args.h"

#include <algorithm>

namespace up::cli {

Args::Args(int argc, char** argv, int first, const std::vector<std::string>& flags) {
    for (int i = first; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0 && a.size() > 2) {
            const std::string name = a.substr(2);
            if (std::find(flags.begin(), flags.end(), name) != flags.end()) {
                flags_.push_back(name);
            } else if (i + 1 < argc) {
                options_[name] = argv[++i];
            } else {
                unknown_.push_back(a);
            }
        } else {
            positional_.push_back(std::move(a));
        }
    }
}

std::optional<std::string> Args::option(const std::string& name) const {
    const auto it = options_.find(name);
    if (it == options_.end()) return std::nullopt;
    return it->second;
}

bool Args::flag(const std::string& name) const {
    return std::find(flags_.begin(), flags_.end(), name) != flags_.end();
}

}  // namespace up::cli

#include "core/Command.h"

namespace up {

Status CompositeCommand::apply() {
    for (std::size_t i = 0; i < children_.size(); ++i) {
        Status s = children_[i]->apply();
        if (!s.ok()) {
            for (std::size_t j = i; j-- > 0;) children_[j]->revert();
            return s;
        }
    }
    return Status::success();
}

void CompositeCommand::revert() {
    for (std::size_t i = children_.size(); i-- > 0;) children_[i]->revert();
}

}  // namespace up

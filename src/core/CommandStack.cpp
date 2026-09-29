#include "core/CommandStack.h"

#include "core/Log.h"

namespace up {

Status CommandStack::execute(std::unique_ptr<Command> command) {
    if (!command) {
        return makeError(ErrorCode::InvalidArgument, "app", "Cannot execute an empty command.");
    }
    Status s = command->apply();
    if (!s.ok()) {
        UP_LOG_DEBUG(log::sub::App, "Command '" << command->name() << "' failed: " << s.error().message);
        return s;
    }
    if (groupDepth_ > 0) {
        groups_.back()->add(std::move(command));
        return s;
    }
    push(std::move(command));
    notify();
    return s;
}

void CommandStack::push(std::unique_ptr<Command> command) {
    if (index_ < history_.size()) {
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(index_), history_.end());
        if (cleanIndex_ > static_cast<std::ptrdiff_t>(index_)) cleanIndex_ = -1;
    }
    history_.push_back(std::move(command));
    ++index_;
    if (limit_ > 0 && history_.size() > limit_) {
        const std::size_t excess = history_.size() - limit_;
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(excess));
        index_ -= excess;
        if (cleanIndex_ >= 0) {
            cleanIndex_ -= static_cast<std::ptrdiff_t>(excess);
            if (cleanIndex_ < 0) cleanIndex_ = -1;
        }
    }
}

std::string CommandStack::undoName() const {
    return canUndo() ? history_[index_ - 1]->name() : std::string();
}

std::string CommandStack::redoName() const {
    return canRedo() ? history_[index_]->name() : std::string();
}

bool CommandStack::undo() {
    if (!canUndo()) return false;
    history_[--index_]->revert();
    notify();
    return true;
}

bool CommandStack::redo() {
    if (!canRedo()) return false;
    Status s = history_[index_]->apply();
    if (!s.ok()) {
        UP_LOG_ERROR(log::sub::App, "Redo of '" << history_[index_]->name() << "' failed: " << s.error().message);
        return false;
    }
    ++index_;
    notify();
    return true;
}

void CommandStack::beginGroup(std::string name) {
    groups_.push_back(std::make_unique<CompositeCommand>(std::move(name)));
    ++groupDepth_;
}

bool CommandStack::endGroup() {
    if (groupDepth_ == 0) return false;
    auto group = std::move(groups_.back());
    groups_.pop_back();
    --groupDepth_;
    if (group->empty()) return false;
    if (groupDepth_ > 0) {
        groups_.back()->add(std::move(group));
        return true;
    }
    push(std::move(group));
    notify();
    return true;
}

void CommandStack::abortGroup() {
    if (groupDepth_ == 0) return;
    auto group = std::move(groups_.back());
    groups_.pop_back();
    --groupDepth_;
    group->revert();
}

void CommandStack::setLimit(std::size_t limit) {
    limit_ = limit;
    if (limit_ > 0 && history_.size() > limit_) {
        // Drop the oldest entries, keeping the redo tail intact where possible.
        const std::size_t excess = history_.size() - limit_;
        const std::size_t removable = std::min(excess, index_);
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(removable));
        index_ -= removable;
        if (cleanIndex_ >= 0) {
            cleanIndex_ -= static_cast<std::ptrdiff_t>(removable);
            if (cleanIndex_ < 0) cleanIndex_ = -1;
        }
    }
}

void CommandStack::clear() {
    history_.clear();
    index_ = 0;
    cleanIndex_ = 0;
    notify();
}

void CommandStack::markClean() {
    cleanIndex_ = static_cast<std::ptrdiff_t>(index_);
    notify();
}

void CommandStack::markDirty() {
    cleanIndex_ = -1;
    notify();
}

bool CommandStack::isClean() const {
    return cleanIndex_ == static_cast<std::ptrdiff_t>(index_) && groupDepth_ == 0;
}

void CommandStack::notify() {
    if (listener_) listener_();
}

}  // namespace up

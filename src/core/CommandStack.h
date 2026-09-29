#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Command.h"

namespace up {

// Linear undo/redo history.
//  - `execute` applies a command and records it; a failed command is not recorded.
//  - Groups (`beginGroup`/`endGroup`) collect several executes into one undo step.
//  - History is bounded by `setLimit` (0 = unlimited).
//  - `isClean` tracks whether the current state matches the last save point.
class CommandStack {
public:
    using Listener = std::function<void()>;

    Status execute(std::unique_ptr<Command> command);

    bool canUndo() const { return index_ > 0 && groupDepth_ == 0; }
    bool canRedo() const { return index_ < history_.size() && groupDepth_ == 0; }
    std::string undoName() const;
    std::string redoName() const;
    bool undo();
    bool redo();

    void beginGroup(std::string name);
    // Records the group as a single step. Returns false if the group was empty.
    bool endGroup();
    // Reverts everything executed since the matching beginGroup and discards it.
    void abortGroup();

    void setLimit(std::size_t limit);
    std::size_t limit() const { return limit_; }
    std::size_t size() const { return history_.size(); }

    void clear();
    void markClean();
    // Marks the current state as differing from anything saved (e.g. after crash recovery).
    void markDirty();
    bool isClean() const;

    // Invoked after every change to the history (execute/undo/redo/clear).
    void setListener(Listener listener) { listener_ = std::move(listener); }

private:
    void push(std::unique_ptr<Command> command);
    void notify();

    std::vector<std::unique_ptr<Command>> history_;
    std::size_t index_ = 0;           // number of applied commands in history_
    std::size_t limit_ = 0;           // 0 = unlimited
    std::ptrdiff_t cleanIndex_ = 0;   // -1 when the clean state was discarded
    std::vector<std::unique_ptr<CompositeCommand>> groups_;
    std::size_t groupDepth_ = 0;
    Listener listener_;
};

// RAII helper: groups every command executed during its lifetime into one undo step.
// Call `commit()` on success; otherwise the destructor aborts (reverts) the group.
class Transaction {
public:
    Transaction(CommandStack& stack, std::string name) : stack_(stack) { stack_.beginGroup(std::move(name)); }
    ~Transaction() {
        if (!done_) stack_.abortGroup();
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        if (!done_) {
            stack_.endGroup();
            done_ = true;
        }
    }

private:
    CommandStack& stack_;
    bool done_ = false;
};

}  // namespace up

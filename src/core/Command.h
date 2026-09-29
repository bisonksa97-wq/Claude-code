#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Result.h"

namespace up {

// A reversible operation. `apply` is called for the first execution and for redo;
// `revert` must restore the exact state that existed before the matching `apply`.
class Command {
public:
    virtual ~Command() = default;
    virtual std::string name() const = 0;
    virtual Status apply() = 0;
    virtual void revert() = 0;
};

// Executes a list of commands as one undoable unit. If a child fails, the
// already-applied children are reverted so the group is all-or-nothing.
class CompositeCommand final : public Command {
public:
    explicit CompositeCommand(std::string name) : name_(std::move(name)) {}

    void add(std::unique_ptr<Command> command) { children_.push_back(std::move(command)); }
    bool empty() const { return children_.empty(); }

    std::string name() const override { return name_; }
    Status apply() override;
    void revert() override;

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> children_;
};

}  // namespace up

#include <gtest/gtest.h>

#include "core/CommandStack.h"

using namespace up;

namespace {

class AddCommand final : public Command {
public:
    AddCommand(int& target, int amount, bool fail = false) : target_(target), amount_(amount), fail_(fail) {}
    std::string name() const override { return "Add " + std::to_string(amount_); }
    Status apply() override {
        if (fail_) return makeError(ErrorCode::InvalidArgument, "test", "fail");
        target_ += amount_;
        return Status::success();
    }
    void revert() override { target_ -= amount_; }

private:
    int& target_;
    int amount_;
    bool fail_;
};

}  // namespace

TEST(CommandStack, UndoRedo) {
    int v = 0;
    CommandStack s;
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 1)).ok());
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 10)).ok());
    EXPECT_EQ(v, 11);
    EXPECT_EQ(s.undoName(), "Add 10");
    EXPECT_TRUE(s.undo());
    EXPECT_EQ(v, 1);
    EXPECT_TRUE(s.redo());
    EXPECT_EQ(v, 11);
    EXPECT_TRUE(s.undo());
    EXPECT_TRUE(s.undo());
    EXPECT_FALSE(s.undo());
    EXPECT_EQ(v, 0);
}

TEST(CommandStack, NewCommandDiscardsRedoTail) {
    int v = 0;
    CommandStack s;
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 1)).ok());
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 2)).ok());
    s.undo();
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 5)).ok());
    EXPECT_FALSE(s.canRedo());
    EXPECT_EQ(v, 6);
}

TEST(CommandStack, FailedCommandIsNotRecorded) {
    int v = 0;
    CommandStack s;
    EXPECT_FALSE(s.execute(std::make_unique<AddCommand>(v, 1, true)).ok());
    EXPECT_FALSE(s.canUndo());
}

TEST(CommandStack, GroupsAreOneStep) {
    int v = 0;
    CommandStack s;
    {
        Transaction t(s, "Batch");
        ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 1)).ok());
        ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 2)).ok());
        t.commit();
    }
    EXPECT_EQ(s.size(), 1u);
    EXPECT_EQ(s.undoName(), "Batch");
    s.undo();
    EXPECT_EQ(v, 0);
    s.redo();
    EXPECT_EQ(v, 3);
}

TEST(CommandStack, UncommittedTransactionRollsBack) {
    int v = 0;
    CommandStack s;
    {
        Transaction t(s, "Batch");
        ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 1)).ok());
        ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 2)).ok());
    }
    EXPECT_EQ(v, 0);
    EXPECT_EQ(s.size(), 0u);
}

TEST(CommandStack, LimitDropsOldest) {
    int v = 0;
    CommandStack s;
    s.setLimit(3);
    for (int i = 1; i <= 5; ++i) ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, i)).ok());
    EXPECT_EQ(s.size(), 3u);
    while (s.undo()) {
    }
    EXPECT_EQ(v, 1 + 2);  // the two oldest can no longer be undone
}

TEST(CommandStack, CleanStateTracksSavePoint) {
    int v = 0;
    CommandStack s;
    EXPECT_TRUE(s.isClean());
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 1)).ok());
    EXPECT_FALSE(s.isClean());
    s.markClean();
    EXPECT_TRUE(s.isClean());
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 1)).ok());
    EXPECT_FALSE(s.isClean());
    s.undo();
    EXPECT_TRUE(s.isClean());
    s.undo();
    ASSERT_TRUE(s.execute(std::make_unique<AddCommand>(v, 7)).ok());  // save point is now unreachable
    EXPECT_FALSE(s.isClean());
    s.markDirty();
    EXPECT_FALSE(s.isClean());
}

TEST(CompositeCommand, FailingChildRevertsSiblings) {
    int v = 0;
    CompositeCommand c("c");
    c.add(std::make_unique<AddCommand>(v, 1));
    c.add(std::make_unique<AddCommand>(v, 2));
    c.add(std::make_unique<AddCommand>(v, 3, true));
    EXPECT_FALSE(c.apply().ok());
    EXPECT_EQ(v, 0);
}

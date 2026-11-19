// Headless tests for app::Document. No display, ImGui or GLFW needed.
// CHECK is used instead of assert because Release builds define NDEBUG.
#include <cstdio>
#include <string>

#include "document.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                             \
            ++failures;                                                \
        }                                                              \
    } while (0)

app::Document make(const std::string& text) {
    return app::Document("Demo.java", text);
}

void test_line_starts() {
    auto empty = make("");
    CHECK(empty.line_count() == 1);
    CHECK(empty.line_start(0) == 0);
    CHECK(empty.line_of_offset(0) == 0);

    // "a\nbc\n" has three lines: "a", "bc", and an empty last line.
    auto doc = make("a\nbc\n");
    CHECK(doc.line_count() == 3);
    CHECK(doc.line_start(0) == 0);
    CHECK(doc.line_start(1) == 2);
    CHECK(doc.line_start(2) == 5);
    CHECK(doc.line_of_offset(0) == 0);
    CHECK(doc.line_of_offset(1) == 0);  // the newline belongs to line 0
    CHECK(doc.line_of_offset(2) == 1);
    CHECK(doc.line_of_offset(4) == 1);
    CHECK(doc.line_of_offset(5) == 2);  // end of text, empty last line

    // Cache follows edits.
    doc.set_text("x\ny");
    CHECK(doc.line_count() == 2);
    CHECK(doc.line_start(1) == 2);
    doc.set_text("no newline at all");
    CHECK(doc.line_count() == 1);
}

void test_dirty_flag() {
    auto doc = make("class A {}\n");
    CHECK(!doc.dirty());

    doc.set_text("class B {}\n");
    CHECK(doc.dirty());

    // Editing back to the saved text is clean again.
    doc.set_text("class A {}\n");
    CHECK(!doc.dirty());

    doc.set_text("class C {}\n");
    CHECK(doc.dirty());
    doc.mark_saved();
    CHECK(!doc.dirty());

    // Undo back to the saved text is clean; undo past it is dirty again.
    doc.set_text("class D {}\n");
    CHECK(doc.dirty());
    CHECK(doc.undo());
    CHECK(!doc.dirty());
    CHECK(doc.undo());
    CHECK(doc.dirty());
}

void test_commit_edit_protocol() {
    auto doc = make("abc");
    doc.edit_buffer() += "d";  // what the text widget does while typing
    doc.commit_edit();
    CHECK(doc.text() == "abcd");
    CHECK(doc.undo_depth() == 1);

    // Committing with no change records nothing.
    doc.commit_edit();
    CHECK(doc.undo_depth() == 1);
    CHECK(doc.dirty());
}

void test_undo_basics() {
    auto doc = make("orig");
    const std::size_t rev = doc.revision();
    CHECK(!doc.undo());  // empty stack

    doc.set_text("one");
    doc.set_text("two");
    CHECK(doc.undo_depth() == 2);
    CHECK(doc.undo());
    CHECK(doc.text() == "one");
    CHECK(doc.undo());
    CHECK(doc.text() == "orig");
    CHECK(!doc.undo());
    CHECK(doc.text() == "orig");
    CHECK(doc.revision() == rev + 2);  // each undo bumps the revision
}

void test_undo_cap() {
    auto doc = make("orig");
    constexpr int kEdits = 60;
    for (int i = 0; i < kEdits; ++i) doc.set_text("v" + std::to_string(i));

    CHECK(app::Document::kUndoLimit == 50);
    CHECK(doc.undo_depth() == app::Document::kUndoLimit);

    int undone = 0;
    while (doc.undo()) ++undone;
    CHECK(undone == 50);
    CHECK(!doc.undo());
    // The oldest kept snapshot is the text before edit #10, which was "v9".
    CHECK(doc.text() == "v9");
}

}  // namespace

int main() {
    test_line_starts();
    test_dirty_flag();
    test_commit_edit_protocol();
    test_undo_basics();
    test_undo_cap();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("document_test: all checks passed");
    return 0;
}

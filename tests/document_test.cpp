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

void test_offset_of() {
    // Empty text: one empty line.
    auto empty = make("");
    CHECK(empty.offset_of(1, 1) == 0);
    CHECK(empty.offset_of(1, 5) == 0);   // column past end clamps to 0
    CHECK(empty.offset_of(9, 1) == 0);   // line past end clamps to the only line

    // "ab\ncde\n" has three lines: "ab" [0,2), "cde" [3,6), "" [7,7).
    auto doc = make("ab\ncde\n");
    CHECK(doc.offset_of(1, 1) == 0);
    CHECK(doc.offset_of(1, 2) == 1);
    CHECK(doc.offset_of(1, 3) == 2);     // column just past the text stops before '\n'
    CHECK(doc.offset_of(1, 99) == 2);
    CHECK(doc.offset_of(2, 1) == 3);
    CHECK(doc.offset_of(2, 3) == 5);
    CHECK(doc.offset_of(3, 1) == 7);     // empty last line after the trailing newline
    CHECK(doc.offset_of(42, 1) == 7);    // line past the end clamps to the last line
    CHECK(doc.offset_of(0, 0) == 0);     // 0 is treated as line 1, column 1
    CHECK(doc.offset_of(2, 0) == 3);

    // No trailing newline: the last line ends at the end of the text.
    auto tail = make("x\nyz");
    CHECK(tail.offset_of(2, 2) == 3);
    CHECK(tail.offset_of(2, 9) == 4);

    // Round trip with the line cache for every position of a multi-line text.
    auto multi = make("class A {\n  int x;\n}\n");
    for (std::size_t line = 1; line <= multi.line_count(); ++line) {
        const std::size_t start = multi.line_start(line - 1);
        CHECK(multi.offset_of(line, 1) == start);
        CHECK(multi.line_of_offset(multi.offset_of(line, 1)) == line - 1);
    }

    // Cache follows edits.
    doc.set_text("q\nrs");
    CHECK(doc.offset_of(2, 2) == 3);
}

void test_version_counter() {
    auto doc = make("a");
    const std::size_t v0 = doc.version();
    doc.set_text("b");
    CHECK(doc.version() > v0);
    const std::size_t v1 = doc.version();

    doc.edit_buffer() += "c";
    doc.commit_edit();
    CHECK(doc.version() > v1);
    const std::size_t v2 = doc.version();

    // A commit with no change does not bump the version.
    doc.commit_edit();
    CHECK(doc.version() == v2);
    // set_text with the same text is also not a change.
    doc.set_text(doc.text());
    CHECK(doc.version() == v2);

    CHECK(doc.undo());
    CHECK(doc.version() > v2);
}

}  // namespace

int main() {
    test_line_starts();
    test_dirty_flag();
    test_commit_edit_protocol();
    test_undo_basics();
    test_undo_cap();
    test_offset_of();
    test_version_counter();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("document_test: all checks passed");
    return 0;
}

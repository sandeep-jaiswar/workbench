// Headless checks for the LLM fixer: prompt, reply parsing and the error paths. With a GGUF path
// as argv[1] it also runs one real completion through the fixer on a small broken class.
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "llm.hpp"
#include "llm_fixer.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace app::fixer;

    FailureContext ctx;
    ctx.class_path = "src/demo/Cart.java";
    ctx.class_source = "package demo;\nclass Cart {}\n";
    ctx.test_output = "1 passed, 1 failed; FAIL demo.CartTest.total";
    ctx.files = {{"src/demo/Cart.java", ctx.class_source}, {"src/demo/Item.java", "class Item {}\n"}};
    const Prompt p = build_prompt(ctx);
    check(p.user.find("File: src/demo/Cart.java") != std::string::npos, "prompt names the file");
    check(p.user.find("File: src/demo/Item.java") != std::string::npos, "prompt lists every candidate file");
    check(p.user.find("FAIL demo.CartTest.total") != std::string::npos, "prompt has the test report");
    check(p.user.find("class Cart {}") != std::string::npos, "prompt has the current source");

    FailureContext with_test = ctx;
    with_test.test_source = "class CartTest { void total() {} }";
    check(build_prompt(with_test).user.find("class CartTest") != std::string::npos,
          "prompt includes the failing test source");
    FailureContext retry = ctx;
    retry.feedback = "javac failed: missing semicolon";
    check(build_prompt(retry).user.find("previous reply was rejected") != std::string::npos &&
              build_prompt(retry).user.find("missing semicolon") != std::string::npos,
          "prompt carries the rejection reason");
    check(test_path_for("src/demo/Cart.java") == "test/demo/CartTest.java", "maps src class to test class");
    check(test_path_for("README.md").empty(), "non-source path has no test class");

    const std::vector<std::string> allowed = {"src/demo/Cart.java", "src/demo/Item.java"};
    const std::string reply =
        "Two files change.\n"
        "File: src/demo/Cart.java\n```java\npackage demo;\nclass Cart { int x = 1; }\n```\n"
        "File: `src/demo/Item.java`\n```java\nclass Item { int y = 2; }\n```\n"
        "File: src/demo/Other.java\n```java\nclass Other {}\n```\n"
        "Why: the field was missing.\n";
    const auto files = parse_files(reply, allowed);
    check(files.size() == 2, "parses two allowed file blocks");
    check(files.size() == 2 && files[0].path == "src/demo/Cart.java", "first block is Cart");
    check(files.size() == 2 && files[1].path == "src/demo/Item.java", "backticked path is accepted");
    check(files.size() == 2 && files[0].code == "package demo;\nclass Cart { int x = 1; }\n",
          "code is the block body");
    check(extract_rationale(reply) == "the field was missing.", "extracts the Why line");
    check(parse_files("no file lines here", allowed).empty(), "no blocks gives no files");
    check(parse_files("File: src/demo/Cart.java\n```java\nclass Cart {}\n", allowed).empty(),
          "unterminated block is ignored");
    check(extract_rationale("no why") == "model proposal", "missing Why gives the default rationale");

    bool threw = false;
    try {
        make_llm_fixer(nullptr)(ctx);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "no model: the fixer throws");

    if (argc > 1 && std::string(argv[1]).size() > 0) {
        wb::llm::Options opt;
        opt.n_ctx = 8192;
        opt.max_new_tokens = 2048;
        std::string error;
        auto model = wb::llm::Model::load(argv[1], opt, &error);
        check(model != nullptr, "load model for the fixer run");
        if (model) {
            FailureContext real = ctx;
            real.class_source =
                "package demo;\n\npublic class Cart {\n"
                "    private int total;\n"
                "    public void add(int price) { total = total - price; }\n"
                "    public int total() { return total; }\n}\n";
            real.files = {{"src/demo/Cart.java", real.class_source}};
            real.test_output = "1 passed, 1 failed; FAIL demo.CartTest.total";
            try {
                const Proposal prop = make_llm_fixer(std::shared_ptr<wb::llm::Model>(std::move(model)))(real);
                check(!prop.edits.empty() && prop.edits[0].class_path == real.class_path,
                      "real run: proposal targets the class");
                std::printf("rationale: %s\n", prop.rationale.c_str());
            } catch (const std::exception& e) {
                std::printf("real run threw: %s\n", e.what());
                check(false, "real run: fixer returned a proposal");
            }
        }
    }

    std::printf("%s\n", failures == 0 ? "all llm fixer checks passed" : "llm fixer checks FAILED");
    return failures == 0 ? 0 : 1;
}

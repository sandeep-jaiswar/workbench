#include <cstdio>
#include <stdexcept>
#include <string>

#include "catalog.hpp"
#include "engine.hpp"
#include "verify.hpp"

namespace {

int failures = 0;
int passed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            ++passed;                                                            \
        } else {                                                                 \
            ++failures;                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_core <catalog.json>\n");
        return 2;
    }
    const wb::Catalog catalog = wb::Catalog::load(argv[1]);
    wb::Verifier verifier;

    // Every catalog template generates valid, structurally correct Java.
    for (const auto& p : catalog.patterns()) {
        const auto r = wb::generate(verifier, p, {{"Name", "Demo"}});
        CHECK(r.ok);
    }

    // Syntax error is rejected.
    {
        const auto* p = catalog.find("singleton");
        CHECK(p != nullptr);
        const auto broken = wb::fill(p->templ, {{"Name", "Demo"}});
        const auto r = wb::generate(verifier, *p, {{"Name", "Demo"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{broken.substr(0, broken.size() - 2)};
                                    });
        CHECK(!r.ok);
        CHECK(r.rejections.size() == 1 && r.rejections[0].rfind("syntax", 0) == 0);
    }

    // Missing required method is rejected by the structure check.
    {
        const auto* p = catalog.find("singleton");
        auto src = wb::fill(p->templ, {{"Name", "Demo"}});
        const auto pos = src.find("getInstance()");
        src.replace(pos, 13, "fetch()");
        const auto r = wb::generate(verifier, *p, {{"Name", "Demo"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{src};
                                    });
        CHECK(!r.ok);
        CHECK(r.rejections.size() == 1 && r.rejections[0].find("getInstance") != std::string::npos);
    }

    // Best-of-N returns the first valid candidate.
    {
        const auto* p = catalog.find("strategy");
        const auto good = wb::fill(p->templ, {{"Name", "Sort"}});
        const auto broken = good.substr(0, good.size() - 2);
        const auto r = wb::generate(verifier, *p, {{"Name", "Sort"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{broken, good};
                                    });
        CHECK(r.ok);
        CHECK(r.source == good);
        CHECK(r.rejections.size() == 1);
    }

    // All-invalid candidates report failure.
    {
        const auto* p = catalog.find("observer");
        const auto broken = wb::fill(p->templ, {{"Name", "Feed"}});
        const auto bad = broken.substr(0, broken.size() - 2);
        const auto r = wb::generate(verifier, *p, {{"Name", "Feed"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{bad, bad};
                                    });
        CHECK(!r.ok);
        CHECK(r.source.empty());
        CHECK(r.rejections.size() == 2);
    }

    // Invalid identifier throws before any work.
    {
        bool threw = false;
        try {
            wb::generate(verifier, *catalog.find("singleton"), {{"Name", "bad name"}});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    // Unknown pattern id.
    CHECK(catalog.find("visitor") == nullptr);

    // analyze reports declared types and methods.
    {
        const auto a = verifier.analyze("class A { void m() {} }");
        CHECK(a.ok);
        CHECK(a.types.count("A") == 1);
        CHECK(a.methods.count("m") == 1);
    }

    // fill rejects missing keys.
    {
        bool threw = false;
        try {
            wb::fill("{{Missing}}", {});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    std::printf("%d checks passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}

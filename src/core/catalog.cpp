#include "catalog.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace wb {

Catalog Catalog::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open catalog: " + path);
    std::stringstream buf;
    buf << in.rdbuf();

    const auto doc = nlohmann::json::parse(buf.str());
    Catalog catalog;
    for (const auto& p : doc.at("patterns")) {
        Pattern pattern;
        pattern.id = p.at("id").get<std::string>();
        pattern.name = p.at("name").get<std::string>();
        pattern.intent = p.value("intent", "");
        pattern.params = p.at("params").get<std::vector<std::string>>();
        pattern.templ = p.at("template").get<std::string>();
        const auto& req = p.at("requires");
        pattern.requires.types = req.value("types", std::vector<std::string>{});
        pattern.requires.methods = req.value("methods", std::vector<std::string>{});
        catalog.index_[pattern.id] = catalog.patterns_.size();
        catalog.patterns_.push_back(std::move(pattern));
    }
    return catalog;
}

const Pattern* Catalog::find(const std::string& id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &patterns_[it->second];
}

}  // namespace wb

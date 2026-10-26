// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.
//
// A configuration written in YAML. It is read with yaml-cpp and turned into the JSON that
// apply_config_json reads, so that there is one place that knows the keys and what is wrong with them.

#include "mirrortest/core/config.h"

#include <cstddef>
#include <string>
#include <string_view>

#ifndef MT_WITH_YAML
#define MT_WITH_YAML 0
#endif

#if MT_WITH_YAML
#include <charconv>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#endif

namespace mt::core {

bool yaml_supported() { return MT_WITH_YAML != 0; }

#if MT_WITH_YAML

namespace {

using json = nlohmann::json;

// A configuration is a handful of lines. These only stop an alias that refers to itself, or one that
// expands a thousand fold a thousand times, from taking the process down.
constexpr int         kMaxDepth = 64;
constexpr std::size_t kMaxNodes = 100000;

ir::SourceLoc at(const std::string& origin, const YAML::Mark& mark) {
    ir::SourceLoc where;
    where.file = origin;
    if (!mark.is_null()) {
        where.line = static_cast<unsigned>(mark.line) + 1;
        where.column = static_cast<unsigned>(mark.column) + 1;
    }
    return where;
}

bool is_integer(const std::string& text) {
    std::size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
    if (i == text.size()) return false;
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

class Converter {
public:
    Converter(const std::string& origin, Diagnostics& diagnostics) : origin_(origin), diagnostics_(diagnostics) {}

    bool failed() const { return failed_; }

    json convert(const YAML::Node& node, int depth) {
        if (failed_) return nullptr;
        if (++nodes_ > kMaxNodes) {
            fail(node, "the document expands to more than " + std::to_string(kMaxNodes) + " nodes (an alias that refers to itself?)");
            return nullptr;
        }
        if (depth > kMaxDepth) {
            fail(node, "the document is nested more than " + std::to_string(kMaxDepth) + " levels deep (an alias that refers to itself?)");
            return nullptr;
        }
        switch (node.Type()) {
            case YAML::NodeType::Scalar:   return scalar(node);
            case YAML::NodeType::Sequence: return sequence(node, depth);
            case YAML::NodeType::Map:      return mapping(node, depth);
            case YAML::NodeType::Null:
            case YAML::NodeType::Undefined:
                break;
        }
        return nullptr;
    }

private:
    void fail(const YAML::Node& node, const std::string& message) {
        failed_ = true;
        diagnostics_.error(code::config, message, at(origin_, node.Mark()));
    }

    // Plain scalars have a type, quoted ones and block ones are strings: `"6"` is not a number and
    // `yes` is not a boolean (it is not in YAML 1.2 and a config should not depend on the 1.1 rules).
    json scalar(const YAML::Node& node) {
        const std::string& text = node.Scalar();
        const std::string& tag = node.Tag();
        if (tag == "!" || tag == "tag:yaml.org,2002:str") return text;
        if (tag != "?") {
            fail(node, "the tag '" + tag + "' is not supported; only !!str is");
            return nullptr;
        }
        if (text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL") return nullptr;
        if (text == "true" || text == "True" || text == "TRUE") return true;
        if (text == "false" || text == "False" || text == "FALSE") return false;
        if (is_integer(text)) {
            long long value = 0;
            const char* first = text.data() + (text[0] == '+' ? 1 : 0);
            const auto parsed = std::from_chars(first, text.data() + text.size(), value);
            if (parsed.ec == std::errc() && parsed.ptr == text.data() + text.size()) return value;
        }
        return text;
    }

    json sequence(const YAML::Node& node, int depth) {
        json out = json::array();
        for (const YAML::Node& item : node) {
            out.push_back(convert(item, depth + 1));
            if (failed_) return nullptr;
        }
        return out;
    }

    json mapping(const YAML::Node& node, int depth) {
        json out = json::object();
        for (auto it = node.begin(); it != node.end(); ++it) {
            const YAML::Node key = it->first;
            if (key.Type() != YAML::NodeType::Scalar) {
                fail(key, "a key must be a plain word or a string");
                return nullptr;
            }
            const std::string& name = key.Scalar();
            if (out.contains(name)) {
                fail(key, "the key '" + name + "' appears twice in the same mapping");
                return nullptr;
            }
            out[name] = convert(it->second, depth + 1);
            if (failed_) return nullptr;
        }
        return out;
    }

    const std::string& origin_;
    Diagnostics&       diagnostics_;
    std::size_t        nodes_ = 0;
    bool               failed_ = false;
};

}  // namespace

bool apply_config_yaml(Config& config, std::string_view text, const std::string& origin, Diagnostics& diagnostics) {
    ir::SourceLoc here;
    here.file = origin;

    std::vector<YAML::Node> documents;
    try {
        documents = YAML::LoadAll(std::string(text));
    } catch (const YAML::Exception& e) {
        diagnostics.error(code::config, "invalid YAML: " + e.msg, at(origin, e.mark));
        return false;
    }
    if (documents.size() > 1) {
        diagnostics.error(code::config, "a configuration is one YAML document, and this has " + std::to_string(documents.size()), here);
        return false;
    }

    json root = json::object();
    if (documents.size() == 1 && !documents[0].IsNull()) {
        Converter converter(origin, diagnostics);
        root = converter.convert(documents[0], 0);
        if (converter.failed()) return false;
        if (!root.is_object()) {
            diagnostics.error(code::config, "a configuration is a YAML mapping of keys to values", at(origin, documents[0].Mark()));
            return false;
        }
    }

    std::string as_json;
    try {
        as_json = root.dump();
    } catch (const json::exception& e) {
        diagnostics.error(code::config, std::string("the YAML holds text that is not valid UTF-8: ") + e.what(), here);
        return false;
    }
    return apply_config_json(config, as_json, origin, diagnostics);
}

#else  // !MT_WITH_YAML

bool apply_config_yaml(Config&, std::string_view, const std::string& origin, Diagnostics& diagnostics) {
    ir::SourceLoc where;
    where.file = origin;
    diagnostics.error(code::config,
                      "this build of mirrortest cannot read YAML; write the configuration as JSON, or build with -DMT_WITH_YAML=ON", where);
    return false;
}

#endif

}  // namespace mt::core

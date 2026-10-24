// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/include_search.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace mt::core {

namespace {

namespace fs = std::filesystem;

bool starts_with(std::string_view s, std::string_view prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

std::string shown_path(const SearchDir& dir, const std::string& path) {
    if (dir.shown.empty() || dir.shown == ".") return path;
    return dir.shown.back() == '/' ? dir.shown + path : dir.shown + "/" + path;
}

bool is_file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && !ec;
}

// QtCore, QtTest, ...: the include directories of Qt's modules. A list rather
// than "starts with Qt", which would also swallow a project's own `QtUtils/`.
bool is_qt_module(std::string_view name) {
    static const char* const kModules[] = {"QtBluetooth", "QtCharts",   "QtConcurrent", "QtCore",   "QtDBus",       "QtGui",
                                           "QtMultimedia", "QtNetwork", "QtOpenGL",     "QtPositioning", "QtPrintSupport", "QtQml",
                                           "QtQuick",    "QtQuickWidgets", "QtSensors", "QtSerialPort", "QtSql",      "QtSvg",
                                           "QtTest",     "QtWebSockets", "QtWidgets",  "QtXml"};
    for (const char* module : kModules) {
        if (name == module) return true;
    }
    return false;
}

bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}

}  // namespace

bool is_foreign_include(std::string_view path) {
    if (starts_with(path, "gtest/") || starts_with(path, "gmock/") || starts_with(path, "catch2/") || starts_with(path, "doctest/")) return true;
    const std::string_view first = path.substr(0, path.find('/'));
    if (is_qt_module(first)) return true;                                     // QtTest, QtCore/qglobal.h
    // <vector>, <QString>: the standard library and Qt's class headers have no extension.
    const std::size_t last_slash = path.rfind('/');
    const std::string_view name = last_slash == std::string_view::npos ? path : path.substr(last_slash + 1);
    return name.find('.') == std::string_view::npos;
}

std::vector<ResolvedInclude> resolve_includes(const std::vector<ir::Include>& includes, const IncludeSearch& search) {
    std::vector<ResolvedInclude> out;
    for (const ir::Include& inc : includes) {
        if (inc.path.empty()) continue;
        const bool repeated = std::any_of(out.begin(), out.end(), [&](const ResolvedInclude& r) { return r.path == inc.path; });
        if (repeated) continue;

        ResolvedInclude r;
        r.path = inc.path;
        r.where = inc.where;
        if (is_foreign_include(inc.path)) {
            r.kind = IncludeKind::Foreign;
            out.push_back(std::move(r));
            continue;
        }

        // The compiler's order for a quoted include; a system include skips the
        // test's own directory.
        std::vector<const SearchDir*> dirs;
        if (!inc.is_system && !search.test_dir.dir.empty()) dirs.push_back(&search.test_dir);
        for (const SearchDir& dir : search.include_dirs) {
            if (!dir.dir.empty()) dirs.push_back(&dir);
        }
        if (!search.out_include.dir.empty()) dirs.push_back(&search.out_include);

        fs::path found;
        for (const SearchDir* dir : dirs) {
            if (is_file(dir->dir / inc.path)) {
                found = dir->dir / inc.path;
                r.shown = shown_path(*dir, inc.path);
                break;
            }
        }

        const fs::path in_out = search.out_include.dir / inc.path;
        const bool in_output = !found.empty() && !search.out_include.dir.empty() && is_file(in_out) && same_file(found, in_out);
        r.file = found;
        if (found.empty()) {
            // Nothing has it. A quoted include is for us to write; an angle-bracket one is somebody's.
            r.kind = inc.is_system ? IncludeKind::Foreign : IncludeKind::Target;
        } else {
            r.kind = in_output ? IncludeKind::Target : IncludeKind::Known;
        }
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace mt::core

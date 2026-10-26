// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include "mirrortest/core/writer.h"

#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

#include "mirrortest/core/diff.h"

namespace mt::core {

namespace fs = std::filesystem;

namespace {

constexpr const char* kTmpSuffix = ".mt-tmp";
constexpr const char* kNewSuffix = ".new";

void io_error(Diagnostics& diagnostics, const std::string& path, const std::string& message) {
    ir::SourceLoc where;
    where.file = path;
    diagnostics.error(code::io, message, where);
}

bool read_whole(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

// One file of the plan, decided but not yet written.
struct Planned {
    FileResult                 result;
    fs::path                   target;     // where the bytes go
    std::string                content;    // what goes there
    std::optional<std::string> existing;   // what `path` holds now, if anything
};

// Looks at `path`: 0 = does not exist, 1 = a regular file, -1 = an error was
// recorded (it exists but is not a regular file, or cannot be inspected).
int inspect(const fs::path& path, const std::string& shown, Diagnostics& diagnostics) {
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (!fs::status_known(status)) {
        io_error(diagnostics, shown, "cannot inspect the path");
        return -1;
    }
    if (!fs::exists(status)) {
        return 0;
    }
    if (!fs::is_regular_file(status)) {
        io_error(diagnostics, shown, "exists and is not a regular file");
        return -1;
    }
    return 1;
}

void remove_quietly(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

}  // namespace

const char* to_string(OverwriteMode mode) {
    switch (mode) {
        case OverwriteMode::Never: return "never";
        case OverwriteMode::Merge: return "merge";
        case OverwriteMode::Force: return "force";
    }
    return "never";
}

std::optional<OverwriteMode> parse_overwrite_mode(std::string_view text) {
    for (OverwriteMode m : {OverwriteMode::Never, OverwriteMode::Merge, OverwriteMode::Force}) {
        if (text == to_string(m)) {
            return m;
        }
    }
    return std::nullopt;
}

bool WriteReport::has_changes() const {
    for (const FileResult& f : files) {
        if (f.action != WriteAction::Unchanged) {
            return true;
        }
    }
    return false;
}

bool write_plan(const OutputPlan& plan, const fs::path& base_dir, const WriterOptions& options,
                WriteReport& report, Diagnostics& diagnostics) {
    report.files.clear();
    const std::size_t errors_before = diagnostics.count(Severity::Error);

    // ---- pass 1: decide everything, touch nothing -----------------------------------
    std::vector<Planned> planned;
    planned.reserve(plan.files.size());
    std::set<fs::path> seen;

    for (const OutputFile& file : plan.files) {
        if (file.path.empty()) {
            io_error(diagnostics, "", "an output has an empty path");
            continue;
        }
        const fs::path resolved = base_dir / file.path;
        if (!seen.insert(resolved.lexically_normal()).second) {
            io_error(diagnostics, file.path, "two outputs for the same path");
            continue;
        }

        Planned p;
        p.result.path = file.path;
        p.result.target = file.path;
        p.target = resolved;
        p.content = file.content;

        const int state = inspect(resolved, file.path, diagnostics);
        if (state < 0) {
            continue;
        }
        if (state == 0) {
            p.result.action = WriteAction::Create;
            planned.push_back(std::move(p));
            continue;
        }

        std::string current;
        if (!read_whole(resolved, current)) {
            io_error(diagnostics, file.path, "cannot read the existing file");
            continue;
        }
        p.existing = current;

        if (current == file.content) {
            p.result.action = WriteAction::Unchanged;
            planned.push_back(std::move(p));
            continue;
        }

        // The file exists and differs.
        bool write_new = options.mode == OverwriteMode::Never;
        if (options.mode == OverwriteMode::Force) {
            p.result.action = WriteAction::Overwrite;
        } else if (options.mode == OverwriteMode::Merge) {
            ir::SourceLoc where;
            where.file = file.path;
            if (!options.merge) {
                diagnostics.warning(code::unsupported,
                                    "merging is not available for this file; writing " + file.path +
                                        kNewSuffix + " instead",
                                    where);
                write_new = true;
            } else if (std::optional<MergeResult> merged = options.merge(file, current, diagnostics)) {
                if (merged->content == current) {
                    p.result.action = WriteAction::Unchanged;
                } else {
                    p.result.action = WriteAction::Merge;
                    p.result.added_members = merged->added_members;
                    p.content = std::move(merged->content);
                }
            } else {
                write_new = true;
            }
        }

        if (write_new) {
            p.result.action = WriteAction::WriteNew;
            p.result.target = file.path + kNewSuffix;
            p.target = fs::path(resolved.string() + kNewSuffix);
            if (inspect(p.target, p.result.target, diagnostics) < 0) {
                continue;
            }
        }
        planned.push_back(std::move(p));
    }

    if (diagnostics.count(Severity::Error) > errors_before) {
        return false;
    }

    // ---- diffs, for the modes that only look ----------------------------------------
    const bool only_look = options.dry_run || options.check;
    if (only_look) {
        for (Planned& p : planned) {
            if (p.result.action != WriteAction::Unchanged) {
                p.result.diff = unified_diff(p.result.path, p.existing, p.content);
            }
        }
    }

    // ---- pass 2: stage every file, then put them all in place ------------------------
    if (!only_look) {
        std::vector<std::pair<fs::path, fs::path>> staged;   // temporary, final
        const auto abandon = [&staged]() {
            for (const auto& entry : staged) {
                remove_quietly(entry.first);
            }
        };

        for (const Planned& p : planned) {
            if (p.result.action == WriteAction::Unchanged) {
                continue;
            }
            std::error_code ec;
            if (p.target.has_parent_path()) {
                fs::create_directories(p.target.parent_path(), ec);
                if (ec) {
                    io_error(diagnostics, p.result.target, "cannot create the directory");
                    abandon();
                    return false;
                }
            }

            const fs::path temporary(p.target.string() + kTmpSuffix);
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                out.write(p.content.data(), static_cast<std::streamsize>(p.content.size()));
                out.flush();
                if (!out) {
                    io_error(diagnostics, p.result.target, "cannot write the file");
                    out.close();
                    remove_quietly(temporary);
                    abandon();
                    return false;
                }
            }
            staged.emplace_back(temporary, p.target);

            // Replacing a file by rename gives it default permissions; keep the
            // ones the user set (an executable bit, a group-writable header).
            if (p.existing && p.result.action != WriteAction::WriteNew) {
                std::error_code perm_ec;
                fs::permissions(temporary, fs::status(p.target, perm_ec).permissions(), perm_ec);
            }
        }

        for (std::size_t i = 0; i < staged.size(); ++i) {
            std::error_code ec;
            fs::rename(staged[i].first, staged[i].second, ec);
            if (ec) {
                io_error(diagnostics, staged[i].second.string(), "cannot move the file into place");
                for (std::size_t j = i; j < staged.size(); ++j) {
                    remove_quietly(staged[j].first);
                }
                return false;
            }
        }
    }

    for (Planned& p : planned) {
        report.files.push_back(std::move(p.result));
    }
    return true;
}

std::string format_summary(const WriteReport& report, bool dry_run) {
    const std::string would = dry_run ? "would " : "";
    std::string out;
    for (const FileResult& f : report.files) {
        switch (f.action) {
            case WriteAction::Create:
                out += would + (dry_run ? "create " : "created ") + f.path;
                break;
            case WriteAction::Unchanged:
                out += "unchanged " + f.path;
                break;
            case WriteAction::Overwrite:
                out += would + (dry_run ? "overwrite " : "overwrote ") + f.path;
                break;
            case WriteAction::WriteNew:
                out += would + (dry_run ? "write " : "wrote ") + f.target + " (" + f.path +
                       " exists and differs)";
                break;
            case WriteAction::Merge:
                out += would + (dry_run ? "merge " : "merged ") + std::to_string(f.added_members) +
                       (f.added_members == 1 ? " member into " : " members into ") + f.path;
                break;
        }
        out += '\n';
    }
    return out;
}

}  // namespace mt::core

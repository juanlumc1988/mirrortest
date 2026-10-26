// SPDX-License-Identifier: Apache-2.0
// Part of mirrortest -- bidirectional C++ test/code generator.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "mirrortest/core/output.h"
#include "mirrortest/core/writer.h"
#include "test_support.h"

using namespace mt::core;
namespace fs = std::filesystem;
using mt::test::read_file;
using mt::test::TempDir;
using mt::test::write_file;

namespace {

struct Outcome {
    bool        ok = false;
    WriteReport report;
    Diagnostics diagnostics;
};

Outcome run(const TempDir& dir, const OutputPlan& plan, WriterOptions options = {}) {
    Outcome r;
    r.ok = write_plan(plan, dir.path(), options, r.report, r.diagnostics);
    return r;
}

OutputPlan plan_of(std::initializer_list<OutputFile> files) {
    OutputPlan p;
    p.files.assign(files.begin(), files.end());
    return p;
}

// Anything left behind by the staging step.
std::vector<std::string> leftovers(const TempDir& dir) {
    std::vector<std::string> found;
    for (const auto& entry : fs::recursive_directory_iterator(dir.path())) {
        if (entry.path().extension() == ".mt-tmp") {
            found.push_back(entry.path().string());
        }
    }
    return found;
}

std::vector<WriteAction> actions(const Outcome& r) {
    std::vector<WriteAction> out;
    for (const FileResult& f : r.report.files) out.push_back(f.action);
    return out;
}

}  // namespace

// ---- create ---------------------------------------------------------------------------

TEST(Writer, CreatesFilesAndTheirDirectories) {
    TempDir dir;
    const Outcome r = run(dir, plan_of({{"src/acme/foo.h", "// foo\n"}, {"src/acme/foo.cpp", "// impl\n"}}));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(read_file(dir.path() / "src/acme/foo.h"), "// foo\n");
    EXPECT_EQ(read_file(dir.path() / "src/acme/foo.cpp"), "// impl\n");
    EXPECT_EQ(actions(r), (std::vector<WriteAction>{WriteAction::Create, WriteAction::Create}));
    EXPECT_TRUE(leftovers(dir).empty());
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(Writer, WritesBytesExactly) {
    TempDir dir;
    const std::string bytes("a\r\nb\0c\n", 7);
    ASSERT_TRUE(run(dir, plan_of({{"x.bin", bytes}})).ok);
    EXPECT_EQ(read_file(dir.path() / "x.bin"), bytes);
}

TEST(Writer, AnEmptyPlanIsFine) {
    TempDir dir;
    const Outcome r = run(dir, {});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.report.files.empty());
    EXPECT_FALSE(r.report.has_changes());
}

TEST(Writer, ReportsInPlanOrderWithThePathsAsGiven) {
    TempDir dir;
    const Outcome r = run(dir, plan_of({{"b/z.h", "1"}, {"a/y.h", "2"}, {"c.h", "3"}}));
    ASSERT_EQ(r.report.files.size(), 3u);
    EXPECT_EQ(r.report.files[0].path, "b/z.h");
    EXPECT_EQ(r.report.files[1].path, "a/y.h");
    EXPECT_EQ(r.report.files[2].path, "c.h");
}

TEST(Writer, AnAbsolutePathIgnoresTheBaseDirectory) {
    TempDir base;
    TempDir elsewhere;
    const std::string absolute = elsewhere.file("abs.h");
    ASSERT_TRUE(run(base, plan_of({{absolute, "x"}})).ok);
    EXPECT_EQ(read_file(absolute), "x");
    EXPECT_FALSE(fs::exists(base.path() / "abs.h"));
}

// ---- unchanged ------------------------------------------------------------------------

TEST(Writer, IdenticalContentIsNotTouched) {
    // A build system must not see a rebuild trigger it did not earn.
    TempDir dir;
    const std::string path = write_file(dir.path() / "foo.h", "same\n");
    const auto past = fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(path, past);

    const Outcome r = run(dir, plan_of({{"foo.h", "same\n"}}));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(actions(r), std::vector<WriteAction>{WriteAction::Unchanged});
    EXPECT_EQ(fs::last_write_time(path), past);
    EXPECT_FALSE(r.report.has_changes());
    EXPECT_FALSE(fs::exists(dir.path() / "foo.h.new"));
}

TEST(Writer, RunningTwiceIsIdempotent) {
    TempDir dir;
    const OutputPlan plan = plan_of({{"a.h", "A\n"}, {"b.h", "B\n"}});
    ASSERT_TRUE(run(dir, plan).ok);
    const Outcome second = run(dir, plan);
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(actions(second), (std::vector<WriteAction>{WriteAction::Unchanged, WriteAction::Unchanged}));
}

// ---- never (the default) --------------------------------------------------------------

TEST(Writer, NeverLeavesADifferingFileAloneAndWritesNew) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "mine\n");

    const Outcome r = run(dir, plan_of({{"foo.h", "generated\n"}}));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(read_file(dir.path() / "foo.h"), "mine\n");
    EXPECT_EQ(read_file(dir.path() / "foo.h.new"), "generated\n");
    ASSERT_EQ(r.report.files.size(), 1u);
    EXPECT_EQ(r.report.files[0].action, WriteAction::WriteNew);
    EXPECT_EQ(r.report.files[0].path, "foo.h");
    EXPECT_EQ(r.report.files[0].target, "foo.h.new");
    EXPECT_TRUE(r.report.has_changes());
}

TEST(Writer, ANewFromAPreviousRunIsReplaced) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "mine\n");
    write_file(dir.path() / "foo.h.new", "stale\n");

    ASSERT_TRUE(run(dir, plan_of({{"foo.h", "fresh\n"}})).ok);
    EXPECT_EQ(read_file(dir.path() / "foo.h"), "mine\n");
    EXPECT_EQ(read_file(dir.path() / "foo.h.new"), "fresh\n");
}

TEST(Writer, AStaleNewNextToAnUnchangedFileIsLeftAlone) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "same\n");
    write_file(dir.path() / "foo.h.new", "stale\n");

    const Outcome r = run(dir, plan_of({{"foo.h", "same\n"}}));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(actions(r), std::vector<WriteAction>{WriteAction::Unchanged});
    EXPECT_EQ(read_file(dir.path() / "foo.h.new"), "stale\n");
}

TEST(Writer, NeverOnlyAffectsTheFilesThatDiffer) {
    TempDir dir;
    write_file(dir.path() / "a.h", "mine\n");
    write_file(dir.path() / "b.h", "same\n");

    const Outcome r = run(dir, plan_of({{"a.h", "gen\n"}, {"b.h", "same\n"}, {"c.h", "new\n"}}));
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(actions(r), (std::vector<WriteAction>{WriteAction::WriteNew, WriteAction::Unchanged,
                                                    WriteAction::Create}));
    EXPECT_FALSE(fs::exists(dir.path() / "b.h.new"));
    EXPECT_FALSE(fs::exists(dir.path() / "c.h.new"));
}

// ---- force ------------------------------------------------------------------------------

TEST(Writer, ForceOverwritesADifferingFile) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "mine\n");

    WriterOptions o;
    o.mode = OverwriteMode::Force;
    const Outcome r = run(dir, plan_of({{"foo.h", "generated\n"}}), o);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(read_file(dir.path() / "foo.h"), "generated\n");
    EXPECT_FALSE(fs::exists(dir.path() / "foo.h.new"));
    EXPECT_EQ(actions(r), std::vector<WriteAction>{WriteAction::Overwrite});
    EXPECT_TRUE(leftovers(dir).empty());
}

TEST(Writer, ForceKeepsTheFilePermissions) {
    TempDir dir;
    const std::string path = write_file(dir.path() / "tool.sh", "old\n");
    fs::permissions(path, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
    const fs::perms before = fs::status(path).permissions();

    WriterOptions o;
    o.mode = OverwriteMode::Force;
    ASSERT_TRUE(run(dir, plan_of({{"tool.sh", "new\n"}}), o).ok);
    EXPECT_EQ(fs::status(path).permissions(), before);
}

TEST(Writer, ForceLeavesIdenticalFilesUnchanged) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "same\n");
    WriterOptions o;
    o.mode = OverwriteMode::Force;
    EXPECT_EQ(actions(run(dir, plan_of({{"foo.h", "same\n"}}), o)),
              std::vector<WriteAction>{WriteAction::Unchanged});
}

// ---- merge ------------------------------------------------------------------------------

TEST(Writer, MergeWritesWhatTheMergerReturns) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "user\n");

    WriterOptions o;
    o.mode = OverwriteMode::Merge;
    o.merge = [](const OutputFile& generated, const std::string& existing, Diagnostics&) {
        return std::optional<MergeResult>(MergeResult{existing + "+" + generated.content, 3});
    };
    const Outcome r = run(dir, plan_of({{"foo.h", "gen\n"}}), o);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(read_file(dir.path() / "foo.h"), "user\n+gen\n");
    EXPECT_FALSE(fs::exists(dir.path() / "foo.h.new"));
    EXPECT_EQ(actions(r), std::vector<WriteAction>{WriteAction::Merge});
    EXPECT_EQ(r.report.files[0].added_members, 3u);
}

TEST(Writer, AMergerThatCannotMergeFallsBackToNew) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "user\n");

    WriterOptions o;
    o.mode = OverwriteMode::Merge;
    o.merge = [](const OutputFile&, const std::string&, Diagnostics& d) {
        d.warning(code::merge_no_region, "no generated region");
        return std::optional<MergeResult>();
    };
    const Outcome r = run(dir, plan_of({{"foo.h", "gen\n"}}), o);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(read_file(dir.path() / "foo.h"), "user\n");
    EXPECT_EQ(read_file(dir.path() / "foo.h.new"), "gen\n");
    EXPECT_EQ(actions(r), std::vector<WriteAction>{WriteAction::WriteNew});
    EXPECT_EQ(r.diagnostics.count(Severity::Warning), 1u);
}

TEST(Writer, AMergeThatChangesNothingIsUnchanged) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "user\n");

    WriterOptions o;
    o.mode = OverwriteMode::Merge;
    o.merge = [](const OutputFile&, const std::string& existing, Diagnostics&) {
        return std::optional<MergeResult>(MergeResult{existing, 0});
    };
    EXPECT_EQ(actions(run(dir, plan_of({{"foo.h", "gen\n"}}), o)),
              std::vector<WriteAction>{WriteAction::Unchanged});
    EXPECT_FALSE(fs::exists(dir.path() / "foo.h.new"));
}

TEST(Writer, AMergeErrorStopsEverything) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "user\n");

    WriterOptions o;
    o.mode = OverwriteMode::Merge;
    o.merge = [](const OutputFile&, const std::string&, Diagnostics& d) {
        d.error(code::merge_region, "region opened twice");
        return std::optional<MergeResult>();
    };
    const Outcome r = run(dir, plan_of({{"other.h", "x"}, {"foo.h", "gen\n"}}), o);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(fs::exists(dir.path() / "other.h")) << "nothing may be written";
    EXPECT_FALSE(fs::exists(dir.path() / "foo.h.new"));
}

TEST(Writer, MergeModeWithoutAMergerIsNonDestructiveAndSaysSo) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "user\n");

    WriterOptions o;
    o.mode = OverwriteMode::Merge;
    const Outcome r = run(dir, plan_of({{"foo.h", "gen\n"}}), o);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(read_file(dir.path() / "foo.h"), "user\n");
    EXPECT_EQ(read_file(dir.path() / "foo.h.new"), "gen\n");
    ASSERT_EQ(r.diagnostics.all().size(), 1u);
    EXPECT_EQ(r.diagnostics.all()[0].code, code::unsupported);
}

TEST(Writer, MergeModeStillCreatesMissingFiles) {
    TempDir dir;
    WriterOptions o;
    o.mode = OverwriteMode::Merge;
    EXPECT_EQ(actions(run(dir, plan_of({{"new.h", "x"}}), o)), std::vector<WriteAction>{WriteAction::Create});
}

// ---- dry run and check --------------------------------------------------------------------

TEST(Writer, DryRunWritesNothingAndShowsDiffs) {
    TempDir dir;
    write_file(dir.path() / "old.h", "mine\n");

    WriterOptions o;
    o.dry_run = true;
    const Outcome r = run(dir, plan_of({{"new.h", "n\n"}, {"old.h", "gen\n"}}), o);
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(fs::exists(dir.path() / "new.h"));
    EXPECT_FALSE(fs::exists(dir.path() / "old.h.new"));
    EXPECT_EQ(read_file(dir.path() / "old.h"), "mine\n");

    ASSERT_EQ(r.report.files.size(), 2u);
    EXPECT_EQ(r.report.files[0].diff, "--- /dev/null\n+++ b/new.h\n@@ -0,0 +1 @@\n+n\n");
    // For a file that would get a .new, the diff is against the file itself.
    EXPECT_EQ(r.report.files[1].action, WriteAction::WriteNew);
    EXPECT_EQ(r.report.files[1].diff, "--- a/old.h\n+++ b/old.h\n@@ -1 +1 @@\n-mine\n+gen\n");
}

TEST(Writer, DryRunShowsTheMergedResult) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "a\n");
    WriterOptions o;
    o.dry_run = true;
    o.mode = OverwriteMode::Merge;
    o.merge = [](const OutputFile&, const std::string&, Diagnostics&) {
        return std::optional<MergeResult>(MergeResult{"a\nb\n", 1});
    };
    const Outcome r = run(dir, plan_of({{"foo.h", "gen\n"}}), o);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.report.files[0].diff, "--- a/foo.h\n+++ b/foo.h\n@@ -1 +1,2 @@\n a\n+b\n");
    EXPECT_EQ(read_file(dir.path() / "foo.h"), "a\n");
}

TEST(Writer, UnchangedFilesHaveNoDiff) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "same\n");
    WriterOptions o;
    o.dry_run = true;
    const Outcome r = run(dir, plan_of({{"foo.h", "same\n"}}), o);
    EXPECT_EQ(r.report.files[0].diff, "");
}

TEST(Writer, CheckWritesNothingAndReportsWhetherAnythingWouldChange) {
    TempDir dir;
    write_file(dir.path() / "same.h", "s\n");

    WriterOptions o;
    o.check = true;
    const Outcome clean = run(dir, plan_of({{"same.h", "s\n"}}), o);
    ASSERT_TRUE(clean.ok);
    EXPECT_FALSE(clean.report.has_changes());

    const Outcome dirty = run(dir, plan_of({{"same.h", "s\n"}, {"missing.h", "m\n"}}), o);
    ASSERT_TRUE(dirty.ok);
    EXPECT_TRUE(dirty.report.has_changes());
    EXPECT_FALSE(fs::exists(dir.path() / "missing.h"));
}

TEST(Writer, WithoutDryRunOrCheckNoDiffIsComputed) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "mine\n");
    const Outcome r = run(dir, plan_of({{"foo.h", "gen\n"}}));
    EXPECT_EQ(r.report.files[0].diff, "");
}

// ---- all or nothing -----------------------------------------------------------------------

TEST(Writer, AnUnwritableOutputStopsTheWholeRun) {
    TempDir dir;
    write_file(dir.path() / "blocked", "i am a file, not a directory\n");

    const Outcome r = run(dir, plan_of({{"ok.h", "fine\n"}, {"blocked/x.h", "doomed\n"}}));
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.diagnostics.has_errors());
    EXPECT_EQ(r.diagnostics.all()[0].code, code::io);
    EXPECT_EQ(r.diagnostics.all()[0].where.file, "blocked/x.h");
    EXPECT_FALSE(fs::exists(dir.path() / "ok.h")) << "the first file must not survive a failed run";
    EXPECT_TRUE(r.report.files.empty());
    EXPECT_TRUE(leftovers(dir).empty());
}

TEST(Writer, ADirectoryWhereAFileBelongsIsAnError) {
    TempDir dir;
    fs::create_directory(dir.path() / "foo.h");
    const Outcome r = run(dir, plan_of({{"other.h", "x"}, {"foo.h", "y"}}));
    EXPECT_FALSE(r.ok);
    ASSERT_EQ(r.diagnostics.all().size(), 1u);
    EXPECT_EQ(r.diagnostics.all()[0].message, "exists and is not a regular file");
    EXPECT_FALSE(fs::exists(dir.path() / "other.h"));
}

TEST(Writer, ADirectoryWhereTheNewFileWouldGoIsAnError) {
    TempDir dir;
    write_file(dir.path() / "foo.h", "mine\n");
    fs::create_directory(dir.path() / "foo.h.new");
    const Outcome r = run(dir, plan_of({{"foo.h", "gen\n"}}));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.diagnostics.all()[0].where.file, "foo.h.new");
}

TEST(Writer, TwoOutputsForTheSamePathAreRefused) {
    TempDir dir;
    const Outcome r = run(dir, plan_of({{"a/foo.h", "1"}, {"a/../a/foo.h", "2"}}));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.diagnostics.all()[0].message, "two outputs for the same path");
    EXPECT_FALSE(fs::exists(dir.path() / "a"));
}

TEST(Writer, AnEmptyPathIsRefused) {
    TempDir dir;
    EXPECT_FALSE(run(dir, plan_of({{"", "x"}})).ok);
}

TEST(Writer, EveryProblemIsReportedNotJustTheFirst) {
    TempDir dir;
    fs::create_directory(dir.path() / "d1");
    fs::create_directory(dir.path() / "d2");
    const Outcome r = run(dir, plan_of({{"d1", "x"}, {"d2", "y"}}));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.diagnostics.count(Severity::Error), 2u);
}

TEST(Writer, DryRunStillRefusesAnImpossiblePlan) {
    TempDir dir;
    fs::create_directory(dir.path() / "d");
    WriterOptions o;
    o.dry_run = true;
    EXPECT_FALSE(run(dir, plan_of({{"d", "x"}}), o).ok);
}

// ---- summary -------------------------------------------------------------------------------

TEST(Writer, SummaryUsesTheDocumentedWording) {
    WriteReport report;
    report.files = {
        {"a.h", WriteAction::Create, "a.h", 0, ""},
        {"b.h", WriteAction::Unchanged, "b.h", 0, ""},
        {"c.h", WriteAction::Overwrite, "c.h", 0, ""},
        {"d.h", WriteAction::WriteNew, "d.h.new", 0, ""},
        {"e.h", WriteAction::Merge, "e.h", 2, ""},
        {"f.h", WriteAction::Merge, "f.h", 1, ""},
    };
    EXPECT_EQ(format_summary(report, false),
              "created a.h\n"
              "unchanged b.h\n"
              "overwrote c.h\n"
              "wrote d.h.new (d.h exists and differs)\n"
              "merged 2 members into e.h\n"
              "merged 1 member into f.h\n");
    EXPECT_EQ(format_summary(report, true),
              "would create a.h\n"
              "unchanged b.h\n"
              "would overwrite c.h\n"
              "would write d.h.new (d.h exists and differs)\n"
              "would merge 2 members into e.h\n"
              "would merge 1 member into f.h\n");
    EXPECT_EQ(format_summary({}, false), "");
}

TEST(OverwriteMode, SpellingsRoundTrip) {
    for (OverwriteMode m : {OverwriteMode::Never, OverwriteMode::Merge, OverwriteMode::Force}) {
        EXPECT_EQ(parse_overwrite_mode(to_string(m)), m);
    }
    EXPECT_FALSE(parse_overwrite_mode("always").has_value());
    EXPECT_FALSE(parse_overwrite_mode("").has_value());
}

TEST(OutputPath, JoinsTheDirectoryAndThePathBelowIt) {
    EXPECT_EQ(output_path("out", "acme/foo.h"), "out/acme/foo.h");
    EXPECT_EQ(output_path("out/", "foo.h"), "out/foo.h") << "no doubled slash";
    EXPECT_EQ(output_path("/abs/out", "foo.h"), "/abs/out/foo.h");
}

TEST(OutputPath, TheWorkingDirectoryAddsNothing) {
    EXPECT_EQ(output_path("", "foo.h"), "foo.h");
    EXPECT_EQ(output_path(".", "acme/foo.h"), "acme/foo.h");
}

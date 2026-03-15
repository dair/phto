/**
 * dbcli — command-line interface for the database library
 *
 * Usage:
 *   dbcli <database> <operation> [arguments...]
 *
 * Operations:
 *   file add    <id> <name> <size> <ext>
 *   file del    <id>
 *   file rename <id> <new-name>
 *   file get    <id>
 *   file list   [--offset N] [--limit N]
 *   file count
 *
 *   tag add     <name>
 *   tag del     <name>
 *   tag list    [--offset N] [--limit N]
 *   tag count
 *
 *   bind        <file-id> <tag-name>
 *   unbind      <file-id> <tag-name>
 *   file-tags   <file-id>   [--offset N] [--limit N]
 *   tag-files   <tag> [<tag>...] [--offset N] [--limit N]
 */

#include "database/Database.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace db;

// ---------------------------------------------------------------------------
// Argument parsing helpers
// ---------------------------------------------------------------------------

struct Args {
    std::string              dbPath;
    std::string              op;       // first token of operation
    std::string              sub;      // second token (subcommand) if applicable
    std::vector<std::string> rest;     // remaining positional arguments
    std::optional<Pagination> page;    // --offset / --limit if provided
};

static void printUsage(std::ostream& out) {
    out <<
R"(Usage: dbcli <database> <operation> [arguments...]

Operations:
  file add    <id> <name> <size> <ext>     Add a file record
  file del    <id>                          Delete a file (cascades bindings)
  file rename <id> <new-name>              Rename a file
  file get    <id>                          Show a file record
  file list   [--offset N] [--limit N]     List all files
  file count                               Print total file count

  tag add     <name>                       Add a tag
  tag del     <name>                       Delete a tag (cascades bindings)
  tag list    [--offset N] [--limit N]     List all tags
  tag count                               Print total tag count

  bind        <file-id> <tag-name>         Bind a tag to a file
  unbind      <file-id> <tag-name>         Remove a tag-file binding
  file-tags   <file-id>                    List tags for a file
              [--offset N] [--limit N]
  tag-files   <tag> [<tag>...]             List files that have ALL given tags
              [--offset N] [--limit N]
)";
}

/// Parse --offset / --limit flags out of argv, leaving other args in place.
static std::optional<Pagination> parsePagination(std::vector<std::string>& args) {
    uint32_t offset = 0;
    uint32_t limit  = 0;
    bool hasOffset = false, hasLimit = false;

    for (auto it = args.begin(); it != args.end(); ) {
        if (*it == "--offset" && std::next(it) != args.end()) {
            offset = static_cast<uint32_t>(std::stoul(*std::next(it)));
            hasOffset = true;
            it = args.erase(it, it + 2);
        } else if (*it == "--limit" && std::next(it) != args.end()) {
            limit = static_cast<uint32_t>(std::stoul(*std::next(it)));
            hasLimit = true;
            it = args.erase(it, it + 2);
        } else {
            ++it;
        }
    }

    if (!hasOffset && !hasLimit) return std::nullopt;
    Pagination p;
    if (hasOffset) p.offset = offset;
    if (hasLimit)  p.limit  = limit;
    return p;
}

// ---------------------------------------------------------------------------
// Output formatters
// ---------------------------------------------------------------------------

static void printFile(const File& f) {
    std::cout << f.id << '\t' << f.name << '\t' << f.size << '\t' << f.ext << '\n';
}

static void printFiles(const std::vector<File>& files) {
    std::cout << "id\tname\tsize\text\n";
    for (const auto& f : files) printFile(f);
    std::cout << files.size() << " record(s)\n";
}

static void printTags(const std::vector<std::string>& tags) {
    for (const auto& t : tags) std::cout << t << '\n';
    std::cout << tags.size() << " tag(s)\n";
}

// ---------------------------------------------------------------------------
// Operation dispatch
// ---------------------------------------------------------------------------

static int runFileOp(Database& db, const std::string& sub,
                     std::vector<std::string> rest, std::optional<Pagination> page) {
    if (sub == "add") {
        if (rest.size() < 4) {
            std::cerr << "file add requires: <id> <name> <size> <ext>\n";
            return 1;
        }
        uint64_t size = std::stoull(rest[2]);
        db.addFile(rest[0], rest[1], size, rest[3]);
        std::cout << "Added file: " << rest[0] << '\n';
        return 0;
    }

    if (sub == "del") {
        if (rest.empty()) { std::cerr << "file del requires: <id>\n"; return 1; }
        db.deleteFile(rest[0]);
        std::cout << "Deleted file: " << rest[0] << '\n';
        return 0;
    }

    if (sub == "rename") {
        if (rest.size() < 2) { std::cerr << "file rename requires: <id> <new-name>\n"; return 1; }
        db.editFileName(rest[0], rest[1]);
        std::cout << "Renamed file " << rest[0] << " → " << rest[1] << '\n';
        return 0;
    }

    if (sub == "get") {
        if (rest.empty()) { std::cerr << "file get requires: <id>\n"; return 1; }
        auto f = db.getFile(rest[0]);
        if (!f) { std::cerr << "File not found: " << rest[0] << '\n'; return 1; }
        std::cout << "id\tname\tsize\text\n";
        printFile(*f);
        return 0;
    }

    if (sub == "list") {
        printFiles(db.getAllFiles(page));
        return 0;
    }

    if (sub == "count") {
        std::cout << db.fileCount() << " file(s)\n";
        return 0;
    }

    std::cerr << "Unknown file sub-command: " << sub << '\n';
    return 1;
}

static int runTagOp(Database& db, const std::string& sub,
                    std::vector<std::string> rest, std::optional<Pagination> page) {
    if (sub == "add") {
        if (rest.empty()) { std::cerr << "tag add requires: <name>\n"; return 1; }
        db.addTag(rest[0]);
        std::cout << "Added tag: " << rest[0] << '\n';
        return 0;
    }

    if (sub == "del") {
        if (rest.empty()) { std::cerr << "tag del requires: <name>\n"; return 1; }
        db.deleteTag(rest[0]);
        std::cout << "Deleted tag: " << rest[0] << '\n';
        return 0;
    }

    if (sub == "list") {
        printTags(db.getAllTags(page));
        return 0;
    }

    if (sub == "count") {
        std::cout << db.tagCount() << " tag(s)\n";
        return 0;
    }

    std::cerr << "Unknown tag sub-command: " << sub << '\n';
    return 1;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(std::cerr);
        return 1;
    }

    // Collect all raw arguments after the program name
    std::vector<std::string> allArgs(argv + 1, argv + argc);

    const std::string dbPath = allArgs[0];
    const std::string op     = allArgs[1];

    // Remaining args after <database> <op>
    std::vector<std::string> rest(allArgs.begin() + 2, allArgs.end());

    // Extract pagination flags from anywhere in rest
    auto page = parsePagination(rest);

    try {
        Database db(dbPath);

        // Two-word operations: file <sub> / tag <sub>
        if (op == "file") {
            if (rest.empty()) { std::cerr << "file requires a sub-command\n"; return 1; }
            std::string sub = rest[0];
            rest.erase(rest.begin());
            return runFileOp(db, sub, std::move(rest), page);
        }

        if (op == "tag") {
            if (rest.empty()) { std::cerr << "tag requires a sub-command\n"; return 1; }
            std::string sub = rest[0];
            rest.erase(rest.begin());
            return runTagOp(db, sub, std::move(rest), page);
        }

        // Single-word operations
        if (op == "bind") {
            if (rest.size() < 2) { std::cerr << "bind requires: <file-id> <tag-name>\n"; return 1; }
            db.bindTag(rest[0], rest[1]);
            std::cout << "Bound tag '" << rest[1] << "' to file '" << rest[0] << "'\n";
            return 0;
        }

        if (op == "unbind") {
            if (rest.size() < 2) { std::cerr << "unbind requires: <file-id> <tag-name>\n"; return 1; }
            db.unbindTag(rest[0], rest[1]);
            std::cout << "Unbound tag '" << rest[1] << "' from file '" << rest[0] << "'\n";
            return 0;
        }

        if (op == "file-tags") {
            if (rest.empty()) { std::cerr << "file-tags requires: <file-id>\n"; return 1; }
            printTags(db.getTagsForFile(rest[0], page));
            return 0;
        }

        if (op == "tag-files") {
            if (rest.empty()) { std::cerr << "tag-files requires at least one <tag>\n"; return 1; }
            printFiles(db.getFilesByTags(rest, page));
            return 0;
        }

        if (op == "--help" || op == "help") {
            printUsage(std::cout);
            return 0;
        }

        std::cerr << "Unknown operation: " << op << '\n';
        printUsage(std::cerr);
        return 1;

    } catch (const DatabaseException& e) {
        std::cerr << "Database error: " << e.what() << '\n';
        return 2;
    } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid argument: " << e.what() << '\n';
        return 1;
    } catch (const std::out_of_range& e) {
        std::cerr << "Argument out of range: " << e.what() << '\n';
        return 1;
    }
}

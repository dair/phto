/**
 * imager_cli — command-line demo for libimager
 *
 * Usage:
 *   imager_cli <config.toml> <command> [args...]
 *
 * Commands:
 *   add    <file>                  Add an image/video file
 *   get    <id>                    Show metadata for an image
 *   list   [--offset N] [--limit N] List all images
 *   delete <id>                    Delete an image
 *   tag    <id> <tag>              Tag an image
 *   untag  <id> <tag>              Remove a tag from an image
 *   tags   [--offset N] [--limit N] List all tags
 *   search <tag> [<tag>...]        Find images matching ALL tags
 *   count                          Print total image count
 */

#include "imager/Imager.h"
#include "config/Config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace imager;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void printImage(const ImageInfo& img) {
    std::cout << img.id << '\t' << img.name << '\t' << img.size << '\t' << img.ext;
    if (!img.tags.empty()) {
        std::cout << '\t';
        for (size_t i = 0; i < img.tags.size(); ++i) {
            if (i) std::cout << ',';
            std::cout << img.tags[i];
        }
    }
    std::cout << '\n';
}

static std::optional<uint32_t> parseUInt(const std::string& s) {
    try { return static_cast<uint32_t>(std::stoul(s)); }
    catch (...) { return std::nullopt; }
}

struct PageArgs {
    uint32_t offset{0};
    uint32_t limit{50};
};

static PageArgs parsePage(std::vector<std::string>& args) {
    PageArgs p;
    for (auto it = args.begin(); it != args.end(); ) {
        if (*it == "--offset" && std::next(it) != args.end()) {
            if (auto v = parseUInt(*std::next(it))) p.offset = *v;
            it = args.erase(it, it + 2);
        } else if (*it == "--limit" && std::next(it) != args.end()) {
            if (auto v = parseUInt(*std::next(it))) p.limit = *v;
            it = args.erase(it, it + 2);
        } else {
            ++it;
        }
    }
    return p;
}

static const char* errName(ErrorCode c) {
    switch (c) {
        case ErrorCode::Ok:              return "Ok";
        case ErrorCode::BrokenFile:      return "BrokenFile";
        case ErrorCode::DuplicateFile:   return "DuplicateFile";
        case ErrorCode::UnsupportedFormat: return "UnsupportedFormat";
        case ErrorCode::FileNotFound:    return "FileNotFound";
        case ErrorCode::StorageError:    return "StorageError";
        case ErrorCode::DatabaseError:   return "DatabaseError";
        case ErrorCode::ConfigError:     return "ConfigError";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: imager_cli <config.toml> <command> [args...]\n"
                     "Commands: add get list delete tag untag tags search count\n";
        return 1;
    }

    const std::string configPath = argv[1];
    const std::string cmd        = argv[2];
    std::vector<std::string> rest(argv + 3, argv + argc);

    config::AppConfig cfg;
    try {
        cfg = config::loadConfig(configPath);
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << '\n';
        return 1;
    }

    try {
        Imager img(cfg);

        if (cmd == "add") {
            if (rest.empty()) { std::cerr << "add requires <file>\n"; return 1; }
            std::ifstream f(rest[0], std::ios::binary);
            if (!f) { std::cerr << "Cannot open: " << rest[0] << '\n'; return 1; }
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
            // Use only the filename component
            std::filesystem::path p(rest[0]);
            auto result = img.addImage(data.data(), data.size(), p.filename().string());
            if (result.code == ErrorCode::Ok)
                std::cout << "Added: " << result.id << '\n';
            else
                std::cerr << errName(result.code) << ": " << result.message << '\n';
            return result.code == ErrorCode::Ok ? 0 : 2;
        }

        if (cmd == "get") {
            if (rest.empty()) { std::cerr << "get requires <id>\n"; return 1; }
            auto info = img.getImage(rest[0]);
            if (!info) { std::cerr << "Not found: " << rest[0] << '\n'; return 2; }
            std::cout << "id\tname\tsize\text\ttags\n";
            printImage(*info);
            return 0;
        }

        if (cmd == "list") {
            auto pg = parsePage(rest);
            std::cout << "id\tname\tsize\text\ttags\n";
            for (const auto& im : img.listImages(pg.offset, pg.limit))
                printImage(im);
            return 0;
        }

        if (cmd == "delete") {
            if (rest.empty()) { std::cerr << "delete requires <id>\n"; return 1; }
            auto ec = img.deleteImage(rest[0]);
            if (ec == ErrorCode::Ok) std::cout << "Deleted: " << rest[0] << '\n';
            else std::cerr << errName(ec) << '\n';
            return ec == ErrorCode::Ok ? 0 : 2;
        }

        if (cmd == "tag") {
            if (rest.size() < 2) { std::cerr << "tag requires <id> <tag>\n"; return 1; }
            // Ensure tag exists first
            img.createTag(rest[1]);
            auto ec = img.tagImage(rest[0], rest[1]);
            if (ec == ErrorCode::Ok) std::cout << "Tagged\n";
            else std::cerr << errName(ec) << '\n';
            return ec == ErrorCode::Ok ? 0 : 2;
        }

        if (cmd == "untag") {
            if (rest.size() < 2) { std::cerr << "untag requires <id> <tag>\n"; return 1; }
            auto ec = img.untagImage(rest[0], rest[1]);
            if (ec == ErrorCode::Ok) std::cout << "Untagged\n";
            else std::cerr << errName(ec) << '\n';
            return ec == ErrorCode::Ok ? 0 : 2;
        }

        if (cmd == "tags") {
            auto pg = parsePage(rest);
            for (const auto& t : img.listTags(pg.offset, pg.limit))
                std::cout << t << '\n';
            return 0;
        }

        if (cmd == "search") {
            if (rest.empty()) { std::cerr << "search requires at least one <tag>\n"; return 1; }
            auto pg = parsePage(rest);
            std::cout << "id\tname\tsize\text\ttags\n";
            for (const auto& im : img.getImagesByTags(rest, pg.offset, pg.limit))
                printImage(im);
            return 0;
        }

        if (cmd == "count") {
            std::cout << img.imageCount() << " image(s)\n";
            return 0;
        }

        std::cerr << "Unknown command: " << cmd << '\n';
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }
}

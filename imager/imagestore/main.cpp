#include "ErrorFile.h"

#include <imager/Imager.h>
#include <imager/Types.h>
#include <config/Config.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include <cstdlib>
#include <getopt.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

struct Stats {
    std::atomic<uint64_t> processed{0};
    std::atomic<uint64_t> added{0};
    std::atomic<uint64_t> duplicates{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> skipped{0};
    std::atomic<uint64_t> totalBytes{0};
};

// ---------------------------------------------------------------------------
// Verbose / progress output
// ---------------------------------------------------------------------------

static std::mutex g_outputMutex;

static void stderrLine(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_outputMutex);
    std::cerr << line << '\n';
}

struct ProgressTracker {
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastPrint;
    uint64_t lastCount{0};
    std::mutex mtx;

    ProgressTracker() {
        startTime = std::chrono::steady_clock::now();
        lastPrint = startTime;
    }

    void maybeReport(const Stats& stats) {
        uint64_t processed = stats.processed.load();

        std::lock_guard<std::mutex> lk(mtx);
        auto now = std::chrono::steady_clock::now();
        bool byCount = (processed - lastCount) >= 1000;
        bool byTime  = std::chrono::duration_cast<std::chrono::seconds>(
                           now - lastPrint).count() >= 5;
        if (!byCount && !byTime) return;
        lastCount = processed;
        lastPrint = now;

        uint64_t bytes   = stats.totalBytes.load();
        double elapsedS  = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - startTime).count() / 1000.0;
        double mbps      = (elapsedS > 0.0)
                           ? (static_cast<double>(bytes) / (1024.0 * 1024.0)) / elapsedS
                           : 0.0;

        std::lock_guard<std::mutex> outLk(g_outputMutex);
        std::cerr << "[progress] " << processed << "/? processed, "
                  << stats.errors.load() << " errors, "
                  << stats.duplicates.load() << " duplicates, "
                  << std::fixed << std::setprecision(1) << mbps << " MB/s\n";
    }
};

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

static void printUsage(const char* prog) {
    std::cerr <<
        "Usage: " << prog << " [OPTIONS]\n"
        "\n"
        "Reads file paths from stdin (one per line) and stores each via libimager.\n"
        "\n"
        "Options:\n"
        "  -c, --config PATH    Configuration file (default: ~/.imagestore.toml)\n"
        "  -e, --errors PATH    Error file: skip paths listed here, append new failures\n"
        "  -j, --jobs N         Concurrent image processing limit (default: nproc)\n"
        "  -n, --dry-run        Validate and hash only, do not write to storage or DB\n"
        "  -v, --verbose        Print progress and per-file status to stderr\n"
        "  -h, --help           Show usage and exit\n"
        "\n"
        "Exit codes:\n"
        "  0   All files processed successfully (duplicates are not errors)\n"
        "  1   Usage error or configuration failure\n"
        "  2   Some files failed\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Defaults
    std::string configPath;
    std::string errorsPath;
    unsigned int jobs = std::thread::hardware_concurrency();
    if (jobs == 0) jobs = 4;
    jobs = std::min(jobs, 256u);
    bool dryRun  = false;
    bool verbose = false;

    static const option longOpts[] = {
        {"config",  required_argument, nullptr, 'c'},
        {"errors",  required_argument, nullptr, 'e'},
        {"jobs",    required_argument, nullptr, 'j'},
        {"dry-run", no_argument,       nullptr, 'n'},
        {"verbose", no_argument,       nullptr, 'v'},
        {"help",    no_argument,       nullptr, 'h'},
        {nullptr,   0,                 nullptr,  0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:e:j:nvh", longOpts, nullptr)) != -1) {
        switch (opt) {
            case 'c': configPath = optarg; break;
            case 'e': errorsPath = optarg; break;
            case 'j': {
                int n = std::atoi(optarg);
                if (n <= 0) {
                    std::cerr << "Invalid --jobs value: " << optarg << '\n';
                    return 1;
                }
                jobs = std::min(static_cast<unsigned int>(n), 256u);
                break;
            }
            case 'n': dryRun  = true; break;
            case 'v': verbose = true; break;
            case 'h': printUsage(argv[0]); return 0;
            default:  printUsage(argv[0]); return 1;
        }
    }

    // Resolve config path
    if (configPath.empty()) {
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "Cannot determine home directory; use --config\n";
            return 1;
        }
        configPath = std::string(home) + "/.imagestore.toml";
    }

    // Load config
    config::AppConfig cfg;
    try {
        cfg = config::loadConfig(configPath);
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << '\n';
        return 1;
    }

    // Create Imager
    imager::Imager img(cfg);

    // Create ErrorFile if requested
    std::unique_ptr<imagestore::ErrorFile> errorFile;
    if (!errorsPath.empty()) {
        try {
            errorFile = std::make_unique<imagestore::ErrorFile>(errorsPath);
        } catch (const std::exception& e) {
            std::cerr << "Cannot open error file: " << e.what() << '\n';
            return 1;
        }
        if (verbose && errorFile->initialSkipCount() > 0) {
            stderrLine("Loaded " + std::to_string(errorFile->initialSkipCount()) +
                       " paths to skip from " + errorsPath);
        }
    }

    Stats stats;
    ProgressTracker progress;
    auto startTime = std::chrono::steady_clock::now();

    // Bounded semaphore — caps files in-flight at `jobs`
    std::counting_semaphore<256> sem(static_cast<std::ptrdiff_t>(jobs));

    std::vector<std::future<void>> futures;
    futures.reserve(1024);

    // Read stdin line by line
    std::string line;
    while (std::getline(std::cin, line)) {
        // Trim leading/trailing whitespace
        auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        auto last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);

        if (line.empty() || line[0] == '#') continue;

        // Resolve to absolute path
        std::error_code ec;
        fs::path absPath = fs::absolute(line, ec);
        if (ec) {
            if (verbose) stderrLine("ERR " + line + ": cannot resolve path: " + ec.message());
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            stats.processed.fetch_add(1, std::memory_order_relaxed);
            if (errorFile) errorFile->recordError(line);
            continue;
        }
        std::string absStr = absPath.string();

        // Check skip set
        if (errorFile && errorFile->shouldSkip(absStr)) {
            if (verbose) stderrLine("SKIP " + absStr + " (in error list)");
            stats.skipped.fetch_add(1, std::memory_order_relaxed);
            stats.processed.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Backpressure: block until a slot is free
        sem.acquire();

        futures.push_back(std::async(std::launch::async,
            [&img, &stats, &sem, &errorFile, &progress, verbose, dryRun,
             capturedPath = std::move(absPath),
             capturedStr  = std::move(absStr)]() mutable
            {
                // Release semaphore slot on scope exit
                struct Release {
                    std::counting_semaphore<256>& s;
                    ~Release() { s.release(); }
                } guard{sem};

                // Stat the file
                std::error_code fec;
                auto fileSize = fs::file_size(capturedPath, fec);
                if (fec) {
                    if (verbose)
                        stderrLine("ERR " + capturedStr + ": " + fec.message());
                    stats.errors.fetch_add(1, std::memory_order_relaxed);
                    stats.processed.fetch_add(1, std::memory_order_relaxed);
                    if (errorFile) errorFile->recordError(capturedStr);
                    return;
                }

                // Read file into Blob
                imager::Blob blob(fileSize);
                {
                    std::ifstream in(capturedPath, std::ios::binary);
                    if (!in) {
                        if (verbose)
                            stderrLine("ERR " + capturedStr + ": cannot open file");
                        stats.errors.fetch_add(1, std::memory_order_relaxed);
                        stats.processed.fetch_add(1, std::memory_order_relaxed);
                        if (errorFile) errorFile->recordError(capturedStr);
                        return;
                    }
                    if (fileSize > 0) {
                        in.read(reinterpret_cast<char*>(blob.writableData()),
                                static_cast<std::streamsize>(fileSize));
                        if (in.fail() && !in.eof()) {
                            if (verbose)
                                stderrLine("ERR " + capturedStr + ": read error");
                            stats.errors.fetch_add(1, std::memory_order_relaxed);
                            stats.processed.fetch_add(1, std::memory_order_relaxed);
                            if (errorFile) errorFile->recordError(capturedStr);
                            return;
                        }
                    }
                }
                blob.freeze();
                stats.totalBytes.fetch_add(fileSize, std::memory_order_relaxed);

                // Dispatch to libimager
                imager::AddResult result = dryRun
                    ? img.validateOnly(blob, capturedPath.filename().string())
                    : img.addImage   (blob, capturedPath.filename().string());

                stats.processed.fetch_add(1, std::memory_order_relaxed);

                switch (result.code) {
                    case imager::ErrorCode::Ok:
                        stats.added.fetch_add(1, std::memory_order_relaxed);
                        if (verbose)
                            stderrLine("OK " + result.id + " " + capturedStr);
                        break;
                    case imager::ErrorCode::DuplicateFile:
                        stats.duplicates.fetch_add(1, std::memory_order_relaxed);
                        if (verbose)
                            stderrLine("DUP " + capturedStr);
                        break;
                    default:
                        stats.errors.fetch_add(1, std::memory_order_relaxed);
                        if (verbose)
                            stderrLine("ERR " + capturedStr + ": " + result.message);
                        if (errorFile) errorFile->recordError(capturedStr);
                        break;
                }

                if (verbose)
                    progress.maybeReport(stats);
            }
        ));
    }

    // Wait for all in-flight tasks
    for (auto& f : futures)
        if (f.valid()) f.get();

    // Final summary — always printed
    auto endTime  = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         endTime - startTime).count() / 1000.0;

    uint64_t processed   = stats.processed.load();
    uint64_t added       = stats.added.load();
    uint64_t duplicates  = stats.duplicates.load();
    uint64_t errors      = stats.errors.load();
    uint64_t skipped     = stats.skipped.load();
    uint64_t totalBytes  = stats.totalBytes.load();

    double filesPerSec   = (elapsed > 0.0) ? static_cast<double>(processed) / elapsed : 0.0;
    double totalGB       = static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0);
    double totalMB       = static_cast<double>(totalBytes) / (1024.0 * 1024.0);

    std::cerr << std::fixed;
    if (dryRun) {
        std::cerr << processed << " processed, "
                  << added      << " valid, "
                  << duplicates << " would-be-duplicates, "
                  << errors     << " errors"
                  << " — " << std::setprecision(1) << elapsed << "s"
                  << " (dry run, no writes)\n";
    } else {
        std::cerr << processed  << " processed, "
                  << added      << " added, "
                  << duplicates << " duplicates, "
                  << errors     << " errors, "
                  << skipped    << " skipped — "
                  << std::setprecision(1) << elapsed << "s ("
                  << std::setprecision(0) << filesPerSec << " files/s, ";
        if (totalGB >= 1.0)
            std::cerr << std::setprecision(1) << totalGB << " GB total";
        else
            std::cerr << std::setprecision(1) << totalMB << " MB total";
        std::cerr << ")\n";
    }

    return (errors > 0) ? 2 : 0;
}

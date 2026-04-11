#include <config/Config.h>
#include <getopt.h>
#include <imager/Imager.h>
#include <imager/Types.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
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

#include "DisplayMode.h"
#include "ErrorFile.h"
#include "Output.h"
#include "ProgressReporter.h"
#include "Stats.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

static void printUsage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [OPTIONS]\n"
               "\n"
               "Reads file paths from stdin (one per line) and stores each via libimager.\n"
               "\n"
               "Options:\n"
               "  -c, --config PATH    Configuration file (default: ~/.imagestore.toml)\n"
               "  -e, --errors PATH    Error file: skip paths listed here, append new failures\n"
               "  -j, --jobs N         Concurrent image processing limit (default: nproc)\n"
               "  -n, --dry-run        Validate and hash only, do not write to storage or DB\n"
               "  -v, --verbose        Print per-file status lines to stderr (OK/DUP/ERR/SKIP)\n"
               "  -q, --quiet          Suppress all progress and summary output\n"
               "      --graph          Display animated pipeline graph (requires TTY on stderr)\n"
               "  -h, --help           Show usage and exit\n"
               "\n"
               "Exit codes:\n"
               "  0   All files processed successfully (duplicates are not errors)\n"
               "  1   Usage error or configuration failure\n"
               "  2   Some files failed\n"
               "\n"
               "Notes:\n"
               "  --quiet and --graph are mutually exclusive.\n"
               "  --graph and --verbose are mutually exclusive.\n"
               "  --graph falls back to normal mode if stderr is not a TTY.\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  // Defaults
  std::string configPath;
  std::string errorsPath;
  unsigned int jobs = std::thread::hardware_concurrency();
  if (jobs == 0) {
    jobs = 4;
  }
  jobs = std::min(jobs, 256u);
  bool dryRun = false;
  bool verbose = false;
  bool quiet = false;
  bool graphMode = false;

  static const option longOpts[] = {
    {"config", required_argument, nullptr, 'c'},
    {"errors", required_argument, nullptr, 'e'},
    {"jobs", required_argument, nullptr, 'j'},
    {"dry-run", no_argument, nullptr, 'n'},
    {"verbose", no_argument, nullptr, 'v'},
    {"quiet", no_argument, nullptr, 'q'},
    {"graph", no_argument, nullptr, 1},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0}
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "c:e:j:nvqh", longOpts, nullptr)) != -1) {
    switch (opt) {
      case 'c':
        configPath = optarg;
        break;
      case 'e':
        errorsPath = optarg;
        break;
      case 'j': {
        int n = std::atoi(optarg);
        if (n <= 0) {
          std::cerr << "Invalid --jobs value: " << optarg << '\n';
          return 1;
        }
        jobs = std::min(static_cast<unsigned int>(n), 256u);
        break;
      }
      case 'n':
        dryRun = true;
        break;
      case 'v':
        verbose = true;
        break;
      case 'q':
        quiet = true;
        break;
      case 1:
        graphMode = true;
        break;
      case 'h':
        printUsage(argv[0]);
        return 0;
      default:
        printUsage(argv[0]);
        return 1;
    }
  }

  // Validate mutually exclusive flag combinations
  if (quiet && graphMode) {
    std::cerr << "--quiet and --graph are mutually exclusive\n";
    return 1;
  }
  if (graphMode && verbose) {
    std::cerr << "--graph and --verbose are mutually exclusive\n";
    return 1;
  }

  // Resolve display mode
  imagestore::DisplayMode mode = imagestore::DisplayMode::Normal;
  if (quiet) {
    mode = imagestore::DisplayMode::Quiet;
  } else if (graphMode) {
    mode = imagestore::DisplayMode::Graph;
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
      imagestore::stderrLine(
        "Loaded " + std::to_string(errorFile->initialSkipCount()) + " paths to skip from " + errorsPath
      );
    }
  }

  imagestore::Stats stats;
  auto startTime = std::chrono::steady_clock::now();

  imagestore::ProgressReporter progress(mode, img, stats, jobs, dryRun, startTime);

  // Bounded semaphore — caps files in-flight at `jobs`
  std::counting_semaphore<256> sem(static_cast<std::ptrdiff_t>(jobs));

  std::vector<std::future<void>> futures;
  futures.reserve(1024);

  // Read stdin line by line
  std::string line;
  while (std::getline(std::cin, line)) {
    // Trim leading/trailing whitespace
    auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      continue;
    }
    auto last = line.find_last_not_of(" \t\r\n");
    line = line.substr(first, last - first + 1);

    if (line.empty() || line[0] == '#') {
      continue;
    }

    // Resolve to absolute path
    std::error_code ec;
    fs::path absPath = fs::absolute(line, ec);
    if (ec) {
      if (verbose) {
        imagestore::stderrLine("ERR " + line + ": cannot resolve path: " + ec.message());
      }
      stats.errors.fetch_add(1, std::memory_order_relaxed);
      stats.processed.fetch_add(1, std::memory_order_relaxed);
      if (errorFile) {
        errorFile->recordError(line);
      }
      continue;
    }
    std::string absStr = absPath.string();

    // Check skip set
    if (errorFile && errorFile->shouldSkip(absStr)) {
      if (verbose) {
        imagestore::stderrLine("SKIP " + absStr + " (in error list)");
      }
      stats.skipped.fetch_add(1, std::memory_order_relaxed);
      stats.processed.fetch_add(1, std::memory_order_relaxed);
      continue;
    }

    // Backpressure: block until a slot is free
    sem.acquire();

    futures.push_back(
      std::async(
        std::launch::async,
        [&img,
         &stats,
         &sem,
         &errorFile,
         verbose,
         dryRun,
         capturedPath = std::move(absPath),
         capturedStr = std::move(absStr)]() mutable {
          // Release semaphore slot on scope exit
          struct Release {
            std::counting_semaphore<256>& s;
            ~Release() {
              s.release();
            }
          } guard{sem};

          // Dispatch to libimager
          imager::AddResult result;
          if (dryRun) {
            // Dry-run: must pre-load blob for validateOnly (no validateOnlyFile method)
            std::error_code fec;
            auto fileSize = fs::file_size(capturedPath, fec);
            if (fec) {
              if (verbose) {
                imagestore::stderrLine("ERR " + capturedStr + ": " + fec.message());
              }
              stats.errors.fetch_add(1, std::memory_order_relaxed);
              stats.processed.fetch_add(1, std::memory_order_relaxed);
              if (errorFile) {
                errorFile->recordError(capturedStr);
              }
              return;
            }

            imager::Blob blob(fileSize);
            {
              std::ifstream in(capturedPath, std::ios::binary);
              if (!in) {
                if (verbose) {
                  imagestore::stderrLine("ERR " + capturedStr + ": cannot open file");
                }
                stats.errors.fetch_add(1, std::memory_order_relaxed);
                stats.processed.fetch_add(1, std::memory_order_relaxed);
                if (errorFile) {
                  errorFile->recordError(capturedStr);
                }
                return;
              }
              if (fileSize > 0) {
                in.read(reinterpret_cast<char*>(blob.writableData()), static_cast<std::streamsize>(fileSize));
                if (in.fail() && !in.eof()) {
                  if (verbose) {
                    imagestore::stderrLine("ERR " + capturedStr + ": read error");
                  }
                  stats.errors.fetch_add(1, std::memory_order_relaxed);
                  stats.processed.fetch_add(1, std::memory_order_relaxed);
                  if (errorFile) {
                    errorFile->recordError(capturedStr);
                  }
                  return;
                }
              }
            }
            blob.freeze();
            stats.totalBytes.fetch_add(fileSize, std::memory_order_relaxed);
            result = img.validateOnly(blob, capturedPath.filename().string());
          } else {
            // Non-dry-run: stat for totalBytes accounting, then delegate I/O to addFile
            std::error_code fec;
            auto fileSize = fs::file_size(capturedPath, fec);
            if (fec) {
              if (verbose) {
                imagestore::stderrLine("ERR " + capturedStr + ": " + fec.message());
              }
              stats.errors.fetch_add(1, std::memory_order_relaxed);
              stats.processed.fetch_add(1, std::memory_order_relaxed);
              if (errorFile) {
                errorFile->recordError(capturedStr);
              }
              return;
            }
            stats.totalBytes.fetch_add(fileSize, std::memory_order_relaxed);
            result = img.addFile(capturedPath);
          }

          stats.processed.fetch_add(1, std::memory_order_relaxed);

          switch (result.code) {
            case imager::ErrorCode::Ok:
              stats.added.fetch_add(1, std::memory_order_relaxed);
              if (verbose) {
                imagestore::stderrLine("OK " + result.id + " " + capturedStr);
              }
              break;
            case imager::ErrorCode::DuplicateFile:
              stats.duplicates.fetch_add(1, std::memory_order_relaxed);
              if (verbose) {
                imagestore::stderrLine("DUP " + capturedStr);
              }
              break;
            default:
              stats.errors.fetch_add(1, std::memory_order_relaxed);
              if (verbose) {
                imagestore::stderrLine("ERR " + capturedStr + ": " + result.message);
              }
              if (errorFile) {
                errorFile->recordError(capturedStr);
              }
              break;
          }
        }
      )
    );
  }

  // Wait for all in-flight tasks
  for (auto& f : futures) {
    if (f.valid()) {
      f.get();
    }
  }

  progress.stop();

  auto endTime = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / 1000.0;
  progress.printFinalSummary(elapsed);

  return (stats.errors.load() > 0) ? 2 : 0;
}

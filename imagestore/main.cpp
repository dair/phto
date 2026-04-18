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
#include "MemoryReporter.h"
#include "Output.h"
#include "ProgressReporter.h"
#include "ResultLog.h"
#include "SlotTracker.h"
#include "Stats.h"

#ifdef MEMORY_TRACE
  #include "MemoryTrace.h"
#endif

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
               "  -d, --delete         Delete mode: treat stdin paths as files to delete from the\n"
               "                       collection (file is read, hashed, and the matching id is\n"
               "                       removed from all databases and storage). No validation is\n"
               "                       performed.\n"
               "  -e, --errors PATH    Error file: skip paths listed here, append new failures\n"
               "  -j, --jobs N         Concurrent image processing limit (default: nproc)\n"
               "  -n, --dry-run        Validate and hash only, do not write to storage or DB\n"
               "  -v, --verbose        Real-time slot display (TTY) or per-file lines (non-TTY)\n"
               "  -q, --quiet          Suppress all output\n"
               "      --memory-report  Periodically log RSS + libimager memory gauges to stderr\n"
               "      --memory-interval N  Seconds between memory reports (default: 5)\n"
               "  -h, --help           Show usage and exit\n"
               "\n"
               "Exit codes:\n"
               "  0   All files processed successfully (duplicates/missing are not errors)\n"
               "  1   Usage error or configuration failure\n"
               "  2   Some files failed\n"
               "\n"
               "Notes:\n"
               "  --quiet and --verbose are mutually exclusive.\n"
               "  In verbose mode, stderr must be a TTY for real-time display;\n"
               "  otherwise falls back to per-file line output.\n"
               "  In delete mode, --dry-run computes hashes and checks existence without deleting.\n";
}

// ---------------------------------------------------------------------------
// Stage mapping: imager::ProcessingStage -> imagestore::PipelineStage
// ---------------------------------------------------------------------------

static imagestore::PipelineStage mapStage(imager::ProcessingStage stage) {
  using IP = imager::ProcessingStage;
  using SP = imagestore::PipelineStage;
  switch (stage) {
    case IP::Reading:
      return SP::Reading;
    case IP::Validating:
      return SP::Validating;
    case IP::Hashing:
      return SP::Hashing;
    case IP::WaitingMutex:
      return SP::WaitingMutex;
    case IP::DedupChecking:
      return SP::DedupChecking;
    case IP::WritingStorage:
      return SP::WritingStorage;
    case IP::InsertingDb:
      return SP::InsertingDb;
  }
  return SP::Reading;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef MEMORY_TRACE
  imagestore::memoryTraceInit();
#endif
  // Defaults
  std::string configPath;
  std::string errorsPath;
  unsigned int jobs = std::thread::hardware_concurrency();
  if (jobs == 0) {
    jobs = 4;
  }
  jobs = std::min(jobs, 256u);
  bool dryRun = false;
  bool deleteMode = false;
  bool verbose = false;
  bool quiet = false;
  bool memoryReport = false;
  unsigned int memoryIntervalSec = 5;

  static constexpr int OPT_MEMORY_REPORT = 1000;
  static constexpr int OPT_MEMORY_INTERVAL = 1001;

  static const option longOpts[] = {
    {"config", required_argument, nullptr, 'c'},
    {"delete", no_argument, nullptr, 'd'},
    {"errors", required_argument, nullptr, 'e'},
    {"jobs", required_argument, nullptr, 'j'},
    {"dry-run", no_argument, nullptr, 'n'},
    {"verbose", no_argument, nullptr, 'v'},
    {"quiet", no_argument, nullptr, 'q'},
    {"memory-report", no_argument, nullptr, OPT_MEMORY_REPORT},
    {"memory-interval", required_argument, nullptr, OPT_MEMORY_INTERVAL},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0}
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "c:de:j:nvqh", longOpts, nullptr)) != -1) {
    switch (opt) {
      case 'c':
        configPath = optarg;
        break;
      case 'd':
        deleteMode = true;
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
      case OPT_MEMORY_REPORT:
        memoryReport = true;
        break;
      case OPT_MEMORY_INTERVAL: {
        int n = std::atoi(optarg);
        if (n <= 0) {
          std::cerr << "Invalid --memory-interval value: " << optarg << '\n';
          return 1;
        }
        memoryIntervalSec = static_cast<unsigned int>(n);
        break;
      }
      case 'h':
        printUsage(argv[0]);
        return 0;
      default:
        printUsage(argv[0]);
        return 1;
    }
  }

  // Validate mutually exclusive flag combinations
  if (quiet && verbose) {
    std::cerr << "--quiet and --verbose are mutually exclusive\n";
    return 1;
  }
  if (quiet && memoryReport) {
    std::cerr << "--quiet and --memory-report are mutually exclusive\n";
    return 1;
  }

  // Resolve display mode
  imagestore::DisplayMode mode = imagestore::DisplayMode::Normal;
  if (quiet) {
    mode = imagestore::DisplayMode::Quiet;
  } else if (verbose) {
    mode = imagestore::DisplayMode::Verbose;
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
    if (mode != imagestore::DisplayMode::Quiet && errorFile->initialSkipCount() > 0) {
      imagestore::stderrLine(
        "Loaded " + std::to_string(errorFile->initialSkipCount()) + " paths to skip from " + errorsPath
      );
    }
  }

  imagestore::Stats stats;
  auto startTime = std::chrono::steady_clock::now();

  // Result log and slot tracker are wired together via ProgressReporter.
  imagestore::SlotTracker slots(jobs);
  imagestore::ResultLog resultLog;

  if (mode == imagestore::DisplayMode::Quiet) {
    resultLog.setEnabled(false);
  }

  imagestore::ProgressReporter progress(mode, stats, slots, resultLog, jobs, dryRun, startTime, deleteMode);
  imagestore::MemoryReporter memoryReporter(
    memoryReport,
    std::chrono::seconds(memoryIntervalSec),
    stats,
    img.metrics(),
    resultLog
  );

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
      resultLog.append("ERR  " + line + ": cannot resolve path: " + ec.message());
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
      resultLog.append("SKIP " + absStr + " (in error list)");
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
         &slots,
         &resultLog,
         &errorFile,
         dryRun,
         deleteMode,
         capturedPath = std::move(absPath),
         capturedStr = std::move(absStr)]() mutable {
          // Acquire a slot tracker slot — semaphore guarantees a free slot exists
          unsigned int slot = slots.acquire(capturedStr);

          // RAII: release semaphore and slot on scope exit
          struct SemRelease {
            std::counting_semaphore<256>& s;
            ~SemRelease() {
              s.release();
            }
          } semGuard{sem};

          struct SlotRelease {
            imagestore::SlotTracker& t;
            unsigned int s;
            ~SlotRelease() {
              t.release(s);
            }
          } slotGuard{slots, slot};

          // Stage callback: maps imager stages to slot tracker updates
          auto onStage = [&slots, slot](imager::ProcessingStage stage) {
            slots.setStage(slot, mapStage(stage));
          };

          // ---- Delete mode ----
          if (deleteMode) {
            if (dryRun) {
              // Dry-run delete: hash only, check existence, do not mutate
              std::string id = img.hashOnlyFile(capturedPath, onStage);
              stats.processed.fetch_add(1, std::memory_order_relaxed);
              if (id.empty()) {
                resultLog.append("ERR  " + capturedStr + ": failed to hash file");
                stats.errors.fetch_add(1, std::memory_order_relaxed);
                if (errorFile) {
                  errorFile->recordError(capturedStr);
                }
                return;
              }
              std::string idPfx = id.size() >= 12 ? id.substr(0, 12) : id;
              if (img.getImage(id)) {
                resultLog.append("DRY  " + capturedStr + "  " + idPfx);
                stats.added.fetch_add(1, std::memory_order_relaxed);
              } else {
                resultLog.append("DRY? " + capturedStr + "  " + idPfx);
                stats.duplicates.fetch_add(1, std::memory_order_relaxed);
              }
            } else {
              // Real delete
              imager::DeleteResult res = img.deleteFile(capturedPath, onStage);
              stats.processed.fetch_add(1, std::memory_order_relaxed);
              std::string idPfx = res.id.size() >= 12 ? res.id.substr(0, 12) : res.id;
              switch (res.code) {
                case imager::ErrorCode::Ok:
                  stats.added.fetch_add(1, std::memory_order_relaxed);
                  resultLog.append("DEL  " + capturedStr + "  " + idPfx);
                  break;
                case imager::ErrorCode::FileNotFound:
                  // Not an error — idempotent
                  stats.duplicates.fetch_add(1, std::memory_order_relaxed);
                  resultLog.append("MISS " + capturedStr + "  " + idPfx);
                  break;
                default:
                  stats.errors.fetch_add(1, std::memory_order_relaxed);
                  resultLog.append("ERR  " + capturedStr + ": " + res.message);
                  if (errorFile) {
                    errorFile->recordError(capturedStr);
                  }
                  break;
              }
            }
            return;
          }

          // ---- Import mode ----
          imager::AddResult result;

          if (dryRun) {
            // Dry-run: manually read the file and call validateOnly
            slots.setStage(slot, imagestore::PipelineStage::Reading);

            std::error_code fec;
            auto fileSize = fs::file_size(capturedPath, fec);
            if (fec) {
              resultLog.append("ERR  " + capturedStr + ": " + fec.message());
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
                resultLog.append("ERR  " + capturedStr + ": cannot open file");
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
                  resultLog.append("ERR  " + capturedStr + ": read error");
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

            slots.setStage(slot, imagestore::PipelineStage::Validating);
            result = img.validateOnly(blob, capturedPath.filename().string());
          } else {
            // Non-dry-run: stat for totalBytes, then delegate I/O to addFile with callback
            std::error_code fec;
            auto fileSize = fs::file_size(capturedPath, fec);
            if (fec) {
              resultLog.append("ERR  " + capturedStr + ": " + fec.message());
              stats.errors.fetch_add(1, std::memory_order_relaxed);
              stats.processed.fetch_add(1, std::memory_order_relaxed);
              if (errorFile) {
                errorFile->recordError(capturedStr);
              }
              return;
            }
            stats.totalBytes.fetch_add(fileSize, std::memory_order_relaxed);
            result = img.addFile(capturedPath, "", onStage);
          }

          stats.processed.fetch_add(1, std::memory_order_relaxed);

          switch (result.code) {
            case imager::ErrorCode::Ok: {
              stats.added.fetch_add(1, std::memory_order_relaxed);
              // Show first 12 hex chars of hash + original extension
              std::string hashPfx = result.id.size() >= 12 ? result.id.substr(0, 12) : result.id;
              std::string ext = capturedPath.extension().string();
              resultLog.append("OK   " + capturedStr + " -> " + hashPfx + "..." + ext);
              break;
            }
            case imager::ErrorCode::DuplicateFile:
              stats.duplicates.fetch_add(1, std::memory_order_relaxed);
              resultLog.append("DUP  " + capturedStr);
              break;
            default:
              stats.errors.fetch_add(1, std::memory_order_relaxed);
              resultLog.append("ERR  " + capturedStr + ": " + result.message);
              if (errorFile) {
                errorFile->recordError(capturedStr);
              }
              break;
          }
        }
      )
    );

    // Drain completed futures to avoid unbounded memory growth on large inputs.
    futures.erase(
      std::remove_if(
        futures.begin(),
        futures.end(),
        [](std::future<void>& f) {
          if (f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            f.get();
            return true;
          }
          return false;
        }
      ),
      futures.end()
    );
  }

  // Wait for all in-flight tasks
  for (auto& f : futures) {
    if (f.valid()) {
      f.get();
    }
  }

  progress.stop();
  memoryReporter.stop();

  auto endTime = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / 1000.0;
  progress.printFinalSummary(elapsed);

  return (stats.errors.load() > 0) ? 2 : 0;
}

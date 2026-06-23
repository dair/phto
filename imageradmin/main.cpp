#include <auth/PasswordHash.h>
#include <auth/UserStore.h>
#include <auth/types/AuthError.h>
#include <auth/types/User.h>
#include <config/Config.h>
#include <database/Database.h>
#include <termios.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

void printUsage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " <config.toml> <command> [args]\n"
               "\n"
               "Commands:\n"
               "  user add    <login> <full name> [--admin] [--password-stdin]\n"
               "  user del    <login>\n"
               "  user passwd <login> [--password-stdin]\n"
               "  user enable  <login>\n"
               "  user disable <login>\n"
               "  user promote <login>\n"
               "  user demote  <login>\n"
               "  user list   [--offset N] [--limit N]\n"
               "\n"
               "Exit codes:\n"
               "  0  success\n"
               "  1  usage / argument error\n"
               "  2  config / auth / DB error (e.g. user not found, duplicate)\n";
}

// ---------------------------------------------------------------------------
// Password reading
// ---------------------------------------------------------------------------

/// Read a single line from stdin (--password-stdin mode).
std::optional<std::string> readPasswordFromStdin() {
  std::string pw;
  if (!std::getline(std::cin, pw)) {
    std::cerr << "Error: failed to read password from stdin\n";
    return std::nullopt;
  }
  return pw;
}

/// Read a password from the TTY with no echo. Prints newline after.
/// Returns nullopt on I/O error.
std::optional<std::string> readPasswordFromTty(const char* prompt) {
  // Open /dev/tty directly so we get the real TTY even if stdin is redirected.
  FILE* tty = fopen("/dev/tty", "r+");
  if (!tty) {
    std::cerr << "Error: cannot open /dev/tty\n";
    return std::nullopt;
  }

  // Save current terminal settings.
  struct termios oldTerm{};
  if (tcgetattr(fileno(tty), &oldTerm) != 0) {
    fclose(tty);
    std::cerr << "Error: tcgetattr failed\n";
    return std::nullopt;
  }

  // Disable echo.
  struct termios noEcho = oldTerm;
  noEcho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
  if (tcsetattr(fileno(tty), TCSAFLUSH, &noEcho) != 0) {
    fclose(tty);
    std::cerr << "Error: tcsetattr failed\n";
    return std::nullopt;
  }

  fprintf(tty, "%s", prompt);
  fflush(tty);

  std::string pw;
  int ch = 0;
  while ((ch = fgetc(tty)) != EOF && ch != '\n') {
    pw += static_cast<char>(ch);
  }

  // Restore terminal settings.
  tcsetattr(fileno(tty), TCSAFLUSH, &oldTerm);
  fprintf(tty, "\n");
  fflush(tty);
  fclose(tty);

  return pw;
}

/// Read password interactively, prompting twice for add/passwd commands.
/// Returns nullopt on mismatch or error.
std::optional<std::string> promptPassword(bool confirm) {
  auto pw1 = readPasswordFromTty("Password: ");
  if (!pw1) {
    return std::nullopt;
  }
  if (!confirm) {
    return pw1;
  }
  auto pw2 = readPasswordFromTty("Confirm password: ");
  if (!pw2) {
    return std::nullopt;
  }
  if (*pw1 != *pw2) {
    std::cerr << "Error: passwords do not match\n";
    return std::nullopt;
  }
  return pw1;
}

// ---------------------------------------------------------------------------
// Table formatting for user list
// ---------------------------------------------------------------------------

/// Format unix epoch as ISO-8601 date string (UTC).
std::string formatTime(uint64_t epochSec) {
  if (epochSec == 0) {
    return "-";
  }
  time_t t = static_cast<time_t>(epochSec);
  struct tm tmVal{};
  gmtime_r(&t, &tmVal);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmVal);
  return buf;
}

void printUserTable(const std::vector<auth::User>& users) {
  // Header
  std::cout << std::left;
  constexpr int W_LOGIN = 20;
  constexpr int W_NAME = 30;
  constexpr int W_ADMIN = 6;
  constexpr int W_ENABLED = 8;

  auto pad = [](const std::string& s, int w) -> std::string {
    if (static_cast<int>(s.size()) >= w) {
      return s.substr(0, static_cast<size_t>(w - 1)) + " ";
    }
    return s + std::string(static_cast<size_t>(w - static_cast<int>(s.size())), ' ');
  };

  std::cout << pad("login", W_LOGIN) << pad("full name", W_NAME) << pad("admin", W_ADMIN) << pad("enabled", W_ENABLED)
            << "created\n";
  std::cout << std::string(static_cast<size_t>(W_LOGIN + W_NAME + W_ADMIN + W_ENABLED + 20), '-') << "\n";

  for (const auto& u : users) {
    std::cout << pad(u.login, W_LOGIN) << pad(u.fullName, W_NAME) << pad(u.isAdmin ? "yes" : "no", W_ADMIN)
              << pad(u.enabled ? "yes" : "no", W_ENABLED) << formatTime(u.createdAt) << "\n";
  }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

int cmdUserAdd(auth::UserStore& store, const config::AuthConfig& authCfg, const std::vector<std::string>& args) {
  // args: <login> <full name> [--admin] [--password-stdin]
  if (args.size() < 2) {
    std::cerr << "Usage: user add <login> <full name> [--admin] [--password-stdin]\n";
    return 1;
  }
  const std::string& login = args[0];
  const std::string& fullName = args[1];
  bool isAdmin = false;
  bool pwStdin = false;

  for (size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--admin") {
      isAdmin = true;
    } else if (args[i] == "--password-stdin") {
      pwStdin = true;
    } else {
      std::cerr << "Unknown option: " << args[i] << "\n";
      return 1;
    }
  }

  std::optional<std::string> pw;
  if (pwStdin) {
    pw = readPasswordFromStdin();
  } else {
    pw = promptPassword(/*confirm=*/true);
  }
  if (!pw) {
    return 2;
  }

  try {
    auto rec = auth::hashPassword(*pw, authCfg.pbkdf2Iterations);
    store.create(login, fullName, rec, isAdmin);
    std::cout << "User '" << login << "' created.\n";
    return 0;
  } catch (const auth::AuthException& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}

int cmdUserDel(auth::UserStore& store, const std::vector<std::string>& args) {
  if (args.size() != 1) {
    std::cerr << "Usage: user del <login>\n";
    return 1;
  }
  try {
    store.remove(args[0]);
    std::cout << "User '" << args[0] << "' deleted.\n";
    return 0;
  } catch (const auth::AuthException& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}

int cmdUserPasswd(auth::UserStore& store, const config::AuthConfig& authCfg, const std::vector<std::string>& args) {
  if (args.empty()) {
    std::cerr << "Usage: user passwd <login> [--password-stdin]\n";
    return 1;
  }
  const std::string& login = args[0];
  bool pwStdin = false;
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--password-stdin") {
      pwStdin = true;
    } else {
      std::cerr << "Unknown option: " << args[i] << "\n";
      return 1;
    }
  }

  std::optional<std::string> pw;
  if (pwStdin) {
    pw = readPasswordFromStdin();
  } else {
    pw = promptPassword(/*confirm=*/true);
  }
  if (!pw) {
    return 2;
  }

  try {
    auto rec = auth::hashPassword(*pw, authCfg.pbkdf2Iterations);
    store.setPassword(login, rec);
    std::cout << "Password updated for '" << login << "'.\n";
    return 0;
  } catch (const auth::AuthException& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}

int cmdUserEnable(auth::UserStore& store, const std::vector<std::string>& args, bool enabled) {
  if (args.size() != 1) {
    std::cerr << "Usage: user " << (enabled ? "enable" : "disable") << " <login>\n";
    return 1;
  }
  try {
    store.setEnabled(args[0], enabled);
    std::cout << "User '" << args[0] << "' " << (enabled ? "enabled" : "disabled") << ".\n";
    return 0;
  } catch (const auth::AuthException& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}

int cmdUserSetAdmin(auth::UserStore& store, const std::vector<std::string>& args, bool isAdmin) {
  if (args.size() != 1) {
    std::cerr << "Usage: user " << (isAdmin ? "promote" : "demote") << " <login>\n";
    return 1;
  }
  try {
    store.setAdmin(args[0], isAdmin);
    std::cout << "User '" << args[0] << "' " << (isAdmin ? "promoted to admin" : "demoted from admin") << ".\n";
    return 0;
  } catch (const auth::AuthException& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}

int cmdUserList(auth::UserStore& store, const std::vector<std::string>& args) {
  std::optional<db::Pagination> page;
  db::Pagination p;
  bool hasPagination = false;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--offset" && i + 1 < args.size()) {
      p.offset = static_cast<uint32_t>(std::stoul(args[++i]));
      hasPagination = true;
    } else if (args[i] == "--limit" && i + 1 < args.size()) {
      p.limit = static_cast<uint32_t>(std::stoul(args[++i]));
      hasPagination = true;
    } else {
      std::cerr << "Unknown option: " << args[i] << "\n";
      return 1;
    }
  }
  if (hasPagination) {
    page = p;
  }

  try {
    auto users = store.list(page);
    if (users.empty()) {
      std::cout << "(no users)\n";
    } else {
      printUserTable(users);
    }
    return 0;
  } catch (const auth::AuthException& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage(argv[0]);
    return 1;
  }

  const std::string configPath = argv[1];
  const std::string command = argv[2];

  // Load config
  config::AppConfig cfg;
  try {
    cfg = config::loadConfig(configPath);
  } catch (const std::exception& e) {
    std::cerr << "Config error: " << e.what() << "\n";
    return 2;
  }

  // Only "user" command group is supported
  if (command != "user") {
    std::cerr << "Unknown command: " << command << "\n";
    printUsage(argv[0]);
    return 1;
  }

  if (argc < 4) {
    std::cerr << "Missing subcommand for 'user'\n";
    printUsage(argv[0]);
    return 1;
  }

  const std::string sub = argv[3];

  // Collect remaining args (argv[4..])
  std::vector<std::string> rest;
  rest.reserve(static_cast<size_t>(argc - 4));
  for (int i = 4; i < argc; ++i) {
    rest.emplace_back(argv[i]);
  }

  // Open UserStore
  auth::UserStore store(cfg.auth.database);

  try {
    if (sub == "add") {
      return cmdUserAdd(store, cfg.auth, rest);
    }
    if (sub == "del") {
      return cmdUserDel(store, rest);
    }
    if (sub == "passwd") {
      return cmdUserPasswd(store, cfg.auth, rest);
    }
    if (sub == "enable") {
      return cmdUserEnable(store, rest, true);
    }
    if (sub == "disable") {
      return cmdUserEnable(store, rest, false);
    }
    if (sub == "promote") {
      return cmdUserSetAdmin(store, rest, true);
    }
    if (sub == "demote") {
      return cmdUserSetAdmin(store, rest, false);
    }
    if (sub == "list") {
      return cmdUserList(store, rest);
    }
    std::cerr << "Unknown subcommand: " << sub << "\n";
    printUsage(argv[0]);
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 2;
  }
}

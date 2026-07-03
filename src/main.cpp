#include <iostream>
#include <string>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <fcntl.h>
#include <set>
#include <algorithm>
#include <unordered_map>
#ifdef _WIN32
#include <process.h>
#include <io.h>
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>
#endif
namespace fs = std::filesystem;
std::unordered_map<std::string, std::string> completions;

//Struct that contains redirect file info
struct RedirectInfo {
    std::string stdout_file = "";
    std::string stderr_file = "";
    bool stdout_append = false;
    bool stderr_append = false;
};

// Parse a command line into arguments, respecting single quotes.
//   - Outside quotes: whitespace delimits tokens; multiple spaces collapse.
//   - Inside single quotes: ALL characters are literal (spaces, $, \, etc.).
//   - Adjacent quoted/unquoted segments are concatenated into one argument.
//   - Empty quotes ('') contribute an empty-string argument.
std::vector<std::string> parse_args(const std::string& input) {
    std::vector<std::string> args;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;
    bool has_token = false; // tracks if we've started building an arg (including from empty quotes)

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (escaped) {
            current += c;
            escaped = false;
            has_token = true;
        } else if (c == '\\' && !in_single_quote) {
            escaped = true;
            has_token = true;
        } else if (c == '\'' && !in_double_quote) {
            // Toggling single-quote mode marks the start of a token
            has_token = true;
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            has_token = true;
            in_double_quote = !in_double_quote;
        } else if ((c == ' ' || c == '\t') && !in_single_quote && !in_double_quote) {
            if (has_token) {
                args.push_back(current);
                current.clear();
                has_token = false;
            }
        } else {
            current += c;
            has_token = true;
        }
    }
    if (has_token) args.push_back(current);
    return args;
}

// Helper: find an executable in PATH
std::string get_executable_path(const std::string& cmd) {
  const char* pathEnv = std::getenv("PATH");
  if(pathEnv){
    std::string pathStr(pathEnv);
    std::stringstream ss(pathStr);
    std::string dir;
#ifdef _WIN32
    char delimiter = ';';
#else
    char delimiter = ':';
#endif
    while(std::getline(ss, dir, delimiter)){
      fs::path fullPath = fs::path(dir) / cmd;
#ifdef _WIN32
      if (!fs::exists(fullPath)) {
        if (fs::exists(fullPath.string() + ".exe")) fullPath = fullPath.string() + ".exe";
        else if (fs::exists(fullPath.string() + ".cmd")) fullPath = fullPath.string() + ".cmd";
        else if (fs::exists(fullPath.string() + ".bat")) fullPath = fullPath.string() + ".bat";
      }
#endif
      if (fs::exists(fullPath) && !fs::is_directory(fullPath)){
#ifndef _WIN32
        fs::perms p = fs::status(fullPath).permissions();
        bool canExecute = ((p & fs::perms::owner_exec) != fs::perms::none)
                       || ((p & fs::perms::group_exec) != fs::perms::none)
                       || ((p & fs::perms::others_exec) != fs::perms::none);
        if (canExecute) return fullPath.string();
#else
        return fullPath.string();
#endif
      }
    }
  }
  return "";
}

// Extract stdout or stderr redirection info
RedirectInfo extract_redirects(std::vector<std::string>& args) {
  RedirectInfo info;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == ">" || args[i] == "1>") {
      if (i + 1 < args.size()) {
        info.stdout_file = args[i + 1];
        args.erase(args.begin() + i, args.begin() + i + 2);
        return info;
      }
    } else if (args[i] == ">>" || args[i] == "1>>") {
      if (i + 1 < args.size()) {
        info.stdout_file = args[i + 1];
        info.stdout_append = true;
        args.erase(args.begin() + i, args.begin() + i + 2);
        return info;
      }
    } else if (args[i] == "2>") {
      if (i + 1 < args.size()) {
        info.stderr_file = args[i + 1];
        args.erase(args.begin() + i, args.begin() + i + 2);
        return info;
      }
    } else if (args[i] == "2>>") {
      if (i + 1 < args.size()) {
        info.stderr_file = args[i + 1];
        info.stderr_append = true;
        args.erase(args.begin() + i, args.begin() + i + 2);
        return info;
      }
    }
  }
  return info;
}

// Build the set of all completable names: builtins + executables found in PATH
std::set<std::string> build_completions() {
    std::set<std::string> completions = {"echo", "exit"};

    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return completions;

    std::string pathStr(pathEnv);
    std::stringstream ss(pathStr);
    std::string dir;
#ifdef _WIN32
    char delimiter = ';';
#else
    char delimiter = ':';
#endif
    while (std::getline(ss, dir, delimiter)) {
        if (dir.empty()) continue;
        fs::path dirPath(dir);
        if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) continue;
        try {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                if (!entry.is_regular_file()) continue;
#ifndef _WIN32
                fs::perms p = entry.status().permissions();
                bool canExecute = ((p & fs::perms::owner_exec) != fs::perms::none)
                               || ((p & fs::perms::group_exec) != fs::perms::none)
                               || ((p & fs::perms::others_exec) != fs::perms::none);
                if (!canExecute) continue;
#endif
                completions.insert(entry.path().filename().string());
            }
        } catch (...) {
            // Skip directories we can't read
        }
    }
    return completions;
}

std::string longest_common_prefix(const std::vector<std::string>& matches) {
    if (matches.empty()) return "";

    std::string prefix = matches[0];

    for (size_t i = 1; i < matches.size(); ++i) {
        size_t j = 0;

        while (j < prefix.size() && j < matches[i].size() && prefix[j] == matches[i][j]) {
            ++j;
        }

        prefix = prefix.substr(0, j);

        if (prefix.empty())
            break;
    }

    return prefix;
}

std::vector<std::string> run_completer(const std::string& script_path, const std::string& command, const std::string& current_word, const std::string& previous_word, const std::string& buffer) {
  std::vector<std::string> matches;
  std::string cmd = script_path + " \"" + command + "\" \"" + current_word + "\" \"" + previous_word + "\"";
  std::string point = std::to_string(buffer.length());
#ifdef _WIN32
    _putenv_s("COMP_LINE", buffer.c_str());
    _putenv_s("COMP_POINT", point.c_str());
#else
    setenv("COMP_LINE", buffer.c_str(), 1);
    setenv("COMP_POINT", point.c_str(), 1);
#endif

    FILE* pipe = popen(cmd.c_str(), "r");

    if (!pipe) return {};

    char buf[256];

    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
          line.pop_back();
        }

        if (!line.empty()) matches.push_back(line);
    }

    pclose(pipe);

    return matches;
}

std::string read_command_with_autocomplete() {
    std::string buffer;
    bool previous_tab = false;
#ifdef _WIN32
    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            std::cout << '\n';
            break;
        } else if (ch == '\b' || ch == 8) { // Backspace
            if (!buffer.empty()) {
                buffer.pop_back();
                std::cout << "\b \b";
            }
        } else if (ch == '\t') {
            std::vector<std::string> matches;
            size_t last_space = buffer.find_last_of(' ');
            size_t last_slash = buffer.find_last_of('/');
            std::string path = "";
            std::string search_term;
            std::vector<std::string> words = parse_args(buffer);
            std::string command = words.empty() ? "" : words[0];
            std::string current_word = "";
            std::string previous_word = "";
            if (!buffer.empty() && buffer.back() == ' ') {
                if (!words.empty()) previous_word = words.back();
            } else {
                if (!words.empty()) current_word = words.back();
                if (words.size() >= 2) previous_word = words[words.size()-2];
            }

            if (last_space != std::string::npos) {
                if (completions.contains(command)) {
                  matches = run_completer(completions[command], command, current_word, previous_word, buffer);
                  search_term = current_word;
                } else if (last_slash != std::string::npos && last_slash > last_space) {
                  search_term = buffer.substr(last_slash + 1);
                  path = buffer.substr(last_space + 1, last_slash - last_space - 1);
                } else {
                  search_term = buffer.substr(last_space + 1);
                  path = fs::current_path().string();
                }
                try {
                  for (const auto& entry : fs::directory_iterator(path)) {
                    std::string filename = entry.path().filename().string();
                    if (search_term.length() <= filename.length() && filename.compare(0, search_term.length(), search_term) == 0) {
                        if (entry.is_directory()) matches.push_back(filename + "/");
                        else matches.push_back(filename);
                    }
                  } 
                } catch (...) {}
            } else {
                search_term = buffer;
                std::set<std::string> autocompletions = build_completions();
                for (const auto& b : autocompletions) {
                    if (search_term.length() <= b.length() && b.compare(0, search_term.length(), search_term) == 0) {
                        matches.push_back(b);
                    }
                }
            }
            if (matches.empty()) {
                std::cout << '\a';
            } else if (matches.size() == 1) {
              std::string match = matches[0];
              std::string remaining = match.substr(search_term.length());
              if (match.back() == '/') {
                  buffer += remaining;
                  std::cout << remaining;
              } else {
                  buffer += remaining + " ";
                  std::cout << remaining << " ";
              }
            } else if (matches.size() > 1) {
              std::string lcp = longest_common_prefix(matches);

              if (lcp.size() > search_term.size()) {
                std::string remaining = lcp.substr(search_term.size());

                buffer += remaining;
                std::cout << remaining;

                previous_tab = false;
              } else {
                if (!previous_tab) {
                  std::cout << '\a';
                  previous_tab = true;
                } else {
                  std::sort(matches.begin(), matches.end());
                  std::cout << '\n';
                  for (size_t i = 0; i < matches.size(); ++i) {
                      if (i > 0) 
                          std::cout << "  ";
                      std::cout << matches[i];
                  }
                  std::cout << "\n$ " << buffer;
                  previous_tab = false;
                }
              }
            }
        } else if (ch >= 32 && ch <= 126) {
            buffer += (char)ch;
            std::cout << (char)ch;
        }
    }
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    char ch;
    while (read(STDIN_FILENO, &ch, 1) == 1) {
        if (ch == '\n' || ch == '\r') {
            std::cout << '\n';
            break;
        } else if (ch == 127 || ch == '\b') { // Backspace
            if (!buffer.empty()) {
                buffer.pop_back();
                std::cout << "\b \b";
            }
        } else if (ch == '\t') {
            std::vector<std::string> matches;
            size_t last_space = buffer.find_last_of(' ');
            size_t last_slash = buffer.find_last_of('/');
            std::string path = "";
            std::string search_term;
            std::vector<std::string> words = parse_args(buffer);
            std::string command = words.empty() ? "" : words[0];
            std::string current_word = "";
            std::string previous_word = "";
            if (!buffer.empty() && buffer.back() == ' ') {
                if (!words.empty()) previous_word = words.back();
            } else {
                if (!words.empty()) current_word = words.back();
                if (words.size() >= 2) previous_word = words[words.size()-2];
            }

            if (last_space != std::string::npos) {
                std::string first_word = buffer.substr(0, last_space);
                if (completions.contains(command)) {
                  matches = run_completer(completions[command], command, current_word, previous_word, buffer);
                  search_term = current_word;
                } else if (last_slash != std::string::npos && last_slash > last_space) {
                  search_term = buffer.substr(last_slash + 1);
                  path = buffer.substr(last_space + 1, last_slash - last_space - 1);
                } else {
                  search_term = buffer.substr(last_space + 1);
                  path = fs::current_path().string();
                }
                try {
                  for (const auto& entry : fs::directory_iterator(path)) {
                    std::string filename = entry.path().filename().string();
                    if (search_term.length() <= filename.length() && filename.compare(0, search_term.length(), search_term) == 0) {
                        if (entry.is_directory()) matches.push_back(filename + "/");
                        else matches.push_back(filename);
                    }
                  } 
                } catch (...) {}
            } else {
                search_term = buffer;
                std::set<std::string> autocompletions = build_completions();
                for (const auto& b : autocompletions) {
                    if (search_term.length() <= b.length() && b.compare(0, search_term.length(), search_term) == 0) {
                        matches.push_back(b);
                    }
                }
            }
            if (matches.empty()) {
                std::cout << '\a';
            } else if (matches.size() == 1) {
              std::string match = matches[0];
              std::string remaining = match.substr(search_term.length());

              if (match.back() == '/') {
                  buffer += remaining;
                  std::cout << remaining;
              } else {
                  buffer += remaining + " ";
                  std::cout << remaining << " ";
              }
            } else if (matches.size() > 1) {
              std::string lcp = longest_common_prefix(matches);

              if (lcp.size() > search_term.size()) {
                std::string remaining = lcp.substr(search_term.size());

                buffer += remaining;
                std::cout << remaining;

                previous_tab = false;
              } else {
                if (!previous_tab) {
                  std::cout << '\a';
                  previous_tab = true;
                } else {
                  std::sort(matches.begin(), matches.end());
                  std::cout << '\n';
                  for (size_t i = 0; i < matches.size(); ++i) {
                      if (i > 0)
                          std::cout << "  ";
                      std::cout << matches[i];
                  }
                  std::cout << "\n$ " << buffer;
                  previous_tab = false;
                }
              }
            }
        } else if (ch >= 32 && ch <= 126) {
            buffer += ch;
            std::cout << ch;
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
    return buffer;
}

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while(true){
    std::cout << "$ ";
    std::string command = read_command_with_autocomplete();

    // Parse the full command line respecting single/double quotes
    std::vector<std::string> args = parse_args(command);
    if (args.empty()) continue;

    // Extract stdout and stderr redirections before dispatching
    RedirectInfo r = extract_redirects(args);
    std::string redirect_stdout   = r.stdout_file;
    std::string redirect_stderr = r.stderr_file;
    bool is_stdout_append = r.stdout_append;
    bool is_stderr_append = r.stderr_append;
    if (args.empty()) continue;

    const std::string& cmd = args[0];

    // exit
    if (cmd == "exit") break;

    // --- For built-in commands: swap rdbuf for stdout and/or stderr ---
    std::ofstream out_file;
    std::ofstream err_file;
    std::streambuf* old_cout_rdbuf = nullptr;
    std::streambuf* old_cerr_rdbuf = nullptr;

    if (!redirect_stdout.empty()) {
      if (is_stdout_append) out_file.open(redirect_stdout, std::ios::out | std::ios::app);
      else out_file.open(redirect_stdout, std::ios::out | std::ios::trunc);
      if (out_file.is_open())
        old_cout_rdbuf = std::cout.rdbuf(out_file.rdbuf());
    }
    if (!redirect_stderr.empty()) {
      if (is_stderr_append) err_file.open(redirect_stderr, std::ios::out | std::ios::app);
      else err_file.open(redirect_stderr, std::ios::out | std::ios::trunc);
      if (err_file.is_open())
        old_cerr_rdbuf = std::cerr.rdbuf(err_file.rdbuf());
    }

    auto restore_streams = [&]() {
      if (old_cout_rdbuf) { std::cout.rdbuf(old_cout_rdbuf); old_cout_rdbuf = nullptr; }
      if (old_cerr_rdbuf) { std::cerr.rdbuf(old_cerr_rdbuf); old_cerr_rdbuf = nullptr; }
      if (out_file.is_open()) out_file.close();
      if (err_file.is_open()) err_file.close();
    };

    // pwd
    if (cmd == "pwd"){
      std::cout << fs::current_path().string() << std::endl;
      restore_streams();
      continue;
    }

    // echo — args already unquoted; print joined with single spaces
    if (cmd == "echo"){
      for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) std::cout << ' ';
        std::cout << args[i];
      }
      std::cout << std::endl;
      restore_streams();
      continue;
    }

    // cd
    if (cmd == "cd"){
      restore_streams();
      std::string absPath = (args.size() > 1) ? args[1] : "~";
      if (absPath == "~"){
        const char* homeDir = std::getenv("HOME");
        if (homeDir) fs::current_path(homeDir);
      } else if (fs::exists(absPath)){
        fs::current_path(absPath);
      } else {
        std::cout << "cd: " << absPath << ": No such file or directory" << std::endl;
      }
      continue;
    }

    // type
    if (cmd == "type") {
      std::string arg = (args.size() > 1) ? args[1] : "";
      if (arg == "type" || arg == "echo" || arg == "exit" || arg == "pwd" || arg == "complete"){
        std::cout << arg << " is a shell builtin" << std::endl;
        restore_streams();
        continue;
      }
      std::string ep = get_executable_path(arg);
      if (!ep.empty()) std::cout << arg << " is " << ep << std::endl;
      else std::cout << arg << ": not found" << std::endl;
      restore_streams();
      continue;
    }

    // complete
    if (cmd == "complete") {
      std::string arg = (args.size() > 2) ? args[1] : "";
      if (!arg.empty() && arg.front() == '-'){
        if (arg == "-p"){
          if (completions.contains(args[2])) {
            std::cout << "complete -C '" << completions[args[2]] << "' " << args[2] << std::endl;
            restore_streams();
            continue;
          } else {
            std::cout << "complete: " << args[2] << ": no completion specification" << std::endl;
            restore_streams();
            continue;
          }
        } else if (arg == "-C") {
          completions[args[3]] = args[2];
          restore_streams();
          continue;
        } else if (arg == "-r") {
          completions.erase(args[2]);
          restore_streams();
          continue;
        }
      }
    }

    // Restore streams before forking — child handles its own redirection
    restore_streams();

    // External program — args are already properly split and unquoted
    std::string exec_path = get_executable_path(cmd);
    if (!exec_path.empty()) {
      std::vector<char*> argv;
      for (auto& s : args)
        argv.push_back(const_cast<char*>(s.c_str()));
      argv.push_back(nullptr);
#ifdef _WIN32
      // Save and redirect stdout if needed
      int old_stdout = -1, old_stderr = -1;
      HANDLE hOldStdout = INVALID_HANDLE_VALUE, hOldStderr = INVALID_HANDLE_VALUE;
      
      if (!redirect_stdout.empty()) {
        old_stdout = _dup(1);
        hOldStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        int fd;
        if (is_stdout_append) {
            fd = _open(redirect_stdout.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND, 0644);
            if (fd != -1) _lseek(fd, 0, SEEK_END);
        } else {
            fd = _open(redirect_stdout.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC, 0644);
        }
        if (fd != -1) { 
            _dup2(fd, 1); 
            SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(1));
            _close(fd); 
        }
      }
      // Save and redirect stderr if needed
      if (!redirect_stderr.empty()) {
        old_stderr = _dup(2);
        hOldStderr = GetStdHandle(STD_ERROR_HANDLE);
        int fd;
        if (is_stderr_append) {
            fd = _open(redirect_stderr.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND, 0644);
            if (fd != -1) _lseek(fd, 0, SEEK_END);
        } else {
            fd = _open(redirect_stderr.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC, 0644);
        }
        if (fd != -1) { 
            _dup2(fd, 2); 
            SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(2));
            _close(fd); 
        }
      }
      _spawnv(_P_WAIT, exec_path.c_str(), argv.data());
      // Restore stdout/stderr
      if (old_stdout != -1) { 
          _dup2(old_stdout, 1); 
          SetStdHandle(STD_OUTPUT_HANDLE, hOldStdout);
          _close(old_stdout); 
      }
      if (old_stderr != -1) { 
          _dup2(old_stderr, 2); 
          SetStdHandle(STD_ERROR_HANDLE, hOldStderr);
          _close(old_stderr); 
      }
#else
      pid_t pid = fork();
      if (pid == 0) {
        if (!redirect_stdout.empty()) {
          int fd;
          if (is_stdout_append) fd = open(redirect_stdout.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
          else fd = open(redirect_stdout.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fd != -1) { dup2(fd, STDOUT_FILENO); close(fd); }
        } 
        if (!redirect_stderr.empty()) {
          int fd;
          if (is_stderr_append) fd = open(redirect_stderr.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
          else fd = open(redirect_stderr.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fd != -1) { dup2(fd, STDERR_FILENO); close(fd); }
        }
        execvp(exec_path.c_str(), argv.data());
        exit(1);
      } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
      }
#endif
      continue;
    }

    std::cout << command << ": command not found" << std::endl;
  }
  return 0;
}
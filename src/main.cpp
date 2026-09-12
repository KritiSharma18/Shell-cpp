#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <climits>

std::vector<std::string> getPathDirs() {
  std::vector<std::string> dirs;
  const char* pathEnv = std::getenv("PATH");
  if (!pathEnv) return dirs;
  std::string path(pathEnv);
  std::stringstream ss(path);
  std::string dir;
#ifdef _WIN32
  char delimiter = ';';
#else
  char delimiter = ':';
#endif
  while (std::getline(ss, dir, delimiter)) dirs.push_back(dir);
  return dirs;
}

std::string findExecutable(const std::string& cmd) {
  for (const auto& dir : getPathDirs()) {
    std::string fullPath = dir + "/" + cmd;
    if (access(fullPath.c_str(), X_OK) == 0) return fullPath;
  }
  return "";
}

std::vector<std::string> tokenize(const std::string& input) {
  std::vector<std::string> tokens;
  std::stringstream ss(input);
  std::string token;
  while (ss >> token) {
    if (token.size() >= 2) {
      char first = token.front();
      char last = token.back();
      if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
        token = token.substr(1, token.size() - 2);
      }
    }
    tokens.push_back(token);
  }
  return tokens;
}

struct Redirects {
  std::string stdoutFile;
  bool stdoutAppend = false;
  std::string stderrFile;
  bool stderrAppend = false;
};

Redirects extractRedirects(std::vector<std::string>& tokens) {
  Redirects r;
  for (size_t i = 0; i < tokens.size(); ) {
    if ((tokens[i] == ">" || tokens[i] == "1>") && i + 1 < tokens.size()) {
      r.stdoutFile = tokens[i + 1]; r.stdoutAppend = false;
      tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
    } else if ((tokens[i] == ">>" || tokens[i] == "1>>") && i + 1 < tokens.size()) {
      r.stdoutFile = tokens[i + 1]; r.stdoutAppend = true;
      tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
    } else if (tokens[i] == "2>" && i + 1 < tokens.size()) {
      r.stderrFile = tokens[i + 1]; r.stderrAppend = false;
      tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
    } else if (tokens[i] == "2>>" && i + 1 < tokens.size()) {
      r.stderrFile = tokens[i + 1]; r.stderrAppend = true;
      tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
    } else {
      i++;
    }
  }
  return r;
}

void applyRedirects(const Redirects& redirects) {
  if (!redirects.stdoutFile.empty()) {
    int flags = O_WRONLY | O_CREAT | (redirects.stdoutAppend ? O_APPEND : O_TRUNC);
    int fd = open(redirects.stdoutFile.c_str(), flags, 0644);
    if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
  }
  if (!redirects.stderrFile.empty()) {
    int flags = O_WRONLY | O_CREAT | (redirects.stderrAppend ? O_APPEND : O_TRUNC);
    int fd = open(redirects.stderrFile.c_str(), flags, 0644);
    if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); }
  }
}

std::set<std::string> builtins = {"echo", "exit", "type", "pwd", "cd"};

void runBuiltin(const std::vector<std::string>& tokens) {
  const std::string& cmd = tokens[0];
  if (cmd == "pwd") {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
      std::cout << cwd << std::endl;
    }
  } else if (cmd == "echo") {
    std::string output;
    for (size_t i = 1; i < tokens.size(); i++) {
      if (i > 1) output += " ";
      output += tokens[i];
    }
    std::cout << output << std::endl;
  } else if (cmd == "type") {
    std::string target = tokens.size() > 1 ? tokens[1] : "";
    if (builtins.count(target)) {
      std::cout << target << " is a shell builtin" << std::endl;
    } else {
      std::string fullPath = findExecutable(target);
      if (!fullPath.empty()) {
        std::cout << target << " is " << fullPath << std::endl;
      } else {
        std::cout << target << ": not found" << std::endl;
      }
    }
  }
}

void execStage(const std::vector<std::string>& tokens, int inFd, int outFd) {
  if (inFd != STDIN_FILENO) { dup2(inFd, STDIN_FILENO); close(inFd); }
  if (outFd != STDOUT_FILENO) { dup2(outFd, STDOUT_FILENO); close(outFd); }

  if (builtins.count(tokens[0]) && tokens[0] != "exit") {
    runBuiltin(tokens);
    exit(0);
  }

  std::string fullPath = findExecutable(tokens[0]);
  if (fullPath.empty()) {
    std::cerr << tokens[0] << ": command not found" << std::endl;
    exit(1);
  }
  std::vector<char*> argv;
  for (const auto& arg : tokens) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  execv(fullPath.c_str(), argv.data());
  std::cerr << tokens[0] << ": exec failed" << std::endl;
  exit(1);
}

void executeExternal(const std::string& fullPath, const std::vector<std::string>& args,
                      const Redirects& redirects) {
  pid_t pid = fork();
  if (pid == 0) {
    applyRedirects(redirects);
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    execv(fullPath.c_str(), argv.data());
    std::cerr << args[0] << ": exec failed" << std::endl;
    exit(1);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    std::cerr << "fork failed" << std::endl;
  }
}

void writeToFile(const std::string& filename, bool append, const std::string& content) {
  std::ofstream out(filename, append ? std::ios::app : std::ios::trunc);
  out << content << std::endl;
}

void touchFile(const std::string& filename, bool append) {
  std::ofstream out(filename, append ? std::ios::app : std::ios::trunc);
}

std::vector<std::string> splitPipeline(const std::string& input) {
  std::vector<std::string> stages;
  std::stringstream ss(input);
  std::string stage;
  while (std::getline(ss, stage, '|')) {
    stages.push_back(stage);
  }
  return stages;
}

// Runs a pipeline of N stages, connecting each stage's stdout to the next stage's stdin
void runPipeline(const std::vector<std::vector<std::string>>& stages) {
  int numStages = stages.size();
  std::vector<int> pipefds((numStages - 1) * 2);

  for (int i = 0; i < numStages - 1; i++) {
    if (pipe(&pipefds[i * 2]) < 0) {
      std::cerr << "pipe failed" << std::endl;
      return;
    }
  }

  std::vector<pid_t> pids;

  for (int i = 0; i < numStages; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      int inFd = (i == 0) ? STDIN_FILENO : pipefds[(i - 1) * 2];
      int outFd = (i == numStages - 1) ? STDOUT_FILENO : pipefds[i * 2 + 1];

      for (int fd : pipefds) {
        if (fd != inFd && fd != outFd) close(fd);
      }

      execStage(stages[i], inFd, outFd);
    } else if (pid > 0) {
      pids.push_back(pid);
    } else {
      std::cerr << "fork failed" << std::endl;
    }
  }

  for (int fd : pipefds) close(fd);
  for (pid_t pid : pids) {
    int status;
    waitpid(pid, &status, 0);
  }
}

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true) {
    std::cout << "$ ";
    std::string input;
    std::getline(std::cin, input);
    if (input.empty() && std::cin.eof()) break;

    if (input.find('|') != std::string::npos) {
      std::vector<std::string> stageStrings = splitPipeline(input);
      std::vector<std::vector<std::string>> stages;
      bool valid = true;
      for (const auto& s : stageStrings) {
        std::vector<std::string> tokens = tokenize(s);
        if (tokens.empty()) { valid = false; break; }
        stages.push_back(tokens);
      }
      if (valid && stages.size() >= 2) {
        runPipeline(stages);
      }
      continue;
    }

    std::vector<std::string> tokens = tokenize(input);
    if (tokens.empty()) continue;

    Redirects redirects = extractRedirects(tokens);
    std::string cmd = tokens[0];

    if (cmd == "exit") {
      break;
    } else if (cmd == "pwd" || cmd == "echo") {
      std::ostringstream captured;
      std::streambuf* oldBuf = std::cout.rdbuf(captured.rdbuf());
      runBuiltin(tokens);
      std::cout.rdbuf(oldBuf);
      std::string output = captured.str();
      if (!output.empty() && output.back() == '\n') output.pop_back();

      if (!redirects.stdoutFile.empty()) {
        writeToFile(redirects.stdoutFile, redirects.stdoutAppend, output);
      } else {
        std::cout << output << std::endl;
      }
      if (!redirects.stderrFile.empty()) {
        touchFile(redirects.stderrFile, redirects.stderrAppend);
      }
        } else if (cmd == "type") {
      runBuiltin(tokens);
        } else if (cmd == "cd") {
      if (tokens.size() < 2) {
        std::cerr << "cd: missing argument" << std::endl;
      } else {
        std::string dir = tokens[1];
        if (dir == "~") {
          const char* home = std::getenv("HOME");
          if (home != nullptr) {
            dir = home;
          }
        }
        if (chdir(dir.c_str()) != 0) {
          std::cout << "cd: " << tokens[1] << ": No such file or directory" << std::endl;
        }
      }
    } else {
      std::string fullPath = findExecutable(cmd);
      if (!fullPath.empty()) {
        executeExternal(fullPath, tokens, redirects);
      } else {
        std::cout << cmd << ": command not found" << std::endl;
      }
    }
  }
}
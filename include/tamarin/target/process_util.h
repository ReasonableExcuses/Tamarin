// ============================================================================
// process_util.h - Safe, Shell-Free Process Spawning Utilities
// ============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cctype>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
#include <errno.h>
#endif

namespace tamarin::target {

/// Parse a command-line string into distinct argument tokens respecting quotes.
inline std::vector<std::string> splitCommandLine(std::string_view cmd) {
    std::vector<std::string> args;
    std::string current;
    bool inQuotes = false;
    char quoteChar = 0;

    for (size_t i = 0; i < cmd.size(); ++i) {
        char c = cmd[i];
        if (inQuotes) {
            if (c == quoteChar) {
                inQuotes = false;
            } else if (c == '\\' && i + 1 < cmd.size() && (cmd[i + 1] == quoteChar || cmd[i + 1] == '\\')) {
                current += cmd[++i];
            } else {
                current += c;
            }
        } else {
            if (c == '"' || c == '\'') {
                inQuotes = true;
                quoteChar = c;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!current.empty()) {
                    args.push_back(std::move(current));
                    current.clear();
                }
            } else {
                current += c;
            }
        }
    }
    if (!current.empty()) {
        args.push_back(std::move(current));
    }
    return args;
}

#ifdef _WIN32

inline std::string buildWindowsCommandLine(const std::vector<std::string>& args) {
    std::string cmdLine;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmdLine += " ";
        const std::string& arg = args[i];
        bool needQuote = arg.empty() || arg.find_first_of(" \t\n\v\"") != std::string::npos;
        if (!needQuote) {
            cmdLine += arg;
        } else {
            cmdLine += '"';
            for (size_t j = 0; j < arg.size(); ++j) {
                if (arg[j] == '"') {
                    cmdLine += "\\\"";
                } else if (arg[j] == '\\') {
                    size_t k = j;
                    while (k < arg.size() && arg[k] == '\\') ++k;
                    size_t backslashes = k - j;
                    if (k == arg.size() || arg[k] == '"') {
                        cmdLine.append(backslashes * 2, '\\');
                    } else {
                        cmdLine.append(backslashes, '\\');
                    }
                    j = k - 1;
                } else {
                    cmdLine += arg[j];
                }
            }
            cmdLine += '"';
        }
    }
    return cmdLine;
}

inline int runProcess(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::string cmdLine = buildWindowsCommandLine(args);

    if (!CreateProcessA(NULL, cmdLine.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

inline std::string runProcessWithCapture(const std::vector<std::string>& args, int& exitCode) {
    if (args.empty()) { exitCode = -1; return ""; }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        exitCode = -1;
        return "failed to create pipe";
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    PROCESS_INFORMATION pi = {};

    std::string cmdLine = buildWindowsCommandLine(args);
    if (!CreateProcessA(NULL, cmdLine.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        exitCode = -1;
        return "failed to launch process";
    }
    CloseHandle(hWrite);

    std::string output;
    char buf[1024];
    DWORD bytesRead;
    while (ReadFile(hRead, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
        output.append(buf, bytesRead);
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    exitCode = static_cast<int>(code);
    return output;
}

#else

inline int runProcess(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

inline std::string runProcessWithCapture(const std::vector<std::string>& args, int& exitCode) {
    if (args.empty()) { exitCode = -1; return ""; }
    int pipefd[2];
    if (pipe(pipefd) < 0) { exitCode = -1; return "failed to create pipe"; }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        exitCode = -1;
        return "failed to fork";
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(pipefd[1]);

    std::string output;
    char buf[1024];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        output.append(buf, static_cast<size_t>(n));
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) { exitCode = -1; return output; }
    }
    if (WIFEXITED(status)) exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) exitCode = 128 + WTERMSIG(status);
    else exitCode = -1;
    return output;
}

#endif

inline int executeCommand(std::string_view cmdLine) {
    auto args = splitCommandLine(cmdLine);
    return runProcess(args);
}

inline std::string executeCommandWithCapture(std::string_view cmdLine, int& exitCode) {
    auto args = splitCommandLine(cmdLine);
    return runProcessWithCapture(args, exitCode);
}

} // namespace tamarin::target

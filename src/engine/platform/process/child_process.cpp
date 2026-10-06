#include "platform/process/child_process.h"

#include <chrono>

#include "platform/windows_api.h"

#if !defined(_WIN32)
    #include <cerrno>
    #include <csignal>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace Vkm::Engine {

namespace {

#if defined(_WIN32)

/**
 * @brief One argument quoted as CommandLineToArgvW reads it back.
 *
 * @param arg The argument.
 * @return It as it goes on a command line.
 */
std::wstring quoted(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        // Backslashes before a quote are doubled, and the quote escaped.
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

#endif

} // namespace

ChildProcess::~ChildProcess() {
    stop();
}

#if defined(_WIN32)

bool ChildProcess::start(
    const std::filesystem::path& program,
    const std::vector<std::string>& args,
    const std::filesystem::path& workingDir
) {
    stop();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output.clear();
        m_exitCode = -1;
    }

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
        m_output = "could not open a pipe to the program\n";
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nothing = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &inherit, OPEN_EXISTING, 0, nullptr);

    // A batch file runs through cmd.exe: /s strips the outer quotes and runs the rest as written.
    std::wstring line;
    const std::wstring ext = program.extension().wstring();
    const bool batch = _wcsicmp(ext.c_str(), L".cmd") == 0 || _wcsicmp(ext.c_str(), L".bat") == 0;
    std::wstring command = quoted(program.wstring());
    for (const std::string& arg : args) {
        command += L" " + quoted(std::filesystem::path(arg).wstring());
    }
    if (batch) {
        wchar_t comspec[MAX_PATH];
        const DWORD length = GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
        const std::wstring shell = length > 0 && length < MAX_PATH ? comspec : L"cmd.exe";
        line = quoted(shell) + L" /d /s /c \"" + command + L"\"";
    } else {
        line = command;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nothing;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;

    // Created suspended, so it is in the job before it can start anything of its own.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    PROCESS_INFORMATION info{};
    const std::wstring dir = workingDir.wstring();
    const BOOL started = CreateProcessW(
        nullptr,
        line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        dir.empty() ? nullptr : dir.c_str(),
        &startup,
        &info
    );
    CloseHandle(writeEnd);
    if (nothing != INVALID_HANDLE_VALUE) CloseHandle(nothing);
    if (!started) {
        CloseHandle(readEnd);
        CloseHandle(job);
        m_output = "could not start " + program.string() + "\n";
        return false;
    }
    AssignProcessToJobObject(job, info.hProcess);
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);

    m_process = info.hProcess;
    m_job = job;
    m_pipe = readEnd;
    m_running = true;
    m_reader = std::thread(&ChildProcess::readUntilExit, this);
    return true;
}

void ChildProcess::readUntilExit() {
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(static_cast<HANDLE>(m_pipe), buffer, sizeof(buffer), &got, nullptr) && got > 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output.append(buffer, got);
    }
    WaitForSingleObject(static_cast<HANDLE>(m_process), INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(m_process), &code);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_exitCode = static_cast<int>(code);
    m_running = false;
}

void ChildProcess::stop() {
    if (!m_reader.joinable()) return;
    const bool ended = running();
    if (ended) TerminateJobObject(static_cast<HANDLE>(m_job), 1);
    m_reader.join();
    if (ended) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exitCode = -1;
    }
    CloseHandle(static_cast<HANDLE>(m_pipe));
    CloseHandle(static_cast<HANDLE>(m_process));
    CloseHandle(static_cast<HANDLE>(m_job));
    m_pipe = m_process = m_job = nullptr;
}

#else

bool ChildProcess::start(
    const std::filesystem::path& program,
    const std::vector<std::string>& args,
    const std::filesystem::path& workingDir
) {
    stop();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_output.clear();
        m_exitCode = -1;
    }

    // Everything the child needs is made here: between fork and exec it may not allocate.
    const std::string path = program.string();
    const std::string dir = workingDir.string();
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(path.c_str()));
    for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        m_output = "could not open a pipe to the program\n";
        return false;
    }

    const pid_t pid = fork();
    if (pid == 0) {
        // A group of its own, so stop() reaches everything it starts.
        setpgid(0, 0);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        if (!dir.empty() && chdir(dir.c_str()) != 0) _exit(127);
        execv(path.c_str(), argv.data());
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        m_output = "could not start " + path + "\n";
        return false;
    }
    // Said by both sides: a stop() before the child runs must still find its group.
    setpgid(pid, pid);

    m_pid = pid;
    m_pipeFd = fds[0];
    m_running = true;
    m_reader = std::thread(&ChildProcess::readUntilExit, this);
    return true;
}

void ChildProcess::readUntilExit() {
    char buffer[4096];
    for (;;) {
        const ssize_t got = read(m_pipeFd, buffer, sizeof(buffer));
        if (got > 0) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_output.append(buffer, static_cast<size_t>(got));
        } else if (got == 0 || errno != EINTR) {
            break;
        }
    }
    int status = 0;
    while (waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {}

    std::lock_guard<std::mutex> lock(m_mutex);
    m_exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    m_running = false;
}

void ChildProcess::stop() {
    if (!m_reader.joinable()) return;
    const bool ended = running();
    if (ended) {
        // Asked first; whatever is still holding the pipe two seconds on is ended.
        kill(-m_pid, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (running() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (running()) kill(-m_pid, SIGKILL);
    }
    m_reader.join();
    if (ended) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_exitCode = -1;
    }
    close(m_pipeFd);
    m_pipeFd = -1;
    m_pid = 0;
}

#endif

std::string ChildProcess::takeOutput() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string out;
    out.swap(m_output);
    return out;
}

bool ChildProcess::running() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
}

int ChildProcess::exitCode() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_exitCode;
}

} // namespace Vkm::Engine

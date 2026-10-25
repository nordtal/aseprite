// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/app.h"
#include "app/script/engine.h"
#include "app/script/luacpp.h"
#include "app/script/security.h"
#include "ui/system.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include "base/string.h"
  #include <cctype>
  #include <windows.h>
#else
  #include <cerrno>
  #include <csignal>
  #include <fcntl.h>
  #include <poll.h>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace app { namespace script {

namespace {

class Process;

enum class Kind { Stdout, Stderr, Exit };

struct Event {
  Kind kind;
  std::string line;
  int code = 0;
};

// What is read from a pipe after the process ended: a full pipe
// buffer on Linux and macOS. A process it left behind could keep
// writing for ever.
constexpr std::size_t kDrainLimit = 64 * 1024;

void dispatch(Process* process, lua_State* L);
void dispatch_from_ui(Process* process);

// State shared between a Process, its background threads and the
// callbacks queued on the UI thread.
struct Shared {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Event> events;
  // The process was reaped, so its ID can belong to another process.
  // Guarded by the mutex.
  bool reaped = false;
  // Only touched from the script thread
  Process* owner = nullptr;

  static void push(const std::shared_ptr<Shared>& shared, Event&& ev)
  {
    {
      std::lock_guard lock(shared->mutex);
      shared->events.push_back(std::move(ev));
    }
    shared->cv.notify_all();

    // In the GUI the UI thread hands the events to Lua as they come,
    // in batch mode only Process:wait() does.
    if (App::instance() && App::instance()->isGui()) {
      std::weak_ptr<Shared> weak = shared;
      ui::execute_from_ui_thread([weak]() {
        if (auto s = weak.lock()) {
          if (s->owner)
            dispatch_from_ui(s->owner);
        }
      });
    }
  }
};

// Pushes the complete lines of the given output and keeps the rest.
void push_lines(const std::shared_ptr<Shared>& shared, const Kind kind, std::string& pending)
{
  std::size_t pos;
  while ((pos = pending.find('\n')) != std::string::npos) {
    std::string line = pending.substr(0, pos);
    pending.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    Shared::push(shared, { kind, std::move(line) });
  }
}

void push_rest(const std::shared_ptr<Shared>& shared, const Kind kind, std::string& pending)
{
  if (!pending.empty())
    Shared::push(shared, { kind, std::move(pending) });
  pending.clear();
}

// A child process started from an argument list, without a shell.
// Its output is read line by line in background threads, and each
// line and the exit code reach the Lua callbacks on the script thread.
class Process {
public:
  // The main Lua thread, where the callbacks run in the GUI. The
  // thread that created the object may be a coroutine that's gone.
  lua_State* L = nullptr;
  std::vector<std::string> cmd;
  std::string cwd;
  bool started = false;
  bool running = false;
  bool dispatching = false;
  int exitCode = 0;
  // Keeps the object alive while the process runs
  int runningRef = LUA_REFNIL;
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();

  Process() { shared->owner = this; }

  // Kills what is left of the process, so the app never waits for it
  ~Process()
  {
    shared->owner = nullptr;
    if (!started)
      return;
    stop(true);
    m_waiter.join();
#ifdef _WIN32
    CloseHandle(m_process);
    CloseHandle(m_job);
#else
    m_reader.join();
    std::lock_guard lock(shared->mutex);
    if (!shared->reaped) {
      while (waitpid(m_pid, nullptr, 0) < 0 && errno == EINTR) {}
      shared->reaped = true;
    }
#endif
  }

  // Ends the process and every process it started. That works after
  // the process ended too, for the ones it left behind.
  void stop(bool force)
  {
    if (!started)
      return;
#ifdef _WIN32
    (void)force;
    TerminateJobObject(m_job, 1);
#else
    std::lock_guard lock(shared->mutex);
    if (!shared->reaped)
      ::kill(-m_pid, force ? SIGKILL : SIGTERM);
#endif
  }

  // Returns an empty string if the process started, or the reason why
  // it didn't.
  std::string start()
  {
    std::string error = spawn();
    if (error.empty())
      started = running = true;
    return error;
  }

private:
#ifdef _WIN32

  HANDLE m_process = nullptr;
  // Holds the process and every process it starts, and kills them
  // when it's closed
  HANDLE m_job = nullptr;

  static std::wstring quote_arg(const std::wstring& arg, const bool always)
  {
    if (!always && !arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
      return arg;
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
      int backslashes = 0;
      while (it != arg.end() && *it == L'\\') {
        ++it;
        ++backslashes;
      }
      if (it == arg.end()) {
        out.append(backslashes * 2, L'\\');
        break;
      }
      if (*it == L'"')
        out.append(backslashes * 2 + 1, L'\\');
      else
        out.append(backslashes, L'\\');
      out.push_back(*it);
    }
    out.push_back(L'"');
    return out;
  }

  static bool is_batch_file(std::string program)
  {
    // Windows drops trailing dots and spaces from a file name, so
    // "run.bat. " runs run.bat
    while (!program.empty() && (program.back() == '.' || program.back() == ' '))
      program.pop_back();
    std::string ext = program.substr(program.size() < 4 ? 0 : program.size() - 4);
    for (char& c : ext)
      c = char(std::tolower((unsigned char)c));
    return (ext == ".bat" || ext == ".cmd");
  }

  std::string spawn()
  {
    // Windows runs a batch file through cmd.exe, which reads the
    // command line with rules of its own. An argument that cmd.exe
    // could take as a command is refused.
    const bool batch = is_batch_file(cmd[0]);
    if (batch) {
      for (const auto& arg : cmd) {
        if (arg.find_first_of("\"%!^&|<>()\r\n") != std::string::npos)
          return "cannot pass '" + arg + "' to the batch file '" + cmd[0] + "'";
      }
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE outRead, outWrite, errRead, errWrite;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0))
      return "cannot create a pipe";
    if (!CreatePipe(&errRead, &errWrite, &sa, 0)) {
      CloseHandle(outRead);
      CloseHandle(outWrite);
      return "cannot create a pipe";
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL",
                             GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa,
                             OPEN_EXISTING,
                             0,
                             nullptr);

    std::wstring cmdline;
    for (const auto& arg : cmd) {
      if (!cmdline.empty())
        cmdline.push_back(L' ');
      // Every argument of a batch file is quoted, so cmd.exe doesn't
      // split it at a comma, a semicolon or an equals sign
      cmdline += quote_arg(base::from_utf8(arg), batch);
    }
    const std::wstring wcwd = base::from_utf8(cwd);

    // The child inherits its standard handles and nothing else
    HANDLE inherited[3];
    DWORD inheritedCount = 0;
    if (nul != INVALID_HANDLE_VALUE)
      inherited[inheritedCount++] = nul;
    inherited[inheritedCount++] = outWrite;
    inherited[inheritedCount++] = errWrite;
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    const BOOL attrsReady = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    BOOL ok = attrsReady &&
              UpdateProcThreadAttribute(attrs,
                                        0,
                                        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                        inherited,
                                        inheritedCount * sizeof(HANDLE),
                                        nullptr,
                                        nullptr);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = outWrite;
    si.StartupInfo.hStdError = errWrite;
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi = {};
    // Suspended until it's in the job, so nothing it starts escapes
    ok = ok && job &&
         CreateProcessW(nullptr,
                        cmdline.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr,
                        wcwd.empty() ? nullptr : wcwd.c_str(),
                        &si.StartupInfo,
                        &pi);
    DWORD lastError = GetLastError();
    if (ok && !AssignProcessToJobObject(job, pi.hProcess)) {
      lastError = GetLastError();
      TerminateProcess(pi.hProcess, 1);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      ok = FALSE;
    }
    if (attrsReady)
      DeleteProcThreadAttributeList(attrs);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (nul != INVALID_HANDLE_VALUE)
      CloseHandle(nul);
    if (!ok) {
      CloseHandle(outRead);
      CloseHandle(errRead);
      if (job)
        CloseHandle(job);
      return "cannot start '" + cmd[0] + "' (error " + std::to_string(lastError) + ")";
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    m_process = pi.hProcess;
    m_job = job;

    auto shared = this->shared;
    const HANDLE process = m_process;
    m_waiter = std::thread([shared, process, outRead, errRead]() {
      std::atomic<bool> ended = false;
      // Reads until the pipe closes. Once the process ended, it reads
      // what's left, up to kDrainLimit, and stops, even if a process
      // it started still holds the pipe.
      auto read_pipe = [&ended, &shared](HANDLE h, Kind kind) {
        std::string pending;
        char buf[4096];
        std::size_t drained = 0;
        for (;;) {
          if (ended) {
            DWORD available = 0;
            if (drained >= kDrainLimit ||
                !PeekNamedPipe(h, nullptr, 0, nullptr, &available, nullptr) || available == 0)
              break;
          }
          DWORD n = 0;
          if (ReadFile(h, buf, DWORD(sizeof(buf)), &n, nullptr)) {
            if (ended)
              drained += n;
            pending.append(buf, n);
            push_lines(shared, kind, pending);
          }
          else if (GetLastError() != ERROR_OPERATION_ABORTED)
            break;
        }
        push_rest(shared, kind, pending);
      };
      std::thread out([&]() { read_pipe(outRead, Kind::Stdout); });
      std::thread err([&]() { read_pipe(errRead, Kind::Stderr); });
      WaitForSingleObject(process, INFINITE);
      ended = true;
      // Wakes a reader that waits for more output
      for (std::thread* reader : { &out, &err }) {
        const HANDLE h = reader->native_handle();
        while (WaitForSingleObject(h, 10) == WAIT_TIMEOUT)
          CancelSynchronousIo(h);
        reader->join();
      }
      CloseHandle(outRead);
      CloseHandle(errRead);
      DWORD code = 0;
      GetExitCodeProcess(process, &code);
      Shared::push(shared, { Kind::Exit, std::string(), int(code) });
    });
    return std::string();
  }

#else

  pid_t m_pid = -1;

  std::string spawn()
  {
    int outPipe[2], errPipe[2], execPipe[2];
    if (pipe(outPipe) != 0)
      return std::strerror(errno);
    if (pipe(errPipe) != 0) {
      close(outPipe[0]);
      close(outPipe[1]);
      return std::strerror(errno);
    }
    if (pipe(execPipe) != 0) {
      close(outPipe[0]);
      close(outPipe[1]);
      close(errPipe[0]);
      close(errPipe[1]);
      return std::strerror(errno);
    }
    fcntl(outPipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(errPipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(execPipe[1], F_SETFD, FD_CLOEXEC);

    std::vector<char*> argv;
    for (auto& arg : cmd)
      argv.push_back(arg.data());
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid == 0) {
      // Its own process group, so stop() reaches its children too
      setpgid(0, 0);
      const int nul = open("/dev/null", O_RDONLY);
      if (nul >= 0)
        dup2(nul, 0);
      dup2(outPipe[1], 1);
      dup2(errPipe[1], 2);
      int err;
      if (!cwd.empty() && chdir(cwd.c_str()) != 0)
        err = errno;
      else {
        execvp(argv[0], argv.data());
        err = errno;
      }
      (void)!write(execPipe[1], &err, sizeof(err));
      _exit(127);
    }

    const int forkErr = errno;
    close(outPipe[1]);
    close(errPipe[1]);
    close(execPipe[1]);
    if (pid < 0) {
      close(outPipe[0]);
      close(errPipe[0]);
      close(execPipe[0]);
      return std::strerror(forkErr);
    }

    // The exec pipe closes without data when execvp() succeeds
    int childErr = 0;
    ssize_t n;
    do {
      n = read(execPipe[0], &childErr, sizeof(childErr));
    } while (n < 0 && errno == EINTR);
    close(execPipe[0]);
    if (n == sizeof(childErr)) {
      close(outPipe[0]);
      close(errPipe[0]);
      waitpid(pid, nullptr, 0);
      return "cannot start '" + cmd[0] + "': " + std::strerror(childErr);
    }

    // The waiter tells the reader through this pipe that the process
    // ended, and with which exit code
    int endPipe[2];
    if (pipe(endPipe) != 0) {
      const int err = errno;
      ::kill(-pid, SIGKILL);
      close(outPipe[0]);
      close(errPipe[0]);
      waitpid(pid, nullptr, 0);
      return std::strerror(err);
    }
    fcntl(endPipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(endPipe[1], F_SETFD, FD_CLOEXEC);

    m_pid = pid;
    const int endWrite = endPipe[1];
    // waitid() with WNOWAIT leaves the process a zombie, so its ID and
    // its process group stay its own until it's reaped, and stop()
    // reaches what it left behind.
    m_waiter = std::thread([pid, endWrite]() {
      siginfo_t info = {};
      while (waitid(P_PID, pid, &info, WEXITED | WNOWAIT) < 0 && errno == EINTR) {}
      const int code = (info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status);
      (void)!write(endWrite, &code, sizeof(code));
      close(endWrite);
    });
    m_reader = std::thread(
      [shared = this->shared, pid, outFd = outPipe[0], errFd = errPipe[0], endRead = endPipe[0]]() {
        read_until_end(shared, pid, outFd, errFd, endRead);
      });
    return std::string();
  }

  // Reads stdout and stderr until the process ends. Then it reads
  // what's left, up to kDrainLimit, and stops, even if a process it started still holds
  // the pipes, and pushes the exit code.
  static void read_until_end(const std::shared_ptr<Shared>& shared,
                             const pid_t pid,
                             const int outFd,
                             const int errFd,
                             const int endRead)
  {
    struct Stream {
      int fd;
      Kind kind;
      std::string pending;
      bool open = true;
    };
    Stream streams[2] = {
      { outFd, Kind::Stdout },
      { errFd, Kind::Stderr }
    };
    // Reads once, returns how much, or 0 at the end of the pipe or
    // when nothing is there to read
    auto read_some = [&shared](Stream& s) -> std::size_t {
      char buf[4096];
      ssize_t n;
      do {
        n = read(s.fd, buf, sizeof(buf));
      } while (n < 0 && errno == EINTR);
      if (n <= 0) {
        if (n == 0 || errno != EAGAIN)
          s.open = false;
        return 0;
      }
      s.pending.append(buf, n);
      push_lines(shared, s.kind, s.pending);
      return std::size_t(n);
    };

    int code = -1;
    for (bool ended = false; !ended;) {
      pollfd fds[3];
      Stream* polled[3] = {};
      int count = 0;
      for (auto& s : streams) {
        if (s.open) {
          fds[count] = { s.fd, POLLIN, 0 };
          polled[count++] = &s;
        }
      }
      fds[count++] = { endRead, POLLIN, 0 };
      if (poll(fds, count, -1) < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      for (int i = 0; i < count; ++i) {
        if (!fds[i].revents)
          continue;
        if (polled[i])
          read_some(*polled[i]);
        else {
          (void)!read(endRead, &code, sizeof(code));
          ended = true;
        }
      }
    }
    close(endRead);

    for (auto& s : streams) {
      if (s.open) {
        fcntl(s.fd, F_SETFL, fcntl(s.fd, F_GETFL) | O_NONBLOCK);
        for (std::size_t drained = 0; drained < kDrainLimit;) {
          const std::size_t n = read_some(s);
          if (n == 0)
            break;
          drained += n;
        }
      }
      push_rest(shared, s.kind, s.pending);
      close(s.fd);
    }

    // With the pipes closed by everyone, the zombie isn't needed for
    // stop() anymore
    if (!streams[0].open && !streams[1].open) {
      std::lock_guard lock(shared->mutex);
      while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
      shared->reaped = true;
    }
    Shared::push(shared, { Kind::Exit, std::string(), code });
  }

  std::thread m_reader;

#endif

  std::thread m_waiter;
};

void call_callback(lua_State* L, Process* process, const char* name, const Event& ev)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, process->runningRef);
  lua_getuservalue(L, -1);
  if (lua_istable(L, -1) && lua_getfield(L, -1, name) == LUA_TFUNCTION) {
    if (ev.kind == Kind::Exit)
      lua_pushinteger(L, ev.code);
    else
      lua_pushlstring(L, ev.line.data(), ev.line.size());
    if (lua_pcall(L, 1, 0, 0)) {
      if (const char* s = lua_tostring(L, -1))
        App::instance()->scriptEngine()->consolePrint(s);
      lua_pop(L, 1);
    }
  }
  else {
    lua_pop(L, 1); // Pop the field
  }
  lua_pop(L, 2); // Pop the uservalue and the process
}

// Hands the queued events to the Lua callbacks, which run on the
// given Lua thread.
void dispatch(Process* process, lua_State* L)
{
  if (process->dispatching || process->runningRef == LUA_REFNIL)
    return;
  process->dispatching = true;
  for (;;) {
    Event ev;
    {
      std::lock_guard lock(process->shared->mutex);
      if (process->shared->events.empty())
        break;
      ev = std::move(process->shared->events.front());
      process->shared->events.pop_front();
    }
    try {
      switch (ev.kind) {
        case Kind::Stdout: call_callback(L, process, "onstdout", ev); break;
        case Kind::Stderr: call_callback(L, process, "onstderr", ev); break;
        case Kind::Exit:
          process->running = false;
          process->exitCode = ev.code;
          call_callback(L, process, "onexit", ev);
          luaL_unref(L, LUA_REGISTRYINDEX, process->runningRef);
          process->runningRef = LUA_REFNIL;
          break;
      }
    }
    catch (const std::exception& ex) {
      App::instance()->scriptEngine()->consolePrint(ex.what());
    }
    if (process->runningRef == LUA_REFNIL)
      break;
  }
  process->dispatching = false;
}

void dispatch_from_ui(Process* process)
{
  dispatch(process, process->L);
}

int Process_new(lua_State* L)
{
  luaL_checktype(L, 1, LUA_TTABLE);

  std::vector<std::string> cmd;
  if (lua_getfield(L, 1, "cmd") == LUA_TTABLE) {
    for (int i = 1;; ++i) {
      if (lua_geti(L, -1, i) == LUA_TNIL) {
        lua_pop(L, 1);
        break;
      }
      if (!lua_isstring(L, -1))
        return luaL_error(L, "every item of 'cmd' must be a string");
      cmd.push_back(lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  if (cmd.empty())
    return luaL_error(L, "'cmd' must be a non-empty list of strings");

  std::string cwd;
  if (lua_getfield(L, 1, "cwd") == LUA_TSTRING)
    cwd = lua_tostring(L, -1);
  lua_pop(L, 1);

  auto process = push_new<Process>(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
  process->L = lua_tothread(L, -1);
  lua_pop(L, 1);
  process->cmd = std::move(cmd);
  process->cwd = std::move(cwd);

  // The callbacks live in the uservalue of the object
  lua_newtable(L);
  for (const char* name : { "onstdout", "onstderr", "onexit" }) {
    if (lua_getfield(L, 1, name) == LUA_TFUNCTION)
      lua_setfield(L, -2, name);
    else
      lua_pop(L, 1);
  }
  lua_setuservalue(L, -2);
  return 1;
}

int Process_gc(lua_State* L)
{
  auto process = get_obj<Process>(L, 1);
  process->~Process();
  return 0;
}

int Process_start(lua_State* L)
{
  auto process = get_obj<Process>(L, 1);
  if (process->started)
    return luaL_error(L, "the process was already started");

  if (!ask_access(L, process->cmd[0].c_str(), FileAccessMode::Execute, ResourceType::Command))
    return luaL_error(L,
                      "the script doesn't have access to execute the command: '%s'",
                      process->cmd[0].c_str());

  const std::string error = process->start();
  if (!error.empty())
    return luaL_error(L, "%s", error.c_str());

  lua_pushvalue(L, 1);
  process->runningRef = luaL_ref(L, LUA_REGISTRYINDEX);
  return 0;
}

int Process_stop(lua_State* L)
{
  auto process = get_obj<Process>(L, 1);
  process->stop(lua_toboolean(L, 2));
  return 0;
}

int Process_wait(lua_State* L)
{
  auto process = get_obj<Process>(L, 1);
  if (process->dispatching)
    return luaL_error(L, "Process:wait() cannot be called from its own callbacks");
  while (process->running) {
    {
      std::unique_lock lock(process->shared->mutex);
      process->shared->cv.wait(lock, [process] { return !process->shared->events.empty(); });
    }
    dispatch(process, L);
  }
  if (process->started)
    lua_pushinteger(L, process->exitCode);
  else
    lua_pushnil(L);
  return 1;
}

int Process_get_isRunning(lua_State* L)
{
  auto process = get_obj<Process>(L, 1);
  lua_pushboolean(L, process->running);
  return 1;
}

int Process_get_exitCode(lua_State* L)
{
  auto process = get_obj<Process>(L, 1);
  if (process->started && !process->running)
    lua_pushinteger(L, process->exitCode);
  else
    lua_pushnil(L);
  return 1;
}

const luaL_Reg Process_methods[] = {
  { "__gc",  Process_gc    },
  { "start", Process_start },
  { "stop",  Process_stop  },
  { "wait",  Process_wait  },
  { nullptr, nullptr       }
};

const Property Process_properties[] = {
  { "isRunning", Process_get_isRunning, nullptr },
  { "exitCode",  Process_get_exitCode,  nullptr },
  { nullptr,     nullptr,               nullptr }
};

} // anonymous namespace

DEF_MTNAME(Process);

void register_process_class(lua_State* L)
{
  REG_CLASS(L, Process);
  REG_CLASS_NEW(L, Process);
  REG_CLASS_PROPERTIES(L, Process);
}

}} // namespace app::script

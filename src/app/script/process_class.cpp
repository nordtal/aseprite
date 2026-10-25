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
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include "base/string.h"
  #include <windows.h>
#else
  #include <cerrno>
  #include <csignal>
  #include <fcntl.h>
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

void dispatch(Process* process);

// State shared between a Process, its background threads and the
// callbacks queued on the UI thread.
struct Shared {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Event> events;
  std::atomic<bool> exited = false;
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
            dispatch(s->owner);
        }
      });
    }
  }
};

void read_lines(const std::shared_ptr<Shared>& shared,
                const Kind kind,
                const std::function<long(char*, long)>& read)
{
  std::string pending;
  char buf[4096];
  long n;
  while ((n = read(buf, sizeof(buf))) > 0) {
    pending.append(buf, n);
    std::size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, pos);
      pending.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      Shared::push(shared, { kind, std::move(line) });
    }
  }
  if (!pending.empty())
    Shared::push(shared, { kind, std::move(pending) });
}

// A child process started from an argument list, without a shell.
// Its output is read line by line in background threads, and each
// line and the exit code reach the Lua callbacks on the script thread.
class Process {
public:
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

  ~Process()
  {
    shared->owner = nullptr;
    stop(true);
    if (m_waiter.joinable())
      m_waiter.join();
#ifdef _WIN32
    if (m_process)
      CloseHandle(m_process);
#endif
  }

  void stop(bool force)
  {
    if (!started || shared->exited)
      return;
#ifdef _WIN32
    TerminateProcess(m_process, 1);
#else
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

  static std::wstring quote_arg(const std::wstring& arg)
  {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
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

  std::string spawn()
  {
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
      cmdline += quote_arg(base::from_utf8(arg));
    }
    const std::wstring wcwd = base::from_utf8(cwd);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(nullptr,
                                   cmdline.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   CREATE_NO_WINDOW,
                                   nullptr,
                                   wcwd.empty() ? nullptr : wcwd.c_str(),
                                   &si,
                                   &pi);
    const DWORD lastError = GetLastError();
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (nul != INVALID_HANDLE_VALUE)
      CloseHandle(nul);
    if (!ok) {
      CloseHandle(outRead);
      CloseHandle(errRead);
      return "cannot start '" + cmd[0] + "' (error " + std::to_string(lastError) + ")";
    }
    CloseHandle(pi.hThread);
    m_process = pi.hProcess;

    auto shared = this->shared;
    const HANDLE process = m_process;
    m_waiter = std::thread([shared, process, outRead, errRead]() {
      auto reader = [](HANDLE h) {
        return [h](char* buf, long size) -> long {
          DWORD n = 0;
          if (!ReadFile(h, buf, DWORD(size), &n, nullptr))
            return 0;
          return long(n);
        };
      };
      std::thread out([&]() { read_lines(shared, Kind::Stdout, reader(outRead)); });
      std::thread err([&]() { read_lines(shared, Kind::Stderr, reader(errRead)); });
      WaitForSingleObject(process, INFINITE);
      out.join();
      err.join();
      CloseHandle(outRead);
      CloseHandle(errRead);
      DWORD code = 0;
      GetExitCodeProcess(process, &code);
      shared->exited = true;
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

    m_pid = pid;
    auto shared = this->shared;
    const int outFd = outPipe[0];
    const int errFd = errPipe[0];
    m_waiter = std::thread([shared, pid, outFd, errFd]() {
      auto reader = [](int fd) {
        return [fd](char* buf, long size) -> long {
          ssize_t n;
          do {
            n = read(fd, buf, size);
          } while (n < 0 && errno == EINTR);
          return long(n);
        };
      };
      std::thread out([&]() { read_lines(shared, Kind::Stdout, reader(outFd)); });
      std::thread err([&]() { read_lines(shared, Kind::Stderr, reader(errFd)); });
      int status = 0;
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
      shared->exited = true;
      out.join();
      err.join();
      close(outFd);
      close(errFd);
      const int code = (WIFEXITED(status)   ? WEXITSTATUS(status) :
                        WIFSIGNALED(status) ? 128 + WTERMSIG(status) :
                                              -1);
      Shared::push(shared, { Kind::Exit, std::string(), code });
    });
    return std::string();
  }

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
      lua_pushstring(L, ev.line.c_str());
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

// Hands the queued events to the Lua callbacks.
void dispatch(Process* process)
{
  if (process->dispatching || process->runningRef == LUA_REFNIL)
    return;
  process->dispatching = true;
  lua_State* L = process->L;
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
  process->L = L;
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
    dispatch(process);
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

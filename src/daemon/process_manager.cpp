#include "process_manager.hpp"

#include <spectra/logger.hpp>

#ifndef _WIN32
    #include <spawn.h>
    #include <sys/wait.h>
    #include <unistd.h>

    #ifdef __APPLE__
        #include <crt_externs.h>
    #else
extern char** environ;
    #endif
#endif

namespace spectra::daemon
{

pid_t ProcessManager::spawn_agent()
{
#ifndef _WIN32
    std::lock_guard lock(mu_);

    if (agent_path_.empty() || socket_path_.empty())
        return -1;

    pid_t       pid    = 0;
    const char* argv[] = {agent_path_.c_str(), "--socket", socket_path_.c_str(), nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    #ifdef __APPLE__
    char** env = *_NSGetEnviron();
    #else
    char** env = environ;
    #endif

    int ret = posix_spawn(&pid,
                          agent_path_.c_str(),
                          &actions,
                          nullptr,
                          const_cast<char* const*>(argv),
                          env);
    posix_spawn_file_actions_destroy(&actions);

    if (ret != 0)
    {
        SPECTRA_LOG_ERROR("daemon", "posix_spawn failed: {}", ret);
        return -1;
    }

    ProcessEntry entry;
    entry.pid         = pid;
    entry.socket_path = socket_path_;
    entry.alive       = true;
    processes_[pid]   = std::move(entry);

    SPECTRA_LOG_INFO("daemon", "Spawned agent pid={}", pid);
    return pid;
#else
    return -1;
#endif
}

pid_t ProcessManager::spawn_agent_for_window(ipc::WindowId wid)
{
    pid_t pid = spawn_agent();
    if (pid > 0)
    {
        std::lock_guard lock(mu_);
        auto            it = processes_.find(pid);
        if (it != processes_.end())
            it->second.window_id = wid;
    }
    return pid;
}

bool ProcessManager::is_alive(pid_t pid) const
{
#ifndef _WIN32
    int   status = 0;
    pid_t result = ::waitpid(pid, &status, WNOHANG);
    return result == 0;   // 0 means still running
#else
    (void)pid;
    return false;
#endif
}

std::vector<pid_t> ProcessManager::reap_finished()
{
#ifndef _WIN32
    std::lock_guard    lock(mu_);
    std::vector<pid_t> reaped;

    for (auto it = processes_.begin(); it != processes_.end();)
    {
        int   status = 0;
        pid_t result = ::waitpid(it->first, &status, WNOHANG);
        if (result > 0)
        {
            std::string reap_msg = "Reaped pid=" + std::to_string(it->first);
            if (WIFEXITED(status))
                reap_msg += " exit_code=" + std::to_string(WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                reap_msg += " signal=" + std::to_string(WTERMSIG(status));
            SPECTRA_LOG_DEBUG("daemon", "{}", reap_msg);

            reaped.push_back(it->first);
            it = processes_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return reaped;
#else
    return {};
#endif
}

size_t ProcessManager::process_count() const
{
    std::lock_guard lock(mu_);
    return processes_.size();
}

std::vector<ProcessEntry> ProcessManager::all_processes() const
{
    std::lock_guard           lock(mu_);
    std::vector<ProcessEntry> result;
    result.reserve(processes_.size());
    for (auto& [_, entry] : processes_)
        result.push_back(entry);
    return result;
}

void ProcessManager::remove_process(pid_t pid)
{
    std::lock_guard lock(mu_);
    processes_.erase(pid);
}

void ProcessManager::set_window_id(pid_t pid, ipc::WindowId wid)
{
    std::lock_guard lock(mu_);
    auto            it = processes_.find(pid);
    if (it != processes_.end())
        it->second.window_id = wid;
}

pid_t ProcessManager::pid_for_window(ipc::WindowId wid) const
{
    std::lock_guard lock(mu_);
    for (auto& [pid, entry] : processes_)
    {
        if (entry.window_id == wid)
            return pid;
    }
    return 0;
}

}   // namespace spectra::daemon

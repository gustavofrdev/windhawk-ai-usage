#pragma once
#include "common.h"

namespace {

// Runs a command line with no console window and returns its stdout. Only the
// pipe and NUL handles are inherited, so explorer.exe handles never leak into it.
// Example: std::string json = HiddenProcessRunner(30000, stopEvent).run(L"wsl.exe -e ai-usagebar usage --json");
class HiddenProcessRunner {
public:
    HiddenProcessRunner(DWORD timeoutMs, HANDLE cancelEvent)
        : timeoutMs_(timeoutMs), cancelEvent_(cancelEvent) {}

    std::string run(const std::wstring& commandLine) const {
        UniqueHandle readEnd;
        UniqueHandle writeEnd;
        createStdoutPipe(readEnd, writeEnd, commandLine);
        UniqueHandle process(launchHidden(commandLine, writeEnd.get()));
        writeEnd.reset();
        std::string output = collectOutput(readEnd.get(), process.get(), commandLine);
        requireZeroExitCode(process.get(), commandLine);
        return output;
    }

private:
    // Any failure while reading (such as output over kMaxCommandOutput) must
    // not leave the child running after the mod gives up on it.
    std::string collectOutput(HANDLE readEnd, HANDLE process, const std::wstring& commandLine) const {
        std::string output;
        ULONGLONG deadline = GetTickCount64() + timeoutMs_;
        try {
            while (!hasExited(process, deadline, commandLine)) {
                drainAvailable(readEnd, output);
            }
            drainAvailable(readEnd, output);
        } catch (...) {
            TerminateProcess(process, 1);
            throw;
        }
        return output;
    }

    static SECURITY_ATTRIBUTES inheritableAttributes() {
        return {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    }

    static void createStdoutPipe(UniqueHandle& readEnd, UniqueHandle& writeEnd,
                                 const std::wstring& commandLine) {
        SECURITY_ATTRIBUTES attributes = inheritableAttributes();
        HANDLE readRaw = nullptr;
        HANDLE writeRaw = nullptr;
        if (!CreatePipe(&readRaw, &writeRaw, &attributes, 0)) {
            throwLastError("CreatePipe", commandLine);
        }
        readEnd.reset(readRaw);
        writeEnd.reset(writeRaw);
        SetHandleInformation(readRaw, HANDLE_FLAG_INHERIT, 0);
    }

    static HANDLE launchHidden(const std::wstring& commandLine, HANDLE stdoutWrite) {
        SECURITY_ATTRIBUTES attributes = inheritableAttributes();
        UniqueHandle nullDevice(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                            OPEN_EXISTING, 0, nullptr));
        HANDLE inherited[2] = {stdoutWrite, nullDevice.get()};
        SIZE_T listSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
        std::vector<BYTE> listBuffer(listSize);
        auto attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listBuffer.data());
        InitializeProcThreadAttributeList(attributeList, 1, 0, &listSize);
        UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                  sizeof(inherited), nullptr, nullptr);
        HANDLE process = createProcess(commandLine, stdoutWrite, nullDevice.get(), attributeList);
        DeleteProcThreadAttributeList(attributeList);
        return process;
    }

    static HANDLE createProcess(const std::wstring& commandLine, HANDLE stdoutWrite,
                                HANDLE nullDevice, LPPROC_THREAD_ATTRIBUTE_LIST attributeList) {
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = nullDevice;
        startup.StartupInfo.hStdOutput = stdoutWrite;
        startup.StartupInfo.hStdError = nullDevice;
        startup.lpAttributeList = attributeList;
        PROCESS_INFORMATION info{};
        std::wstring mutableLine = commandLine;
        DWORD flags = CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
        if (!CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, flags, nullptr,
                            nullptr, &startup.StartupInfo, &info)) {
            throwLastError("CreateProcessW (expected an executable such as wsl.exe)", commandLine);
        }
        CloseHandle(info.hThread);
        return info.hProcess;
    }

    bool hasExited(HANDLE process, ULONGLONG deadline, const std::wstring& commandLine) const {
        HANDLE waitHandles[2] = {process, cancelEvent_};
        switch (WaitForMultipleObjects(2, waitHandles, FALSE, 50)) {
            case WAIT_OBJECT_0:
                return true;
            case WAIT_OBJECT_0 + 1:
                abortProcess(process, "was canceled because the mod is stopping", commandLine);
            default:
                break;
        }
        if (GetTickCount64() >= deadline) {
            abortProcess(process, "timed out after " + std::to_string(timeoutMs_) + " ms",
                         commandLine);
        }
        return false;
    }

    [[noreturn]] static void abortProcess(HANDLE process, const std::string& reason,
                                          const std::wstring& commandLine) {
        TerminateProcess(process, 1);
        throw std::runtime_error("Command '" + wideToUtf8(commandLine) + "' " + reason);
    }

    static void drainAvailable(HANDLE readEnd, std::string& output) {
        char chunk[4096];
        DWORD available = 0;
        DWORD bytesRead = 0;
        while (PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && available > 0 &&
               ReadFile(readEnd, chunk, sizeof(chunk), &bytesRead, nullptr) && bytesRead > 0) {
            output.append(chunk, bytesRead);
        }
        if (output.size() > kMaxCommandOutput) {
            throw std::runtime_error("Command output has " + std::to_string(output.size()) +
                                     " bytes; expected at most " + std::to_string(kMaxCommandOutput));
        }
    }

    static void requireZeroExitCode(HANDLE process, const std::wstring& commandLine) {
        DWORD exitCode = 0;
        GetExitCodeProcess(process, &exitCode);
        if (exitCode != 0) {
            throw std::runtime_error("Command '" + wideToUtf8(commandLine) + "' exited with code " +
                                     std::to_string(exitCode) + "; expected 0");
        }
    }

    DWORD timeoutMs_;
    HANDLE cancelEvent_;
};

}  // namespace

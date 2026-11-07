#pragma once

#include "PluginHostProtocol.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

struct PluginHostReply {
    PluginHostResponse header;
    std::vector<char> payload;
};

inline bool writePipe(HANDLE pipe, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        const DWORD chunk = static_cast<DWORD>(
            size > std::numeric_limits<DWORD>::max() ? std::numeric_limits<DWORD>::max() : size
        );

        DWORD written = 0;
        if (!WriteFile(pipe, bytes, chunk, &written, nullptr) 
        ||  written == 0
        ) {
            return false;
        }

        bytes += written;
        size -= written;
    }

    
    return true;
}

inline bool runPluginHost(
    const std::string& pluginPath,
    PluginHostOperation operation,
    const std::string& imagePath,
    PluginHostReply& reply,
    std::string& error,
    const std::string& extra = {},
    const std::vector<std::uint8_t>& data = {}
) {
    if (pluginPath.size() > std::numeric_limits<std::uint32_t>::max()
    ||  imagePath.size() > std::numeric_limits<std::uint32_t>::max()
    ||  extra.size() > std::numeric_limits<std::uint32_t>::max()
    ||  data.size() > std::numeric_limits<std::uint32_t>::max()
    ) {
        error = "Plugin or image path is too long for the compatibility host";
        return false;
    }

    std::array<wchar_t, 32768> executableBuffer{};
    const DWORD executableLength = GetModuleFileNameW(
        nullptr,
        executableBuffer.data(),
        static_cast<DWORD>(executableBuffer.size())
    );
    if (executableLength == 0 || executableLength >= executableBuffer.size()) {
        error = "Could not locate the application executable";
        return false;
    }
    const std::filesystem::path hostPath
    =   std::filesystem::path(std::wstring(executableBuffer.data(), executableLength))
    .   parent_path()
    /   L"openSusie_plugin_host32.exe";

    if (!std::filesystem::exists(hostPath)) {
        error 
        =   "The 32-bit plugin compatibility host is missing: "
        +   hostPath.string();
        return false;
    }
    DWORD hostBinaryType = 0;
    if (!GetBinaryTypeW(hostPath.c_str(), &hostBinaryType)) {
        error
        =   "Could not inspect the 32-bit plugin compatibility host (Windows error "
        +   std::to_string(GetLastError()) + ")";
        return false;
    }
    if (hostBinaryType != SCS_32BIT_BINARY) {
        error = "The plugin compatibility host is not a 32-bit executable; "
                "build it with an x86 toolchain";
        return false;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE childInput = nullptr;
    HANDLE parentInput = nullptr;
    HANDLE parentOutput = nullptr;
    HANDLE childOutput = nullptr;
    if (!CreatePipe(&childInput, &parentInput, &security, 0)
    ||  !CreatePipe(&parentOutput, &childOutput, &security, 0)
    ) {
        error = "Could not create compatibility-host pipes (Windows error "
            + std::to_string(GetLastError()) + ")";
        if (childInput) CloseHandle(childInput);
        if (parentInput) CloseHandle(parentInput);
        if (parentOutput) CloseHandle(parentOutput);
        if (childOutput) CloseHandle(childOutput);
        return false;
    }
    if (!SetHandleInformation(parentInput, HANDLE_FLAG_INHERIT, 0)
    ||  !SetHandleInformation(parentOutput, HANDLE_FLAG_INHERIT, 0)
    ) {
        const DWORD errorCode = GetLastError();
        CloseHandle(childInput);
        CloseHandle(parentInput);
        CloseHandle(parentOutput);
        CloseHandle(childOutput);
        error
        =   "Could not configure compatibility-host pipes (Windows error "
        +   std::to_string(errorCode) + ")";
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput;
    startup.hStdOutput = childOutput;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + hostPath.wstring() + L"\"";
    const BOOL created = CreateProcessW(
        hostPath.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process
    );

    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(childInput);
    CloseHandle(childOutput);
    if (!created) {
        error
        =   "Could not start the 32-bit plugin compatibility host (Windows error "
        +   std::to_string(createError) + ")";
        CloseHandle(parentInput);
        CloseHandle(parentOutput);
        return false;
    }

    PluginHostRequest request;
    request.operation = static_cast<std::uint32_t>(operation);
    request.pluginPathBytes = static_cast<std::uint32_t>(pluginPath.size());
    request.imagePathBytes = static_cast<std::uint32_t>(imagePath.size());
    request.extraBytes = static_cast<std::uint32_t>(extra.size());
    request.dataBytes = static_cast<std::uint32_t>(data.size());

    const bool requestWritten
    =   writePipe(parentInput, &request, sizeof(request))
    &&  writePipe(parentInput, pluginPath.data(), pluginPath.size())
    &&  writePipe(parentInput, imagePath.data(), imagePath.size())
    &&  writePipe(parentInput, extra.data(), extra.size())
    &&  writePipe(parentInput, data.data(), data.size());

    CloseHandle(parentInput);

    if (!requestWritten) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(parentOutput);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        error = "Could not send a request to the 32-bit plugin compatibility host";
        return false;
    }

    constexpr std::size_t maxPayloadBytes = 1024ull * 1024 * 1024;
    std::array<char, 65536> buffer{};
    bool readSucceeded = true;
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(parentOutput, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            if (GetLastError() != ERROR_BROKEN_PIPE) readSucceeded = false;
            break;
        }
        if (bytesRead == 0) break;
        if (bytesRead > maxPayloadBytes
        ||  reply.payload.size() > maxPayloadBytes - bytesRead
        ) {
            readSucceeded = false;
            TerminateProcess(process.hProcess, 1);
            break;
        }

        reply.payload.insert(reply.payload.end(), buffer.data(), buffer.data() + bytesRead);
    }
    CloseHandle(parentOutput);
    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!readSucceeded || exitCode != 0) {
        error = "The 32-bit plugin compatibility host failed";
        return false;
    }

    if (reply.payload.size() < sizeof(PluginHostResponse)) {
        error = "The 32-bit plugin compatibility host returned an incomplete response";
        return false;
    }

    std::memcpy(&reply.header, reply.payload.data(), sizeof(reply.header));
    if (reply.header.magic != PluginHostProtocolMagic) {
        error = "The 32-bit plugin compatibility host returned an invalid response";
        return false;
    }
    reply.payload.erase(reply.payload.begin(), reply.payload.begin() + sizeof(reply.header));

    const std::uint64_t expectedPayload 
    =   static_cast<std::uint64_t>(reply.header.nameBytes)
    +   reply.header.extensionsBytes
    +   reply.header.descriptionBytes
    +   reply.header.metadataBytes
    +   reply.header.pixelsBytes
    +   reply.header.reasonBytes;

    if (expectedPayload != reply.payload.size()) {
        error = "The 32-bit plugin compatibility host returned malformed data";
        return false;
    }

    return true;
}
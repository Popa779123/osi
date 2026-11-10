#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <limits>
#define NOMINMAX  // чтобы макросы min/max из windows.h не ломали numeric_limits::max()
#include <windows.h>
#include <cstdlib>

// Имя переменной среды, из которой берётся имя файла
// (по заданию - фамилия студента)
static const char kEnvVarName[] = "ZHDANOVICH";

struct FileHeader {
    int capacity;
    int readIndex;
    int writeIndex;
    int count;
};

struct Message {
    char text[20];
};

int main(int argc, char* argv[]) {
    std::string filename;
    int capacity;
    int choice;

    std::cout << "1. Enter filename manually\n";
    std::cout << "2. Get filename from environment variable\n";
    std::cout << "Choose: ";
    std::cin >> choice;

    if (choice == 1) {
        std::cout << "Enter filename: ";
        std::cin >> filename;
    }
    else if (choice == 2) {
        // По заданию имя файла берётся из переменной среды через _dupenv_s.
        char* buffer = nullptr;
        size_t len = 0;
        errno_t err = _dupenv_s(&buffer, &len, kEnvVarName);

        if (err != 0 || buffer == nullptr) {
            std::cerr << "Environment variable '" << kEnvVarName
                      << "' not found\n";
            if (buffer != nullptr) free(buffer);
            return 1;
        }

        filename = buffer;
        free(buffer);  // _dupenv_s выделяет память - обязательно освобождаем
        std::cout << filename << "\n";
    }
    else {
        std::cerr << "Invalid choice\n";
        return 1;
    }

    std::cout << "Enter queue capacity: ";
    std::cin >> capacity;

    if (!std::cin || capacity <= 0) {
        std::cerr << "Invalid capacity\n";
        return 1;
    }

    std::fstream file(
        filename,
        std::ios::binary | std::ios::in |
        std::ios::out | std::ios::trunc
    );

    if (!file.is_open()) {
        std::cerr << "Failed to create file\n";
        return 1;
    }

    FileHeader header{capacity, 0, 0, 0};

    file.write(
        reinterpret_cast<const char*>(&header),
        sizeof(header)
    );

    Message emptyMessage{};

    for (int i = 0; i < capacity; ++i) {
        file.write(
            reinterpret_cast<const char*>(&emptyMessage),
            sizeof(Message)
        );
    }

    if (!file) {
        std::cerr << "Failed to initialize file\n";
        return 1;
    }

    file.close();

    std::cout << "File created successfully\n";

    HANDLE mutex = CreateMutex(nullptr, FALSE, "FileMutex");
    HANDLE freeSlots = CreateSemaphore(nullptr, capacity, capacity, "FreeSlots"); // все слоты свободны в начале -> capacity
    HANDLE usedSlots = CreateSemaphore(nullptr, 0, capacity, "UsedSlots"); // ни один слот не занят в начале -> 0

    if (mutex == nullptr || freeSlots == nullptr || usedSlots == nullptr) {
        std::cerr << "Failed to create synchronization objects\n";

        if (mutex != nullptr) CloseHandle(mutex);
        if (freeSlots != nullptr) CloseHandle(freeSlots);
        if (usedSlots != nullptr) CloseHandle(usedSlots);

        return 1;
    }

    int senderCount;

    std::cout << "Enter number of senders: ";
    std::cin >> senderCount;

    if (!std::cin || senderCount <= 0) {
        std::cerr << "Invalid sender count\n";
        return 1;
    }

    std::filesystem::path senderPath =
        std::filesystem::absolute(argv[0]).parent_path() / "Sender.exe";

    std::filesystem::path absoluteFilename =
        std::filesystem::absolute(filename);

    std::vector<HANDLE> senderHandles;
    std::vector<HANDLE> readyEvents;

    for (int i = 0; i < senderCount; ++i) {
        std::string eventName = "SenderReady" + std::to_string(i);

        HANDLE readyEvent = CreateEvent(
            nullptr,
            TRUE,
            FALSE,
            eventName.c_str()
        );

        if (readyEvent == nullptr) {
            std::cerr << "Failed to create event. Error: " << GetLastError() << '\n';
            continue;
        }

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        ZeroMemory(&pi, sizeof(pi));

        si.cb = sizeof(si);

        std::string commandLine = "\"" + senderPath.string() + "\" \"" + absoluteFilename.string() + "\" \"" + eventName + "\"";

        std::vector<char> commandBuffer(commandLine.begin(), commandLine.end());

        commandBuffer.push_back('\0');

        BOOL created = CreateProcess(
            senderPath.string().c_str(),
            commandBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NEW_CONSOLE,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        if (!created) {
            std::cerr << "Failed to create Sender " << i + 1 << ". Error: " << GetLastError() << '\n';
            CloseHandle(readyEvent);
            continue;
        }

        senderHandles.push_back(pi.hProcess);
        readyEvents.push_back(readyEvent);

        CloseHandle(pi.hThread);

        std::cout << "Sender " << i + 1 << " created. PID: " << pi.dwProcessId << '\n';
    }

    std::cout << "\nWaiting for all Senders...\n";

    bool allReady = true;

    for (int i = 0; i < static_cast<int>(readyEvents.size()); ++i) {
        DWORD result = WaitForSingleObject(readyEvents[i], INFINITE);

        if (result == WAIT_OBJECT_0) {
            std::cout << "Sender " << i + 1 << " is ready\n";
        }
        else {
            std::cerr << "Sender " << i + 1 << " readiness wait failed or timed out\n";

            allReady = false;
        }
    }

    for (HANDLE event : readyEvents) {
        CloseHandle(event);
    }

    if (allReady && static_cast<int>(readyEvents.size()) == senderCount) {
        std::cout << "All Senders are ready!\n";
    }
    else {
        std::cout << "Not all Senders are ready\n";
    }

    std::fstream fileToRead(filename, std::ios::binary | std::ios::in | std::ios::out);

    if (!fileToRead.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    std::string command;

    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    while (true) {
        std::cout << "\n1. Read message\n";
        std::cout << "2. Exit\n";
        std::cout << "Choose: ";
        std::getline(std::cin, command);

        if (command == "2") {
            bool allFinished = true;

            for (int i = 0; i < static_cast<int>(senderHandles.size()); ++i) {
                DWORD result = WaitForSingleObject(senderHandles[i], 0);

                if (result == WAIT_TIMEOUT) {
                    std::cout << "Sender " << i + 1 << " is still running\n";
                    allFinished = false;
                }
                else if (result == WAIT_FAILED) {
                    std::cerr << "Failed to check Sender " << i + 1 << '\n';
                    allFinished = false;
                }
            }

            if (!allFinished) {
                std::cout << "Finish all Senders before exiting Receiver\n";
                continue;
            }

            break;
        }

        if (command != "1") {
            std::cout << "Invalid command\n";
            continue;
        }

        if (WaitForSingleObject(usedSlots, INFINITE) != WAIT_OBJECT_0) {
            std::cerr << "Failed to wait for message\n";
            break;
        }

        DWORD result = WaitForSingleObject(mutex, INFINITE);

        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            std::cerr << "Failed to lock mutex\n";
            ReleaseSemaphore(usedSlots, 1, nullptr);
            break;
        }

        FileHeader header;

        fileToRead.clear();
        fileToRead.seekg(0);
        fileToRead.read(reinterpret_cast<char*>(&header), sizeof(header));

        Message message{};

        std::streamoff offset = sizeof(FileHeader) + header.readIndex * sizeof(Message);

        fileToRead.seekg(offset);
        fileToRead.read(reinterpret_cast<char*>(&message), sizeof(message));

        header.readIndex = (header.readIndex + 1) % header.capacity;
        header.count--;

        fileToRead.seekp(0);
        fileToRead.write(reinterpret_cast<const char*>(&header), sizeof(header));
        fileToRead.flush();

        ReleaseMutex(mutex);

        ReleaseSemaphore(freeSlots, 1, nullptr);

        std::cout << "Received message: " << message.text << '\n';
    }

    for (HANDLE handle : senderHandles) {
        CloseHandle(handle);
    }

    CloseHandle(mutex);
    CloseHandle(freeSlots);
    CloseHandle(usedSlots);

    return 0;
}

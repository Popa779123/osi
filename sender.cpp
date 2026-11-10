#include <iostream>
#include <string>
#include <fstream>
#include <windows.h>

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
    if (argc < 3) {
        std::cerr << "Not enough arguments\n";
        return 1;
    }

    std::string filename = argv[1];
    std::string eventName = argv[2];

    std::cout << "Sender started\n";
    std::cout << "Filename: " << filename << '\n';

    std::fstream file(
        filename,
        std::ios::binary | std::ios::in | std::ios::out
    );

    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    HANDLE readyEvent = OpenEvent(
        EVENT_MODIFY_STATE,
        FALSE,
        eventName.c_str()
    );

    if (readyEvent == nullptr) {
        std::cerr << "Failed to open event. Error: "
                  << GetLastError() << '\n';
        return 1;
    }

    HANDLE mutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, "FileMutex");
    HANDLE freeSlots = OpenSemaphore(SEMAPHORE_ALL_ACCESS, FALSE, "FreeSlots");
    HANDLE usedSlots = OpenSemaphore(SEMAPHORE_ALL_ACCESS, FALSE, "UsedSlots");

    if (mutex == nullptr || freeSlots == nullptr || usedSlots == nullptr) {
        std::cerr << "Failed to open synchronization objects\n";

        if (mutex != nullptr) CloseHandle(mutex);
        if (freeSlots != nullptr) CloseHandle(freeSlots);
        if (usedSlots != nullptr) CloseHandle(usedSlots);

        CloseHandle(readyEvent);
        return 1;
    }

    if (!SetEvent(readyEvent)) {
        std::cerr << "Failed to signal readiness\n";
        CloseHandle(readyEvent);
        return 1;
    }

    std::cout << "Sender is ready!\n";

    CloseHandle(readyEvent);

    std::string command;

    while (true) {
        std::cout << "\n1. Send message\n";
        std::cout << "2. Exit\n";
        std::cout << "Choose: ";
        std::getline(std::cin, command);

        if (command == "2") {
            break;
        }

        if (command != "1") {
            std::cout << "Invalid command\n";
            continue;
        }

        std::string text;
        std::cout << "Enter message: ";
        std::getline(std::cin, text);

        if (text.empty() || text.size() >= 20) {
            std::cout << "Message must contain from 1 to 19 bytes\n";
            continue;
        }

        if (WaitForSingleObject(freeSlots, INFINITE) != WAIT_OBJECT_0) {
            std::cerr << "Failed to wait for free slot\n";
            break;
        }

        DWORD result = WaitForSingleObject(mutex, INFINITE);

        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            std::cerr << "Failed to lock mutex\n";
            ReleaseSemaphore(freeSlots, 1, nullptr);
            break;
        }

        FileHeader header;

        file.clear();
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        Message message{};
        text.copy(message.text, text.size());

        std::streamoff offset = sizeof(FileHeader) + header.writeIndex * sizeof(Message);

        file.seekp(offset);
        file.write(reinterpret_cast<const char*>(&message), sizeof(message));

        header.writeIndex = (header.writeIndex + 1) % header.capacity;
        header.count++;

        file.seekp(0);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.flush();

        ReleaseMutex(mutex);

        ReleaseSemaphore(usedSlots, 1, nullptr);

        std::cout << "Message sent\n";
    }

    CloseHandle(mutex);
    CloseHandle(freeSlots);
    CloseHandle(usedSlots);

    file.close();

    return 0;
}

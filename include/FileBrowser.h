#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <Arduino.h>
#include <SdFat.h>

#define MAX_FILES 256
#define MAX_PATH_LENGTH 128
#define MAX_FILENAME_LENGTH 64

struct FileEntry {
    char filename[MAX_FILENAME_LENGTH];
    char fullPath[MAX_PATH_LENGTH];
    bool isDirectory;
    uint32_t fileSize;
};

class FileBrowser {
public:
    FileBrowser();
    bool begin(SdFat* sd);
    void setRootPath(const char* path);
    bool scanCurrentDirectory();

    // Navigation
    void selectNext();
    void selectPrevious();
    void enterDirectory();
    void goUp();

    // Getters
    uint16_t getFileCount() { return fileCount; }
    uint16_t getCurrentIndex() { return currentIndex; }
    FileEntry* getCurrentFile();
    FileEntry* getFile(uint16_t index);
    const char* getCurrentPath() { return currentPath; }

    // File operations
    bool openFile(FatFile* file);

private:
    SdFat* sd;
    FileEntry files[MAX_FILES];
    uint16_t fileCount;
    uint16_t currentIndex;
    char currentPath[MAX_PATH_LENGTH];
    char rootPath[MAX_PATH_LENGTH];

    void sortFiles();
    bool isMidiFile(const char* filename);
};

#endif // FILE_BROWSER_H

#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <Arduino.h>
#include <SdFat.h>

#define MAX_FILES 1024
#define MAX_SUBFOLDERS 64
#define MAX_PATH_LENGTH 128
#define MAX_FILENAME_LENGTH 64

struct FileEntry {
    char filename[MAX_FILENAME_LENGTH];
    bool isDirectory;
    uint32_t fileSize;
};

class FileBrowser {
public:
    FileBrowser();
    bool begin(SdFat* sd);
    void setRootPath(const char* path);
    bool scanCurrentDirectory();

    // File navigation
    void selectNext();
    void selectPrevious();
    bool setCurrentIndex(uint16_t idx);

    // Subfolder navigation
    void scanSubfolders();
    void selectNextSubfolder();
    void selectPreviousSubfolder();
    const char* getCurrentSubfolderName();
    void navigateToCurrentSubfolder();
    uint16_t getSubfolderCount() { return subfolderCount; }
    uint16_t getCurrentSubfolderIndex() { return currentSubfolderIndex; }

    // Legacy directory navigation
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
    void getFullPath(const FileEntry* entry, char* outPath, size_t maxLen);

private:
    SdFat* sd;
    FileEntry files[MAX_FILES];
    uint16_t fileCount;
    uint16_t currentIndex;
    char currentPath[MAX_PATH_LENGTH];
    char rootPath[MAX_PATH_LENGTH];

    // Subfolder data
    char subfolderNames[MAX_SUBFOLDERS][MAX_FILENAME_LENGTH];
    uint16_t subfolderCount;
    uint16_t currentSubfolderIndex;

    void sortFiles();
    void sortSubfolders();
    bool isMidiFile(const char* filename);
    bool isSysexFile(const char* filename);
    bool isPlayableFile(const char* filename) {
        return isMidiFile(filename) || isSysexFile(filename);
    }
};

#endif // FILE_BROWSER_H

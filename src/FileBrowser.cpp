#include "FileBrowser.h"

FileBrowser::FileBrowser() {
    sd = nullptr;
    fileCount = 0;
    currentIndex = 0;
    subfolderCount = 0;
    currentSubfolderIndex = 0;
    strcpy(currentPath, "/");
    strcpy(rootPath, "/MIDI");
}

bool FileBrowser::begin(SdFat* sdCard) {
    sd = sdCard;
    if (!sd) return false;

    // Set initial path to root MIDI folder
    setRootPath("/MIDI");
    scanSubfolders();
    return scanCurrentDirectory();
}

void FileBrowser::setRootPath(const char* path) {
    strncpy(rootPath, path, MAX_PATH_LENGTH - 1);
    rootPath[MAX_PATH_LENGTH - 1] = '\0';
    strncpy(currentPath, rootPath, MAX_PATH_LENGTH - 1);
    currentPath[MAX_PATH_LENGTH - 1] = '\0';
}

bool FileBrowser::isMidiFile(const char* filename) {
    size_t len = strlen(filename);
    if (len < 4) return false;

    // Check for .mid extension
    if (len >= 4) {
        const char* ext = filename + len - 4;
        if (strcasecmp(ext, ".mid") == 0) {
            return true;
        }
    }

    // Check for .midi extension
    if (len >= 5) {
        const char* ext = filename + len - 5;
        if (strcasecmp(ext, ".midi") == 0) {
            return true;
        }
    }

    return false;
}

bool FileBrowser::scanCurrentDirectory() {
    if (!sd) return false;

    fileCount = 0;
    currentIndex = 0;

    FatFile dir;
    if (!dir.open(currentPath)) {
        return false;
    }

    FatFile file;
    while (file.openNext(&dir, O_RDONLY)) {
        if (fileCount >= MAX_FILES) {
            file.close();
            break;
        }

        FileEntry& entry = files[fileCount];

        // Get filename
        file.getName(entry.filename, MAX_FILENAME_LENGTH);

        // Check if directory
        entry.isDirectory = file.isDir();
        entry.fileSize = file.fileSize();

        // Skip hidden files and current directory marker
        if (entry.filename[0] == '.') {
            file.close();
            continue;
        }

        // Skip directories — subfolders are handled separately
        if (entry.isDirectory) {
            file.close();
            continue;
        }

        // Only add MIDI files
        if (isMidiFile(entry.filename)) {
            fileCount++;
        }

        file.close();
    }

    dir.close();

    // Sort files
    sortFiles();

    return true;
}

void FileBrowser::sortFiles() {
    // Simple bubble sort - alphabetically
    for (uint16_t i = 0; i < fileCount - 1; i++) {
        for (uint16_t j = 0; j < fileCount - i - 1; j++) {
            if (strcasecmp(files[j].filename, files[j + 1].filename) > 0) {
                FileEntry temp = files[j];
                files[j] = files[j + 1];
                files[j + 1] = temp;
            }
        }
    }
}

void FileBrowser::selectNext() {
    if (fileCount == 0) return;
    currentIndex = (currentIndex + 1) % fileCount;
}

void FileBrowser::selectPrevious() {
    if (fileCount == 0) return;
    if (currentIndex == 0) {
        currentIndex = fileCount - 1;
    } else {
        currentIndex--;
    }
}

void FileBrowser::enterDirectory() {
    if (fileCount == 0) return;

    FileEntry* current = getCurrentFile();
    if (!current || !current->isDirectory) return;

    // Build new path
    if (strcmp(currentPath, "/") == 0) {
        snprintf(currentPath, MAX_PATH_LENGTH, "/%s", current->filename);
    } else {
        snprintf(currentPath, MAX_PATH_LENGTH, "%s/%s",
                 currentPath, current->filename);
    }

    scanCurrentDirectory();
}

void FileBrowser::goUp() {
    // Don't go above root MIDI folder
    if (strcmp(currentPath, rootPath) == 0) {
        return;
    }

    // Find last slash
    char* lastSlash = strrchr(currentPath, '/');
    if (lastSlash && lastSlash != currentPath) {
        *lastSlash = '\0';
    } else {
        strcpy(currentPath, rootPath);
    }

    scanCurrentDirectory();
}

FileEntry* FileBrowser::getCurrentFile() {
    if (fileCount == 0 || currentIndex >= fileCount) return nullptr;
    return &files[currentIndex];
}

FileEntry* FileBrowser::getFile(uint16_t index) {
    if (index >= fileCount) return nullptr;
    return &files[index];
}

bool FileBrowser::openFile(FatFile* file) {
    FileEntry* current = getCurrentFile();
    if (!current || current->isDirectory) return false;

    char path[MAX_PATH_LENGTH];
    getFullPath(current, path, sizeof(path));
    return file->open(path, O_RDONLY);
}

void FileBrowser::getFullPath(const FileEntry* entry, char* outPath, size_t maxLen) {
    snprintf(outPath, maxLen, "%s/%s", currentPath, entry->filename);
}

void FileBrowser::scanSubfolders() {
    subfolderCount = 0;
    currentSubfolderIndex = 0;

    // Entry 0 is always root (files directly in /MIDI)
    strncpy(subfolderNames[0], "/", MAX_FILENAME_LENGTH - 1);
    subfolderNames[0][MAX_FILENAME_LENGTH - 1] = '\0';
    subfolderCount = 1;

    if (!sd) return;

    FatFile dir;
    if (!dir.open(rootPath)) return;

    FatFile file;
    while (file.openNext(&dir, O_RDONLY)) {
        if (subfolderCount >= MAX_SUBFOLDERS) {
            file.close();
            break;
        }

        if (!file.isDir()) {
            file.close();
            continue;
        }

        char name[MAX_FILENAME_LENGTH];
        file.getName(name, MAX_FILENAME_LENGTH);

        // Skip hidden directories and config
        if (name[0] == '.' || strcasecmp(name, "config") == 0) {
            file.close();
            continue;
        }

        strncpy(subfolderNames[subfolderCount], name, MAX_FILENAME_LENGTH - 1);
        subfolderNames[subfolderCount][MAX_FILENAME_LENGTH - 1] = '\0';
        subfolderCount++;

        file.close();
    }

    dir.close();

    // Sort subfolders alphabetically (skip entry 0 which is always "/")
    sortSubfolders();
}

void FileBrowser::sortSubfolders() {
    if (subfolderCount <= 2) return; // 0=root + at most 1 subfolder, nothing to sort

    for (uint16_t i = 1; i < subfolderCount - 1; i++) {
        for (uint16_t j = 1; j < subfolderCount - i; j++) {
            if (strcasecmp(subfolderNames[j], subfolderNames[j + 1]) > 0) {
                char temp[MAX_FILENAME_LENGTH];
                strncpy(temp, subfolderNames[j], MAX_FILENAME_LENGTH);
                strncpy(subfolderNames[j], subfolderNames[j + 1], MAX_FILENAME_LENGTH);
                strncpy(subfolderNames[j + 1], temp, MAX_FILENAME_LENGTH);
            }
        }
    }
}

void FileBrowser::selectNextSubfolder() {
    if (subfolderCount == 0) return;
    currentSubfolderIndex = (currentSubfolderIndex + 1) % subfolderCount;
    navigateToCurrentSubfolder();
}

void FileBrowser::selectPreviousSubfolder() {
    if (subfolderCount == 0) return;
    if (currentSubfolderIndex == 0) {
        currentSubfolderIndex = subfolderCount - 1;
    } else {
        currentSubfolderIndex--;
    }
    navigateToCurrentSubfolder();
}

const char* FileBrowser::getCurrentSubfolderName() {
    if (currentSubfolderIndex >= subfolderCount) return "/";
    return subfolderNames[currentSubfolderIndex];
}

void FileBrowser::navigateToCurrentSubfolder() {
    if (currentSubfolderIndex == 0) {
        // Root: files directly in /MIDI
        strncpy(currentPath, rootPath, MAX_PATH_LENGTH - 1);
        currentPath[MAX_PATH_LENGTH - 1] = '\0';
    } else {
        snprintf(currentPath, MAX_PATH_LENGTH, "%s/%s",
                 rootPath, subfolderNames[currentSubfolderIndex]);
    }
    scanCurrentDirectory();
}

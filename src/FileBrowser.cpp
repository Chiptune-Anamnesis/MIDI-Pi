#include "FileBrowser.h"

FileBrowser::FileBrowser() {
    sd = nullptr;
    fileCount = 0;
    currentIndex = 0;
    strcpy(currentPath, "/");
    strcpy(rootPath, "/MIDI");
}

bool FileBrowser::begin(SdFat* sdCard) {
    sd = sdCard;
    if (!sd) return false;

    // Set initial path to root MIDI folder
    setRootPath("/MIDI");
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

        // Build full path
        snprintf(entry.fullPath, MAX_PATH_LENGTH, "%s/%s",
                 currentPath, entry.filename);

        // Check if directory
        entry.isDirectory = file.isDir();
        entry.fileSize = file.fileSize();

        // Skip hidden files and current directory marker
        if (entry.filename[0] == '.') {
            file.close();
            continue;
        }

        // Skip config directory
        if (entry.isDirectory && strcasecmp(entry.filename, "config") == 0) {
            file.close();
            continue;
        }

        // Only add MIDI files or directories
        if (entry.isDirectory || isMidiFile(entry.filename)) {
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
    // Simple bubble sort - directories first, then alphabetically
    for (uint16_t i = 0; i < fileCount - 1; i++) {
        for (uint16_t j = 0; j < fileCount - i - 1; j++) {
            bool swap = false;

            // Directories come first
            if (!files[j].isDirectory && files[j + 1].isDirectory) {
                swap = true;
            } else if (files[j].isDirectory == files[j + 1].isDirectory) {
                // Same type, sort alphabetically
                if (strcasecmp(files[j].filename, files[j + 1].filename) > 0) {
                    swap = true;
                }
            }

            if (swap) {
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

    return file->open(current->fullPath, O_RDONLY);
}

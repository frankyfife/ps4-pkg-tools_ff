// Refactored to use shared library logic
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include "pkg_tool/lib.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

// Returns the command line arguments as paths. On Windows argv is limited to the
// ANSI code page, so read the UTF-16 command line instead to support any file name.
static std::vector<std::filesystem::path> GetArgs(int argc, char** argv) {
    std::vector<std::filesystem::path> args;
#ifdef _WIN32
    int wargc = 0;
    if (LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc)) {
        args.assign(wargv, wargv + wargc);
        LocalFree(wargv);
        return args;
    }
#endif
    args.assign(argv, argv + argc);
    return args;
}

static bool ProcessPkg(const std::filesystem::path& pkgPath, const std::filesystem::path& baseOutDir) {
    std::cout << "\nProcessing PKG: " << pkgPath.filename() << std::endl;
    PkgMetadata meta;
    if (auto err = ReadPkgMetadata(pkgPath, meta)) {
        std::cerr << "Failed to open PKG: " << *err << "\n";
        return false;
    }
    std::filesystem::path actualOutDir = (baseOutDir.filename() == meta.title_id)
                                             ? baseOutDir
                                             : baseOutDir / meta.title_id;
    std::cout << "Title ID: " << meta.title_id << "\n";
    std::cout << "PKG Size: " << meta.pkg_size << " bytes\n";
    std::cout << "Content Flags: " << meta.flags << "\n";
    std::cout << "Extracting to: " << actualOutDir << "\n";
    uint32_t last_report = 0;
    auto err = ExtractPkg(pkgPath, actualOutDir, {}, [&](uint32_t extracted, uint32_t total) {
        if (extracted == total || extracted - last_report >= 10) {
            std::cout << "Progress: " << extracted << " / " << total << " files\n";
            last_report = extracted;
        }
    });
    if (err) {
        std::cerr << "Extraction failed: " << *err << "\n";
        return false;
    }
    std::cout << "Extraction complete. Files extracted to: " << actualOutDir << "\n";
    return true;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // Print non-ASCII file names correctly in the console
    SetConsoleOutputCP(CP_UTF8);
#endif
    const auto args = GetArgs(argc, argv);
    argc = static_cast<int>(args.size());

    // Check for directory mode flag
    if (argc >= 3 && args[1] == "--dir") {
        std::filesystem::path sourceDir = args[2];
        std::filesystem::path outputBaseDir;
        
        // Use the source directory as the default output if no output directory is specified
        if (argc == 3) {
            outputBaseDir = sourceDir;
            std::cout << "No output directory specified. Using source directory: " << outputBaseDir << std::endl;
        } else {
            outputBaseDir = args[3];
        }
        
        if (!std::filesystem::exists(sourceDir) || !std::filesystem::is_directory(sourceDir)) {
            std::cerr << "Error: Source directory not found or not a directory: " << sourceDir << "\n";
            return 1;
        }
        
        // Find all PKG files in the directory
        auto pkgFiles = ListPkgFiles(sourceDir, true);
        std::cout << "Searching for PKG files in: " << sourceDir << " (this may take a moment)...\n";
        
        if (pkgFiles.empty()) {
            std::cout << "No PKG files found in the specified directory.\n";
            return 0;
        }
        
        std::cout << "Found " << pkgFiles.size() << " PKG files to process.\n";
        
        // Process each PKG file
        int successCount = 0;
        int failedCount = 0;
        
        for (size_t i = 0; i < pkgFiles.size(); i++) {
            const auto& pkgPath = pkgFiles[i];
            std::cout << "\n[" << (i + 1) << "/" << pkgFiles.size() << "] Processing " 
                      << pkgPath.filename() << "...\n";
            
            if (ProcessPkg(pkgPath, outputBaseDir)) {
                std::cout << "Successfully processed " << pkgPath.filename() << "\n";
                successCount++;
            } else {
                std::cout << "Failed to process " << pkgPath.filename() << "\n";
                failedCount++;
            }
        }
        
        std::cout << "\nBatch processing complete: " << successCount << " successful, " 
                  << failedCount << " failed.\n";
        
        return failedCount > 0 ? 1 : 0;
    } 
    // Standard single file processing mode
    else if (argc >= 2 && argc <= 3) {
        std::filesystem::path pkgPath = args[1];
        std::filesystem::path outDir;
        
        // Handle help flags
        if (pkgPath == "-h" || pkgPath == "--help") {
            std::cerr << "Usage: ps4-pkg-tool <path/to/pkg> [path/to/output]\n";
            std::cerr << "   OR: ps4-pkg-tool --dir <directory/with/pkgs> [path/to/output]\n";
            std::cerr << "       If output path is omitted, the PKG will be extracted to its parent directory\n";
            return 0;
        }
        
        // Use the PKG's parent directory as the default output if no output directory is specified
        if (argc == 2) {
            outDir = pkgPath.parent_path();
            std::cout << "No output directory specified. Using PKG parent directory: " << outDir << std::endl;
        } else {
            outDir = args[2];
        }
        
        return ProcessPkg(pkgPath, outDir) ? 0 : 1;
    }
    // Invalid arguments
    else {
        std::cerr << "Usage: ps4-pkg-tool <path/to/pkg> [path/to/output]\n";
        std::cerr << "   OR: ps4-pkg-tool --dir <directory/with/pkgs> [path/to/output]\n";
        std::cerr << "       If output path is omitted, the PKG will be extracted to its parent directory\n";
        return 1;
    }
}

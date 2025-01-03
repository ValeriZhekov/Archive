#include <fstream>
#include "json.hpp"
#include "zlib.h"
#include <iostream>
#include <filesystem>
#include <openssl/sha.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdio>
std::vector<char> compressData(const std::vector<char> &data)
{
    uLong compressedSize = compressBound(data.size()); // estimates the maximum size of the compressed file
    std::vector<char> compressedData(compressedSize);

    if (compress(reinterpret_cast<Byte *>(compressedData.data()), &compressedSize,
                 reinterpret_cast<const Byte *>(data.data()), data.size()) != Z_OK) // returns Z_OK if success
    {
        throw std::runtime_error("Compression failed");
    }

    compressedData.resize(compressedSize);
    return compressedData;
}

std::vector<char> decompressData(const std::vector<char> &compressedData, uLong originalSize)
{
    std::vector<char> decompressedData(originalSize);

    if (uncompress(reinterpret_cast<Byte *>(decompressedData.data()), &originalSize,
                   reinterpret_cast<const Byte *>(compressedData.data()), compressedData.size()) != Z_OK)
    {
        throw std::runtime_error("Decompression failed");
    }

    return decompressedData;
}
std::string computeHash(const std::vector<char> &data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), hash);

    char buffer[2 * SHA256_DIGEST_LENGTH + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        snprintf(buffer + (i * 2), 3, "%02x", hash[i]);
    }
    return std::string(buffer);
}
class Storage
{
    struct FileEntry
    {
        std::string hash;
        uLong originalSize;
        uLong compressedSize;
        FileEntry(std::string _hash, uLong _originalSize, uLong _compressedSize)
            : hash(_hash), originalSize(_originalSize), compressedSize(_compressedSize) {}
        FileEntry() : hash(""), originalSize(0), compressedSize(0) {}
    };

    std::unordered_map<std::string, FileEntry> fileTable; // Metadata table
    std::string dataDirectory = "data";                   // directory with compressed files

public:
    Storage()
    {
        std::filesystem::create_directory(dataDirectory); // ensure data directory exists
    }

    bool addFile(const std::string &hash, const std::vector<char> &content) // adds compressed file to archive and stores metaData
    {
        if (fileTable.find(hash) != fileTable.end())
        {
            return false; // file already exists
        }

        std::vector<char> compressedContent = compressData(content);

        std::ofstream outFile(dataDirectory + "/" + hash, std::ios::binary); // output file stream in binary mode
        outFile.write(compressedContent.data(), compressedContent.size());
        outFile.close();

        fileTable[hash] = FileEntry(hash, content.size(), compressedContent.size());
        return true;
    }

    std::vector<char> loadFile(const std::string &hash) // loads orignal file content from archive
    {
        if (fileTable.find(hash) == fileTable.end())
        {
            throw std::runtime_error("File not found in storage");
        }

        std::ifstream inFile(dataDirectory + "/" + hash, std::ios::binary); // input file stream in binary mode
        std::vector<char> compressedContent((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        // vec constructor with iterators, default one is file end
        return decompressData(compressedContent, fileTable[hash].originalSize);
    }

    void saveToFile(const std::string &filename) // saves all metadata to disk
    {
        std::ofstream file(filename, std::ios::binary);
        nlohmann::json json; // default json class

        for (const auto &[hash, entry] : fileTable)
        {
            json[hash] = {{"originalSize", entry.originalSize}, {"compressedSize", entry.compressedSize}};
        }

        file << json.dump(4); // converts class to json format with pretty-print with 4 spaces
        file.close();
    }

    void loadFromFile(const std::string &filename) // load metadata from disk
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
            return;

        nlohmann::json json;
        file >> json;

        for (auto &[hash, entry] : json.items())
        {
            fileTable[hash] = FileEntry(hash, entry["originalSize"].get<uLong>(), entry["compressedSize"].get<uLong>());
        }

        file.close();
    }
    bool fileExists(const std::string &hash) const
    {
        return fileTable.find(hash) != fileTable.end();
    }
};

class ArchiveManager
{
private:
    Storage &storage;           // reference to storage
    nlohmann::json archiveData; // metadata for all archives
    std::string metadataFile = "archivesMetaData.json";

public:
    ArchiveManager(Storage &_storage) : storage(_storage)
    {
        loadMetadata();
    }

    ~ArchiveManager()
    {
        saveMetadata();
    }

    void createArchive(const std::string &archiveName, const std::vector<std::string> &directories, bool hashOnly)
    {
        if (archiveData.contains(archiveName))
        {
            throw std::runtime_error("Archive with this name already exists");
        }

        nlohmann::json archiveContents;

        for (const auto &dir : directories)
        { // go through all directories
            for (const auto &entry : std::filesystem::recursive_directory_iterator(dir))
            { // all files in directory
                if (!entry.is_regular_file())
                    continue; // skip nonfiles

                std::ifstream file(entry.path(), std::ios::binary);
                std::vector<char> content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()); // iterator constructor
                file.close();

                std::string hash = computeHash(content);
                if (hashOnly || !storage.fileExists(hash))
                {
                    storage.addFile(hash, content);
                }
                else
                {
                    std::vector<char> toCheck = storage.loadFile(hash);
                    if (toCheck != content)
                    {
                        throw std::runtime_error("Same hash diffrent file");
                    }
                }
                archiveContents[std::filesystem::relative(entry.path(), dir).string()] = hash; // realtive path of file in directory : file hash,
                                                                                               //  which is the name of the compressed file in data
            }
        }

        archiveData[archiveName] = archiveContents; // archive name : [file relative paths : hash]
    }

    void extractArchive(const std::string &archiveName, const std::string &targetPath, const std::vector<std::string> &paths = {})
    {
        if (!archiveData.contains(archiveName))
        {
            throw std::runtime_error("Archive not found");
        }

        const auto &archiveContents = archiveData[archiveName]; // get the archive we need

        std::vector<std::string> filesToExtract;
        if (paths.empty())
        { // no argument of relative paths passed, we add all the elements,else only the specified ones
            for (const auto &[relativePath, _] : archiveContents.items())
            {
                filesToExtract.push_back(relativePath);
            }
        }
        else
        {
            filesToExtract = paths;
        }

        for (const auto &relativePath : filesToExtract)
        {
            if (!archiveContents.contains(relativePath))
            {
                throw std::runtime_error("File path not found in archive: " + relativePath);
            }

            std::string hash = archiveContents[relativePath];   // get the hash with relative path
            std::vector<char> content = storage.loadFile(hash); // we decompress the file with this hash

            // write the file at the correct relative path from the target path
            std::filesystem::path outputPath = std::filesystem::path(targetPath) / relativePath;
            // get the path to the file and creates the directories holding it
            std::filesystem::create_directories(outputPath.parent_path());
            // create it
            std::ofstream outFile(outputPath, std::ios::binary);
            outFile.write(content.data(), content.size());
            outFile.close();
        }
    }

    void saveMetadata()
    {
        std::ofstream file(metadataFile, std::ios::binary);
        file << archiveData.dump(4);
        file.close();
    }

    void loadMetadata()
    {
        std::ifstream file(metadataFile, std::ios::binary);
        if (file.is_open())
        {
            file >> archiveData;
        }
        file.close();
    }
};

int main(int argc, char *argv[])
{   
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: backup.exe <command> [<args>]\n";
            return 1;
        }

        std::string command = argv[1], storageData = "metaData.json";
        Storage storage;
        storage.loadFromFile(storageData);
        ArchiveManager archiveManager(storage);
        if (command == "create")
        {
            if (argc < 4)
            {
                std::cerr << "Usage: backup.exe create [hash-only] <name> <directory>+\n";
                return 1;
            }

            bool hashOnly = false;
            int nameIndex = 2;

            if (std::string(argv[2]) == "hash-only")
            {
                hashOnly = true;
                nameIndex = 3;
            }

            std::string archiveName = argv[nameIndex];
            std::vector<std::string> directories;
            for (int i = nameIndex + 1; i < argc; ++i)
            {
                directories.push_back(argv[i]);
            }
            archiveManager.createArchive(archiveName, directories, hashOnly);
            std::cout << "Archive '" << archiveName << "' created successfully.\n";
        }
        else if (command == "extract")
        {
            if (argc < 4)
            {
                std::cerr << "Usage: backup.exe extract <name> <target-path> [<archive-path>*]\n";
                return 1;
            }

            std::string archiveName = argv[2];
            std::string targetPath = argv[3];
            std::vector<std::string> paths;

            for (int i = 4; i < argc; ++i)
            {
                paths.push_back(argv[i]);
            }

            archiveManager.extractArchive(archiveName, targetPath, paths);
            std::cout << "Archive '" << archiveName << "' extracted to '" << targetPath << "' successfully.\n";
        }
        storage.saveToFile(storageData);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
#include <fstream>
#include "json.hpp"
#include <zlib.h>
#include <iostream>
#include <filesystem>
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
class Storage
{
    struct FileEntry
    {
        std::string hash;
        uLong originalSize;
        uLong compressedSize;
        FileEntry(std::string _hash, uLong _originalSize, uLong _compressedSize)
            : hash(_hash), originalSize(_originalSize), compressedSize(_compressedSize) {}
    };

    std::unordered_map<std::string, FileEntry> fileTable; // Metadata table
    std::string dataDirectory = "data";                   // directory with compressed files

public:
    Storage()
    {
        std::filesystem::create_directory(dataDirectory); // ensure data directory exists
    }

    bool addFile(const std::string &hash, const std::vector<char> &content) //adds compressed file to archive and stores metaData
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

    
    std::vector<char> loadFile(const std::string &hash) //loads orignal file content from archive
    {
        if (fileTable.find(hash) == fileTable.end())
        {
            throw std::runtime_error("File not found in storage");
        }

        std::ifstream inFile(dataDirectory + "/" + hash, std::ios::binary); //input file stream in binary mode
        std::vector<char> compressedContent((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        //vec constructor with iterators, default one is file end
        return decompressData(compressedContent, fileTable[hash].originalSize); 
    }

    
    void saveToFile(const std::string &filename) // saves all metadata to disk
    {
        std::ofstream file(filename, std::ios::binary);
        nlohmann::json json; //default json class

        for (const auto &[hash, entry] : fileTable)
        {
            json[hash] = {{"originalSize", entry.originalSize}, {"compressedSize", entry.compressedSize}};
        }

        file << json.dump(4); //converts class to json format with pretty-print with 4 spaces
        file.close();
    }

    void loadFromFile(const std::string &filename) //load metadata from disk
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
            return;

        nlohmann::json json;
        file >> json;

        for (auto &[hash, entry] : json.items())
        {
            fileTable[hash] = {hash, entry["originalSize"].get<uLong>(), entry["compressedSize"].get<uLong>()};
        }

        file.close();
    }
};
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Correct syntax: backup.exe create hash-only? <name> <directory>" << std::endl;
        return 1;
    }

    std::string command = argv[1];
    Storage storage;

    // Load the storage metadata from disk
    storage.loadFromFile("archive_storage.json");

    if (command == "create")
    {
        bool hashOnly = false;
        int argIndex = 2;

        // Check for the optional argument hash-only
        if (std::string(argv[argIndex]) == "hash-only")
        {
            hashOnly = true;
            ++argIndex;
        }

        if (argc < argIndex + 2)
        {
            std::cerr << "Usage: backup.exe create hash-only? <name> <directory>+\n";
            return 1;
        }

        std::string archiveName = argv[argIndex++];
        std::vector<std::filesystem::path> directories;

        for (int i = argIndex; i < argc; ++i)
        {
            directories.emplace_back(argv[i]);
        }

        // Create the archive
        createArchive(archiveName, directories, hashOnly, storage);

        // Save the updated storage metadata to disk
        storage.saveToFile("archive_storage.json");
    }
    else
    {
        std::cerr << "Unknown command: " << command << '\n';
        return 1;
    }

    return 0;
}
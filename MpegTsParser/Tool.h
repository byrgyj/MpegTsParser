#pragma once
#include <string>
#include <vector>
namespace GYJ {
   
    std::string getFileNameFromPath(const std::string &filePath);
    std::string regulateFilePath(std::string &filePath);

    void listFiles(const char *dir, std::vector<std::string> &localFiles);
}



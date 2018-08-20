#pragma once
#include <string>
#include <vector>
#include <stdint.h>
namespace QIYI {
   
    std::string getFileNameFromPath(const std::string &filePath);
    std::string regulateFilePath(std::string &filePath);

    void listFiles(const char *dir, std::vector<std::string> &localFiles);

    uint8_t av_rb8(const unsigned char *data);
    uint16_t av_rb16(const unsigned char *data);
    uint32_t av_rd32(const unsigned char *data);
}



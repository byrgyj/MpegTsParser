#include "StdAfx.h"
#include "Tool.h"
#include <io.h>

namespace QIYI {

    std::string getFileNameFromPath(const std::string &filePath) {
        if (filePath.empty()){
            return "";
        }

        int posBegin = filePath.find_last_of("/");
        if (posBegin == std::string::npos) {
            if (posBegin == std::string::npos) {
                return "";
            }
        }

        int posEnd = filePath.find_last_of(".");
        if (posEnd != std::string::npos) {
            return filePath.substr(posBegin + 1, posEnd - posBegin - 1);
        }

        return "";
    }

    std::string regulateFilePath(std::string &filePath) {
        if (filePath.empty()) {
            return "";
        }

        for (int i = 0; i < filePath.size(); i++) {
            if (filePath[i] == '\\') {
                filePath[i] = '/';
            }
        }

        if (filePath[filePath.size() - 1] != '/') {
            filePath.append("/");
        }
        return filePath;
    }



    void listFiles(const char *dir, std::vector<std::string> &localFiles)
    {
        intptr_t handle;
        _finddata_t findData;

        handle = _findfirst(dir, &findData);
        if (handle == -1){
            return;
        }

        do{
            if (findData.attrib & _A_SUBDIR || strcmp(findData.name, ".") == 0 || strcmp(findData.name, "..") == 0 ){ 
                continue;
            } else {
                if (strstr(findData.name, ".ts") != NULL || strstr(findData.name, ".dbts") != NULL || strstr(findData.name, ".265ts") != NULL || strstr(findData.name, ".bbts") != NULL) {
                    localFiles.push_back(findData.name);
                }

            }
        } while (_findnext(handle, &findData) == 0);

        _findclose(handle); 
    }

    uint8_t av_rb8(const unsigned char *data) {
        uint8_t v = *(uint8_t*)data;
        return v;
    }

    uint16_t av_rb16(const unsigned char *data) {
        uint16_t v = av_rb8(data) << 8;
        v |= av_rb8(data+1);
        return v;
    }

    uint32_t av_rd32(const unsigned char *data) {
        uint32_t v = av_rb16(data) << 16;
        v |= av_rb16(data + 2);
        return v;
    }
}
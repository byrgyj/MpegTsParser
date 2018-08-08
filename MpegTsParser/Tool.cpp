#include "StdAfx.h"
#include "Tool.h"
#include <io.h>

namespace GYJ {

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
}
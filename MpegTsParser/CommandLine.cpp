#include "StdAfx.h"
#include "CommandLine.h"
#include "Tool.h"
#include <io.h>

namespace QIYI {

CommandLine *CommandLine::sInstance = NULL;


CommandLine::CommandLine(void){
}

CommandLine::~CommandLine(void){
}

CommandLine *CommandLine::getInstance() {
    if (sInstance == NULL) {
        sInstance = new CommandLine;
    }

    return sInstance;
}

void CommandLine::setCommandLineParam(CommandLineParam param) {
    mParam = param;
}

int CommandLine::parseCommand(int argc, char *argv[]) {
    int i = 0;

    while (++i < argc) {
        if (strcmp(argv[i], "--channel") == 0 && ++i < argc){
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0){
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--print_video") == 0) {
            mParam.printMediaType = PRINT_MEDIA_VIDEO;
        } else if (strcmp(argv[i], "--print_audio") == 0) {
            mParam.printMediaType = PRINT_MEDIA_AUDIO;
        } else if (strcmp(argv[i], "--print_all") == 0) {
            mParam.printMediaType = PRINT_MEDIA_ALL;
        } else if (strcmp(argv[i], "--all_pts") == 0) {
            mParam.printPtsType = PRINT_ALL_PTS;
        } else if (strcmp(argv[i], "--partly_pts") == 0) {
            mParam.printPtsType = PRINT_PARTLY_PTS;
        } else if (strcmp(argv[i], "--ts_folder_path") == 0 && ++i < argc) {
            mParam.filePath = argv[i];
            mParam.filePath = regulateFilePath(mParam.filePath);
        } else if (strcmp(argv[i], "--check_buffer_out") == 0){
            mParam.checkPacketBufferOut = 1;
        } else if (strcmp(argv[i], "--print_pcr") == 0) {
            mParam.printPcr = 1;
        } else {
            std::string file((char*)argv[i]);
            if (access(file.c_str(), 0) == 0){
                mLocalFiles.push_back(file);
            }
            // test
        }
    }


    if (!mParam.filePath.empty()) {
        std::string destLocaltion = mParam.filePath + "*.*";
        listFiles(destLocaltion.c_str(), mLocalFiles);
    }

    return 0;
}

void CommandLine::usage(const char* cmd){
    printf("Usage: %s [options] <file> \n\n"
        "  --print_video      only print video media info\n"
        "  --print_audio      only print audio media info\n"
        "  --print_all        print video and audio media info \n"
        "  --all_pts          print all pts info \n"
        "  --partly_pts       print partly pts \n"
        "  --ts_folder_path   files location \n"
        "  --print_pcr        print pcr info \n"
        "  --check_buffer_out  checkout distance between the pts and dts info in one frame \n"
        "  -h, --help         print this help\n"
        "\n", cmd
        );
}

}
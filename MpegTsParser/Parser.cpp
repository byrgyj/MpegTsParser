#define __STDC_FORMAT_MACROS 1
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string>
#include <inttypes.h>

#include "debug.h"
#include "ParserdDataContainer.h"
#include "TsLayer.h"
#include "CommandLine.h"
#include "Tool.h"

static void usage(const char* cmd)
{
  printf("Usage: %s [options] <file> | -\n\n"
        "  Enter '-' instead a file name will process stream from standard input\n\n"
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

FILE *g_logFile = NULL;
void  LogOut(int level, char *log) {
    if (log != NULL && level == DEMUX_DBG_INFO) {
       std::string str(log);
       fwrite(log, 1, str.size(), g_logFile);
    }
}

using namespace QIYI;
int main(int argc, char* argv[])
{
  const char* filename = NULL;
  uint16_t channel = 0;
  int i = 0;

  CommandLineParam cmdLine;
  std::string videoFileLocation;
  std::vector<std::string> localFiles;

  while (++i < argc) {
    if (strcmp(argv[i], "--channel") == 0 && ++i < argc){
      channel = atoi(argv[i]);
      fprintf(stderr, "channel=%d, ",channel);
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0){
      usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--print_video") == 0) {
        cmdLine.printMediaType = PRINT_MEDIA_VIDEO;
    } else if (strcmp(argv[i], "--print_audio") == 0) {
        cmdLine.printMediaType = PRINT_MEDIA_AUDIO;
    } else if (strcmp(argv[i], "--print_all") == 0) {
        cmdLine.printMediaType = PRINT_MEDIA_ALL;
    } else if (strcmp(argv[i], "--all_pts") == 0) {
        cmdLine.printPtsType = PRINT_ALL_PTS;
    } else if (strcmp(argv[i], "--partly_pts") == 0) {
        cmdLine.printPtsType = PRINT_PARTLY_PTS;
    } else if (strcmp(argv[i], "--ts_folder_path") == 0 && ++i < argc) {
        cmdLine.filePath = argv[i];
        cmdLine.filePath = regulateFilePath(cmdLine.filePath);
    } else if (strcmp(argv[i], "--check_buffer_out") == 0){
        cmdLine.checkPacketBufferOut = 1;
    } else if (strcmp(argv[i], "--print_pcr") == 0) {
        cmdLine.printPcr = 1;
    } else {
      localFiles.push_back(argv[i]);
    }
  }

  std::string path = "D:\\data\\8.6\\test";
  cmdLine.filePath = regulateFilePath(path);

  if (localFiles.empty() && cmdLine.filePath.empty()) {
      printf("should specify ts files \n");
      return 0;
  }

  QIYI::DBGLevel(DEMUX_DBG_INFO);
  CommandLine::getInstance()->setCommandLineParam(cmdLine);

  if (!cmdLine.filePath.empty()) {
      std::string destLocaltion = cmdLine.filePath + "*.*";
      listFiles(destLocaltion.c_str(), localFiles);
  }

  if (localFiles.empty()) {
      printf("cannot find any ts files!");
      return 0;
  }

  ParseredDataContainer dataContainer(printParam(cmdLine.printMediaType, cmdLine.printPtsType));
  std::string logFile = "TsParserInfo.log";


  g_logFile = fopen(logFile.c_str(), "w");
  if (g_logFile != NULL) {
      QIYI::SetDBGMsgCallback(LogOut);
  }

  if (!localFiles.empty()){
      int index = 0;
    for (std::vector<std::string>::iterator it = localFiles.begin(); it != localFiles.end(); it++) {
        std::string curFile = cmdLine.filePath + *it;

        FILE* file = NULL;
        if (strcmp(curFile.c_str(), "-") == 0){
            file = stdin;
            filename = "[stdin]";
        } else {
            file = fopen(curFile.c_str(), "rb");
        }

        if (file){
            TsLayer* demux = new TsLayer(file, channel, 0);
            if (demux != NULL) {
                demux->doDemux();
                std::list<QIYI::STREAM_PKT*> *lst = demux->getParseredData();
                tsParam *param = new tsParam(*it, demux->getTsStartTimeStamp(), lst);
                if (param != NULL) {
                    dataContainer.addData(param->tsStartTime, param);
                }

                delete demux;
            }
            
            fclose(file);
        }
        else{
            printf("cannot open file: '%s'\n", curFile.c_str());
        }

        index++;
    }

    dataContainer.printInfo();
  }
  else {
    printf("no file specified \n");
    usage(argv[0]);
  }

  if (g_logFile != NULL) {
      fclose(g_logFile);
  }
  return 0;
}


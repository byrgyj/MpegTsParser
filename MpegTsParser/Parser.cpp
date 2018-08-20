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

using namespace QIYI;
FILE *g_logFile = NULL;
void  LogOut(int level, char *log) {
    if (log != NULL && level == DEMUX_DBG_INFO) {
       std::string str(log);
       fwrite(log, 1, str.size(), g_logFile);
    }
}

int main(int argc, char* argv[])
{
  CommandLine::getInstance()->parseCommand(argc, argv);
  CommandLineParam cmdLine = CommandLine::getInstance()->getCommandLineParam();

  if (CommandLine::getInstance()->getLocalFiles().empty() && cmdLine.filePath.empty()) {
      printf("should input ts files \n");
      return 0;
  }

  QIYI::DBGLevel(DEMUX_DBG_INFO); 
  std::string logFile = "TsParserInfo.log";
  g_logFile = fopen(logFile.c_str(), "w");
  if (g_logFile != NULL) {
      QIYI::SetDBGMsgCallback(LogOut);
  }

  ParseredDataContainer dataContainer(printParam(cmdLine.printMediaType, cmdLine.printPtsType));
  std::vector<std::string> localFiles = CommandLine::getInstance()->getLocalFiles();

  if (!localFiles.empty()){
    int index = 0;
    for (std::vector<std::string>::iterator it = localFiles.begin(); it != localFiles.end(); it++) {
        std::string curFile = cmdLine.filePath + *it;
        if (!curFile.empty()){
            TsLayer* demux = new TsLayer(curFile, 0);
            if (demux != NULL) {
                demux->doDemux();
                std::list<QIYI::STREAM_PKT*> *lst = demux->getParseredData();
                tsParam *param = new tsParam(*it, demux->getTsStartTimeStamp(), lst);
                if (param != NULL) {
                    dataContainer.addData(param->tsStartTime, param);
                }

                delete demux;
            }
        }

        index++;
    }
    dataContainer.printInfo();

  } else {
    printf("no file specified \n");
  }

  if (g_logFile != NULL) {
      fclose(g_logFile);
  }
  return 0;
}


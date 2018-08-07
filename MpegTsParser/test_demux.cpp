/*
 *      Copyright (C) 2013 Jean-Luc Barriere
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#define __STDC_FORMAT_MACROS 1
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string>
#include <inttypes.h>

#include "debug.h"
#include <io.h>
#include "ParserdDataContainer.h"
#include "TsLayer.h"

#define LOGTAG  "[DEMUX] "

int g_loglevel = DEMUX_DBG_DEBUG;
int g_parseonly = 1;
bool g_printVideoPts = false;
bool g_printAudioPts = false;

static void usage(const char* cmd)
{
  fprintf(stderr,
        "Usage: %s [options] <file> | -\n\n"
        "  Enter '-' instead a file name will process stream from standard input\n\n"
        "  --debug            enable debug output\n"
        "  --parseonly        only parse streams\n"
        "  --channel <id>     process channel <id>. Default 0 for all channels\n"
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

void listFiles(const char *dir, std::vector<std::string> &localFiles);

int main(int argc, char* argv[])
{
  const char* filename = NULL;
  uint16_t channel = 0;
  int i = 0;

  TSDemux::DBGLevel(DEMUX_DBG_INFO);

  std::vector<std::string> localFiles;

  //std::string videoLocaltion = "D:/MyProg/MpegTsParser/MpegTsParser/video/";
  std::string videoLocaltion = "D:/data/8.6/ts1/";
  std::string destLocaltion = videoLocaltion + "*.*";
  listFiles(destLocaltion.c_str(), localFiles);

  g_printAudioPts = true;
  g_printVideoPts = false;

  while (++i < argc)
  {
    if (strcmp(argv[i], "--debug") == 0)
    {
      g_loglevel = DEMUX_DBG_DEBUG;
      fprintf(stderr, "debug=Yes, ");
    }
    else if (strcmp(argv[i], "--parseonly") == 0)
    {
      g_parseonly = 1;
      fprintf(stderr, "parseonly=Yes, ");
    }
    else if (strcmp(argv[i], "--channel") == 0 && ++i < argc)
    {
      channel = atoi(argv[i]);
      fprintf(stderr, "channel=%d, ",channel);
    }
    else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
    {
      usage(argv[0]);
      return 0;
    }
    else if (strcmp(argv[i], "--print_video") == 0) {

    }else if (strcmp(argv[i], "--print_audio") == 0) {

    }else if (strcmp(argv[i], "--print_all") == 0) {

    } else if (strcmp(argv[i], "--all_pts") == 0) {

    } else if (strcmp(argv[i], "--partly_pts") == 0) {

    }
    else {
      //filename = argv[i];
      //vecLocalFiles.push_back(argv[i]);
    }
  }

  GYJ::ParseredDataContainer dataContainer(GYJ::printParam(GYJ::PRINT_MEDIA_AUDIO, GYJ::PRINT_PARTLY_PTS));
  std::string logFile = "debug_info.log";


  g_logFile = fopen(logFile.c_str(), "w");
  if (g_logFile != NULL) {
      TSDemux::SetDBGMsgCallback(LogOut);
  }

  if (!localFiles.empty()){
      int index = 0;
    for (std::vector<std::string>::iterator it = localFiles.begin(); it != localFiles.end(); it++) {
        std::string curFile = videoLocaltion + *it;

        FILE* file = NULL;
        if (strcmp(curFile.c_str(), "-") == 0){
            file = stdin;
            filename = "[stdin]";
        } else {
            file = fopen(curFile.c_str(), "rb");
        }

        if (file){
            fprintf(stderr, "## Processing TS stream from %s ##\n", curFile.c_str());
            TsLayer* demux = new TsLayer(file, channel, 0);
            demux->doDemux();
            std::list<TSDemux::STREAM_PKT*> *lst = demux->getParseredData();
            GYJ::tsParam *param = new GYJ::tsParam(*it, demux->getTsStartTimeStamp(), lst);
            if (param != NULL) {
                dataContainer.addData(param->tsStartTime, param);
            }
            
            delete demux;
            fclose(file);
        }
        else{
            fprintf(stderr,"Cannot open file '%s' for read\n", curFile.c_str());
        }
    }

    dataContainer.printInfo();
  }
  else {
    fprintf(stderr, "No file specified\n\n");
    usage(argv[0]);
  }

  if (g_logFile != NULL) {
      fclose(g_logFile);
  }
  return 0;
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
        if (findData.attrib & _A_SUBDIR
            || strcmp(findData.name, ".") == 0
            || strcmp(findData.name, "..") == 0
            ){ 
             continue;
        }else{
            localFiles.push_back(findData.name);
        }
    } while (_findnext(handle, &findData) == 0);

    _findclose(handle); 
}
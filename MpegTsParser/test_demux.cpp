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

#include "test_demux.h"
#include "debug.h"
#include <io.h>
#include "ParserdDataContainer.h"

#define LOGTAG  "[DEMUX] "

int g_loglevel = DEMUX_DBG_DEBUG;
int g_parseonly = 0;
bool g_printVideoPts = false;
bool g_printAudioPts = false;

Demux::Demux(FILE* file, uint16_t channel, int fileIndex)
: m_channel(channel), mFileIndex(fileIndex)
{
  m_ifile = file;
  m_av_buf_size = AV_BUFFER_SIZE;
  m_av_buf = (unsigned char*)malloc(sizeof(*m_av_buf) * (m_av_buf_size + 1));
  if (m_av_buf){
    m_av_pos = 0;
    m_av_rbs = m_av_buf;
    m_av_rbe = m_av_buf;
    m_channel = channel;

    TSDemux::DBGLevel(g_loglevel);

    m_videoPID = 0xffff;
    m_audioPID = 0xffff;
    m_DTS = PTS_UNSET;
    m_PTS = PTS_UNSET;
    m_pinTime = m_curTime = m_endTime = 0;
    m_AVContext = new TSDemux::AVContext(this, 0, m_channel, fileIndex);
  }
  else
  {
    printf(LOGTAG "alloc AV buffer failed\n");
  }
}


Demux::~Demux()
{
  for (std::map<uint16_t, FILE*>::iterator it = m_outfiles.begin(); it != m_outfiles.end(); ++it)
    if (it->second)
      fclose(it->second);

  // Free AV context
  if (m_AVContext)
    delete m_AVContext;
  // Free AV buffer
  if (m_av_buf)
  {
    printf(LOGTAG "free AV buffer: allocated size was %zu\n", m_av_buf_size);
    free(m_av_buf);
    m_av_buf = NULL;
  }
}

const unsigned char* Demux::ReadAV(uint64_t pos, size_t n)
{
  // out of range
  if (n > m_av_buf_size)
    return NULL;

  // Already read ?
  size_t sz = m_av_rbe - m_av_buf;
  if (pos < m_av_pos || pos > (m_av_pos + sz))
  {
    // seek and reset buffer

    int ret = fseek(m_ifile, (int64_t)pos, SEEK_SET);
    if (ret != 0)
      return NULL;
    m_av_pos = (uint64_t)pos;
    m_av_rbs = m_av_rbe = m_av_buf;
  }
  else
  {
    // move to the desired pos in buffer
    m_av_rbs = m_av_buf + (size_t)(pos - m_av_pos);
  }

  size_t dataread = m_av_rbe - m_av_rbs;
  if (dataread >= n)
    return m_av_rbs;

  memmove(m_av_buf, m_av_rbs, dataread);
  m_av_rbs = m_av_buf;
  m_av_rbe = m_av_rbs + dataread;
  m_av_pos = pos;
  unsigned int len = (unsigned int)(m_av_buf_size - dataread);

  while (len > 0)
  {
    size_t c = fread(m_av_rbe, sizeof(*m_av_buf), len, m_ifile);
    if (c > 0)
    {
      m_av_rbe += c;
      dataread += c;
      len -= c;
    }
    if (dataread >= n || c == 0)
      break;
  }
  return dataread >= n ? m_av_rbs : NULL;
}

int Demux::Do()
{
  int ret = 0;
  int indexCount = 0;

  while (true)
  {
    {
      ret = m_AVContext->TSResync();
    }
    if (ret != TSDemux::AVCONTEXT_CONTINUE){
      break;
    }

    ret = m_AVContext->ProcessTSPacket();

    indexCount++;

    if (m_AVContext->HasPIDStreamData())
    {
      
      TSDemux::STREAM_PKT pkt;
      while (get_stream_data(&pkt)){
        //if (pkt->streamChange)
          //show_stream_info(pkt->pid);
        //write_stream_data(pkt)
      }
     
    }
    if (m_AVContext->HasPIDPayload())
    {
      ret = m_AVContext->ProcessTSPayload();
      if (ret == TSDemux::AVCONTEXT_PROGRAM_CHANGE)
      {
        register_pmt();
        std::vector<TSDemux::ElementaryStream*> streams = m_AVContext->GetStreams();
        for (std::vector<TSDemux::ElementaryStream*>::const_iterator it = streams.begin(); it != streams.end(); ++it)
        {
          //if ((*it)->has_stream_info)
            //show_stream_info((*it)->pid);
        }
      }
    }

    if (ret < 0)
      printf(LOGTAG "%s: error %d\n", __FUNCTION__, ret);

    if (ret == TSDemux::AVCONTEXT_TS_ERROR)
      m_AVContext->Shift();
    else
      m_AVContext->GoNext();
  }

  printf(LOGTAG "## %d: no sync, %d: eof, %d: ts error ##\n",
         TSDemux::AVCONTEXT_TS_NOSYNC,
         TSDemux::AVCONTEXT_IO_ERROR,
         TSDemux::AVCONTEXT_TS_ERROR);
  printf(LOGTAG "%s: stopped with status %d\n", __FUNCTION__, ret);
  return ret;
}

bool Demux::get_stream_data(TSDemux::STREAM_PKT* pkt)
{
  TSDemux::ElementaryStream* es = m_AVContext->GetPIDStream();
  if (!es)
    return false;

  if (!es->GetStreamPacket(pkt))
    return false;

  if (pkt->duration > 180000)
  {
    pkt->duration = 0;
  }
  else if (pkt->pid == m_videoPID)
  {
    // Fill duration map for main stream
    m_curTime += pkt->duration;
    if (m_curTime >= m_pinTime)
    {
      m_pinTime += POSMAP_PTS_INTERVAL;
      if (m_curTime > m_endTime)
      {
        AV_POSMAP_ITEM item;
        item.av_pts = pkt->pts;
        item.av_pos = m_AVContext->GetPosition();
        m_posmap.insert(std::make_pair(m_curTime, item));
        m_endTime = m_curTime;
        //printf(LOGTAG "time %09.3f : PTS=%" PRIu64 " Position=%" PRIu64 "\n", (double)(m_curTime) / PTS_TIME_BASE, item.av_pts, item.av_pos);
      }
    }
    m_DTS = pkt->dts;
    m_PTS = pkt->pts;

    if (g_printVideoPts){
        //TSDemux::DBG(DEMUX_DBG_INFO, "[video] pts=%lld, dts=%lld \n", pkt->pts, pkt->dts);
    }
  } else if (pkt->pid == m_audioPID) {
     if (g_printAudioPts) {
         //TSDemux::DBG(DEMUX_DBG_INFO, "[audio] pts=%lld, dts=%lld \n", pkt->pts, pkt->dts);
     }
  }
  return true;
}

void Demux::reset_posmap()
{
  if (m_posmap.empty())
    return;

  {
    m_posmap.clear();
    m_pinTime = m_curTime = m_endTime = 0;
  }
}

void Demux::register_pmt()
{
  const std::vector<TSDemux::ElementaryStream*> es_streams = m_AVContext->GetStreams();
  if (!es_streams.empty())
  {
    m_videoPID = es_streams[0]->pid;

    if (es_streams.size() > 1) {
        m_audioPID = es_streams[1]->pid;
    }

    for (std::vector<TSDemux::ElementaryStream*>::const_iterator it = es_streams.begin(); it != es_streams.end(); ++it)
    {
      uint16_t channel = m_AVContext->GetChannel((*it)->pid);
      const char* codec_name = (*it)->GetStreamCodecName();
      if (!g_parseonly)
      {
        std::map<uint16_t, FILE*>::iterator fit = m_outfiles.find((*it)->pid);
        if (fit == m_outfiles.end())
        {
          char filename[512];
          sprintf(filename, "stream_%u_%.4x_%s", channel, (*it)->pid, codec_name);
          printf(LOGTAG "stream channel %u PID %.4x codec %s to file '%s'\n", channel, (*it)->pid, codec_name, filename);
          FILE* ofile = fopen(filename, "wb+");
          if (ofile)
            m_outfiles.insert(std::make_pair((*it)->pid, ofile));
          else
            printf(LOGTAG "cannot open file '%s' for write\n", filename);
        }
      }
      m_AVContext->StartStreaming((*it)->pid);
    }
  }
}


static inline int stream_identifier(int composition_id, int ancillary_id)
{
  return ((composition_id & 0xff00) >> 8)
    | ((composition_id & 0xff) << 8)
    | ((ancillary_id & 0xff00) << 16)
    | ((ancillary_id & 0xff) << 24);
}

void Demux::show_stream_info(uint16_t pid)
{
  TSDemux::ElementaryStream* es = m_AVContext->GetStream(pid);
  if (!es)
    return;

  uint16_t channel = m_AVContext->GetChannel(pid);
  printf(LOGTAG "dump stream infos for channel %u PID %.4x\n", channel, es->pid);
  printf("  Codec name     : %s\n", es->GetStreamCodecName());
  printf("  Language       : %s\n", es->stream_info.language);
  printf("  Identifier     : %.8x\n", stream_identifier(es->stream_info.composition_id, es->stream_info.ancillary_id));
  printf("  FPS scale      : %d\n", es->stream_info.fps_scale);
  printf("  FPS rate       : %d\n", es->stream_info.fps_rate);
  printf("  Interlaced     : %s\n", (es->stream_info.interlaced ? "true" : "false"));
  printf("  Height         : %d\n", es->stream_info.height);
  printf("  Width          : %d\n", es->stream_info.width);
  printf("  Aspect         : %3.3f\n", es->stream_info.aspect);
  printf("  Channels       : %d\n", es->stream_info.channels);
  printf("  Sample rate    : %d\n", es->stream_info.sample_rate);
  printf("  Block align    : %d\n", es->stream_info.block_align);
  printf("  Bit rate       : %d\n", es->stream_info.bit_rate);
  printf("  Bit per sample : %d\n", es->stream_info.bits_per_sample);
  printf("\n");
}

void Demux::write_stream_data(TSDemux::STREAM_PKT* pkt)
{
  if (!pkt)
    return;

  if (!g_parseonly && pkt->size > 0 && pkt->data)
  {
    std::map<uint16_t, FILE*>::const_iterator it = m_outfiles.find(pkt->pid);
    if (it != m_outfiles.end())
    {
      size_t c = fwrite(pkt->data, sizeof(*pkt->data), pkt->size, it->second);
      if (c != pkt->size)
        m_AVContext->StopStreaming(pkt->pid);
    }
  }
}

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

void listFiles(const char *dir, std::map<int, std::string> &mapFiles);

int main(int argc, char* argv[])
{
  const char* filename = NULL;
  uint16_t channel = 0;
  int i = 0;

  ParseredDataContainer dataContainer(PRINT_AUDIO);
  std::map<int, std::string> localFiles;

  //std::string videoLocaltion = "D:/MyProg/MpegTsParser/MpegTsParser/video/";
  std::string videoLocaltion = "E:/Data/debug/8_5/ts/ts/";
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
    else {
      //filename = argv[i];
      //vecLocalFiles.push_back(argv[i]);
    }
  }

  g_logFile = fopen("debug.log", "w");
  if (g_logFile != NULL) {
      TSDemux::SetDBGMsgCallback(LogOut);
  }

  if (!localFiles.empty()){
      int index = 0;
    for (std::map<int, std::string>::iterator it = localFiles.begin(); it != localFiles.end(); it++) {
        std::string curFile = videoLocaltion + it->second;

        FILE* file = NULL;
        if (strcmp(curFile.c_str(), "-") == 0){
            file = stdin;
            filename = "[stdin]";
        } else {
            file = fopen(curFile.c_str(), "rb");
        }

        if (file){
            fprintf(stderr, "## Processing TS stream from %s ##\n", curFile.c_str());
            Demux* demux = new Demux(file, channel, it->first);
            demux->Do();
            std::list<TSDemux::STREAM_PKT*> *lst = demux->getParseredData();
            dataContainer.printCurrentList(lst, it->first);
            //dataContainer.addData(lst, it->first);
            delete demux;
            fclose(file);
        }
        else{
            fprintf(stderr,"Cannot open file '%s' for read\n", curFile.c_str());
        }
    }

    //dataContainer.printInfo();
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


void listFiles(const char *dir, std::map<int, std::string> &mapFiles)
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
            int index = atoi(findData.name);
            mapFiles.insert(std::make_pair(index, findData.name));
        }
    } while (_findnext(handle, &findData) == 0);

    _findclose(handle); 
}
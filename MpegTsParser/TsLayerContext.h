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
 *  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301 USA
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef TSDEMUXER_H
#define TSDEMUXER_H

#include "tsPacket.h"
#include "elementaryStream.h"
#include "mutex.h"

#include <map>
#include <vector>
#include <list>

#define FLUTS_NORMAL_TS_PACKETSIZE  188
#define FLUTS_M2TS_TS_PACKETSIZE    192
#define FLUTS_DVB_ASI_TS_PACKETSIZE 204
#define FLUTS_ATSC_TS_PACKETSIZE    208

#define AV_CONTEXT_PACKETSIZE       208
#define TS_CHECK_MIN_SCORE          2
#define TS_CHECK_MAX_SCORE          10


namespace TSDemux
{
  class TSDemuxer
  {
  public:
    virtual const unsigned char* ReadAV(uint64_t pos, size_t len) = 0;
  };

  enum {
    AVCONTEXT_TS_ERROR            = -3,
    AVCONTEXT_IO_ERROR            = -2,
    AVCONTEXT_TS_NOSYNC           = -1,
    AVCONTEXT_CONTINUE            = 0,
    AVCONTEXT_PROGRAM_CHANGE      = 1,
    AVCONTEXT_STREAM_PID_DATA     = 2,
    AVCONTEXT_DISCONTINUITY       = 3
  };

  struct PESPacket {
      PESPacket() : pts(PTS_UNSET), dts(PTS_UNSET), streamId(-1) {}
      int64_t pts;
      int64_t dts;
      uint8_t streamId;
  };

  struct TsPacket {
      int pid;
      bool transportError;
      bool payloadUnitStart;
      bool hasPayload;
      bool hasAdaptation;
      uint8_t payload_counter;
      int32_t payloadLength;
      TS_PCR pcr;
      PACKET_TYPE tsType;
      PESPacket pes;
      uint8_t *payload;
  };


  class TsLayerContext
  {
  public:
    TsLayerContext(TSDemuxer* const demux, uint64_t pos, uint16_t channel, int fileIndex);
    void Reset(void);

    bool HasPIDStreamData() const;
    bool HasPIDPayload() const;
    ElementaryStream* GetPIDStream();
    std::vector<ElementaryStream*> GetStreams();
    void StartStreaming(uint16_t pid);
    void StopStreaming(uint16_t pid);

    ElementaryStream* GetStream(uint16_t pid) const;
    uint16_t GetChannel(uint16_t pid) const;
    void ResetPackets();

    const Packet *getCurrentPacket() { return mCurrentPkt; }
    std::list<TSDemux::STREAM_PKT*> *getMediaPkts() { return mMediaPkts; }

    // TS parser
    int tsSync();
    uint64_t goNext();
    uint64_t Shift();
    void GoPosition(uint64_t pos);
    uint64_t GetPosition() const;

    TSDemux::TsPacket *parserTsPacket();
    int parserTsPayload();
    int parsePATSection(const uint8_t *data, int dataLength);
    int parsePMTSection(const uint8_t *data, int dataLength);
    int processOneFrame(std::list<const TsPacket*> &packets);
    int parsePESPacket(TsPacket *packet);
    int pushTsPacket(const TsPacket *pkt);


    int ProcessTSPacket();
    int ProcessTSPayload();

    int64_t getTsStartTimeStamp() { return mTsStartTimeStamp; }
  private:
    TsLayerContext(const TsLayerContext&);
    TsLayerContext& operator=(const TsLayerContext&);

    int configure_ts();
    static STREAM_TYPE get_stream_type(uint8_t pes_type);
    static uint8_t av_rb8(const unsigned char* p);
    static uint16_t av_rb16(const unsigned char* p);
    static uint32_t av_rb32(const unsigned char* p);
    static uint64_t decode_pts(const unsigned char* p);
     static STREAM_INFO parse_pes_descriptor(const unsigned char* p, size_t len, STREAM_TYPE* st);
    void clear_pmt();
    void clear_pes(uint16_t channel);
    int parse_ts_psi();
    int parse_ts_pes();

    int parsePat(const unsigned char *data, const unsigned char *dataEnd);
    int parsePmt(const unsigned char *data, const unsigned char *dataEnd);

    // Critical section
    mutable PLATFORM::CMutex mutex;

    // AV stream owner
    TSDemuxer* m_demux;

    int mVideoPktCount;
    int mAudioPktCount;
    int mVideoPid;
    int mAudioPid;
    int mPmtPid;

    std::list<const TsPacket*> mMediaDatas;

    int mFileIndex;

    // Raw packet buffer
    uint64_t av_pos;
    size_t mAvDataLen;
    size_t av_pkt_size;
    unsigned char mTsPktBuffer[AV_CONTEXT_PACKETSIZE];

    // TS Streams context
    bool is_configured;
    uint16_t channel;
    int64_t mTsStartTimeStamp; // first video packet dts;
    std::map<uint16_t, Packet> mTsTypePkts;
    std::list<TSDemux::STREAM_PKT*> *mMediaPkts;

    // Packet context
    uint16_t pid;
    bool transport_error;
    bool mHasPayload;
    bool mPayloadUnitStart;
    bool discontinuity;
    const unsigned char *mTsPayload;
    size_t mPayloadLen;
    Packet* mCurrentPkt;
  };
}

#endif /* TSDEMUXER_H */

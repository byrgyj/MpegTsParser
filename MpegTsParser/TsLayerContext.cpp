#include "TsLayerContext.h"
#include "VideoStreamAVC.h"
#include "VideoStreamHEVC.h"
#include "AudioStreamAAC.h"
#include "AudioStreamAC3.h"
#include "debug.h"
#include "Tool.h"

#include <cassert>

#define MAX_RESYNC_SIZE         65536
#define FREE_PTR(ptr) if (ptr != NULL) { delete ptr ;}
using namespace QIYI;

TsLayerContext::TsLayerContext(TSDemuxer* const demux, uint64_t pos, uint16_t channel, int fileIndex)
  : av_pos(pos)
  , mAvDataLen(FLUTS_NORMAL_TS_PACKETSIZE)
  , av_pkt_size(0)
  , is_configured(false)
  , channel(channel)
  , pid(0xffff)
  , transport_error(false)
  , mHasPayload(false)
  , mPayloadUnitStart(false)
  , discontinuity(false)
  , mTsPayload(NULL)
  , mPayloadLen(0)
  , mCurrentPkt(NULL)
  , mVideoPktCount(0)
  , mAudioPktCount(0)
  , mVideoPid(-1)
  , mAudioPid(-1)
  , mFileIndex(fileIndex)
  , mTsStartTimeStamp(-1)
  , mPmtPid(0)
  , mVideoStream(NULL),
  mAudioStream(NULL)
{
  m_demux = demux;
  memset(mTsPktBuffer, 0, sizeof(mTsPktBuffer));

  mMediaPkts = new std::list<QIYI::STREAM_PKT*>;
}

TsLayerContext::~TsLayerContext() {
    destroyStream();
}

void TsLayerContext::Reset(void)
{
  PLATFORM::CLockObject lock(mutex);

  pid = 0xffff;
  transport_error = false;
  mHasPayload = false;
  mPayloadUnitStart = false;
  discontinuity = false;
  mTsPayload = NULL;
  mPayloadLen = 0;
  mCurrentPkt = NULL;
}


bool TsLayerContext::HasPIDStreamData() const
{
  PLATFORM::CLockObject lock(mutex);

  // PES packets append frame buffer of elementary stream until next start of unit
  // On new unit start, flag is held
  if (mCurrentPkt && mCurrentPkt->has_stream_data)
    return true;
  return false;
}

bool TsLayerContext::HasPIDPayload() const
{
  return mHasPayload;
}

ElementaryStream* TsLayerContext::GetPIDStream()
{
  PLATFORM::CLockObject lock(mutex);

  if (mCurrentPkt && mCurrentPkt->packet_type == PACKET_TYPE_PES)
    return mCurrentPkt->stream;
  return NULL;
}

std::vector<ElementaryStream*> TsLayerContext::GetStreams()
{
  PLATFORM::CLockObject lock(mutex);

  std::vector<ElementaryStream*> v;
  for (std::map<uint16_t, Packet>::iterator it = mTsTypePkts.begin(); it != mTsTypePkts.end(); ++it)
    if (it->second.packet_type == PACKET_TYPE_PES && it->second.stream)
      v.push_back(it->second.stream);
  return v;
}

void TsLayerContext::StartStreaming(uint16_t pid)
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::iterator it = mTsTypePkts.find(pid);
  if (it != mTsTypePkts.end())
    it->second.streaming = true;
}

void TsLayerContext::StopStreaming(uint16_t pid)
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::iterator it = mTsTypePkts.find(pid);
  if (it != mTsTypePkts.end())
    it->second.streaming = false;
}

ElementaryStream* TsLayerContext::GetStream(uint16_t pid) const
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::const_iterator it = mTsTypePkts.find(pid);
  if (it != mTsTypePkts.end())
    return it->second.stream;
  return NULL;
}

uint16_t TsLayerContext::GetChannel(uint16_t pid) const
{
  PLATFORM::CLockObject lock(mutex);

  std::map<uint16_t, Packet>::const_iterator it = mTsTypePkts.find(pid);
  if (it != mTsTypePkts.end())
    return it->second.channel;
  return 0xffff;
}

void TsLayerContext::ResetPackets()
{
  PLATFORM::CLockObject lock(mutex);

  for (std::map<uint16_t, Packet>::iterator it = mTsTypePkts.begin(); it != mTsTypePkts.end(); ++it)
  {
    it->second.Reset();
  }
}

////////////////////////////////////////////////////////////////////////////////
/////
/////  MPEG-TS parser for the context
/////

uint64_t TsLayerContext::decode_pts(const unsigned char* p)
{
  uint64_t pts = (uint64_t)(av_rb8(p) & 0x0e) << 29 | (av_rb16(p + 1) >> 1) << 15 | av_rb16(p + 3) >> 1;
  return pts;
}

STREAM_TYPE TsLayerContext::get_stream_type(uint8_t pes_type)
{
  switch (pes_type)
  {
    case 0x06:
      return STREAM_TYPE_PRIVATE_DATA;
    case 0x0f:
    case 0x11:
      return STREAM_TYPE_AUDIO_AAC;
    case 0x10:
      return STREAM_TYPE_VIDEO_MPEG4;
    case 0x1b:
      return STREAM_TYPE_VIDEO_H264;
    case 0x24:
      return STREAM_TYPE_VIDEO_HEVC;
    case 0x80:
      return STREAM_TYPE_AUDIO_LPCM;
    case 0x81:
    case 0x83:
    case 0x84:
    case 0x87:
      return STREAM_TYPE_AUDIO_AC3;
    case 0x82:
    case 0x85:
    case 0x8a:
      return STREAM_TYPE_AUDIO_DTS;
  }
  return STREAM_TYPE_UNKNOWN;
}

int TsLayerContext::configure_ts()
{
  size_t data_size = AV_CONTEXT_PACKETSIZE;
  uint64_t pos = av_pos;
  int fluts[][2] = {
    {FLUTS_NORMAL_TS_PACKETSIZE, 0},
    {FLUTS_M2TS_TS_PACKETSIZE, 0},
    {FLUTS_DVB_ASI_TS_PACKETSIZE, 0},
    {FLUTS_ATSC_TS_PACKETSIZE, 0}
  };
  int nb = sizeof (fluts) / (2 * sizeof (int));
  int score = TS_CHECK_MIN_SCORE;

  for (int i = 0; i < MAX_RESYNC_SIZE; i++)
  {
    const unsigned char* data = m_demux->ReadAV(pos, data_size);
    if (!data)
      return AVCONTEXT_IO_ERROR;
    if (data[0] == 0x47)
    {
      int count, found;
      for (int t = 0; t < nb; t++) // for all fluts
      {
        const unsigned char* ndata;
        uint64_t npos = pos;
        int do_retry = score; // Reach for score
        do
        {
          --do_retry;
          npos += fluts[t][0];
          if (!(ndata = m_demux->ReadAV(npos, data_size)))
            return AVCONTEXT_IO_ERROR;
        }
        while (ndata[0] == 0x47 && (++fluts[t][1]) && do_retry);
      }
      // Is score reached ?
      count = found = 0;
      for (int t = 0; t < nb; t++)
      {
        if (fluts[t][1] == score)
        {
          found = t;
          ++count;
        }
        // Reset score for next retry
        fluts[t][1] = 0;
      }
      // One and only one is eligible
      if (count == 1)
      {
        DBG(DEMUX_DBG_DEBUG, "%s: packet size is %d\n", __FUNCTION__, fluts[found][0]);
        av_pkt_size = fluts[found][0];
        av_pos = pos;
        return AVCONTEXT_CONTINUE;
      }
      // More one: Retry for highest score
      else if (count > 1 && ++score > TS_CHECK_MAX_SCORE)
        // Packet size remains undetermined
        break;
      // None: Bad sync. Shift and retry
      else
        pos++;
    }
    else
      pos++;
  }

  DBG(DEMUX_DBG_ERROR, "%s: invalid stream\n", __FUNCTION__);
  return AVCONTEXT_TS_NOSYNC;
}

int TsLayerContext::tsSync(){
  if (!is_configured)
  {
    int ret = configure_ts();
    if (ret != AVCONTEXT_CONTINUE)
      return ret;
    is_configured = true;
  }
  for (int i = 0; i < MAX_RESYNC_SIZE; i++)
  {
    const unsigned char* data = m_demux->ReadAV(av_pos, av_pkt_size);
    if (!data)
      return AVCONTEXT_IO_ERROR;
    if (data[0] == 0x47)
    {
      memcpy(mTsPktBuffer, data, av_pkt_size);
      Reset();
      return AVCONTEXT_CONTINUE;
    }
    av_pos++;
  }

  return AVCONTEXT_TS_NOSYNC;
}

uint64_t TsLayerContext::goNext(){
  av_pos += av_pkt_size;
  Reset();
  return av_pos;
}

uint64_t TsLayerContext::Shift(){
  av_pos++;
  Reset();
  return av_pos;
}

void TsLayerContext::GoPosition(uint64_t pos)
{
  av_pos = pos;
  Reset();
}

uint64_t TsLayerContext::GetPosition() const
{
  return av_pos;
}
void TsLayerContext::destroyStream() {
   FREE_PTR(mVideoStream);
   FREE_PTR(mAudioStream);

}

TsPacket *TsLayerContext::parserTsPacket() {
    int ret = AVCONTEXT_CONTINUE;
    std::map<uint16_t, Packet>::iterator it;
    if (av_rb8(mTsPktBuffer) != 0x47){
        return NULL;
    }



    int pid  = av_rb16(mTsPktBuffer + 1) & 0x1FFF;
    bool transportError = (av_rb16(mTsPktBuffer + 1) & 0x8000) != 0;
    if (transportError) {
        return NULL;
    }

    if (pid == 0x1FFF) { // empty ts packet
        return NULL;
    }

    TsPacket *tsPkt = new TsPacket;
    tsPkt->pid = pid;
    tsPkt->transportError = transportError;
    tsPkt->payloadUnitStart = (av_rb16(mTsPktBuffer + 1) & 0x4000) != 0;

    // Cleaning context
    discontinuity = false;
    mHasPayload = false;
    mTsPayload = NULL;
    mPayloadLen = 0;

    uint8_t flags = av_rb8(mTsPktBuffer + 3);
    tsPkt->hasPayload = (flags & 0x10) != 0;
    bool is_discontinuity = false;
    tsPkt->payload_counter = flags & 0x0F;
    tsPkt->hasAdaptation = (flags & 0x20) != 0;

    size_t n = 0;
    if (tsPkt->hasAdaptation) {
        size_t len = (size_t)av_rb8(mTsPktBuffer + 4);
        if (len > (mAvDataLen - 5)){
            return NULL;
        }
        
        n = len + 1;
        if (len > 0)
        {
            if ((mTsPktBuffer[5] >> 4) & 0x1) { // PCR flag
                int64_t pcr_high = av_rb16(mTsPktBuffer + 6) << 16 | av_rb16(mTsPktBuffer + 8);
                tsPkt->pcr.pcr_base = (pcr_high << 1) | (mTsPktBuffer[10] >> 7);
                tsPkt->pcr.pcr_ext = ((mTsPktBuffer[10] & 1) << 8) | mTsPktBuffer[11];
                tsPkt->pcr.pcr = tsPkt->pcr.pcr_base * 300 + tsPkt->pcr.pcr_ext;
            }

            is_discontinuity = (av_rb8(mTsPktBuffer + 5) & 0x80) != 0;
        }
    }

    if (tsPkt->hasPayload) {
        tsPkt->payloadLength = mAvDataLen - n - 4;
        tsPkt->payload = new uint8_t[tsPkt->payloadLength];
        memcpy(tsPkt->payload, mTsPktBuffer + n + 4, tsPkt->payloadLength);
    }


    if (tsPkt->pid == 0) {
        tsPkt->tsType = PACKET_TYPE_PSI;
        parsePATSection(tsPkt->payload + 1, tsPkt->payloadLength - 1);
    } else if (tsPkt->pid == mPmtPid) {
        parsePMTSection(tsPkt->payload + 1, tsPkt->payloadLength - 1);
    } else if (tsPkt->pid == mVideoPid || tsPkt->pid == mAudioPid) {
      
    }
   
    return tsPkt;
}

int TsLayerContext::parserTsPayload() {
    return 0;
}

int TsLayerContext::parsePATSection(const uint8_t *data, int dataLength) {
    if (data == NULL || dataLength == 0) {
        return -1;
    }

    const uint8_t *dataEnd = data + dataLength;
  
    uint8_t tableId = av_rb8(data);
    int sectionLength = (data[1] & 0x0F) << 8 | data[2];

    for (int i = 0; i < sectionLength  - 12; i+=4) {
        int programNum = data[8 + i] << 8 | data[9 + i];
        if (programNum == 0) {

        } else {
            mPmtPid = (data[10 + i] & 0x1F) << 8 | data[11 + i];
        }
    }
    return 0;
}

int TsLayerContext::parsePMTSection(const uint8_t *data, int dataLength) {
    uint8_t tableId = av_rb8(data);
     int sectionLength = (data[1] & 0x0F) << 8 | data[2];
     int programInfoLength = (data[10] & 0x0F) << 8 | data[11];

     int index = 12 + programInfoLength;
     for ( ; index <= (sectionLength + 2 ) -  4; ) {
         STREAM_TYPE type = get_stream_type(data[index]);
         int32_t pid = ((data[index+1] << 8) | data[index+2]) & 0x1FFF;
         if (type == STREAM_TYPE_AUDIO_AAC || type == STREAM_TYPE_AUDIO_EAC3 || type == STREAM_TYPE_AUDIO_AC3) {
             mAudioPid = pid;
             if (mAudioStream == NULL){
                 mAudioStream = createElementaryStream(type, pid);
             }
         } else if (type == STREAM_TYPE_VIDEO_H264 || type == STREAM_TYPE_VIDEO_HEVC) {
             mVideoPid = pid;
             if (mVideoStream == NULL) {
                 mVideoStream = createElementaryStream(type, pid);
             }
         }
         index += 5;
     }
     return 0;
}

int64_t TsLayerContext::processOneFrame(std::list<const TsPacket*> &packets, QIYI::STREAM_PKT *pkt) {
    int64_t frameDuration = 0;
    if (packets.empty()) {
        return frameDuration;
    }

    std::list<const TsPacket*>::iterator it = packets.begin();
    ElementaryStream *em = (*it)->pid == mVideoPid ? mVideoStream : mAudioStream;

    if (em != NULL) {
        frameDuration =  em->parse(*it);
        pkt->duration = frameDuration;
    }

    for (; it != packets.end(); it++) {
        const TsPacket *pkt = *it;
        if (pkt == NULL) {
            continue;
        } else {
            if (pkt->pid == mVideoPid) {
                // video
            } else if (pkt->pid == mAudioPid) {
                // audio
            }
        }

        delete []pkt->payload;
        delete pkt;
    }

    packets.clear();
    return frameDuration;
}


int TsLayerContext::parsePESPacket(TsPacket *packet) {
    if (packet == NULL) {
        return 0;
    }
 
    size_t pos = 0;
    bool has_pts = false;
    if (packet->payloadLength >= 9){
        uint8_t streamId = av_rb8(packet->payload + 3); // stream_id
        uint16_t pesLength = av_rb16(packet->payload + 6); // PES_packet_length
        uint8_t flags = av_rb8(packet->payload + 7);

        uint8_t pesHeadLength = av_rb8(packet->payload + 8); // PES_header_data_length

        switch (flags & 0xC0)
        {
        case 0x80: // PTS only
            {
                has_pts = true;
                uint64_t pts = decode_pts(packet->payload + 9);
                packet->pes.pts = packet->pes.dts = pts;
            }
            break;
        case 0xc0: // PTS,DTS
            {
                has_pts = true;
                uint64_t pts = decode_pts(packet->payload + 9);
                uint64_t dts = decode_pts(packet->payload + 14);
                packet->pes.pts = pts;
                packet->pes.dts = dts;
            }
            break;
        }
    }

    if (packet->pid == mVideoPid && mTsStartTimeStamp == -1) {
        mTsStartTimeStamp = packet->pes.dts;
    }

    return AVCONTEXT_CONTINUE;
}

int TsLayerContext::pushTsPacket(const TsPacket *pkt) {
    if (pkt == NULL) {
        return -1;
    }

    if (mMediaPkts->size() == 306) {
        printf("");
    }

    if (pkt->pid == mVideoPid || pkt->pid == mAudioPid) {
        ElementaryStream *es = pkt->pid == mVideoPid ? mVideoStream :mAudioStream;
        if (pkt->payloadUnitStart) {               

            QIYI::STREAM_PKT *curPkt = NULL;
            std::list<QIYI::STREAM_PKT*>::const_reverse_iterator it = mMediaPkts->rbegin();
            if (it != mMediaPkts->rend()) {
                curPkt = *it;
            }

            if (!mMediaDatas.empty()) {
               processOneFrame(mMediaDatas, curPkt);
            }

            QIYI::STREAM_PKT *resultPkt = new QIYI::STREAM_PKT();
            if (resultPkt != NULL) {
                resultPkt->pid = pkt->pid;
                resultPkt->pts = pkt->pes.pts;
                resultPkt->dts = pkt->pes.dts;
                resultPkt->pcr = pkt->pcr;
                resultPkt->duration = curPkt != NULL ? curPkt->duration : 0;
                mMediaPkts->push_back(resultPkt);
            }
        }

        es->Append(pkt->payload, pkt->payloadLength);

        mMediaDatas.push_back(pkt);
    }
    return 0;
}

void TsLayerContext::processLastFrame() {
    QIYI::STREAM_PKT *curPkt = NULL;
    std::list<QIYI::STREAM_PKT*>::const_reverse_iterator it = mMediaPkts->rbegin();
    if (it != mMediaPkts->rend()) {
        curPkt = *it;
    }

    if (!mMediaDatas.empty()) {
        processOneFrame(mMediaDatas, curPkt);
    }
}

QIYI::ElementaryStream *TsLayerContext::createElementaryStream(STREAM_TYPE streamType, int pid) {
    if (streamType != STREAM_TYPE_UNKNOWN) {
        ElementaryStream *es = NULL;
        switch (streamType){
        case STREAM_TYPE_AUDIO_AAC:
        case STREAM_TYPE_AUDIO_AAC_ADTS:
        case STREAM_TYPE_AUDIO_AAC_LATM:
            es = new AudioStreamAAC(pid);
            break;
        case STREAM_TYPE_VIDEO_H264:
            es = new VideoStreamAVC(pid);
            break;
        case STREAM_TYPE_VIDEO_HEVC:
            es = new VideoStreamHEVC(pid);
            break;
        case STREAM_TYPE_AUDIO_AC3:
        case STREAM_TYPE_AUDIO_EAC3:
            es = new AudioStreamAC3(pid);
            break;
        default:
            es = new ElementaryStream(pid);
            es->has_stream_info = true;
            break;
        }
        es->stream_type = streamType;
        return es;
    }

    return NULL;
}


/*
 * Process TS packet
 *
 * returns:
 *
 * AVCONTEXT_CONTINUE
 *   Parse completed. If has payload, process it else Continue to next packet.
 *
 * AVCONTEXT_STREAM_PID_DATA
 *   Parse completed. A new PES unit starts and data of elementary stream for
 *   the PID must be picked before processing this payload.
 *
 * AVCONTEXT_DISCONTINUITY
 *   Discontinuity. PID will wait until next unit start. So continue to next
 *   packet.
 *
 * AVCONTEXT_TS_NOSYNC
 *   Bad sync byte. Should run TSResync().
 *
 * AVCONTEXT_TS_ERROR
 *  Parsing error !
 */
int TsLayerContext::ProcessTSPacket()
{
  PLATFORM::CLockObject lock(mutex);

  int ret = AVCONTEXT_CONTINUE;
  std::map<uint16_t, Packet>::iterator it;

  if (av_rb8(mTsPktBuffer) != 0x47){
    return AVCONTEXT_TS_NOSYNC;
  }

  uint16_t header = av_rb16(mTsPktBuffer + 1);
  pid = header & 0x1fff;
  transport_error = (header & 0x8000) != 0;
  mPayloadUnitStart = (header & 0x4000) != 0;
  // Cleaning context
  discontinuity = false;
  mHasPayload = false;
  mTsPayload = NULL;
  mPayloadLen = 0;

  if (transport_error) {
    return AVCONTEXT_CONTINUE;
  }
  
  // Null packet
  if (pid == 0x1fff) {
    return AVCONTEXT_CONTINUE;
  }

  uint8_t flags = av_rb8(mTsPktBuffer + 3);
  bool is_payload = (flags & 0x10) != 0;
  bool is_discontinuity = false;
  uint8_t continuity_counter = flags & 0x0f;
  bool has_adaptation = (flags & 0x20) != 0;
  TS_PCR pcr;
  size_t n = 0;
  if (has_adaptation) {
    size_t len = (size_t)av_rb8(mTsPktBuffer + 4);
    if (len > (mAvDataLen - 5))
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }
    n = len + 1;
    if (len > 0)
    {
        if ((mTsPktBuffer[5] >> 4) & 0x1) { // PCR flag
            int64_t pcr_high = av_rb16(mTsPktBuffer + 6) << 16 | av_rb16(mTsPktBuffer + 8);
            pcr.pcr_base = (pcr_high << 1) | (mTsPktBuffer[10] >> 7);
            pcr.pcr_ext = ((mTsPktBuffer[10] & 1) << 8) | mTsPktBuffer[11];
            pcr.pcr = pcr.pcr_base * 300 + pcr.pcr_ext;
        }

      is_discontinuity = (av_rb8(mTsPktBuffer + 5) & 0x80) != 0;
    }
  }
  if (is_payload)
  {
    // Payload start after adaptation fields
    mTsPayload = mTsPktBuffer + n + 4;
    mPayloadLen = mAvDataLen - n - 4;
  }

  it = mTsTypePkts.find(pid);
  if (it == mTsTypePkts.end()){
    // Not registred PID
    // We are waiting for unit start of PID 0 else next packet is required
    if (pid == 0 && mPayloadUnitStart)
    {
      // Registering PID 0
      Packet pid0;
      pid0.pid = pid;
      pid0.packet_type = PACKET_TYPE_PSI;
      pid0.continuity = continuity_counter;
      it = mTsTypePkts.insert(it, std::make_pair(pid, pid0));
    } else {
      return AVCONTEXT_CONTINUE;
    }
  } else {
    // PID is registred
    // Checking unit start is required
    if (it->second.wait_unit_start && !mPayloadUnitStart)
    {
      // Not unit start. Save packet flow continuity...
      it->second.continuity = continuity_counter;
      discontinuity = true;
      return AVCONTEXT_DISCONTINUITY;
    }
    // Checking continuity where possible
    if (it->second.continuity != 0xff)
    {
      uint8_t expected_cc = is_payload ? (it->second.continuity + 1) & 0x0f : it->second.continuity;
      if (!is_discontinuity && expected_cc != continuity_counter)
      {
        this->discontinuity = true;
        // If unit is not start then reset PID and wait the next unit start
        if (!mPayloadUnitStart)
        {
          it->second.Reset();
          DBG(DEMUX_DBG_WARN, "PID %.4x discontinuity detected: found %u, expected %u\n", this->pid, continuity_counter, expected_cc);
          return AVCONTEXT_DISCONTINUITY;
        }
      }
    }
    it->second.continuity = continuity_counter;
  }

  this->discontinuity |= is_discontinuity;
  mHasPayload = is_payload;
  mCurrentPkt = &(it->second);
  mCurrentPkt->pcr = pcr;

  // It is time to stream data for PES
  if (mPayloadUnitStart &&
          mCurrentPkt->streaming &&
          mCurrentPkt->packet_type == PACKET_TYPE_PES &&
          !mCurrentPkt->wait_unit_start)
  {
    mCurrentPkt->has_stream_data = true;
    ret = AVCONTEXT_STREAM_PID_DATA;
  }
  return ret;
}

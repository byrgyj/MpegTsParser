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

#include "TsLayerContext.h"
#include "ES_MPEGVideo.h"
#include "ES_MPEGAudio.h"
#include "ES_h264.h"
#include "ES_hevc.h"
#include "ES_AAC.h"
#include "ES_AC3.h"
#include "ES_Subtitle.h"
#include "ES_Teletext.h"
#include "debug.h"

#include <cassert>

#define MAX_RESYNC_SIZE         65536

using namespace TSDemux;

TsLayerContext::TsLayerContext(TSDemuxer* const demux, uint64_t pos, uint16_t channel, int fileIndex)
  : av_pos(pos)
  , av_data_len(FLUTS_NORMAL_TS_PACKETSIZE)
  , av_pkt_size(0)
  , is_configured(false)
  , channel(channel)
  , pid(0xffff)
  , transport_error(false)
  , mHasPayload(false)
  , payload_unit_start(false)
  , discontinuity(false)
  , mTsPayload(NULL)
  , payload_len(0)
  , mCurrentPkt(NULL)
  , mVideoPktCount(0)
  , mAudioPktCount(0)
  , mVideoPid(0)
  , mAudioPid(0)
  , mFileIndex(fileIndex)
  , mTsStartTimeStamp(-1)
{
  m_demux = demux;
  memset(av_buf, 0, sizeof(av_buf));

  mMediaPkts = new std::list<TSDemux::STREAM_PKT*>;
};

void TsLayerContext::Reset(void)
{
  PLATFORM::CLockObject lock(mutex);

  pid = 0xffff;
  transport_error = false;
  mHasPayload = false;
  payload_unit_start = false;
  discontinuity = false;
  mTsPayload = NULL;
  payload_len = 0;
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

uint8_t TsLayerContext::av_rb8(const unsigned char* p)
{
  uint8_t val = *(uint8_t*)p;
  return val;
}

uint16_t TsLayerContext::av_rb16(const unsigned char* p)
{
  uint16_t val = av_rb8(p) << 8;
  val |= av_rb8(p + 1);
  return val;
}

uint32_t TsLayerContext::av_rb32(const unsigned char* p)
{
  uint32_t val = av_rb16(p) << 16;
  val |= av_rb16(p + 2);
  return val;
}

uint64_t TsLayerContext::decode_pts(const unsigned char* p)
{
  uint64_t pts = (uint64_t)(av_rb8(p) & 0x0e) << 29 | (av_rb16(p + 1) >> 1) << 15 | av_rb16(p + 3) >> 1;
  return pts;
}

STREAM_TYPE TsLayerContext::get_stream_type(uint8_t pes_type)
{
  switch (pes_type)
  {
    case 0x01:
      return STREAM_TYPE_VIDEO_MPEG1;
    case 0x02:
      return STREAM_TYPE_VIDEO_MPEG2;
    case 0x03:
      return STREAM_TYPE_AUDIO_MPEG1;
    case 0x04:
      return STREAM_TYPE_AUDIO_MPEG2;
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
    case 0xea:
      return STREAM_TYPE_VIDEO_VC1;
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
      memcpy(av_buf, data, av_pkt_size);
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

  if (av_rb8(av_buf) != 0x47){
    return AVCONTEXT_TS_NOSYNC;
  }

  uint16_t header = av_rb16(av_buf + 1);
  pid = header & 0x1fff;
  transport_error = (header & 0x8000) != 0;
  payload_unit_start = (header & 0x4000) != 0;
  // Cleaning context
  discontinuity = false;
  mHasPayload = false;
  mTsPayload = NULL;
  payload_len = 0;

  if (transport_error)
    return AVCONTEXT_CONTINUE;
  // Null packet
  if (pid == 0x1fff)
    return AVCONTEXT_CONTINUE;

  uint8_t flags = av_rb8(av_buf + 3);
  bool is_payload = (flags & 0x10) != 0;
  bool is_discontinuity = false;
  uint8_t continuity_counter = flags & 0x0f;
  bool has_adaptation = (flags & 0x20) != 0;
  TS_PCR pcr;
  size_t n = 0;
  if (has_adaptation) {
    size_t len = (size_t)av_rb8(av_buf + 4);
    if (len > (av_data_len - 5))
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
        if ((av_buf[5] >> 4) & 0x1) {
            int64_t pcr_high = av_rb16(av_buf + 6) << 16 | av_rb16(av_buf + 8);
            pcr.pcr_base = (pcr_high << 1) | (av_buf[10] >> 7);
            pcr.pcr_ext = ((av_buf[10] & 1) << 8) | av_buf[11];
            pcr.pcr = pcr.pcr_base * 300 + pcr.pcr_ext;
        }

      is_discontinuity = (av_rb8(av_buf + 5) & 0x80) != 0;
    }
  }
  if (is_payload)
  {
    // Payload start after adaptation fields
    mTsPayload = av_buf + n + 4;
    payload_len = av_data_len - n - 4;
  }

  it = mTsTypePkts.find(pid);
  if (it == mTsTypePkts.end()){
    // Not registred PID
    // We are waiting for unit start of PID 0 else next packet is required
    if (pid == 0 && payload_unit_start)
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
    if (it->second.wait_unit_start && !payload_unit_start)
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
        if (!this->payload_unit_start)
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
  if (this->payload_unit_start &&
          this->mCurrentPkt->streaming &&
          this->mCurrentPkt->packet_type == PACKET_TYPE_PES &&
          !this->mCurrentPkt->wait_unit_start)
  {
    this->mCurrentPkt->has_stream_data = true;
    ret = AVCONTEXT_STREAM_PID_DATA;
  }
  return ret;
}

/*
 * Process payload of packet depending of its type
 *
 * PACKET_TYPE_PSI -> parse_ts_psi()
 * PACKET_TYPE_PES -> parse_ts_pes()
 */
int TsLayerContext::ProcessTSPayload()
{
  PLATFORM::CLockObject lock(mutex);

  if (!this->mCurrentPkt)
    return AVCONTEXT_CONTINUE;

  int ret = 0;
  switch (mCurrentPkt->packet_type)
  {
    case PACKET_TYPE_PSI:
      ret = parse_ts_psi();
      break;
    case PACKET_TYPE_PES:
      ret = parse_ts_pes();
      break;
    case PACKET_TYPE_UNKNOWN:
      break;
  }

  return ret;
}

void TsLayerContext::clear_pmt()
{
  DBG(DEMUX_DBG_DEBUG, "%s\n", __FUNCTION__);
  std::vector<uint16_t> pid_list;
  for (std::map<uint16_t, Packet>::iterator it = mTsTypePkts.begin(); it != mTsTypePkts.end(); ++it)
  {
    if (it->second.packet_type == PACKET_TYPE_PSI && it->second.packet_table.table_id == 0x02)
    {
      pid_list.push_back(it->first);
      clear_pes(it->second.channel);
    }
  }
  for (std::vector<uint16_t>::iterator it = pid_list.begin(); it != pid_list.end(); ++it)
    mTsTypePkts.erase(*it);
}

void TsLayerContext::clear_pes(uint16_t channel)
{
  DBG(DEMUX_DBG_DEBUG, "%s(%u)\n", __FUNCTION__, channel);
  std::vector<uint16_t> pid_list;
  for (std::map<uint16_t, Packet>::iterator it = mTsTypePkts.begin(); it != mTsTypePkts.end(); ++it)
  {
    if (it->second.packet_type == PACKET_TYPE_PES && it->second.channel == channel)
      pid_list.push_back(it->first);
  }
  for (std::vector<uint16_t>::iterator it = pid_list.begin(); it != pid_list.end(); ++it)
    mTsTypePkts.erase(*it);
}

/*
 * Parse PSI payload
 *
 * returns:
 *
 * AVCONTEXT_CONTINUE
 *   Parse completed. Continue to next packet
 *
 * AVCONTEXT_PROGRAM_CHANGE
 *   Parse completed. The program has changed. All streams are resetted and
 *   streaming flag is set to false. Client must inspect streams MAP and enable
 *   streaming for those recognized.
 *
 * AVCONTEXT_TS_ERROR
 *  Parsing error !
 */
int TsLayerContext::parse_ts_psi()
{
  size_t len;

  if (!mHasPayload || !mTsPayload || !this->payload_len || !mCurrentPkt)
    return AVCONTEXT_CONTINUE;

  if (this->payload_unit_start){
    // Reset wait for unit start
    mCurrentPkt->wait_unit_start = false;
    // pointer field present
    len = (size_t)av_rb8(mTsPayload);
    if (len > this->payload_len)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }

    // table ID
    uint8_t table_id = av_rb8(mTsPayload + 1);

    // table length
    len = (size_t)av_rb16(mTsPayload + 2);
    if ((len & 0x3000) != 0x3000)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }
    len &= 0x0fff;

    mCurrentPkt->packet_table.Reset();

    size_t n = this->payload_len - 4;
    memcpy(mCurrentPkt->packet_table.buf, mTsPayload + 4, n);
    mCurrentPkt->packet_table.table_id = table_id;
    mCurrentPkt->packet_table.offset = n;
    mCurrentPkt->packet_table.len = len;
    // check for incomplete section
    if (mCurrentPkt->packet_table.offset < mCurrentPkt->packet_table.len)
      return AVCONTEXT_CONTINUE;
  }else{
    // next part of PSI
    if (mCurrentPkt->packet_table.offset == 0)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }

    if ((this->payload_len + mCurrentPkt->packet_table.offset) > TABLE_BUFFER_SIZE)
    {
#if defined(TSDEMUX_DEBUG)
      assert(false);
#else
      return AVCONTEXT_TS_ERROR;
#endif
    }
    memcpy(mCurrentPkt->packet_table.buf + mCurrentPkt->packet_table.offset, mTsPayload, this->payload_len);
    mCurrentPkt->packet_table.offset += this->payload_len;
    // check for incomplete section
    if (mCurrentPkt->packet_table.offset < mCurrentPkt->packet_table.len)
      return AVCONTEXT_CONTINUE;
  }

  // now entire table is filled
  const unsigned char* psi = mCurrentPkt->packet_table.buf;
  const unsigned char* end_psi = psi + mCurrentPkt->packet_table.len;

  switch (mCurrentPkt->packet_table.table_id)
  {
    case 0x00: // parse PAT table
      parsePat(psi, end_psi);
      break;
    case 0x02: // parse PMT table
      return parsePmt(psi, end_psi);
    default:
      // CAT, NIT table
      break;
  }

  return AVCONTEXT_CONTINUE;
}

STREAM_INFO TsLayerContext::parse_pes_descriptor(const unsigned char* p, size_t len, STREAM_TYPE* st)
{
  const unsigned char* desc_end = p + len;
  STREAM_INFO si;
  memset(&si, 0, sizeof(STREAM_INFO));

  while (p < desc_end)
  {
    uint8_t desc_tag = av_rb8(p);
    uint8_t desc_len = av_rb8(p + 1);
    p += 2;
    DBG(DEMUX_DBG_DEBUG, "%s: tag %.2x len %d\n", __FUNCTION__, desc_tag, desc_len);
    switch (desc_tag)
    {
      case 0x02:
      case 0x03:
        break;
      case 0x0a: /* ISO 639 language descriptor */
        if (desc_len >= 4)
        {
          si.language[0] = av_rb8(p);
          si.language[1] = av_rb8(p + 1);
          si.language[2] = av_rb8(p + 2);
          si.language[3] = 0;
        }
        break;
      case 0x56: /* DVB teletext descriptor */
        *st = STREAM_TYPE_DVB_TELETEXT;
        break;
      case 0x6a: /* DVB AC3 */
      case 0x81: /* AC3 audio stream */
        *st = STREAM_TYPE_AUDIO_AC3;
        break;
      case 0x7a: /* DVB enhanced AC3 */
        *st = STREAM_TYPE_AUDIO_EAC3;
        break;
      case 0x7b: /* DVB DTS */
        *st = STREAM_TYPE_AUDIO_DTS;
        break;
      case 0x7c: /* DVB AAC */
        *st = STREAM_TYPE_AUDIO_AAC;
        break;
      case 0x59: /* subtitling descriptor */
        if (desc_len >= 8)
        {
          /*
           * Byte 4 is the subtitling_type field
           * av_rb8(p + 3) & 0x10 : normal
           * av_rb8(p + 3) & 0x20 : for the hard of hearing
           */
          *st = STREAM_TYPE_DVB_SUBTITLE;
          si.language[0] = av_rb8(p);
          si.language[1] = av_rb8(p + 1);
          si.language[2] = av_rb8(p + 2);
          si.language[3] = 0;
          si.composition_id = (int)av_rb16(p + 4);
          si.ancillary_id = (int)av_rb16(p + 6);
        }
        break;
      case 0x05: /* registration descriptor */
      case 0x1E: /* SL descriptor */
      case 0x1F: /* FMC descriptor */
      case 0x52: /* stream identifier descriptor */
    default:
      break;
    }
    p += desc_len;
  }

  return si;
}

/*
 * Parse PES payload
 *
 * returns:
 *
 * AVCONTEXT_CONTINUE
 *   Parse completed. When streaming is enabled for PID, data is appended to
 *   the frame buffer of corresponding elementary stream.
 *
 * AVCONTEXT_TS_ERROR
 *  Parsing error !
 */
int TsLayerContext::parse_ts_pes()
{
  if (!mHasPayload|| !mTsPayload || !this->payload_len || !mCurrentPkt)
    return AVCONTEXT_CONTINUE;

  if (!mCurrentPkt->stream)
    return AVCONTEXT_CONTINUE;

  if (this->payload_unit_start)
  {
    // Wait for unit start: Reset frame buffer to clear old data
    if (mCurrentPkt->wait_unit_start)
    {
      mCurrentPkt->stream->Reset();
      mCurrentPkt->stream->p_dts = PTS_UNSET;
      mCurrentPkt->stream->p_pts = PTS_UNSET;
    }
    mCurrentPkt->wait_unit_start = false;
    mCurrentPkt->has_stream_data = false;
    // Reset header table
    mCurrentPkt->packet_table.Reset();
    // Header len is at least 6 bytes. So getting 6 bytes first
    mCurrentPkt->packet_table.len = 6;
  }

  // Position in the payload buffer. Start at 0
  size_t pos = 0;

  while (mCurrentPkt->packet_table.offset < mCurrentPkt->packet_table.len)
  {
    if (pos >= this->payload_len)
      return AVCONTEXT_CONTINUE;

    size_t n = mCurrentPkt->packet_table.len - mCurrentPkt->packet_table.offset;

    if (n > (this->payload_len - pos))
      n = this->payload_len - pos;

    memcpy(mCurrentPkt->packet_table.buf + mCurrentPkt->packet_table.offset, mTsPayload + pos, n);
    mCurrentPkt->packet_table.offset += n;
    pos += n;

    if (mCurrentPkt->packet_table.offset == 6)
    {
      if (memcmp(mCurrentPkt->packet_table.buf, "\x00\x00\x01", 3) == 0)
      {
        uint8_t stream_id = av_rb8(mCurrentPkt->packet_table.buf + 3);
        if (stream_id == 0xbd || (stream_id >= 0xc0 && stream_id <= 0xef))
          mCurrentPkt->packet_table.len = 9;
      }
    }
    else if (mCurrentPkt->packet_table.offset == 9)
    {
      mCurrentPkt->packet_table.len += av_rb8(mCurrentPkt->packet_table.buf + 8);
    }
  }

  // parse header table
  bool has_pts = false;

  if (mCurrentPkt->packet_table.len >= 9)
  {
    uint8_t flags = av_rb8(mCurrentPkt->packet_table.buf + 7);

    //mCurrentPkt->stream->frame_num++;
    TSDemux::STREAM_PKT *curPkt = new TSDemux::STREAM_PKT;
    curPkt->pid = mCurrentPkt->stream->pid;
    switch (flags & 0xc0)
    {
      case 0x80: // PTS only
      {
        has_pts = true;

        if (mCurrentPkt->packet_table.len >= 14)
        {
          uint64_t pts = decode_pts(mCurrentPkt->packet_table.buf + 9);
          mCurrentPkt->stream->p_dts = mCurrentPkt->stream->c_dts;
          mCurrentPkt->stream->p_pts = mCurrentPkt->stream->c_pts;
          mCurrentPkt->stream->c_dts = mCurrentPkt->stream->c_pts = pts;
        }
        else
        {
          mCurrentPkt->stream->c_dts = mCurrentPkt->stream->c_pts = PTS_UNSET;
        }
      }
      break;
      case 0xc0: // PTS,DTS
      {
        has_pts = true;
        if (mCurrentPkt->packet_table.len >= 19 )
        {
          uint64_t pts = decode_pts(mCurrentPkt->packet_table.buf + 9);
          uint64_t dts = decode_pts(mCurrentPkt->packet_table.buf + 14);
          int64_t d = (pts - dts) & PTS_MASK;
          // more than two seconds of PTS/DTS delta, probably corrupt
//           if(d > 90000){
//             mCurrentPkt->stream->c_dts = mCurrentPkt->stream->c_pts = PTS_UNSET;
//           } else {
//             mCurrentPkt->stream->p_dts = mCurrentPkt->stream->c_dts;
//             mCurrentPkt->stream->p_pts = mCurrentPkt->stream->c_pts;
//             mCurrentPkt->stream->c_dts = dts;
//             mCurrentPkt->stream->c_pts = pts;
//           }
          mCurrentPkt->stream->p_dts = mCurrentPkt->stream->c_dts;
          mCurrentPkt->stream->p_pts = mCurrentPkt->stream->c_pts;
          mCurrentPkt->stream->c_dts = dts;
          mCurrentPkt->stream->c_pts = pts;
        }
        else
        {
          mCurrentPkt->stream->c_dts = mCurrentPkt->stream->c_pts = PTS_UNSET;
        }
      }
      break;
    }
    mCurrentPkt->packet_table.Reset();

    curPkt->dts = mCurrentPkt->stream->c_dts;
    curPkt->pts = mCurrentPkt->stream->c_pts;
    curPkt->pcr = mCurrentPkt->pcr;

    if (curPkt->pid == mVideoPid) {
        mVideoPktCount++;
    } else if (curPkt->pid == mAudioPid) {
        mAudioPktCount++;
    }

    if (mTsStartTimeStamp == -1) {
        mTsStartTimeStamp = curPkt->dts;
    }

    mMediaPkts->push_back(curPkt);
  }

  if (mCurrentPkt->streaming)
  {
    const unsigned char* data = mTsPayload + pos;
    size_t len = this->payload_len - pos;
    mCurrentPkt->stream->Append(data, len, has_pts);
  }

  return AVCONTEXT_CONTINUE;
}

int TsLayerContext::parsePat(const unsigned char *data, const unsigned char *dataEnd){
    if (data == NULL || dataEnd == NULL) {
        return -1;
    }
    // check if version number changed
    uint16_t id = av_rb16(data);
    // check if applicable
    if ((av_rb8(data + 2) & 0x01) == 0)
        return AVCONTEXT_CONTINUE;
    // check if version number changed
    uint8_t version = (av_rb8(data + 2) & 0x3e) >> 1;
    if (id == mCurrentPkt->packet_table.id && version == mCurrentPkt->packet_table.version)
        return AVCONTEXT_CONTINUE;
    DBG(DEMUX_DBG_DEBUG, "%s: new PAT version %u\n", __FUNCTION__, version);

    // clear old associated pmt
    clear_pmt();

    // parse new version of PAT
    data += 5;

    dataEnd -= 4; // CRC32

    if (data >= dataEnd)
    {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
    }

    int len = dataEnd - data;

    if (len % 4)
    {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
    }

    size_t n = len / 4;

    for (size_t i = 0; i < n; i++, data += 4)
    {
        uint16_t channel = av_rb16(data);
        uint16_t pmt_pid = av_rb16(data + 2);

        // Reserved fields in table sections must be "set" to '1' bits.
        //if ((pmt_pid & 0xe000) != 0xe000)
        //  return AVCONTEXT_TS_ERROR;

        pmt_pid &= 0x1fff;

        DBG(DEMUX_DBG_DEBUG, "%s: PAT version %u: new PMT %.4x channel %u\n", __FUNCTION__, version, pmt_pid, channel);
        if (this->channel == 0 || this->channel == channel)
        {
            Packet& pmt = mTsTypePkts[pmt_pid];
            pmt.pid = pmt_pid;
            pmt.packet_type = PACKET_TYPE_PSI;
            pmt.channel = channel;
            DBG(DEMUX_DBG_DEBUG, "%s: PAT version %u: register PMT %.4x channel %u\n", __FUNCTION__, version, pmt_pid, channel);
        }
    }
    // PAT is processed. New version is available
    mCurrentPkt->packet_table.id = id;
    mCurrentPkt->packet_table.version = version;

    return 0;
}

int TsLayerContext::parsePmt(const unsigned char *data, const unsigned char *dataEnd) {
    if (data == NULL || dataEnd == NULL) {
        return -4;
    }
    const unsigned char *psi = data;
    const unsigned char *end_psi = dataEnd;

    uint16_t id = av_rb16(psi);
    // check if applicable
    if ((av_rb8(psi + 2) & 0x01) == 0)
        return AVCONTEXT_CONTINUE;
    // check if version number changed
    uint8_t version = (av_rb8(psi + 2) & 0x3e) >> 1;
    if (id == mCurrentPkt->packet_table.id && version == mCurrentPkt->packet_table.version)
        return AVCONTEXT_CONTINUE;
    DBG(DEMUX_DBG_DEBUG, "%s: PMT(%.4x) version %u\n", __FUNCTION__, mCurrentPkt->pid, version);

    // clear old pes
    clear_pes(mCurrentPkt->channel);

    // parse new version of PMT
    psi += 7;

    end_psi -= 4; // CRC32

    if (psi >= end_psi)
    {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
    }

    int len = (size_t)(av_rb16(psi) & 0x0fff);
    psi += 2 + len;

    while (psi < end_psi)
    {
        if (end_psi - psi < 5)
        {
#if defined(TSDEMUX_DEBUG)
            assert(false);
#else
            return AVCONTEXT_TS_ERROR;
#endif
        }

        uint8_t pes_type = av_rb8(psi);
        uint16_t pes_pid = av_rb16(psi + 1);

        pes_pid &= 0x1fff;

        // len of descriptor section
        len = (size_t)(av_rb16(psi + 3) & 0x0fff);
        psi += 5;

        // ignore unknown streams
        STREAM_TYPE stream_type = get_stream_type(pes_type);
        DBG(DEMUX_DBG_DEBUG, "%s: PMT(%.4x) version %u: new PES %.4x %s\n", __FUNCTION__,
            mCurrentPkt->pid, version, pes_pid, ElementaryStream::GetStreamCodecName(stream_type));
        if (stream_type != STREAM_TYPE_UNKNOWN)
        {
            Packet& pes = mTsTypePkts[pes_pid];
            pes.pid = pes_pid;
            pes.packet_type = PACKET_TYPE_PES;
            pes.channel = mCurrentPkt->channel;
            // Disable streaming by default
            pes.streaming = false;
            // Get basic stream infos from PMT table
            STREAM_INFO stream_info;
            stream_info = parse_pes_descriptor(psi, len, &stream_type);

            ElementaryStream* es;
            switch (stream_type)
            {
            case STREAM_TYPE_VIDEO_MPEG1:
            case STREAM_TYPE_VIDEO_MPEG2:
                mVideoPid = pes_pid;
                es = new ES_MPEG2Video(pes_pid);
                break;
            case STREAM_TYPE_AUDIO_MPEG1:
            case STREAM_TYPE_AUDIO_MPEG2:
                mAudioPid = pes_pid;
                es = new ES_MPEG2Audio(pes_pid);
                break;
            case STREAM_TYPE_AUDIO_AAC:
            case STREAM_TYPE_AUDIO_AAC_ADTS:
            case STREAM_TYPE_AUDIO_AAC_LATM:
                mAudioPid = pes_pid;
                es = new ES_AAC(pes_pid);
                break;
            case STREAM_TYPE_VIDEO_H264:
                mVideoPid = pes_pid;
                es = new ES_h264(pes_pid);
                break;
            case STREAM_TYPE_VIDEO_HEVC:
                mVideoPid = pes_pid;
                es = new ES_hevc(pes_pid);
                break;
            case STREAM_TYPE_AUDIO_AC3:
            case STREAM_TYPE_AUDIO_EAC3:
                mAudioPid = pes_pid;
                es = new ES_AC3(pes_pid);
                break;
            case STREAM_TYPE_DVB_SUBTITLE:
                es = new ES_Subtitle(pes_pid);
                break;
            case STREAM_TYPE_DVB_TELETEXT:
                es = new ES_Teletext(pes_pid);
                break;
            default:
                // No parser: pass-through
                es = new ElementaryStream(pes_pid);
                es->has_stream_info = true;
                break;
            }

            es->stream_type = stream_type;
            es->stream_info = stream_info;
            pes.stream = es;
            DBG(DEMUX_DBG_DEBUG, "%s: PMT(%.4x) version %u: register PES %.4x %s\n", __FUNCTION__,
                mCurrentPkt->pid, version, pes_pid, es->GetStreamCodecName());
        }
        psi += len;
    }

    if (psi != end_psi)
    {
#if defined(TSDEMUX_DEBUG)
        assert(false);
#else
        return AVCONTEXT_TS_ERROR;
#endif
    }

    // PMT is processed. New version is available
    mCurrentPkt->packet_table.id = id;
    mCurrentPkt->packet_table.version = version;
    return AVCONTEXT_PROGRAM_CHANGE;

}

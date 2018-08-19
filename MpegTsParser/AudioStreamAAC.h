
#ifndef ES_AAC_H
#define ES_AAC_H

#include "elementaryStream.h"
#include "bitstream.h"

namespace TSDemux
{
  class AudioStreamAAC : public ElementaryStream
  {
  private:
    int     m_SampleRate;
    int     m_Channels;
    int     m_BitRate;
    int     m_FrameSize;
    int     mFrameCount;

    double mCurrentFrameDuration;

    int64_t     mNextPts;
    int64_t     m_PTS;                /* pts of the current frame */
    int64_t     m_DTS;                /* dts of the current frame */

    bool        m_Configured;
    int         m_AudioMuxVersion_A;
    int         m_FrameLengthType;


    int FindHeaders(uint8_t *buf, int buf_size);
    bool ParseLATMAudioMuxElement(CBitstream *bs);
    void ReadStreamMuxConfig(CBitstream *bs);
    void ReadAudioSpecificConfig(CBitstream *bs);
    uint32_t LATMGetValue(CBitstream *bs) { return bs->readBits(bs->readBits(2) * 8); }

  public:
    AudioStreamAAC(uint16_t pes_pid);
    virtual ~AudioStreamAAC();

    virtual int64_t parse(const TsPacket*pkt);
    virtual void Parse(STREAM_PKT* pkt);
    virtual void Reset();
  };
}

#endif /* ES_AAC_H */

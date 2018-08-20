
#ifndef ES_AC3_H
#define ES_AC3_H

#include "elementaryStream.h"

namespace QIYI
{
  class AudioStreamAC3 : public ElementaryStream
  {
  private:
    int         m_SampleRate;
    int         m_Channels;
    int         m_BitRate;
    int         m_FrameSize;

    int64_t     m_PTS;                /* pts of the current frame */
    int64_t     m_DTS;                /* dts of the current frame */

    int FindHeaders(uint8_t *buf, int buf_size);

  public:
    AudioStreamAC3(uint16_t pid);
    virtual ~AudioStreamAC3();

    virtual int64_t parse(const TsPacket *pkt);
    virtual void Reset();
  };
}

#endif /* ES_AC3_H */

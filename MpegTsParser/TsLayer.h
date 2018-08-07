#pragma once
#include "tsDemuxer.h"

#define AV_BUFFER_SIZE          131072
#define POSMAP_PTS_INTERVAL     270000LL

class TsLayer : public TSDemux::TSDemuxer
{
public:
    TsLayer(FILE* file, uint16_t channel, int fileIndex);
    ~TsLayer(void);

    int doDemux();
    const unsigned char* ReadAV(uint64_t pos, size_t n);
    std::list<TSDemux::STREAM_PKT*> *getParseredData() { return m_AVContext->getMediaPkts(); }
    int64_t getTsStartTimeStamp() { return m_AVContext->getTsStartTimeStamp(); }

private:
    bool getStreamData(TSDemux::STREAM_PKT* pkt);
    void resetPosmap();
    void registerPMT();
    void showStreamInfo(uint16_t pid);
    void writeStreamData(TSDemux::STREAM_PKT* pkt);

private:
    FILE* m_ifile;
    int mFileIndex;
    uint16_t m_channel;

    // AV raw buffer
    size_t mBufferSize;         ///< size of av buffer
    uint64_t m_av_pos;            ///< absolute position in av
    unsigned char *mBuffer;      ///< buffer
    unsigned char *mBufferStart;      ///< raw data start in buffer
    unsigned char *mBufferEnd;      ///< raw data end in buffer

    // Playback context
    TSDemux::AVContext* m_AVContext;
    uint16_t mVideoPid;
    uint16_t mAudioPid;
 
    int64_t mPinTime;            ///< pinned relative position (90Khz)
    int64_t mCurTime;            ///< current relative position (90Khz)
    int64_t mEndTime;            ///< last relative marked position (90Khz))
    typedef struct
    {
        uint64_t packetPts;
        uint64_t packetDts;
    } AV_POSMAP_ITEM;
    std::map<int64_t, AV_POSMAP_ITEM> mPosMap;
};


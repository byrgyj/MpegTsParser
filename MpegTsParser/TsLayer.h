#pragma once
#include "TsLayerContext.h"

#define AV_BUFFER_SIZE          131072
#define POSMAP_PTS_INTERVAL     270000LL

class TsLayer : public QIYI::TSDemuxer
{
public:
    TsLayer(std::string &filePath, uint16_t channel);
    ~TsLayer(void);

    int doDemux();
    const unsigned char* ReadAV(uint64_t pos, size_t n);
    std::list<QIYI::STREAM_PKT*> *getParseredData() { return mTsContext->getMediaPkts(); }
    int64_t getTsStartTimeStamp() { return mTsContext->getTsStartTimeStamp(); }

private:
    bool getStreamData(QIYI::STREAM_PKT* pkt);
    void resetPosmap();
    void registerPMT();
    void showStreamInfo(uint16_t pid);
    void writeStreamData(QIYI::STREAM_PKT* pkt);

private:
    FILE* mInputFile;
    uint16_t m_channel;

    // AV raw buffer
    size_t mBufferSize;         ///< size of av buffer
    uint64_t m_av_pos;            ///< absolute position in av
    unsigned char *mBuffer;      ///< buffer
    unsigned char *mBufferStart;      ///< raw data start in buffer
    unsigned char *mBufferEnd;      ///< raw data end in buffer

    // Playback context
    QIYI::TsLayerContext *mTsContext;
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


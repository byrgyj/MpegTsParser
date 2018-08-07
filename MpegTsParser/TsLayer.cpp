#include "StdAfx.h"
#include "TsLayer.h"

extern int g_parseonly;
#define LOGTAG ""
TsLayer::TsLayer(FILE* file, uint16_t channel, int fileIndex) : m_channel(channel), mFileIndex(fileIndex) {
    m_ifile = file;
    mBufferSize = AV_BUFFER_SIZE;
    mBuffer = (unsigned char*)malloc(sizeof(*mBuffer) * (mBufferSize + 1));
    if (mBuffer){
        m_av_pos = 0;
        mBufferStart = mBuffer;
        mBufferEnd = mBuffer;
        m_channel = channel;

        mVideoPid = 0xffff;
        mAudioPid = 0xffff;

        mPinTime = mCurTime = mEndTime = 0;
        m_AVContext = new TSDemux::AVContext(this, 0, m_channel, fileIndex);
    }
    else
    {
        //printf(LOGTAG "alloc AV buffer failed\n");
    }
}

TsLayer::~TsLayer()
{
    if (m_AVContext) {
        delete m_AVContext;
    }

    if (mBuffer != NULL){
        printf(LOGTAG "free AV buffer: allocated size was %zu\n", mBufferSize);
        free(mBuffer);
        mBuffer = NULL;
    }
}

const unsigned char* TsLayer::ReadAV(uint64_t pos, size_t n)
{
    // out of range
    if (n > mBufferSize)
        return NULL;

    // Already read ?
    size_t sz = mBufferEnd - mBuffer;
    if (pos < m_av_pos || pos > (m_av_pos + sz))
    {
        // seek and reset buffer
        int ret = fseek(m_ifile, (int64_t)pos, SEEK_SET);
        if (ret != 0)
            return NULL;
        m_av_pos = (uint64_t)pos;
        mBufferStart = mBufferEnd = mBuffer;
    }
    else
    {
        // move to the desired pos in buffer
        mBufferStart = mBuffer + (size_t)(pos - m_av_pos);
    }

    size_t dataread = mBufferEnd - mBufferStart;
    if (dataread >= n)
        return mBufferStart;

    memmove(mBuffer, mBufferStart, dataread);
    mBufferStart = mBuffer;
    mBufferEnd = mBufferStart + dataread;
    m_av_pos = pos;
    unsigned int len = (unsigned int)(mBufferSize - dataread);

    while (len > 0)
    {
        size_t c = fread(mBufferEnd, sizeof(*mBuffer), len, m_ifile);
        if (c > 0)
        {
            mBufferEnd += c;
            dataread += c;
            len -= c;
        }
        if (dataread >= n || c == 0)
            break;
    }
    return dataread >= n ? mBufferStart : NULL;
}

int TsLayer::doDemux(){
    int ret = 0;
    int indexCount = 0;

    while (true){
        ret = m_AVContext->TSResync();
        if (ret != TSDemux::AVCONTEXT_CONTINUE){
            break;
        }

        ret = m_AVContext->ProcessTSPacket();
        indexCount++;
        if (m_AVContext->HasPIDStreamData()){
            TSDemux::STREAM_PKT pkt;
            while (getStreamData(&pkt)){
                //if (pkt->streamChange)
                //ShowStraemInfo(pkt->pid);
                //WriteStreamData(pkt)
            }
        }
        if (m_AVContext->HasPIDPayload()){
            ret = m_AVContext->ProcessTSPayload();
            if (ret == TSDemux::AVCONTEXT_PROGRAM_CHANGE) {
                registerPMT();
                //std::vector<TSDemux::ElementaryStream*> streams = m_AVContext->GetStreams();
            }
        }

        if (ret < 0) {
            
        }

        if (ret == TSDemux::AVCONTEXT_TS_ERROR) {
            m_AVContext->Shift();
        } else {
            m_AVContext->GoNext();
        }
    }

    return ret;
}

bool TsLayer::getStreamData(TSDemux::STREAM_PKT* pkt) {
    TSDemux::ElementaryStream* es = m_AVContext->GetPIDStream();
    if (!es) {
        return false;
    }

    if (!es->GetStreamPacket(pkt))
        return false;

    if (pkt->duration > 180000){
        pkt->duration = 0;
    }
    else if (pkt->pid == mVideoPid) {
        // Fill duration map for main stream
        mCurTime += pkt->duration;
        if (mCurTime >= mPinTime) {
            mPinTime += POSMAP_PTS_INTERVAL;
            if (mCurTime > mEndTime){
                AV_POSMAP_ITEM item;
                item.packetPts = pkt->pts;
                item.packetDts = m_AVContext->GetPosition();
                mPosMap.insert(std::make_pair(mCurTime, item));
                mEndTime = mCurTime;
            }
        }

    } else if (pkt->pid == mAudioPid) {

    }
    return true;
}

void TsLayer::resetPosmap(){
    if (mPosMap.empty())
        return;
    mPosMap.clear();
    mPinTime = mCurTime = mEndTime = 0;
}

void TsLayer::registerPMT(){
    const std::vector<TSDemux::ElementaryStream*> es_streams = m_AVContext->GetStreams();
    if (!es_streams.empty()) {
        mVideoPid = es_streams[0]->pid;

        if (es_streams.size() > 1) {
            mAudioPid = es_streams[1]->pid;
        }

        for (std::vector<TSDemux::ElementaryStream*>::const_iterator it = es_streams.begin(); it != es_streams.end(); ++it) {
            uint16_t channel = m_AVContext->GetChannel((*it)->pid);
            const char* codec_name = (*it)->GetStreamCodecName();
            m_AVContext->StartStreaming((*it)->pid);
        }
    }
}


static inline int stream_identifier(int composition_id, int ancillary_id){
    return ((composition_id & 0xff00) >> 8)
        | ((composition_id & 0xff) << 8)
        | ((ancillary_id & 0xff00) << 16)
        | ((ancillary_id & 0xff) << 24);
}

void TsLayer::showStreamInfo(uint16_t pid){
    TSDemux::ElementaryStream* es = m_AVContext->GetStream(pid);
    if (!es) {
        return;
    }

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

void TsLayer::writeStreamData(TSDemux::STREAM_PKT* pkt)
{
//     if (!pkt)
//         return;
// 
//     if (!g_parseonly && pkt->size > 0 && pkt->data)
//     {
//         std::map<uint16_t, FILE*>::const_iterator it = m_outfiles.find(pkt->pid);
//         if (it != m_outfiles.end())
//         {
//             size_t c = fwrite(pkt->data, sizeof(*pkt->data), pkt->size, it->second);
//             if (c != pkt->size)
//                 m_AVContext->StopStreaming(pkt->pid);
//         }
//     }
}
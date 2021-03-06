#pragma once
#include <list>
#include <map>
#include <set>
#include "elementaryStream.h"

namespace GYJ{


enum printMediaType { PRINT_MEDIA_VIDEO, PRINT_MEDIA_AUDIO, PRINT_MEDIA_ALL };
enum printPTSLevel { PRINT_ALL_PTS, PRINT_PARTLY_PTS };

typedef struct printParam{
    printParam(int pt, int pl) : printType(pt), printLevel(pl) {}
    int printType;
    int printLevel;
}printParam;

typedef struct tsParam {
    tsParam(std::string name, int64_t startTime, std::list<TSDemux::STREAM_PKT*> *datas) : fileName(name), tsStartTime(startTime), packets(datas) {}
    std::string fileName;
    int64_t tsStartTime;
    std::list<TSDemux::STREAM_PKT*> *packets;
}tsParam;

class ParseredDataContainer
{
public:
    explicit ParseredDataContainer(printParam pp);
    ~ParseredDataContainer();

    void addData(std::list<TSDemux::STREAM_PKT*> *lstData, int64_t index);
    void addData(int64_t startTime, const tsParam *tsInfo);
    void printInfo();
    void printCurrentList(const tsParam *tsSegment);
private:
    bool isEnableVideoPrint();
    bool isEnableAudioPrint();
    bool checkCurrentPrint(int audioIndex, int audioCount);
    bool checkPrintPcr(int currentIndex, int totalPkt);

    void printTimeStamp(const tsParam *tsSegment);
    void dispatchPackets(const std::list<TSDemux::STREAM_PKT*> *lst);
    void printFrameDistance(std::set<int64_t> &Distances, std::string tag);

    void processVideo();
    void processAudio();
    void processPCR();
    bool isPcrValidate(int64_t prePcr, int64_t curPcr);
    const char *pcrToTime(int64_t pcr);
    int roundDouble(double number);
private:

    std::map<int64_t, std::list<TSDemux::STREAM_PKT*>*> mParserdData;
    std::map<int64_t, const tsParam*> mTsSegments;
    std::set<int64_t> mVideoFrameDistanceSets;
    std::set<int64_t> mAudioFrameDistanceSets;

    printParam mPrintParam;

    int mVideoPid;
    int mAudioPid;
    int mCurrentTsSegmentIndex;
    int64_t mLastAudioDts;
    int64_t mLastVideoDts;
    int64_t mLastVideoPts;
    uint64_t mLastPCR;
    char mTimeBuffer[128];

    typedef std::map<int64_t, TSDemux::STREAM_PKT*>::iterator mapIndex;
    std::map<int64_t, TSDemux::STREAM_PKT*> mVideoData;
    std::map<int64_t, TSDemux::STREAM_PKT*> mAudioData;
    std::map<int64_t, TSDemux::STREAM_PKT*> mPcrData;
};

}
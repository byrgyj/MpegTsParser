#pragma once
#include <list>
#include <map>
#include <set>
#include "elementaryStream.h"

enum printMediaType { PRINT_VIDEO, PRINT_AUDIO, PRINT_ALL };

class ParseredDataContainer
{
public:
    ParseredDataContainer(int printMediaType = PRINT_AUDIO);
    ~ParseredDataContainer();

    void addData(std::list<TSDemux::STREAM_PKT*> *lstData, int index);
    void printInfo();
    void printCurrentList(std::list<TSDemux::STREAM_PKT*> *lst, int index);
private:
  
    void printVideoInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index);
    void printAudioInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index);
    void printAllMediaInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index);
private:

    std::map<int, std::list<TSDemux::STREAM_PKT*>*> mParserdData;
    std::set<int64_t> mVideoDistanceSets;

    int mPrintType;
    int mVideoPid;
    int mAudioPid;
    int64_t mLastAudioDts;
    int64_t mLastVideoDts;
    int64_t mLastVideoPts;
};


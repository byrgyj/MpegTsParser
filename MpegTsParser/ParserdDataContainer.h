#pragma once
#include <list>
#include <map>
#include <set>
#include "elementaryStream.h"
class ParseredDataContainer
{
public:
    ParseredDataContainer();
    ~ParseredDataContainer();

    void addData(std::list<TSDemux::STREAM_PKT*> *lstData, int index);
    void printInfo();

private:
    void printCurrentList(std::list<TSDemux::STREAM_PKT*> *lst, int index);
    void printVideoInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index);
    void printAudioInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index);
private:
    //std::list<std::list<TSDemux::STREAM_PKT*>*> mParserdData;
    std::map<int, std::list<TSDemux::STREAM_PKT*>*> mParserdData;

    int64_t mLastAudioDts;
    int64_t mLastVideoDts;
};


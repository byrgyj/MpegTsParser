#include "StdAfx.h"
#include "ParserdDataContainer.h"
#include "debug.h"


ParseredDataContainer::ParseredDataContainer() : mLastVideoDts(0), mLastAudioDts(0){
}


ParseredDataContainer::~ParseredDataContainer(){
}

void ParseredDataContainer::addData(std::list<TSDemux::STREAM_PKT*> *lstData, int index) {
    //mParserdData.push_back(lstData);
    mParserdData.insert(std::make_pair(index, lstData));
}

void ParseredDataContainer::printInfo() {
     std::map<int, std::list<TSDemux::STREAM_PKT*>*>::iterator it = mParserdData.begin();
     int i = 0;
     while(it != mParserdData.end()) {
         std::list<TSDemux::STREAM_PKT*> *curList = it->second;
         printCurrentList(curList, it->first);

         it = mParserdData.erase(it);
         delete curList;
     }
}

void ParseredDataContainer::printCurrentList(std::list<TSDemux::STREAM_PKT*> *lst, int index) {
    if (lst == NULL) {
        return;
    }

    if (true) {
        printVideoInfo(lst, index);
    } else {
        printAudioInfo(lst, index);
    }
}

void ParseredDataContainer::printVideoInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index) {
    if (lst == NULL) {
        return;
    }

    std::list<TSDemux::STREAM_PKT*>::iterator it = lst->begin();
    std::map<int64_t, int64_t> mapContainer;
    while(it != lst->end()) {
        TSDemux::STREAM_PKT *pkt = *it;
        if (pkt->pid == 256){
            mapContainer.insert(std::make_pair(pkt->pts, pkt->dts));
        }   
        it++;
        delete pkt;
    }

    int64_t prePts = 0;
    std::map<int64_t, int64_t>::iterator mapIndex = mapContainer.begin();
    while (mapIndex != mapContainer.end()) {
         TSDemux::DBG(DEMUX_DBG_INFO, "[video-%d] pts=%lld, dts=%lld \n", index, mapIndex->first, mapIndex->second);
         if (prePts != 0) {
             if (mapIndex->first - prePts != 3600) {
                 TSDemux::DBG(DEMUX_DBG_INFO, "invalidate video pts=%lld, dts=%lld \n", mapIndex->first, mapIndex->second);
             }
         }

         prePts = mapIndex->first;
         mapIndex++;
    }
}

void ParseredDataContainer::printAudioInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index) {
    bool firstVideo = true;
    bool firstAudio = true;

    std::list<TSDemux::STREAM_PKT*>::iterator it = lst->begin();
    for (; it != lst->end(); ) {
        TSDemux::STREAM_PKT *pkt = *it;
        it = lst->erase(it);
        if (pkt != NULL) {
            if (pkt->pid == 257){
                if (firstAudio) {
                    firstAudio = false;
                    if (mLastAudioDts != 0 &&  pkt->dts - mLastAudioDts != 2880){
                        TSDemux::DBG(DEMUX_DBG_INFO, "invalidate frame distance:%d, cur pts:%lld, pre pts:%lld \n", pkt->dts - mLastAudioDts,  pkt->dts, mLastAudioDts);
                    }
                }
                mLastAudioDts = pkt->dts;
                TSDemux::DBG(DEMUX_DBG_INFO, "[audio-%d] pts=%lld, dts=%lld \n", index, pkt->pts, pkt->dts);
            }
            delete pkt;
        }
    }
}

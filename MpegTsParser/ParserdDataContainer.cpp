#include "StdAfx.h"
#include "ParserdDataContainer.h"
#include "debug.h"


ParseredDataContainer::ParseredDataContainer(int printMediaType) : mLastVideoDts(0), mLastAudioDts(0), mLastVideoPts(0), mPrintType(printMediaType), mVideoPid(256), mAudioPid(257){
}


ParseredDataContainer::~ParseredDataContainer(){
}

void ParseredDataContainer::addData(std::list<TSDemux::STREAM_PKT*> *lstData, int index) {
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

    if (mPrintType == PRINT_VIDEO) {
        printVideoInfo(lst, index);
    } else if (mPrintType == PRINT_AUDIO) {
        printAudioInfo(lst, index);
    } else {
        printAllMediaInfo(lst, index);
    }
}

void ParseredDataContainer::printVideoInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index) {
    if (lst == NULL) {
        return;
    }

    std::list<TSDemux::STREAM_PKT*>::iterator it = lst->begin();
    std::map<int64_t, int64_t> videoData;
    int64_t preDts = 0;
    while(it != lst->end()) {
        TSDemux::STREAM_PKT *pkt = *it;
        if (pkt->pid == mVideoPid){
            videoData.insert(std::make_pair(pkt->pts, pkt->dts));
            if (preDts != 0) {
                int64_t distance = pkt->dts - preDts;
                if (mVideoDistanceSets.find(distance) == mVideoDistanceSets.end()){
                    mVideoDistanceSets.insert(distance);
                }
            }
          preDts = pkt->dts;
        } 
        it++;
        delete pkt;
    }

    std::map<int64_t, int64_t>::iterator mapIndex = videoData.begin();
    while (mapIndex != videoData.end()) {
         TSDemux::DBG(DEMUX_DBG_INFO, "[video-%d] pts=%lld, dts=%lld \n", index, mapIndex->first, mapIndex->second);
         if (mLastVideoPts != 0) {
             int64_t distance = mapIndex->first - mLastVideoPts;
             if (mVideoDistanceSets.find(distance) == mVideoDistanceSets.end()) {
                 TSDemux::DBG(DEMUX_DBG_INFO, "invalidate video packet, distance:%lld, cur_pts=%lld, cur_dts=%lld, pre_pts:%lld \n", distance, mapIndex->first, mapIndex->second, mLastVideoPts);
             }
         }

         mLastVideoPts = mapIndex->first;
         mapIndex++;
    }

    TSDemux::DBG(DEMUX_DBG_INFO, "video frame distances size:%d ", mVideoDistanceSets.size());
    for(std::set<int64_t>::iterator it = mVideoDistanceSets.begin(); it != mVideoDistanceSets.end(); it++) {
        TSDemux::DBG(DEMUX_DBG_INFO, "  dis:%lld ", *it);
    }
    TSDemux::DBG(DEMUX_DBG_INFO, "\n");
}

void ParseredDataContainer::printAudioInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index) {
    bool firstAudio = true;

    std::list<TSDemux::STREAM_PKT*>::iterator it = lst->begin();
    for (; it != lst->end(); ) {
        TSDemux::STREAM_PKT *pkt = *it;
        it = lst->erase(it);
        if (pkt != NULL) {
            if (pkt->pid == mAudioPid){
                if (mLastAudioDts != 0 &&  pkt->dts - mLastAudioDts != 2880){
                    TSDemux::DBG(DEMUX_DBG_INFO, "invalidate frame distance:%d, cur pts:%lld, pre pts:%lld \n", pkt->dts - mLastAudioDts,  pkt->dts, mLastAudioDts);
                }
                mLastAudioDts = pkt->dts;
                TSDemux::DBG(DEMUX_DBG_INFO, "[audio-%d] pts=%lld, dts=%lld \n", index, pkt->pts, pkt->dts);
            }
            delete pkt;
        }
    }
}

void ParseredDataContainer::printAllMediaInfo(std::list<TSDemux::STREAM_PKT*> *lst, int index){
    if (lst == NULL) {
        return;
    }

    std::list<TSDemux::STREAM_PKT*>::iterator it = lst->begin();
    std::map<int64_t, int64_t> videoData;
    std::map<int64_t, int64_t> audioData;
    int64_t preDts = 0;
    while(it != lst->end()) {
        TSDemux::STREAM_PKT *pkt = *it;
        if (pkt->pid == mVideoPid){
            videoData.insert(std::make_pair(pkt->pts, pkt->dts));
            if (preDts != 0) {
                int64_t distance = pkt->dts - preDts;
                if (mVideoDistanceSets.find(distance) == mVideoDistanceSets.end()){
                    mVideoDistanceSets.insert(distance);
                }
            }
            preDts = pkt->dts;
        } else if (pkt->pid == mAudioPid) {
            audioData.insert(std::make_pair(pkt->pts, pkt->dts));
        }
        it++;
        delete pkt;
    }

    //int64_t prePts = 0;
    std::map<int64_t, int64_t>::iterator mapIndex = videoData.begin();
    while (mapIndex != videoData.end()) {
        TSDemux::DBG(DEMUX_DBG_INFO, "[video-%d] pts=%lld, dts=%lld \n", index, mapIndex->first, mapIndex->second);
        if (mLastVideoPts != 0) {
            int64_t distance = mapIndex->first - mLastVideoPts;
            if (mVideoDistanceSets.find(distance) == mVideoDistanceSets.end()) {
                TSDemux::DBG(DEMUX_DBG_INFO, "invalidate video packet, distance:%lld, cur_pts=%lld, cur_dts=%lld, pre_pts:%lld \n", distance, mapIndex->first, mapIndex->second, mLastVideoPts);
            }
        }

        mLastVideoPts = mapIndex->first;
        mapIndex++;
    }

    mapIndex = audioData.begin();
    while (mapIndex != audioData.end()) {
        if (mLastAudioDts != 0 && mapIndex->first - mLastAudioDts != 2880) {
            TSDemux::DBG(DEMUX_DBG_INFO, "invalidate frame distance:%d, cur pts:%lld, pre pts:%lld \n", mapIndex->first - mLastAudioDts,  mapIndex->first, mLastAudioDts);
        }
        mLastAudioDts = mapIndex->first;
        TSDemux::DBG(DEMUX_DBG_INFO, "[audio-%d] pts=%lld, dts=%lld \n", index, mapIndex->first, mapIndex->second);
        mapIndex++;
    }

    TSDemux::DBG(DEMUX_DBG_INFO, "video frame distances size:%d ", mVideoDistanceSets.size());
    for(std::set<int64_t>::iterator it = mVideoDistanceSets.begin(); it != mVideoDistanceSets.end(); it++) {
        TSDemux::DBG(DEMUX_DBG_INFO, "  dis:%lld ", *it);
    }
    TSDemux::DBG(DEMUX_DBG_INFO, "\n");
}
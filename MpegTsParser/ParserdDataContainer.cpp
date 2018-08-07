#include "StdAfx.h"
#include "ParserdDataContainer.h"
#include "debug.h"

namespace GYJ{

ParseredDataContainer::ParseredDataContainer(printParam pp) : mLastVideoDts(0), mLastAudioDts(0), mLastVideoPts(0), mPrintParam(pp), mVideoPid(256), mAudioPid(257), mCurrentTsSegmentIndex(0){
}

ParseredDataContainer::~ParseredDataContainer(){
}

void ParseredDataContainer::addData(std::list<TSDemux::STREAM_PKT*> *lstData, int64_t index) {
    mParserdData.insert(std::make_pair(index, lstData));
}
void ParseredDataContainer::addData(int64_t startTime, const tsParam *tsInfo) {
    mTsSegments.insert(std::make_pair(startTime, tsInfo));
}

void ParseredDataContainer::printInfo() {
     std::map<int64_t, const tsParam*>::iterator it = mTsSegments.begin();
     int i = 0;
     while(it != mTsSegments.end()) {
         const tsParam *tsSegment = it->second;
         if (tsSegment != NULL) {
             printCurrentList(tsSegment);

             it = mTsSegments.erase(it);
             delete tsSegment->packets;
             delete tsSegment;
         }
     }
}

void ParseredDataContainer::printCurrentList(const tsParam *tsSegment) {
    if (tsSegment == NULL) {
        return;
    }

    printTimeStamp(tsSegment);
}

bool ParseredDataContainer::isEnableVideoPrint() {
    return mPrintParam.printType == PRINT_MEDIA_ALL || mPrintParam.printType == PRINT_MEDIA_VIDEO;
}

bool ParseredDataContainer::isEnableAudioPrint() {
    return mPrintParam.printType == PRINT_MEDIA_ALL || mPrintParam.printType == PRINT_MEDIA_AUDIO;
}

bool ParseredDataContainer::checkCurrentPrint(int currentIndex, int totalCount) {
    if (mPrintParam.printLevel == PRINT_PARTLY_PTS && (currentIndex < 5 || currentIndex > totalCount - 5) || mPrintParam.printLevel == PRINT_ALL_PTS) {
        return true;
    } else {
        return false;
    }
}

void ParseredDataContainer::printTimeStamp(const tsParam *tsSegment){
    if (tsSegment == NULL) {
        return;
    }

    std::list<TSDemux::STREAM_PKT*> *lst = tsSegment->packets;
    std::map<int64_t, int64_t> videoData;
    std::map<int64_t, int64_t> audioData;

    dispatchPackets(lst, videoData, audioData);
    int currentIndex = 0;
    int packetCount = videoData.size();

    TSDemux::DBG(DEMUX_DBG_INFO, "[%d] file name:%s \n", mCurrentTsSegmentIndex++, tsSegment->fileName.c_str());
    // video info
    std::map<int64_t, int64_t>::iterator mapIndex = videoData.begin();
    while (mapIndex != videoData.end()) {
        int64_t pts = mapIndex->first;
        int64_t dts = mapIndex->second;

        if (mLastVideoPts != 0) {
            int64_t distance = mapIndex->first - mLastVideoPts;
            if (mVideoFrameDistanceSets.find(distance) == mVideoFrameDistanceSets.end()) {
                TSDemux::DBG(DEMUX_DBG_INFO, "invalidate video packet, distance:%lld, cur_pts=%lld, cur_dts=%lld, pre_pts:%lld \n", distance, pts, dts, mLastVideoPts);
            }
        }

        if (checkCurrentPrint(currentIndex, packetCount)) {
            TSDemux::DBG(DEMUX_DBG_INFO, "[video-%lld] pts=%I64d, dts=%I64d \n", tsSegment->tsStartTime, pts, dts);
            printf("[video-%lld] pts=%lld, dts=%lld \n", tsSegment->tsStartTime, pts, dts);
        }

        mLastVideoPts = mapIndex->first;
        currentIndex++;
        mapIndex++;
    }

    printFrameDistance(mVideoFrameDistanceSets, "video");

    // audio info
    currentIndex = 0;
    packetCount = audioData.size();
    mapIndex = audioData.begin();
    while (mapIndex != audioData.end()) {
        int64_t distance = mapIndex->first - mLastAudioDts;
        if (mLastAudioDts != 0 && mAudioFrameDistanceSets.find(distance) == mAudioFrameDistanceSets.end()) {
            TSDemux::DBG(DEMUX_DBG_INFO, "invalidate audio packet distance:%lld, cur pts:%lld, pre pts:%lld \n", distance,  mapIndex->first, mLastAudioDts);
        }

        if (checkCurrentPrint(currentIndex, packetCount)) {
            TSDemux::DBG(DEMUX_DBG_INFO, "[audio-%lld] pts=%lld, dts=%lld \n", tsSegment->tsStartTime, mapIndex->first, mapIndex->second);
        }
        mLastAudioDts = mapIndex->first;
        
        currentIndex++;
        mapIndex++;
    }

    printFrameDistance(mAudioFrameDistanceSets, "audio");
}

void ParseredDataContainer::dispatchPackets(const std::list<TSDemux::STREAM_PKT*> *lst, std::map<int64_t, int64_t> &videoPackets, std::map<int64_t, int64_t> &audioPackets) {
    if (lst == NULL) {
        return;
    }

    std::list<TSDemux::STREAM_PKT*>::const_iterator it = lst->begin();
    int64_t preVideoDts = -1;
    int64_t preAudioDts = -1;
    while(it != lst->end()) {
        TSDemux::STREAM_PKT *pkt = *it;
        if (isEnableVideoPrint() && pkt->pid == mVideoPid){
            videoPackets.insert(std::make_pair(pkt->pts, pkt->dts));
            if (preVideoDts != -1) {
                int64_t vDistance = pkt->dts - preVideoDts;
                if (mVideoFrameDistanceSets.find(vDistance) == mVideoFrameDistanceSets.end()){
                    mVideoFrameDistanceSets.insert(vDistance);
                }
            }
            preVideoDts = pkt->dts;
        } else if (isEnableAudioPrint() && pkt->pid == mAudioPid) {
            if (preAudioDts != -1) {
                int64_t aDistance = pkt->dts - preAudioDts;
                if (mAudioFrameDistanceSets.find(aDistance) == mAudioFrameDistanceSets.end()) {
                    mAudioFrameDistanceSets.insert(aDistance);
                }
            }
            audioPackets.insert(std::make_pair(pkt->pts, pkt->dts));
            preAudioDts = pkt->dts;
        }
        it++;
        delete pkt;
    }
}

void ParseredDataContainer::printFrameDistance(std::set<int64_t> &Distances, std::string tag) {
    if (!Distances.empty()){
        TSDemux::DBG(DEMUX_DBG_INFO, "%s frame distances size:%d ", tag.c_str(), Distances.size());
        for(std::set<int64_t>::iterator it = Distances.begin(); it != Distances.end(); it++) {
            TSDemux::DBG(DEMUX_DBG_INFO, "  dis:%lld ", *it);
        }
        TSDemux::DBG(DEMUX_DBG_INFO, "\n");
    }
}

}
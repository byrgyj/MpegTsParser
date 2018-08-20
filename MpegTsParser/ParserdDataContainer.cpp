#include "StdAfx.h"
#include "ParserdDataContainer.h"
#include "debug.h"
#include "Tool.h"
#include "CommandLine.h"

namespace QIYI{

ParseredDataContainer::ParseredDataContainer(printParam pp) : mLastVideoDts(0), mLastAudioDts(0), mLastVideoPts(0), mPrintParam(pp), mVideoPid(256), mAudioPid(257),
    mLastPCR(0), mCurrentTsSegmentIndex(0){
}

ParseredDataContainer::~ParseredDataContainer(){
}

void ParseredDataContainer::addData(std::list<QIYI::STREAM_PKT*> *lstData, int64_t index) {
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
    if (mPrintParam.printLevel == PRINT_PARTLY_PTS && (currentIndex < 3 || currentIndex >= totalCount - 3) || mPrintParam.printLevel == PRINT_ALL_PTS) {
        return true;
    } else {
        return false;
    }
}

bool ParseredDataContainer::checkPrintPcr(int currentIndex, int totalPkt) {
    if (CommandLine::getInstance()->getCommandLineParam().printPcr != 0) {
        return true;
    } else {
        return currentIndex < 3 || currentIndex > totalPkt - 3;
    }
}

void ParseredDataContainer::printTimeStamp(const tsParam *tsSegment){
    if (tsSegment == NULL) {
        return;
    }

    std::list<QIYI::STREAM_PKT*> *lst = tsSegment->packets;
 
    mVideoData.clear();
    mAudioData.clear();
    mPcrData.clear();

    dispatchPackets(lst);

    bool videoStreamValidate = true;
    bool audioStreamValidate = true;

    int currentIndex = 0;
    int packetCount = mVideoData.size();

    QIYI::DBG(DEMUX_DBG_INFO, "###:) \n");
    QIYI::DBG(DEMUX_DBG_INFO, "[%d] file name:%s \n", mCurrentTsSegmentIndex++, tsSegment->fileName.c_str());
    QIYI::DBG(DEMUX_DBG_INFO, "###:) \n");

    processVideo();
    processAudio();
    processPCR();
}

void ParseredDataContainer::processVideo() {
    int currentIndex = 0;
    int packetCount = mVideoData.size();
    bool videoStreamValidate = true;

    mapIndex it = mVideoData.begin();
    while (it != mVideoData.end()) {
        QIYI::STREAM_PKT *packet = it->second;
        if (packet == NULL) {
            continue;
        }

        int64_t pts = it->first;
        int64_t dts = packet->dts;

        if (mLastVideoPts != 0) {
            int64_t distance = it->first - mLastVideoPts;
            if (mVideoFrameDistanceSets.find(distance) == mVideoFrameDistanceSets.end()) {
                QIYI::DBG(DEMUX_DBG_INFO, "video pts is discontinuity, distance:%lld, cur_pts=%lld, cur_dts=%lld, pre_pts:%lld \n", distance, pts, dts, mLastVideoPts);
                videoStreamValidate = false;
            }
        }

        int64_t distance = it->first - dts;
        if (distance >= 90000) {
            QIYI::DBG(DEMUX_DBG_INFO, "video pts:%lld - dts:%lld > 90000 \n", it->first, dts);
            if (CommandLine::getInstance()->getCommandLineParam().checkPacketBufferOut > 0){
                printf("[V] pts(%lld)-dts(%lld)=%lld, out of range (90K)!!!! \n", it->first, dts, distance);
            }
        }

        if (checkCurrentPrint(currentIndex, packetCount)) {
            QIYI::DBG(DEMUX_DBG_INFO, "[V] pts=%lld, dts=%lld\n", pts, dts);
            //printf("[video-%lld] pts=%lld, dts=%lld \n", tsSegment->tsStartTime, pts, dts);
        }

        mLastVideoPts = it->first;
        currentIndex++;
        it++;
    }

    printf("video stream pts : %s ", videoStreamValidate ? "validate" : "invalidate!!");

    printFrameDistance(mVideoFrameDistanceSets, "video");
}

void ParseredDataContainer::processAudio() {
    if (mAudioData.empty()) {
        return;
    }

    bool audioStreamValidate = true;
    int currentIndex = 0;
    int packetCount = mAudioData.size();
    mapIndex it = mAudioData.begin();
    while (it != mAudioData.end()) {
        QIYI::STREAM_PKT *packet = it->second;
        if (packet == NULL) {
            continue;
        }

        int64_t distance = packet->dts - mLastAudioDts - mLastAudioDuration;
        //uint64_t dis = fabs((double)distance - mLastAudioDuration);
        if (mLastAudioDts != 0 && distance > 1){
            QIYI::DBG(DEMUX_DBG_INFO, "audio pts is discontinuity, distance:%lld, cur pts:%lld, pre pts:%lld \n", distance,  it->first, mLastAudioDts);
            audioStreamValidate = false;
        }

        if (checkCurrentPrint(currentIndex, packetCount)) {
            QIYI::DBG(DEMUX_DBG_INFO, "[A] pts=%lld, dts=%lld, next pts:%lld \n", it->first, packet->dts, packet->dts + packet->duration);
        }
        mLastAudioDts = it->first;
        mLastAudioDuration = packet->duration;
        currentIndex++;
        it++;

        delete packet;
    }

    printf("audio stream pts : %s \n",  audioStreamValidate ? "validate" : "invalidate!!");
}

void ParseredDataContainer::processPCR() {
    if (mPcrData.empty()) {
        return;
    }

    int curIndex = 0; 
    int totalPacket = mPcrData.size();
    mapIndex it = mPcrData.begin();
    while (it != mPcrData.end()){
        QIYI::STREAM_PKT *packet = it->second;
        if (packet == NULL) {
            continue;
        }

        if (checkPrintPcr(curIndex++, totalPacket) && packet->pcr.pcr != 0) {
            //double time = pcrToTime(packet->pcr.pcr_base);
            QIYI::DBG(DEMUX_DBG_INFO, "[V-PCR]pcr:%lld, time:%s \n", packet->pcr.pcr, pcrToTime(packet->pcr.pcr_base));
        }

        if (mLastPCR != 0 && packet->pcr.pcr != 0 && isPcrValidate(mLastPCR, packet->pcr.pcr)) {
            QIYI::DBG(DEMUX_DBG_INFO, "pcr is discontinuity, current dts:%lld,  current pcr:%lld, pre pcr:%lld \n", it->first, packet->pcr.pcr, mLastPCR);
            printf("pcr is discontinuity, current dts:%lld,  current pcr:%lld, pre pcr:%lld \n", it->first, packet->pcr.pcr, mLastPCR);
        }
        mLastPCR = packet->pcr.pcr;
        it++;
        delete packet;
    }
}

bool ParseredDataContainer::isPcrValidate(int64_t prePcr, int64_t curPcr) {
    return (curPcr - prePcr > 1080000 * 2.5); // 0.1s
}

const char *ParseredDataContainer::pcrToTime(int64_t pcr) {
    double seconds =  pcr * 300 / (double)27000000;

    int hours = (int)seconds / 3600;
    int mins = (int)(seconds - hours * 3600) / 60;
    int secs = (int)seconds - hours * 3600 - mins * 60;

    int millSecs = roundDouble((seconds - (int)seconds) * 1000);

    memset(mTimeBuffer, sizeof(mTimeBuffer), 0);
    sprintf(mTimeBuffer, "%02d:%02d:%02d.%03d", hours, mins, secs, millSecs);

    return mTimeBuffer;
}

void ParseredDataContainer::dispatchPackets(const std::list<QIYI::STREAM_PKT*> *lst) {
    if (lst == NULL) {
        return;
    }

    std::list<QIYI::STREAM_PKT*>::const_iterator it = lst->begin();
    int64_t preVideoDts = -1;
    int64_t preAudioDts = -1;
    while(it != lst->end()) {
        QIYI::STREAM_PKT *pkt = *it;
        if (isEnableVideoPrint() && pkt->pid == mVideoPid){
            mVideoData.insert(std::make_pair(pkt->pts, pkt));
            mPcrData.insert(std::make_pair(pkt->dts, pkt));
            if (preVideoDts != -1) {
                int64_t vDistance = pkt->dts - preVideoDts;
                if (mVideoFrameDistanceSets.find(vDistance) == mVideoFrameDistanceSets.end()){
                    mVideoFrameDistanceSets.insert(vDistance);
                }
            }
            preVideoDts = pkt->dts;
        } else if (isEnableAudioPrint() && pkt->pid == mAudioPid) {
            mAudioData.insert(std::make_pair(pkt->pts, pkt));
            preAudioDts = pkt->dts;
        }
        it++;
        //delete pkt;
    }
}

void ParseredDataContainer::printFrameDistance(std::set<int64_t> &Distances, std::string tag) {
    if (!Distances.empty()){

        QIYI::DBG(DEMUX_DBG_INFO, "%s frame duration count:%d ", tag.c_str(), Distances.size());
        for(std::set<int64_t>::iterator it = Distances.begin(); it != Distances.end(); it++) {
            QIYI::DBG(DEMUX_DBG_INFO, "  duration:%lld ", *it);
        }
        QIYI::DBG(DEMUX_DBG_INFO, "\n");
    }
}

int ParseredDataContainer::roundDouble(double number){
    return (number > 0.0) ? (number + 0.5) : (number - 0.5); 
}

}
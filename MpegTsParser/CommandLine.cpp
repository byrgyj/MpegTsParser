#include "StdAfx.h"
#include "CommandLine.h"

namespace QIYI {

CommandLine *CommandLine::sInstance = NULL;


CommandLine::CommandLine(void){
}

CommandLine::~CommandLine(void){
}

CommandLine *CommandLine::getInstance() {
    if (sInstance == NULL) {
        sInstance = new CommandLine;
    }

    return sInstance;
}

void CommandLine::setCommandLineParam(CommandLineParam param) {
    mParam = param;
}

}
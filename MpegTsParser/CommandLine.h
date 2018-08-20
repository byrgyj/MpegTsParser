
#include <string>
#include "ParserdDataContainer.h"
namespace QIYI {

typedef struct CommandLineParam {
    CommandLineParam() : printMediaType(PRINT_MEDIA_ALL), printPtsType(PRINT_PARTLY_PTS), checkPacketBufferOut(0), printPcr(0) {}
    int printMediaType;
    int printPtsType;
    int checkPtsDtsDistance;
    int checkPacketBufferOut;
    int printPcr;

    std::string filePath;
} CommandLineParam;

class CommandLine
{
public:
    CommandLine(void);
    ~CommandLine(void);


    static CommandLine *getInstance();

    void setCommandLineParam(CommandLineParam param);
    const CommandLineParam getCommandLineParam() { return mParam; }
private:
    CommandLineParam mParam;
    static CommandLine *sInstance;
};

}
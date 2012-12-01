#include "apsara/common/random.h"
#include "apsara/common/flag.h"
#include "pangu_helper.h"
#include "append_store_types.h"

/* for QFS File System */
#include <iostream>
#include <fstream>
#include <cerrno>

extern "C" {
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
}

#include "libclient/KfsClient.h"
#include "libclient/KfsAttr.h"

using std::cout;
using std::cerr;
using std::endl;
using std::ifstream;
using std::string;
using std::vector;

/* for QFS File System */

// using namespace apsara::pangu;
using namespace apsara::AppendStore;


DEFINE_FLAG_INT32(append_store_LogFile_RetryCount, "retry count for log file operation", 1);
DEFINE_FLAG_INT32(append_store_LogFile_MinSleepInterval, "the minimum sleep interval when an exception happend. (us)", 100 * 1000);
DEFINE_FLAG_INT32(append_store_LogFile_MaxSleepInterval, "the maximum sleep interval when an exception happend. (us)", 500 *  1000);


int64_t QFSHelper::GetFileSize(const std::string& fname)
{
    KFS::KfsClient *kfsClient = NULL;
    kfsClient = KFS::Connect(serverHost, port);
    KFS::KfsFileAttr kfsattr = new KfsFileAttr(); // Stat method takes reference of this object
    try
    {
     kfsClient->Stat(fname.c_str(), kfsattr, true); // true is for computing the size
    }
    catch (Exception e) // check this
    {
        return 0;
    }
    return kfsattr.fileSize;
}

void QFSHelper::CreateFile(const std::string& fname, int min, int max, const std::string& appname, const std::string& partname)
{
    KFS::KfsClient kfsClient = NULL;
    kfsClient = KFS::Connect(serverHost, port);

    try
    {
	if ((fd = kfsClient->Create(fname.c_str())) < 0) {
        cout << "Create failed: " << KFS::ErrorCodeToStr(fd) << endl;
        exit(-1);
    	}
  	// Create(const char *pathname, int numReplicas, bool exclusive, int numStripes, int numRecoveryStripes, int stripeSize, int stripedType, bool forceTypeFlag, kfsMode_t mode)
    }
    catch(Exception e)
    {
        throw;
    }
}

void QFSHelper::CreateLogFile(const std::string& fname, int min, 
    int max, const std::string& appname, const std::string& partname)
{
    KFS::KfsClient kfsClient = NULL;
    kfsClient = KFS::Connect(serverHost, port);

    try
    {
	if ((fd = kfsClient->Create(fname.c_str())) < 0) {
        cout << "Create failed: " << KFS::ErrorCodeToStr(fd) << endl;
        exit(-1);
    	}
  	// Create(const char *pathname, int numReplicas, bool exclusive, int numStripes, int numRecoveryStripes, int stripeSize, int stripedType, bool forceTypeFlag, kfsMode_t mode)
    }
    catch(Exception e)
    {
        throw;
    }
    
/*
    apsara::pangu::FileSystem* fileSystemPtr = apsara::pangu::FileSystem::GetInstance();
    try
    {
        fileSystemPtr->CreateLogFile(fname,
                           min, max,
                           appname, partname,
                           apsara::security::CapabilityGenerator::Generate(std::string("pangu://"),apsara::security::PERMISSION_ALL));
    }
    catch(apsara::ExceptionBase& e)
    {
        throw;
    }
*/
}

bool QFSHelper::IsFileExist(const std::string& fname)
{
 KFS::KfsClient *kfsClient = NULL;
 kfsClient = KFS::Connect(serverHost, port);
 return kfsClient->Exists(fname.c_str());
}

bool QFSHelper::IsDirectoryExist(const std::string& dirname)
{
 KFS::KfsClient *kfsClient = NULL;
 kfsClient = KFS::Connect(serverHost, port);
 return kfsClient->Exists(fname.c_str());
}

/*
Opening file 

int
KfsClient::Open(const char *pathname, int openFlags, int numReplicas,
    int numStripes, int numRecoveryStripes, int stripeSize, int stripedType,
    kfsMode_t mode)
{
    return mImpl->Open(pathname, openFlags, numReplicas,
        numStripes, numRecoveryStripes, stripeSize, stripedType, mode);
}

// Re-open the file
    if ((fd = gKfsClient->Open(newFilename.c_str(), O_RDWR)) < 0) {
        cout << "Open on : " << newFilename << " failed: " << KFS::ErrorCodeToStr(fd) << endl;
        exit(-1);
    }

*/

LogFileOutputStreamPtr QFSHelper::OpenLog4Append(const std::string& fileName)
{
    int32_t retryCount = 0;
    do
    { 
        try
        {
            LogFileOutputStreamPtr os = FileSystem::GetInstance()->OpenLog4Append(fileName);
            return os;
        }
        catch (const PanguFileLockException& e)
        {
            LOG_INFO(sLogger, ("PanguFileLockException", e.ToString())
                ("FileName", fileName)("RetryCount", retryCount));
            ReleaseLogFileLock(fileName);
        }
        catch (const ExceptionBase& e)
        {
            LOG_WARNING(sLogger, ("ExceptionBase", e.ToString())
                ("FileName", fileName)("RetryCount", retryCount));
            if (++retryCount <= INT32_FLAG(append_store_LogFile_RetryCount))
            {
                usleep(sRandom.Get(INT32_FLAG(append_store_LogFile_MinSleepInterval),
                    INT32_FLAG(append_store_LogFile_MaxSleepInterval)));
            }
        }
    } while (retryCount <= INT32_FLAG(append_store_LogFile_RetryCount));

    LOG_WARNING(sLogger, ("OpenLog4Append", "Fail")("FileName", fileName));
    APSARA_THROW(AppendStoreExceptionBase, "OpenLog4Append fail, fileName:" + fileName);
}


LogFileInputStreamPtr QFSHelper::OpenLog4Read(const std::string& fileName)
{
    int32_t retryCount = 0;
    do
    {
        try
        {
            LogFileInputStreamPtr is = FileSystem::GetInstance()->OpenLog4Read(fileName);
            return is;
        }
        catch (const PanguFileLockException& e)
        {
            LOG_INFO(sLogger, ("PanguFileLockException", e.ToString())
                ("FileName", fileName)("RetryCount", retryCount));
            ReleaseLogFileLock(fileName);
        }
        catch (const ExceptionBase& e)
        {
            LOG_WARNING(sLogger, ("ExceptionBase", e.ToString())
                ("FileName", fileName)("RetryCount", retryCount));
            if (++retryCount <= INT32_FLAG(append_store_LogFile_RetryCount))
            {
                usleep(sRandom.Get(INT32_FLAG(append_store_LogFile_MinSleepInterval),
                    INT32_FLAG(append_store_LogFile_MaxSleepInterval)));
            }
        }
    } while (retryCount <= INT32_FLAG(append_store_LogFile_RetryCount));

    LOG_ERROR(sLogger, ("OpenLog4Read", "Fail")("FileName", fileName));
    APSARA_THROW(AppendStoreExceptionBase, "OpenLog4Read fail, fileName:" + fileName);
}

bool QFSHelper::ReleaseLogFileLock(const std::string& fileName)
{
    int32_t retryCount = 0;
    do
    {
        try
        {
            FileSystem::GetInstance()->ReleaseLogFileLock(fileName);
            return true;
        }
        catch (const ExceptionBase& e)
        {
            LOG_WARNING(sLogger, ("ExceptionBase", e.ToString())
                ("FileName", fileName)("RetryCount", retryCount));
            if (++retryCount <= INT32_FLAG(append_store_LogFile_RetryCount))
            {
                usleep(sRandom.Get(INT32_FLAG(append_store_LogFile_MinSleepInterval),
                    INT32_FLAG(append_store_LogFile_MaxSleepInterval)));
            }
        }
    } while (retryCount <= INT32_FLAG(append_store_LogFile_RetryCount));

    LOG_ERROR(sLogger, ("ReleaseLogFileLock", "Fail")("FileName", fileName));
    return false;
}


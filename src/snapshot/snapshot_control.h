#ifndef _SNAPSHOT_CONTROL_H_
#define _SNAPSHOT_CONTROL_H_
#include <iostream>
#include <cstdlib>
#include "../include/store.h"
#include "../include/exception.h"
#include "../append-store/append_store_types.h"
#include "../append-store/append_store.h"
#include "data_source.h"
#include "../fs/qfs_file_helper.h"
#include "../fs/qfs_file_system_helper.h"
#include <log4cxx/logger.h>
#include <log4cxx/xml/domconfigurator.h>

using namespace std;
using namespace log4cxx;
using namespace log4cxx::xml;
using namespace log4cxx::helpers;
using namespace std;

static string append_store_base_path = "root";

class SnapshotControl {
public:
    SnapshotControl(const string& trace_file);
    ~SnapshotControl();
    bool LoadSnapshotMeta();
    bool SaveSnapshotMeta();
    /*
    bool LoadSegmentMeta();
    bool SaveSegmentMeta();
    */
    void SetAppendStore(PanguAppendStore* pas);
public:
    string trace_file_;
    string os_type_;
    string disk_type_;
    string vm_id_;
    string snapshot_id_;
    string store_path_;
    string ss_meta_filename_;
    SnapshotMeta ss_meta_;
private:
    void ParseTraceFile();
private:
    PanguAppendStore* pas_;
    SnapshotMeta ssmeta_;
    static LoggerPtr logger_;
};


#endif // _SNAPSHOT_CONTROL_H_





















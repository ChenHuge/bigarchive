#include "../snapshot/data_source.h"
#include <fstream>
#include "socketstream.h"

using namespace std;
using namespace galik;

int main(int argc, char** argv)
{
    string trace("/home/gautham/VM-2C0A05C1.1000-19189-8071-merged.vhd.bv4");
    string sample_data("sample_data");

    DataSource source(trace, sample_data);
    SegmentMeta sm;

    /*
    string output_file("data_source_test.output");
    ofstream os(output_file.c_str(), ios_base::out | ios_base::binary | ios_base::trunc);
    while (source.GetSegment(sm)) {
        for (size_t i = 0; i < sm.block_list_.size(); ++i)
        {
            os.write(sm.block_list_[i].data_, sm.GetBlockSize(i));
        }
    }
    os.close();
    */
    net::socketstream ss;
    ss.open("localhost", 12346);
    uint16_t get = 1;
    uint32_t totalsize = 0;

    while (source.GetSegment(sm)) {
        for (size_t i = 0; i < sm.block_list_.size(); ++i)
        {
            totalsize = sizeof(get) + CKSUM_LEN;
            // Writing the total size of the data to be sent before sending the data.
            ss.write((char*)&totalsize, sizeof(totalsize));
            ss.write((char*)&get, sizeof(get));
            ss.write((char*)sm.block_list_[i].cksum_, CKSUM_LEN);
        }
    }

    ss.close();
}

















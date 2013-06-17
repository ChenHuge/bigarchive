/*
 * Read a snapshot recipe, dedup it against its parent snapshot and CDS
 * This simulation uses lru strategy at level 2
 */
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <set>
#include <exception>
#include "dedup_types.hpp"
#include "lru_cache.hpp"

using namespace std;

class Hash {
    private:
    Checksum cksum_;
    public:
    Hash() {
    }
    Hash(Block& blk) {
        memcpy(cksum_, blk.cksum_, CKSUM_LEN * sizeof(uint8_t));
    }
    Hash(const Hash& h)
    {
        memcpy(cksum_, h.cksum_, CKSUM_LEN * sizeof(uint8_t));
    }
    void setHash(Block& blk) {
        memcpy(cksum_, blk.cksum_, CKSUM_LEN * sizeof(uint8_t));
    }
    bool operator==(const Hash& other) const {
        return memcmp(this->cksum_, other.cksum_, CKSUM_LEN) == 0;
    }

    bool operator!=(const Hash& other) const {
        return memcmp(this->cksum_, other.cksum_, CKSUM_LEN) != 0;
    }

    bool operator<(const Hash& other) const {
        return memcmp(this->cksum_, other.cksum_, CKSUM_LEN) < 0;
    }
};
void usage(char *progname)
{
	pr_msg("This program read a snapshot recipe, dedup and dedups it.\n"
           "It uses the High Performance Deduplication System paper algorithm\n"
	       "Usage: %s CDS cache_size snapshot [parent]", progname);
}

/*
 * Load a 2MB segment from trace
 */

bool load_segment(Segment& seg, ifstream& is)
{
    Block blk;
    seg.Init();
    while (blk.Load(is)) {
        if (blk.GetSize() == 0) {	// fix the zero-sized block bug in scanner
            pr_msg("ignore zero-sized block");
            continue;
        }
        seg.AddBlock(blk);
        if (seg.GetSize() >= 2*1024*1024)
            break;
    }
    //seg.Final();
    if (seg.GetSize() == 0 || seg.blocklist_.size() == 0) 
        return false;
    else
        return true;
}

//loads a segment and makes random modifications
bool load_rand_segment(Segment& seg, ifstream& is, double blk_threshold)
{
    if (blk_threshold == 0)
        return load_segment(seg,is);
    Block blk;
    seg.Init();
    double rd;
    while (blk.Load(is)) {
        if (blk.GetSize() == 0) {	// fix the zero-sized block bug in scanner
            pr_msg("ignore zero-sized block");
            continue;
        }
        rd = (double)rand() / RAND_MAX;
        if (rd <= blk_threshold)
        {
            rd = (double)rand() / RAND_MAX;
            memcpy(&blk.cksum_[4],&rd,sizeof(rd));
        }
        seg.AddBlock(blk);
        if (seg.GetSize() >= 2*1024*1024)
            break;
    }
    //seg.Final();
    if (seg.GetSize() == 0 || seg.blocklist_.size() == 0) 
        return false;
    else
        return true;
}

void theoretical(std::vector<Segment>& indexBlocks, ifstream& trace, double blk_threshold, unsigned int seed)
{
    //trace.clear();
    //trace.seekg(0,std::ios::beg);
    if (blk_threshold > 0)
        srand(seed);
    map<Block, int> index;
    map<Block, int>::iterator index_it;
    Segment current_seg;
    int segments = 0;
    int blocks = 0;
    int hits = 0;
    int misses = 0;
    int i,j;
    for(i = 0; i < indexBlocks.size(); i++)
    {
        for(j = 0; j < indexBlocks[i].blocklist_.size(); j++)
            index[indexBlocks[i].blocklist_[j]] = segments & (~7); //group blocks by segment
        segments++;
        blocks += indexBlocks[i].blocklist_.size();
    }
    //printf("segments: %d\nblocks: %d\n",segments,blocks);
    while(load_rand_segment(current_seg, trace, blk_threshold)) {
        for (i = 0; i < current_seg.blocklist_.size(); i++)
        {
            index_it = index.find(current_seg.blocklist_[i]);
            if (index_it != index.end())
                hits++;
            else misses;
        }
        //printf("chunk finished (hits=%d;misses=%d)\n",hits,misses);
    }

    //pr_msg("theoretical dedup finish");
    printf("theoretical dedup ratio = %d/%d (%3.3f%%)\n",hits,blocks,(double)(100*hits)/(double)blocks);
}

void min_heapify(int *heap, int len, int i)
{
    int left,right,smallest,t;
    do {
        left = (i << 1) + 1;
        right = (i << 1) + 2;
        smallest = i;
        if (left < len && heap[left] < heap[smallest])
            smallest = left;
        if (right < len && heap[right] < heap[smallest])
            smallest = right;
        if (smallest != i) {
            t = heap[i];
            heap[i] = heap[smallest];
            heap[smallest] = t;
            i = smallest;
        } else {
            break;
        }
    } while (i < ((len - 1) & (~1)));
}

void delete_min(int* heap, int *len)
{
    if (*len < 1)
        return;
    (*len)--;
    if (*len > 0)
    {
        heap[0] = heap[*len];
        min_heapify(heap,*len,0);
    }
}

void insert_min(int *heap, int *len, int n)
{
    int i = *len;
    int parent = (i - 1) >> 1;
    int t;
    heap[i] = n;
    while(i > 0 && heap[parent] > heap[i]) {
        t = heap[i];
        heap[i] = heap[parent];
        heap[parent] = t;
        i = parent;
        parent = (i - 1) >> 1;
    }
    (*len)++;
}

void topk_heap(int* heap, int* cur_length, int max_length, int n)
{
    if (*cur_length >= max_length)
        delete_min(heap,cur_length);
    insert_min(heap,cur_length, n);
}

void do_block_freq(map<Hash, int>& block_freq, ifstream& trace, double cds_percent, double blk_threshold, unsigned int seed) {
    //trace.clear();
    //trace.seekg(0,std::ios::beg);
    if (blk_threshold > 0)
        srand(seed);
    map<Hash, int>::iterator iter;
    Segment current_seg;
    Hash current_hash;
    set<Hash> seg_blocks;
    int i;
    long int blocks = 0;
    int newBlocks = 0;
    int uniqBlocks = 0;
    //printf("segments: %d\nblocks: %d\n",segments,blocks);
    while(load_rand_segment(current_seg, trace, blk_threshold)) {
        for (i = 0; i < current_seg.blocklist_.size(); i++) {
            current_hash.setHash(current_seg.blocklist_[i]);
            if (seg_blocks.find(current_hash) == seg_blocks.end()) {
                seg_blocks.insert(current_hash);
                block_freq[current_hash]++;
                if (block_freq[current_hash] == 1)
                    newBlocks++;
                uniqBlocks++;
            }
            blocks++;
        }
        //printf("chunk finished (hits=%d;misses=%d)\n",hits,misses);
    }

    //pr_msg("trace block frequency scan finished");
    printf("new index size: %d\n", block_freq.size());
    printf("blocks read: %lld\nunique blocks read: %d\nnew blocks read: %d\n",blocks, uniqBlocks,newBlocks);
    int sum = 0;
    int max = 0;
    int entries = 0;
    if (cds_percent > 0) {
        int cds_size = (int)((double)(block_freq.size()) * cds_percent);
        int cds_len = 0;
        int cds_sum = 0;
        int *cds_entries = (int*)malloc(sizeof(int)*cds_size);

        for(iter = block_freq.begin(); iter != block_freq.end(); ++iter) {
            int count = iter->second;
            topk_heap(cds_entries, &cds_len, cds_size, count);
            sum += count;
            entries++;
            //printf("%d\n",count);
        }
        for(i = 0; i < cds_len; i++) {
            cds_sum += cds_entries[i];
            if (cds_entries[i] > max) {
                max = cds_entries[i];
            }
        }
        printf("Max Links: %d\nCDS Entries: %d\n",max,cds_len);
        free(cds_entries);
        printf("Avg links (including CDS blocks): %g\n", (double)sum / (double)entries);
        printf("Avg links (excluding CDS blocks): %g\n", (double)(sum-cds_sum) / (double)(entries - cds_len));
        printf("Avg CDS block links: %g\n", (double)cds_sum / (double)cds_len);
    } else {
        int maxCount = 0;
        for(iter = block_freq.begin(); iter != block_freq.end(); ++iter) {
            int count = iter->second;
            if (count > max) {
                max = count;
                maxCount = 1;
            } else if (count == max) {
                maxCount++;
            }
            sum += count;
            entries++;
            //printf("%d\n",count);
        }
        printf("Max: %d\nMax Entries: %d\nAvg: %g\n", max, maxCount, (double)sum / (double)entries);
    }
}

void container_freq(map<Hash, int>& container_freq, map<int, int>& containerCounts, int& current_container, int& container_length, ifstream& trace, double blk_threshold, unsigned int seed) {
    //trace.clear();
    //trace.seekg(0,std::ios::beg);
    if (blk_threshold > 0)
        srand(seed);
    map<Hash, int>::iterator iter;
    Segment current_seg;
    Hash current_hash;
    set<Hash> seg_blocks;
    int i;
    int blocks = 0;
    int newBlocks = 0;
    //printf("segments: %d\nblocks: %d\n",segments,blocks);
    while(load_rand_segment(current_seg, trace, blk_threshold)) {
        for (i = 0; i < current_seg.blocklist_.size(); i++) {
            current_hash.setHash(current_seg.blocklist_[i]);
            if (seg_blocks.find(current_hash) == seg_blocks.end()) {
                seg_blocks.insert(current_hash);
                container_freq[current_hash]++;
                newBlocks++;
            }
            blocks++;
        }
        //printf("chunk finished (hits=%d;misses=%d)\n",hits,misses);
    }

    pr_msg("block frequency scan finished");
    for(iter = container_freq.begin(); iter != container_freq.end(); ++iter)
    {
        int count = iter->second;
        printf("%s\n",count);
    }
    printf("frequency list finished %d, %d\n", blocks, newBlocks);
}

void sampled(std::vector<Segment>& indexBlocks, ifstream& trace, int cache_size, double blk_threshold, unsigned int seed, bool do_dirty, bool do_parent)
{
    trace.clear();
    trace.seekg(0,std::ios::beg);
    if (blk_threshold > 0)
        srand(seed);
    LruCache cache(cache_size);
    int i, j, k;
    int blockIndex;
    int indexHits = 0, cacheHits = 0, misses = 0, cleanSegments = 0, parentHits = 0;
    map<Block, int> index;
    map<Block, int>::iterator index_it;
    Segment current_seg;
    Segment parent_seg;
    //now create the sampled index
    for (i = 0; i < indexBlocks.size(); i +=8) {
        //loop through every chunk in the container, sampling at least the first block
        blockIndex = 0;
        for(j = 0; j < 8 && (i + j) < indexBlocks.size(); j++)
        {
            for (k = 0; k < indexBlocks[i+j].blocklist_.size(); k ++) {
                if ((blockIndex++) % 100 == 0)
                    index[indexBlocks[i+j].blocklist_[k]] = i;
            }
        }
    }
    //printf("segments: %d\nblocks: %d\n",segments,blocks);

    //for each segment in the trace
    int segi = 0;
    //int t_count = 10000;
    //while(t_count > 0 && load_rand_segment(current_seg, trace, blk_threshold)) {
    while(/*t_count > 0 && */load_rand_segment(current_seg, trace, blk_threshold)) {
        //t_count--;
        //cache.AddItems(indexBlocks[segi++]);
        // prepare current segment, 0 means non-dup
        //for (i = 0; i < current_seg.blocklist_.size(); i ++)
            //current_seg.blocklist_[i].file_id_ = 0;
        
        if (do_dirty)
        {
            if (current_seg == indexBlocks[segi])
            {
                cleanSegments+= current_seg.blocklist_.size();
                segi++;
                continue;
            }
        }

        if (do_parent)
        {
            parent_seg = indexBlocks[segi];
            parent_seg.SortByHash();
        }
        
        //for each block in the segment
        for (i = 0; i < current_seg.blocklist_.size(); i++)
        {
            if (do_parent)
            {
                if (parent_seg.SearchBlock(current_seg.blocklist_[i]))
                {
                    parentHits++;
                    continue;
                }
            }
            //first check if it is in the cache
            if (cache.SearchItem(current_seg.blocklist_[i])) {
                cacheHits++;
                continue;
            }

            //next check in the index, if we get a hit cache the block
            index_it = index.find(current_seg.blocklist_[i]);
            if (index_it != index.end())
            {
                j = index_it->second; //index of the first container block
                for(k = j; k < (j + 8) && k < indexBlocks.size(); k++)
                {
                    cache.AddItems(indexBlocks[k]);
                }
                indexHits++;
            }
            else
                misses++;
        }
        //printf("chunk finished (c=%d;i=%d;m=%d)\n",cacheHits,indexHits,misses);
        segi++;
    }
    int hits = cacheHits+indexHits+cleanSegments+parentHits;
    int blocks = hits + misses;

    pr_msg("dedup finish");
    printf("cache Hits: %d\n",cacheHits);
    printf("index Hits: %d\n",indexHits);
    if(do_dirty) printf("clean segment blocks: %d\n",cleanSegments);
    if(do_parent) printf("parent Hits: %d\n",parentHits);
    printf("dedup Misses: %d\n",misses);
    printf("dedup ratio = %d/%d (%3.3f%%)\n",hits,blocks,(double)(100*hits)/(double)blocks);
}

int main(int argc, char** argv)
{
    //int cacheHits = 0;
    //int indexHits = 0;
    //int misses = 0;

    //std::vector<Block> cds;
    //std::vector<Segment> indexBlocks;
    //std::vector<Block>::iterator it;
    //Segment current_seg;
    ifstream current_input, trace_input;
    //ofstream output;
    uint32_t i, j, k;
    //bool isdup;
    Block blk;
    int blockIndex;
    map<Block, int> index;
    map<Block, int>::iterator index_it;
    map<Hash, int> block_freq;
    //uint64_t new_size = 0;

    //if (argc < 4 || argc > 5) {
        //usage(argv[0]);
        //return 0;
    //}
    int argindex = 1;
    bool do_dirty = false;
    bool do_parent = false;
    bool do_rand = false;
    bool use_trace_file = false;
    double blk_threshold = 0;
    double cds_percent = 0;
    int cds_every = 1;
    int trace_index = 0;
    unsigned int seed;

    while(argindex < argc && argv[argindex][0] == '-')
    {
        if (strcmp(argv[argindex],"--dirty") == 0)
        {
            do_dirty = true;
            argindex++;
        }
        else if (strcmp(argv[argindex],"--parent") == 0)
        {
            do_parent = true;
            argindex++;
        }
        else if (strcmp(argv[argindex],"--rand") == 0)
        {
            do_rand = true;
            argindex++;
            if ((argindex+1) >= argc)
            {
                usage(argv[0]);
                return 0;
            }
            blk_threshold = atof(argv[argindex++]);
            seed = (unsigned int)atol(argv[argindex++]);
        }
        else if (strcmp(argv[argindex],"--cds") == 0)
        {
            argindex++;
            if (argindex >= argc)
            {
                usage(argv[0]);
                return 0;
            }
            cds_percent = atof(argv[argindex++]);
        }
        else if (strcmp(argv[argindex],"--cdsevery") == 0)
        {
            argindex++;
            if (argindex >= argc)
            {
                usage(argv[0]);
                return 0;
            }
            cds_every = atoi(argv[argindex++]);
        }
	else if (strcmp(argv[argindex],"--tracefile") == 0)
	{
            use_trace_file = true;
            argindex++;
            if (argindex >= argc)
            {
                usage(argv[0]);
                return 0;
            }
            trace_input.open(argv[argindex++], std::ios_base::in);
            if (!trace_input.is_open()) {
                pr_msg("unable to open %s", argv[argindex-1]);
                exit(1);
            }
	}
        else if (strcmp(argv[argindex],"--") == 0)
        {
            argindex++;
            break;
        }
        else
        {
            cout << "Invalid argument. Use -- before using filename args beginning with '-'" << endl;
            usage(argv[0]);
            return 0;
        }
    }

    while((!use_trace_file && argindex < argc) || (use_trace_file && trace_input.good())) {
        string line;
        const char *cur_trace_path;
        double temp_cds_percent;
        if (use_trace_file) {
            getline(trace_input,line);
            cur_trace_path = line.c_str();
        } else {
            cur_trace_path = argv[argindex++];
        }

        if (strlen(cur_trace_path) > 0 && cur_trace_path[0] != '\n') {
            current_input.open(cur_trace_path, std::ios_base::in | std::ios_base::binary);
            if (!current_input.is_open()) {
                pr_msg("unable to open %s", cur_trace_path);
                exit(1);
            }
            printf("file: %s\n",cur_trace_path);
            //theoretical(indexBlocks, current_input, blk_threshold, seed);
            //sampled(indexBlocks, current_input, cache_size, blk_threshold, seed, do_dirty, do_parent);
            if ((trace_index % cds_every) == 0) {
                temp_cds_percent = cds_percent;
            } else {
                temp_cds_percent = 0;
            }
            do_block_freq(block_freq, current_input, temp_cds_percent, blk_threshold, seed);

            //output.close();
            //parent_input.close();
            current_input.close();
        }
        trace_index++;
    }
    return 0;
}

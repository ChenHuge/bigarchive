#include "snapshot_control.h"

const string kBasePath = "root";

LoggerPtr SnapshotControl::logger_ = Logger::getLogger("BigArchive.Snapshot.Control");

SnapshotControl::SnapshotControl(const string& trace_file)
{
    trace_file_ = trace_file;
    ParseTraceFile();
    Init();
}


SnapshotControl::SnapshotControl(const string& os_type, const string& disk_type, const string& vm_id, const string& ss_id)
{
    os_type_ = os_type;
    disk_type_ = disk_type;
    ss_meta_.vm_id_ = vm_id;
    ss_meta_.snapshot_id_ = ss_id;
    Init();
}

void SnapshotControl::Init()
{
    vm_path_ = "/" + kBasePath + "/" + ss_meta_.vm_id_;
    vm_meta_pathname_ = vm_path_ + "/vm.meta";
    store_path_ = vm_path_ + "/" + "appendstore";
    ss_meta_pathname_ = vm_path_ + "/" + ss_meta_.snapshot_id_ + ".meta";
    primary_filter_pathname_ = vm_path_ + "/" + ss_meta_.snapshot_id_ + ".bm1";
    secondary_filter_pathname_ = vm_path_ + "/" + ss_meta_.snapshot_id_ + ".bm2";
    ss_meta_.size_ = 0;
    store_ptr_ = NULL;
}

void SnapshotControl::SetAppendStore(PanguAppendStore* pas)
{
    store_ptr_ = pas;
}

void SnapshotControl::ParseTraceFile()
{
    size_t name_seperator = trace_file_.rfind('/');
    size_t disk_seperator = trace_file_.rfind('/', name_seperator - 1);
    size_t os_seperator = trace_file_.rfind('/', disk_seperator - 1);
    size_t vm_seperator = trace_file_.find('.', name_seperator);
    size_t ss_seperator = trace_file_.find_last_of('-');
    os_type_ = trace_file_.substr(os_seperator + 1, disk_seperator - os_seperator - 1);
    disk_type_ = trace_file_.substr(disk_seperator + 1, name_seperator - disk_seperator - 1);
    ss_meta_.vm_id_ = trace_file_.substr(name_seperator + 1, vm_seperator - name_seperator - 1);
    ss_meta_.snapshot_id_ = trace_file_.substr(vm_seperator + 1, ss_seperator - vm_seperator - 1);
    LOG4CXX_INFO(logger_, "trace: " << trace_file_ << " os: " << os_type_ 
                 << " disk: " << disk_type_ << " vm: " << ss_meta_.vm_id_ << " snapshot: " << ss_meta_.snapshot_id_);
}

bool SnapshotControl::LoadSnapshotMeta()
{
    // open the snapshot meta file in qfs and read it
    // TODO: currently the read/write apis provide a log style file
	if (!FileSystemHelper::GetInstance()->IsFileExists(ss_meta_pathname_)) {
        LOG4CXX_ERROR(logger_, "Couldn't find snapshot meta: " << ss_meta_.vm_id_ << " " << ss_meta_.snapshot_id_);
		return false;
	}
	FileHelper* fh = FileSystemHelper::GetInstance()->CreateFileHelper(ss_meta_pathname_, O_RDONLY);
	fh->Open();
	int read_length = fh->GetNextLogSize();
	char *data = new char[read_length];
	fh->Read(data, read_length);
    LOG4CXX_INFO(logger_, "Read " << read_length << " from " << ss_meta_pathname_);

    stringstream buffer;
    buffer.write(data, read_length);
    ss_meta_.Deserialize(buffer);
    ss_meta_.DeserializeRecipe(buffer);
    LOG4CXX_INFO(logger_, "Snapshot meta loaded: " << ss_meta_.vm_id_ << " " << ss_meta_.snapshot_id_);

	fh->Close();
    FileSystemHelper::GetInstance()->DestroyFileHelper(fh);
    delete[] data;
    return true;
}

bool SnapshotControl::SaveSnapshotMeta()
{
    if (!FileSystemHelper::GetInstance()->IsDirectoryExists(vm_path_))
        FileSystemHelper::GetInstance()->CreateDirectory(vm_path_);
    if (FileSystemHelper::GetInstance()->IsFileExists(ss_meta_pathname_)) {
        LOG4CXX_WARN(logger_, "Sanpshot metadata exists, will re-create " << ss_meta_pathname_);
        FileSystemHelper::GetInstance()->RemoveFile(ss_meta_pathname_);
    }
	FileHelper* fh = FileSystemHelper::GetInstance()->CreateFileHelper(ss_meta_pathname_, O_WRONLY);
	fh->Create();

	stringstream buffer;
	ss_meta_.Serialize(buffer);
    ss_meta_.SerializeRecipe(buffer);
	LOG4CXX_INFO(logger_, "save snapshot meta, size is" << buffer.str().size());
    fh->Write((char *)buffer.str().c_str(), buffer.str().size());

    fh->Close();
    FileSystemHelper::GetInstance()->DestroyFileHelper(fh);
    return true;
}

void SnapshotControl::UpdateSnapshotRecipe(const SegmentMeta& sm)
{
    SegmentMeta tmp;
    tmp.size_ = sm.size_;
    tmp.cksum_ = sm.cksum_;
    tmp.end_offset_ = sm.end_offset_;
    tmp.handle_ = sm.handle_;
    ss_meta_.snapshot_recipe_.push_back(tmp);
}

bool SnapshotControl::LoadSegmentRecipe(SegmentMeta& sm, uint32_t idx)
{
    if (idx >= ss_meta_.snapshot_recipe_.size())
        return false;
    sm = ss_meta_.snapshot_recipe_[idx];
    string data;
    string handle((char*)&sm.handle_, sizeof(sm.handle_));
    store_ptr_->Read(handle, &data);
    
    stringstream ss(data);
    sm.DeserializeRecipe(ss);
    LOG4CXX_INFO(logger_, "Read segment meta : " << data.size() << " bytes, " 
                 << sm.segment_recipe_.size() << " items");
    return true;
}

bool SnapshotControl::SaveSegmentRecipe(SegmentMeta& sm)
{
    stringstream buffer;
    sm.SerializeRecipe(buffer);
    string handle = store_ptr_->Append(buffer.str());
    sm.SetHandle(handle);
    return true;
}

bool SnapshotControl::SaveBlockData(BlockMeta& bm)
{
    string data(bm.data_, bm.size_);
    string handle = store_ptr_->Append(data);
    bm.SetHandle(handle);
    bm.flags_ |= IN_AS;
    return true;
}

bool SnapshotControl::LoadBlockData(BlockMeta& bm)
{
    if (bm.flags_ & IN_CDS) {
        LOG4CXX_ERROR(logger_, "This block is not in append store");
        return 0;
    }

    string buf;
    string handle((char*)&bm.handle_, sizeof(bm.handle_));
    bool res = store_ptr_->Read(handle, &buf);
    if (res && bm.size_ == buf.size()) {
        bm.DeserializeData(buf);
        return true;
    }
    LOG4CXX_ERROR(logger_, "append store read " << buf.size()
            << ", block size in meta is " << bm.size_);
    return false;
}

bool SnapshotControl::InitBloomFilters(uint64_t snapshot_size)
{
	if (!FileSystemHelper::GetInstance()->IsFileExists(vm_meta_pathname_)) {
        if (!FileSystemHelper::GetInstance()->IsDirectoryExists(vm_path_))
            FileSystemHelper::GetInstance()->CreateDirectory(vm_path_);
        // init bloom filter params and store into vm meta
        LOG4CXX_INFO(logger_, "VM meta not found, will create " << vm_meta_pathname_);
        vm_meta_.filter_num_items_ = snapshot_size / AVG_BLOCK_SIZE;
        vm_meta_.filter_num_funcs_ = BLOOM_FILTER_NUM_FUNCS;
        vm_meta_.filter_fp_rate_ = BLOOM_FILTER_FP_RATE;

        FileHelper* fh = FileSystemHelper::GetInstance()->CreateFileHelper(vm_meta_pathname_, O_WRONLY);
        fh->Create();
        stringstream buffer;
        vm_meta_.Serialize(buffer);
        LOG4CXX_INFO(logger_, "VM meta size " << buffer.str().size());
        fh->WriteData((char *)buffer.str().c_str(), buffer.str().size());
        fh->Close();
        FileSystemHelper::GetInstance()->DestroyFileHelper(fh);
	}
    else {
        // read bloom filter params
        FileHelper* fh = FileSystemHelper::GetInstance()->CreateFileHelper(vm_meta_pathname_, O_RDONLY);
        fh->Open();
        long read_length = FileSystemHelper::GetInstance()->GetSize(vm_meta_pathname_);
        char *data = new char[read_length];
        fh->Read(data, read_length);
        LOG4CXX_INFO(logger_, "Read " << read_length << " from file");

        stringstream buffer;
        buffer.write(data, read_length);
        vm_meta_.Deserialize(buffer);
        LOG4CXX_INFO(logger_, "VM meta loaded: " 
                     << vm_meta_.filter_num_items_ << " " 
                     << vm_meta_.filter_num_funcs_ << ""
                     << vm_meta_.filter_fp_rate_);
        fh->Close();
        FileSystemHelper::GetInstance()->DestroyFileHelper(fh);
        delete[] data;
    }


    // params ready, now init bloom filters
    primary_filter_ptr_ = new BloomFilter<Checksum>(vm_meta_.filter_num_items_, 
                                                   vm_meta_.filter_fp_rate_, 
                                                   kBloomFilterFunctions, 
                                                   vm_meta_.filter_num_funcs_);
    // for fine-grained deletion we need a bigger filter, using different group of hash functions
    secondary_filter_ptr_ = new BloomFilter<Checksum>(vm_meta_.filter_num_items_ * 2, 
                                                   vm_meta_.filter_fp_rate_, 
                                                   &kBloomFilterFunctions[8], 
                                                   vm_meta_.filter_num_funcs_);
    return true;
}

void SnapshotControl::UpdateBloomFilters(const SegmentMeta& sm)
{
    for (size_t i = 0; i < sm.segment_recipe_.size(); i++) {
        primary_filter_ptr_->AddElement(sm.segment_recipe_[i].cksum_);
        secondary_filter_ptr_->AddElement(sm.segment_recipe_[i].cksum_);
    }
}

bool SnapshotControl::SaveBloomFilters()
{
    return SaveBloomFilter(primary_filter_ptr_, primary_filter_pathname_) &&
        SaveBloomFilter(secondary_filter_ptr_, secondary_filter_pathname_);
}

bool SnapshotControl::SaveBloomFilter(BloomFilter<Checksum>* pbf, const string& bf_name)
{
    if (!FileSystemHelper::GetInstance()->IsDirectoryExists(vm_path_))
        FileSystemHelper::GetInstance()->CreateDirectory(vm_path_);
    if (FileSystemHelper::GetInstance()->IsFileExists(bf_name))
        FileSystemHelper::GetInstance()->RemoveFile(bf_name);

	FileHelper* fh = FileSystemHelper::GetInstance()->CreateFileHelper(bf_name, O_WRONLY);
	fh->Create();

	stringstream buffer;
	pbf->Serialize(buffer);
	LOG4CXX_INFO(logger_, "Bloom filter primary size " << buffer.str().size());
    fh->Write((char *)buffer.str().c_str(), buffer.str().size());

    fh->Close();
    FileSystemHelper::GetInstance()->DestroyFileHelper(fh);
    return true;
}

bool SnapshotControl::RemoveBloomFilters()
{
    return RemoveBloomFilter(primary_filter_pathname_) &&
        RemoveBloomFilter(secondary_filter_pathname_);
}

bool SnapshotControl::RemoveBloomFilter(const string& bf_name)
{
    if (FileSystemHelper::GetInstance()->IsFileExists(bf_name))
        FileSystemHelper::GetInstance()->RemoveFile(bf_name);
    return true;
}

bool SnapshotControl::LoadBloomFilter(BloomFilter<Checksum>* pbf, const string& bf_name)
{
	if (!FileSystemHelper::GetInstance()->IsFileExists(bf_name)) {
        LOG4CXX_ERROR(logger_, "Couldn't find bloom filter: " << bf_name);
		return false;
	}
	FileHelper* fh = FileSystemHelper::GetInstance()->CreateFileHelper(bf_name, O_RDONLY);
	fh->Open();
	int read_length = fh->GetNextLogSize();
	char *data = new char[read_length];
	fh->Read(data, read_length);
    LOG4CXX_INFO(logger_, "Read " << read_length << " from file " << bf_name);

    stringstream buffer;
    buffer.write(data, read_length);
    pbf->Deserialize(buffer);
    LOG4CXX_INFO(logger_, "Bloom filter loaded: " << bf_name);

	fh->Close();
	FileSystemHelper::GetInstance()->DestroyFileHelper(fh);
    delete[] data;
    return true;
}












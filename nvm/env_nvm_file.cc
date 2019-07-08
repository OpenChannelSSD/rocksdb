#include <exception>
#include <iostream>
#include <sstream>
#include <fstream>
#include <csignal>
#include <mutex>
#include "util/coding.h"
#include "env_nvm.h"
#include <liblightnvm.h>

#include <execinfo.h>
#ifndef NVM_TRACE
#define NVM_TRACE 1
void nvm_trace_pr(void) {
  void *array[1024];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace(array, 1024);
  strings = backtrace_symbols(array, size);

  printf("Got %zd stack frames.\n", size);

  for (i = 0; i < size; i++) {
    printf("%s\n", strings[i]);
  }

  free(strings);
}
#endif

namespace rocksdb {

NvmFile::NvmFile(
  EnvNVM* env, const FPathInfo& info, const std::string mpath
) : env_(env), refs_(), info_(info), fsize_(), mpath_(mpath), align_nbytes_(),
    stripe_nbytes_(), blk_nbytes_(), blks_(), lu_bound_(8) {
  NVM_DBG(this, "mpath_:" << mpath_);

  struct nvm_dev *dev = env_->store_->GetDev();
  const struct nvm_geo *geo = nvm_dev_get_geo(env_->store_->GetDev());

  if (env_->posix_->FileExists(mpath_).ok()) {          // Read meta from file
    std::string content;
    size_t blk_idx;

    if (!ReadFileToString(env_->posix_, mpath_, &content).ok()) {
      NVM_DBG(this, "FAILED: ReadFileToString");
      throw std::runtime_error("FAILED: ReadFileToString");
    }

    std::istringstream meta(content);

    if (!(meta >> fsize_)) {                            // Read fsize
      NVM_DBG(this, "FAILED: parsing file size from meta");
      throw std::runtime_error("FAILED: parsing file size from meta");
    }

    while(meta >> blk_idx) {
      struct nvm_vblk *blk;

      blk = env_->store_->get_reserved(blk_idx);
      if (!blk) {
        perror("nvm_vblk_alloc");
        NVM_DBG(this, "FAILED: allocating vblk");
        throw std::runtime_error("FAILED: allocating vblk");
      }

      blks_.push_back(blk);
    }
  }

  align_nbytes_ = geo->nplanes * geo->nsectors * geo->sector_nbytes;
  stripe_nbytes_ = align_nbytes_ * env_->store_->GetPunitCount();
  blk_nbytes_ = stripe_nbytes_ * geo->npages;

  buf_nbytes_ = 0;                              // Setup buffer
  buf_nbytes_max_ = lu_bound_ * stripe_nbytes_;
  buf_ = (char*)nvm_buf_alloc(dev, buf_nbytes_max_, NULL);
  if (!buf_) {
    NVM_DBG(this, "FAILED: allocating buffer");
    throw std::runtime_error("FAILED: allocating buffer");
  }

  NVM_DBG(this, "align_nbytes_(" << align_nbytes_ << ")");
  NVM_DBG(this, "lu_bound_(" << lu_bound_ << ")");
  NVM_DBG(this, "stripe_nbytes_(" << stripe_nbytes_ << ")");
  NVM_DBG(this, "blk_nbytes_(" << blk_nbytes_ << ")");
  NVM_DBG(this, "buf_nbytes_(" << buf_nbytes_ << ")");
  NVM_DBG(this, "buf_nbytes_max_(" << buf_nbytes_max_ << ")");
}

NvmFile::~NvmFile(void) {
  NVM_DBG(this, "");

  free(buf_);
}

bool NvmFile::IsNamed(const std::string& fname) const {
  NVM_DBG(this, "fname(" << fname << ")");

  return !info_.fname().compare(fname);
}

const std::string& NvmFile::GetFname(void) const {
  NVM_DBG(this, "return(" << info_.fname() << ")");

  return info_.fname();
}

const std::string& NvmFile::GetDpath(void) const {
  NVM_DBG(this, "return(" << info_.dpath() << ")");

  return info_.dpath();
}

void NvmFile::Rename(const std::string& fname) {
  NVM_DBG(this, "fname(" << fname << ")");

  info_.fname(fname);
}

size_t NvmFile::GetRequiredBufferAlignment(void) const {
  NVM_DBG(this, "returning(" << align_nbytes_ << ")");

  return align_nbytes_;
}

void NvmFile::Ref(void) {
  NVM_DBG(this, "LOCK ?");
  MutexLock lock(&refs_mutex_);
  NVM_DBG(this, "LOCK !");

  NVM_DBG(this, "refs_(" << refs_ << ")");

  ++refs_;

  NVM_DBG(this, "refs_(" << refs_ << ")");
}

void NvmFile::Unref(void) {
  NVM_DBG(this, "");

  bool do_delete = false;

  {
    NVM_DBG(this, "LOCK ?");
    MutexLock lock(&refs_mutex_);
    NVM_DBG(this, "LOCK !");

    --refs_;
    if (refs_ < 0) {
      do_delete = true;
    }
  }

  if (do_delete) {
    NVM_DBG(this, "deleting!");
    delete this;
  }
}

std::string NvmFile::txt(void) const {
  std::stringstream ss;
  ss << "fname(" << info_.fname() << ") ";
  return ss.str();
}

// Used by WritableFile
bool NvmFile::IsSyncThreadSafe(void) const {
  NVM_DBG(this, "hard-coded return");

  return true;
}

//
// Implemented using the address of the NvmFile instance. This seem to satisfy
// the requirements: it lives as long as need be, is unique and probably won't
// have the same address after termination.
//
// Although... depending on what is meant by prefix... they probably could be.
//
//
// Used by RandomAccessFile, WritableFile
size_t NvmFile::GetUniqueId(char* id, size_t max_size) const {
  NVM_DBG(this, "");

  if (max_size < (kMaxVarint64Length*3)) {
    return 0;
  }

  char* rid = id;
  rid = EncodeVarint64(rid, (uint64_t)this);
  rid = EncodeVarint64(rid, (uint64_t)this);
  rid = EncodeVarint64(rid, (uint64_t)this);
  assert(rid >= id);

  return static_cast<size_t>(rid - id);
}

// Used by WritableFile
uint64_t NvmFile::GetFileSize(void) const {
  NVM_DBG(this, "return(" << fsize_ << ")");

  return fsize_;
}

// Used by WritableFile
void NvmFile::PrepareWrite(size_t offset, size_t len) {
  /*NVM_DBG(this, "offset(" << offset << "), len(" << len << ") ignoring...");*/
}

// Used by WritableFile
Status NvmFile::Allocate(uint64_t offset, uint64_t len) {
  /*NVM_DBG(this, "offset(" << offset << "), len(" << len << ") ignoring");*/

  return Status::OK();
}

// Used by WritableFile
Status NvmFile::Sync(void) {
  NVM_DBG(this, "writing file meta to default env...");

  return Flush();
}

// Used by WritableFile
Status NvmFile::Fsync(void) {
  NVM_DBG(this, "writing file meta to default env...");

  return Flush();
}

// Used by WritableFile
Status NvmFile::RangeSync(uint64_t offset, uint64_t nbytes) {
  NVM_DBG(this, "offset(" << offset << "), nbytes(" << nbytes << ")");

  return Flush();
}

// Used by WritableFile
Status NvmFile::Close(void) {
  NVM_DBG(this, "ignoring...");

  return Status::OK();
}

// Deletes any buffers covering the range [offset; offset+length].
//
// Used by SequentialFile, RandomAccessFile, WritableFile
Status NvmFile::InvalidateCache(size_t offset, size_t length) {
  NVM_DBG(this, "");

  return Status::OK();
}

// Used by WritableFile
Status NvmFile::Append(const Slice& slice) {
  NVM_DBG(this, "fsize_(" << fsize_ << ")-aligned(" << !(fsize_ % align_nbytes_) << ")");
  NVM_DBG(this, "buf_nbytes_(" << buf_nbytes_ << ")-aligned(" << !(buf_nbytes_ % align_nbytes_) << ")");
  NVM_DBG(this, "slice-size(" << slice.size() << ")-aligned(" << !(slice.size() % align_nbytes_) << ")");

  const size_t data_nbytes = slice.size();
  const char* data = slice.data();

  size_t nbytes_remaining = data_nbytes;
  size_t nbytes_written = 0;

  while(nbytes_remaining > 0) {
    size_t avail = buf_nbytes_max_ - buf_nbytes_;
    size_t nbytes = std::min(nbytes_remaining, avail);

    NVM_DBG(this, "avail(" << avail << ", nbytes(" << nbytes << ")");

    memcpy(buf_ + buf_nbytes_, data + nbytes_written, nbytes);

    nbytes_remaining -= nbytes;
    nbytes_written += nbytes;

    buf_nbytes_ += nbytes;
    fsize_ += nbytes;

    NVM_DBG(this, "buf_nbytes_(" << buf_nbytes_ << ")");
    NVM_DBG(this, "fsize_(" << fsize_ << ")");

    // Have bytes remaining but no more room in buffer -> flush to media
    if (nbytes_remaining && (!Flush().ok())) {
      return Status::IOError("Flushing to media failed.");
    }
  }

  return Status::OK();
}

Status NvmFile::wmeta(void) {
  std::stringstream meta;

  meta << std::to_string(fsize_) << std::endl;;

  for (auto &blk : blks_) {
    if (!blk)
      continue;

    meta << nvm_vblk_get_addrs(blk)[0].g.blk << std::endl;
  }

  std::string content = meta.str();
  Slice slice(content.c_str(), content.size());

  NVM_DBG(this, "meta: " << content);

  return WriteStringToFile(env_->posix_, slice, mpath_, true);
}

// Used by WritableFile

Status NvmFile::Flush(void) {
  return Flush(false);
}

Status NvmFile::Flush(bool padded) {
  NVM_DBG(this, "padded(" << padded << ")");

  size_t pad_nbytes = 0;

  if (!buf_nbytes_) {
    NVM_DBG(this, "Nothing to flush (buffer is empty)");
    return Status::OK();
  }

  if (padded && (buf_nbytes_ % stripe_nbytes_ != 0)) {
    //size_t pad_nbytes = align_nbytes_ - (buf_nbytes_ % align_nbytes_);
    pad_nbytes = stripe_nbytes_ - (buf_nbytes_ % stripe_nbytes_);

    NVM_DBG(this, "stripe_nbytes_: " << stripe_nbytes_);
    NVM_DBG(this, "pad_nbytes: " << pad_nbytes);
    NVM_DBG(this, "buf_nbytes_: " << buf_nbytes_);

    memset(buf_ + buf_nbytes_, 'P', pad_nbytes);
    buf_nbytes_ += pad_nbytes;

    NVM_DBG(this, "buf_nbytes_: " << buf_nbytes_);
  }

  /*
  if (buf_nbytes_ < align_nbytes_) {
    NVM_DBG(this, "Nothing to flush (buffer less than align_nbytes_)");
    return Status::OK();
  }*/

  if (buf_nbytes_ < stripe_nbytes_) {
    NVM_DBG(this, "Nothing to flush (buffer less than striped_nbytes_)");
    return Status::OK();
  }

  if ((!padded) && (fsize_ < buf_nbytes_)) {   // Sanity check
    NVM_DBG(this, "FAILED: fsize_ < buf_nbytes_");
    return Status::IOError("FAILED: fsize_ < buf_nbytes_");
  }

  size_t unaligned_nbytes = (buf_nbytes_ % align_nbytes_);
  size_t flush_tbytes = buf_nbytes_ - unaligned_nbytes;

  NVM_DBG(this, "buf_nbytes_(" << buf_nbytes_ << "), "
                "flush_tbytes(" << flush_tbytes << "), "
                "unaligned_nbytes(" << unaligned_nbytes << ")");

  // Ensure that enough blocks are reserved for flushing buffer
  while (blks_.size() <= (fsize_ / blk_nbytes_)) {
    struct nvm_vblk *blk;

    blk = env_->store_->get();
    if (!blk) {
      NVM_DBG(this, "FAILED: reserving NVM");
      return Status::IOError("FAILED: reserving NVM");
    }

    blks_.push_back(blk);

    NVM_DBG(this, "avail: " << blk_nbytes_ - nvm_vblk_get_pos_write(blk));
  }

  size_t offset = fsize_ - (buf_nbytes_ - pad_nbytes);
  size_t nbytes_remaining = flush_tbytes;
  size_t nbytes_written = 0;

  while (nbytes_remaining > 0) {
    size_t blk_idx = (offset + nbytes_written) / blk_nbytes_;
    struct nvm_vblk *blk = blks_[blk_idx];
    size_t avail = blk_nbytes_ - nvm_vblk_get_pos_write(blk);
    size_t nbytes = std::min(nbytes_remaining, avail);
    ssize_t ret;

    NVM_DBG(this, "blk_idx: " << blk_idx);
    NVM_DBG(this, "pos:" << nvm_vblk_get_pos_write(blk));
    NVM_DBG(this, "avail: " << avail);
    NVM_DBG(this, "nbytes_remaining: " << nbytes_remaining);
    NVM_DBG(this, "nbytes_written: " << nbytes_written);
    NVM_DBG(this, "nbytes: " << nbytes);

    ret = nvm_vblk_write(blk, buf_ + nbytes_written, nbytes);
    if (ret < 0) {
      perror("nvm_vblk_write");
      nvm_vblk_pr(blk);
      NVM_DBG(this, "FAILED: nvm_vblk_write(...)");
      return Status::IOError("FAILED: nvm_vblk_write(...)");
    }

    nbytes_remaining -= ret;
    nbytes_written += ret;
  }

  // Move the unaligned bytes to start of buffer
  if (unaligned_nbytes)
    memcpy(buf_, buf_ + nbytes_written, buf_nbytes_ - nbytes_written);

  buf_nbytes_ -= nbytes_written;

  NVM_DBG(this, "buf_nbytes_(" << buf_nbytes_ << "), nbytes_written(" << nbytes_written << ")");

  return wmeta();
}

// Used by WritableFile
Status NvmFile::Truncate(uint64_t size) {
  NVM_DBG(this, "fsize_(" << fsize_ << ")" << ", size(" << size << ")" << ", nvblocks(" << blks_.size() << ")");

  Status s = Flush(true);      // Flush out...
  pad_last_block();

  if (fsize_ < size) {
    NVM_DBG(this, "FAILED: Truncating will grow the file.");
    return Status::IOError("FAILED: Truncating will grow the file.");
  }

  if (fsize_ == size) {
    NVM_DBG(this, "Nothing to do");
    return wmeta();
  }

  size_t blks_nreq = (size + blk_nbytes_ -1) / blk_nbytes_;

  // Release blocks that are no longer required
  for (size_t blk_idx = blks_nreq; blk_idx < blks_.size(); ++blk_idx) {
    NVM_DBG(this, "releasing: blk_idx: " << blk_idx);

    if (!blks_[blk_idx]) {
      NVM_DBG(this, "nothing here... skipping...");
      continue;
    }

    env_->store_->put(blks_[blk_idx]);
    blks_[blk_idx] = NULL;
  }

  fsize_ = size;

  return wmeta();
}

Status NvmFile::pad_last_block(void) {
  NVM_DBG(this, "...");

  if (!fsize_) {
    NVM_DBG(this, "empty file, skipping...");
    return Status::OK();
  }

  const size_t blk_idx = fsize_ / blk_nbytes_;

  if (!blks_[blk_idx]) {
    NVM_DBG(this, "FAILED: No vblk to pad!?");
    return Status::IOError("FAILED: No vblk to pad!?");
  }

  {
    ssize_t err;
    struct nvm_vblk *blk = blks_[blk_idx];
    size_t nbytes_left = nvm_vblk_get_nbytes(blk) - nvm_vblk_get_pos_write(blk);
    ssize_t nbytes_pad = std::min(nbytes_left, buf_nbytes_max_);

    nvm_buf_fill(buf_, nbytes_pad);

    err = nvm_vblk_write(blks_[blk_idx], buf_, nbytes_pad);
    if (err < 0) {
      perror("nvm_vblk_pad");
      NVM_DBG(this, "FAILED: nvm_vblk_pad(...)");
      return Status::IOError("FAILED: nvm_vblk_pad(...)");
    }
  }

  return Status::OK();
}

// Used by SequentialFile, RandomAccessFile
Status NvmFile::Read(
  uint64_t offset, size_t n, Slice* result, char* scratch
) const {
  NVM_DBG(this, "entry");
  NVM_DBG(this, "offset(" << offset << ")-aligned(" << !(offset % align_nbytes_) << ")");
  NVM_DBG(this, "n(" << n << ")-aligned(" << !(n % align_nbytes_) << ")");

  // n is the MAX number of bytes to read, since it is the size of the scratch
  // memory. However, there might be n, less than n, or more than n bytes in the
  // file starting from offset.  So we need to know how many bytes we actually
  // need to read..
  const uint64_t nbytes_from_offset = fsize_ - std::min(fsize_, offset);

  if (n > nbytes_from_offset)
    n = nbytes_from_offset;

  NVM_DBG(this, "n(" << n << ")-aligned(" << !(n % align_nbytes_) << ")");

  if (n == 0) {
    NVM_DBG(this, "nothing left to read...");
    *result = Slice();
    return Status::OK();
  }
  // Now we know that: '0 < n <= nbytes_from_offset'

  uint64_t aligned_offset = offset - offset % align_nbytes_;
  uint64_t aligned_n = ((((n + align_nbytes_ - 1) / align_nbytes_)) + 1) * align_nbytes_;
  uint64_t skip_head_nbytes = offset - aligned_offset;
  uint64_t skip_tail_nbytes = aligned_n - n;

  NVM_DBG(this, "aligned_n(" << aligned_n << ")");
  NVM_DBG(this, "aligned_offset(" << aligned_offset << ")");
  NVM_DBG(this, "skip_head_nbytes(" << skip_head_nbytes << ")");
  NVM_DBG(this, "skip_tail_nbytes(" << skip_tail_nbytes << ")");

  size_t nbytes_remaining = aligned_n;
  size_t nbytes_read = 0;
  size_t read_offset = aligned_offset;

  while (nbytes_remaining > 0) {
    uint64_t blk_idx = read_offset / blk_nbytes_;
    uint64_t blk_offset = read_offset % blk_nbytes_;
    struct nvm_vblk *blk = blks_[blk_idx];
    uint64_t nbytes = std::min({
      nbytes_remaining,
      blk_nbytes_ - blk_offset,
      buf_nbytes_max_
    });

    NVM_DBG(this, "blk(" << blk << ")");
    NVM_DBG(this, "blk_offset(" << blk_offset << ")");
    NVM_DBG(this, "nbytes(" << nbytes << ")");

    NVM_DBG(this, "=nbytes_remaining(" << nbytes_remaining << ")");
    NVM_DBG(this, "=nbytes_read(" << nbytes_read << ")");
    NVM_DBG(this, "=read_offset(" << read_offset << ")");

    ssize_t ret = nvm_vblk_pread(blk, buf_, nbytes, blk_offset);
    if (ret < 0) {
      perror("nvm_vblk_pread");
      NVM_DBG(this, "FAILED: nvm_vblk_read");
      return Status::IOError("FAILED: nvm_vblk_read");
    }

    int first = !nbytes_read;

    nbytes_remaining -= ret;
    nbytes_read += ret;
    read_offset += ret;

    int last = !nbytes_remaining;

    if (first && last) {        // Single short read
      ssize_t scratch_inc = n;
      NVM_DBG(this, "first && last, scratch_inc: " << scratch_inc);

      memcpy(scratch, buf_ + skip_head_nbytes, scratch_inc);

    } else if (first) {
      ssize_t scratch_inc = nbytes - skip_head_nbytes;
      NVM_DBG(this, "first, scratch_inc: " << scratch_inc);

      memcpy(scratch, buf_ + skip_head_nbytes, scratch_inc);
    } else if (last) {
      ssize_t scratch_inc = nbytes - (skip_tail_nbytes - skip_head_nbytes);
      ssize_t scratch_offz = nbytes_read - ret - skip_head_nbytes;

      NVM_DBG(this, "last, scratch_offz: " << scratch_offz << ", scratch_inc: " << scratch_inc);

      memcpy(scratch + scratch_offz, buf_, scratch_inc);
    } else {
      ssize_t scratch_inc = nbytes;
      ssize_t scratch_offz = nbytes_read - ret - skip_head_nbytes;
      NVM_DBG(this, "middle, scratch_offz: " << scratch_offz << ", scratch_inc: " << scratch_inc);

      memcpy(scratch + scratch_offz, buf_, scratch_inc);
    }

    NVM_DBG(this, "-nbytes_remaining(" << nbytes_remaining << ")");
    NVM_DBG(this, "+nbytes_read(" << nbytes_read << ")");
    NVM_DBG(this, "+read_offset(" << read_offset << ")");
  }

  *result = Slice(scratch, n);

  NVM_DBG(this, "exit");

  return Status::OK();
}

}       // namespace rocksdb


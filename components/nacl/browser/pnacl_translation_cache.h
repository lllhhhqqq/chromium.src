// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_NACL_BROWSER_PNACL_TRANSLATION_CACHE_H_
#define COMPONENTS_NACL_BROWSER_PNACL_TRANSLATION_CACHE_H_

#include <map>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/cache_type.h"

namespace disk_cache {
class Backend;
}

namespace nacl {
struct PnaclCacheInfo;
}

namespace net {
class DrainableIOBuffer;
}

namespace pnacl {
typedef base::Callback<void(int)> CompletionCallback;
typedef base::Callback<void(int, scoped_refptr<net::DrainableIOBuffer>)>
    GetNexeCallback;
class PnaclTranslationCacheEntry;
extern const int kMaxMemCacheSize;

class PnaclTranslationCache
    : public base::SupportsWeakPtr<PnaclTranslationCache> {
 public:
  PnaclTranslationCache();
  virtual ~PnaclTranslationCache();

  // Initialize the translation cache in |cache_dir|.  If the return value is
  // net::ERR_IO_PENDING, |callback| will be called with a 0 argument on success
  // and <0 otherwise.
  int InitOnDisk(const base::FilePath& cache_dir,
                 const CompletionCallback& callback);

  // Initialize the translation cache in memory.  If the return value is
  // net::ERR_IO_PENDING, |callback| will be called with a 0 argument on success
  // and <0 otherwise.
  int InitInMemory(const CompletionCallback& callback);

  // Store the nexe in the translation cache, and call |callback| with
  // the result. The result passed to the callback is 0 on success and
  // <0 otherwise. A reference to |nexe_data| is held until completion
  // or cancellation.
  void StoreNexe(const std::string& key,
                 net::DrainableIOBuffer* nexe_data,
                 const CompletionCallback& callback);

  // Retrieve the nexe from the translation cache. Write the data into |nexe|
  // and call |callback|, passing a result code (0 on success and <0 otherwise),
  // and a DrainableIOBuffer with the data.
  void GetNexe(const std::string& key, const GetNexeCallback& callback);

  // Return the number of entries in the cache backend.
  int Size();

  // Return the cache key for |info|
  static std::string GetKey(const nacl::PnaclCacheInfo& info);

  // Doom all entries between |initial| and |end|. If the return value is
  // net::ERR_IO_PENDING, |callback| will be invoked when the operation
  // completes.
  int DoomEntriesBetween(base::Time initial, base::Time end,
                         const CompletionCallback& callback);

 private:
  friend class PnaclTranslationCacheEntry;
  friend class PnaclTranslationCacheTest;
  // PnaclTranslationCacheEntry should only use the
  // OpComplete and backend methods on PnaclTranslationCache.
  void OpComplete(PnaclTranslationCacheEntry* entry);
  disk_cache::Backend* backend() { return disk_cache_.get(); }

  int Init(net::CacheType,
           const base::FilePath& directory,
           int cache_size,
           const CompletionCallback& callback);

  void OnCreateBackendComplete(int rv);

  scoped_ptr<disk_cache::Backend> disk_cache_;
  CompletionCallback init_callback_;
  bool in_memory_;
  std::map<void*, scoped_refptr<PnaclTranslationCacheEntry> > open_entries_;

  DISALLOW_COPY_AND_ASSIGN(PnaclTranslationCache);
};

}  // namespace pnacl

#endif  // COMPONENTS_NACL_BROWSER_PNACL_TRANSLATION_CACHE_H_

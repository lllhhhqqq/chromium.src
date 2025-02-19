// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/web_cache/browser/web_cache_manager.h"

#include <string.h>

#include <algorithm>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/sys_info.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/service_registry.h"

using base::Time;
using base::TimeDelta;

namespace web_cache {

static const int kReviseAllocationDelayMS = 200;

// The default size limit of the in-memory cache is 8 MB
static const int kDefaultMemoryCacheSize = 8 * 1024 * 1024;

namespace {

int GetDefaultCacheSize() {
  // Start off with a modest default
  int default_cache_size = kDefaultMemoryCacheSize;

  // Check how much physical memory the OS has
  int mem_size_mb = base::SysInfo::AmountOfPhysicalMemoryMB();
  if (mem_size_mb >= 1000)  // If we have a GB of memory, set a larger default.
    default_cache_size *= 4;
  else if (mem_size_mb >= 512)  // With 512 MB, set a slightly larger default.
    default_cache_size *= 2;

  UMA_HISTOGRAM_MEMORY_MB("Cache.MaxCacheSizeMB",
                          default_cache_size / 1024 / 1024);

  return default_cache_size;
}

}  // anonymous namespace

// static
WebCacheManager* WebCacheManager::GetInstance() {
  return base::Singleton<WebCacheManager>::get();
}

WebCacheManager::WebCacheManager()
    : global_size_limit_(GetDefaultGlobalSizeLimit()),
      weak_factory_(this) {
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_CREATED,
                 content::NotificationService::AllBrowserContextsAndSources());
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_TERMINATED,
                 content::NotificationService::AllBrowserContextsAndSources());
}

WebCacheManager::~WebCacheManager() {
}

void WebCacheManager::Add(int renderer_id) {
  DCHECK(inactive_renderers_.count(renderer_id) == 0);

  // It is tempting to make the following DCHECK here, but it fails when a new
  // tab is created as we observe activity from that tab because the
  // RenderProcessHost is recreated and adds itself.
  //
  //   DCHECK(active_renderers_.count(renderer_id) == 0);
  //
  // However, there doesn't seem to be much harm in receiving the calls in this
  // order.

  active_renderers_.insert(renderer_id);

  RendererInfo* stats = &(stats_[renderer_id]);
  memset(stats, 0, sizeof(*stats));
  stats->access = Time::Now();

  content::RenderProcessHost* host =
      content::RenderProcessHost::FromID(renderer_id);
  if (host) {
    mojom::WebCachePtr service;
    host->GetServiceRegistry()->ConnectToRemoteService(
        mojo::GetProxy(&service));
    web_cache_services_[renderer_id] = std::move(service);
  }

  // Revise our allocation strategy to account for this new renderer.
  ReviseAllocationStrategyLater();
}

void WebCacheManager::Remove(int renderer_id) {
  // Erase all knowledge of this renderer
  active_renderers_.erase(renderer_id);
  inactive_renderers_.erase(renderer_id);
  stats_.erase(renderer_id);

  web_cache_services_.erase(renderer_id);

  // Reallocate the resources used by this renderer
  ReviseAllocationStrategyLater();
}

void WebCacheManager::ObserveActivity(int renderer_id) {
  StatsMap::iterator item = stats_.find(renderer_id);
  if (item == stats_.end())
    return;  // We might see stats for a renderer that has been destroyed.

  // Record activity.
  active_renderers_.insert(renderer_id);
  item->second.access = Time::Now();

  std::set<int>::iterator elmt = inactive_renderers_.find(renderer_id);
  if (elmt != inactive_renderers_.end()) {
    inactive_renderers_.erase(elmt);

    // A renderer that was inactive, just became active.  We should make sure
    // it is given a fair cache allocation, but we defer this for a bit in
    // order to make this function call cheap.
    ReviseAllocationStrategyLater();
  }
}

void WebCacheManager::ObserveStats(int renderer_id,
                                   uint64_t min_dead_capacity,
                                   uint64_t max_dead_capacity,
                                   uint64_t capacity,
                                   uint64_t live_size,
                                   uint64_t dead_size) {
  StatsMap::iterator entry = stats_.find(renderer_id);
  if (entry == stats_.end())
    return;  // We might see stats for a renderer that has been destroyed.

  // Record the updated stats.
  entry->second.capacity = capacity;
  entry->second.dead_size = dead_size;
  entry->second.live_size = live_size;
  entry->second.max_dead_capacity = max_dead_capacity;
  entry->second.min_dead_capacity = min_dead_capacity;
}

void WebCacheManager::SetGlobalSizeLimit(uint64_t bytes) {
  global_size_limit_ = bytes;
  ReviseAllocationStrategyLater();
}

void WebCacheManager::ClearCache() {
  // Tell each renderer process to clear the cache.
  ClearRendererCache(active_renderers_, INSTANTLY);
  ClearRendererCache(inactive_renderers_, INSTANTLY);
}

void WebCacheManager::ClearCacheOnNavigation() {
  // Tell each renderer process to clear the cache when a tab is reloaded or
  // the user navigates to a new website.
  ClearRendererCache(active_renderers_, ON_NAVIGATION);
  ClearRendererCache(inactive_renderers_, ON_NAVIGATION);
}

void WebCacheManager::Observe(int type,
                              const content::NotificationSource& source,
                              const content::NotificationDetails& details) {
  switch (type) {
    case content::NOTIFICATION_RENDERER_PROCESS_CREATED: {
      content::RenderProcessHost* process =
          content::Source<content::RenderProcessHost>(source).ptr();
      Add(process->GetID());
      break;
    }
    case content::NOTIFICATION_RENDERER_PROCESS_TERMINATED: {
      content::RenderProcessHost* process =
          content::Source<content::RenderProcessHost>(source).ptr();
      Remove(process->GetID());
      break;
    }
    default:
      NOTREACHED();
      break;
  }
}

// static
uint64_t WebCacheManager::GetDefaultGlobalSizeLimit() {
  return GetDefaultCacheSize();
}

void WebCacheManager::GatherStats(const std::set<int>& renderers,
                                  uint64_t* capacity,
                                  uint64_t* live_size,
                                  uint64_t* dead_size) {
  *capacity = *live_size = *dead_size = 0;

  std::set<int>::const_iterator iter = renderers.begin();
  while (iter != renderers.end()) {
    StatsMap::iterator elmt = stats_.find(*iter);
    if (elmt != stats_.end()) {
      *capacity += elmt->second.capacity;
      *live_size += elmt->second.live_size;
      *dead_size += elmt->second.dead_size;
    }
    ++iter;
  }
}

// static
uint64_t WebCacheManager::GetSize(AllocationTactic tactic,
                                 uint64_t live_size,
                                 uint64_t dead_size) {
  switch (tactic) {
  case DIVIDE_EVENLY:
    // We aren't going to reserve any space for existing objects.
    return 0;
  case KEEP_CURRENT_WITH_HEADROOM:
    // We need enough space for our current objects, plus some headroom.
    return 3 * GetSize(KEEP_CURRENT, live_size, dead_size) / 2;
  case KEEP_CURRENT:
    // We need enough space to keep our current objects.
    return live_size + dead_size;
  case KEEP_LIVE_WITH_HEADROOM:
    // We need enough space to keep out live resources, plus some headroom.
    return 3 * GetSize(KEEP_LIVE, live_size, dead_size) / 2;
  case KEEP_LIVE:
    // We need enough space to keep our live resources.
    return live_size;
  default:
    NOTREACHED() << "Unknown cache allocation tactic";
    return 0;
  }
}

bool WebCacheManager::AttemptTactic(
    AllocationTactic active_tactic,
    uint64_t active_live_size,
    uint64_t active_dead_size,
    AllocationTactic inactive_tactic,
    uint64_t inactive_live_size,
    uint64_t inactive_dead_size,
    AllocationStrategy* strategy) {
  DCHECK(strategy);

  uint64_t active_size = GetSize(active_tactic, active_live_size,
                                 active_dead_size);
  uint64_t inactive_size = GetSize(inactive_tactic, inactive_live_size,
                                   inactive_dead_size);

  // Give up if we don't have enough space to use this tactic.
  if (global_size_limit_ < active_size + inactive_size)
    return false;

  // Compute the unreserved space available.
  uint64_t total_extra = global_size_limit_ - (active_size + inactive_size);

  // The plan for the extra space is to divide it evenly amoung the active
  // renderers.
  uint64_t shares = active_renderers_.size();

  // The inactive renderers get one share of the extra memory to be divided
  // among themselves.
  uint64_t inactive_extra = 0;
  if (!inactive_renderers_.empty()) {
    ++shares;
    inactive_extra = total_extra / shares;
  }

  // The remaining memory is allocated to the active renderers.
  uint64_t active_extra = total_extra - inactive_extra;

  // Actually compute the allocations for each renderer.
  AddToStrategy(active_renderers_, active_tactic, active_extra, strategy);
  AddToStrategy(inactive_renderers_, inactive_tactic, inactive_extra, strategy);

  // We succeeded in computing an allocation strategy.
  return true;
}

void WebCacheManager::AddToStrategy(const std::set<int>& renderers,
                                    AllocationTactic tactic,
                                    uint64_t extra_bytes_to_allocate,
                                    AllocationStrategy* strategy) {
  DCHECK(strategy);

  // Nothing to do if there are no renderers.  It is common for there to be no
  // inactive renderers if there is a single active tab.
  if (renderers.empty())
    return;

  // Divide the extra memory evenly among the renderers.
  uint64_t extra_each = extra_bytes_to_allocate / renderers.size();

  std::set<int>::const_iterator iter = renderers.begin();
  while (iter != renderers.end()) {
    uint64_t cache_size = extra_each;

    // Add in the space required to implement |tactic|.
    StatsMap::iterator elmt = stats_.find(*iter);
    if (elmt != stats_.end()) {
      cache_size += GetSize(tactic, elmt->second.live_size,
                            elmt->second.dead_size);
    }

    // Record the allocation in our strategy.
    strategy->push_back(Allocation(*iter, cache_size));
    ++iter;
  }
}

void WebCacheManager::EnactStrategy(const AllocationStrategy& strategy) {
  // Inform each render process of its cache allocation.
  AllocationStrategy::const_iterator allocation = strategy.begin();
  while (allocation != strategy.end()) {
    content::RenderProcessHost* host =
        content::RenderProcessHost::FromID(allocation->first);
    if (host) {
      // This is the capacity this renderer has been allocated.
      uint64_t capacity = allocation->second;

      // We don't reserve any space for dead objects in the cache. Instead, we
      // prefer to keep live objects around. There is probably some performance
      // tuning to be done here.
      uint64_t min_dead_capacity = 0;

      // We allow the dead objects to consume up to half of the cache capacity.
      uint64_t max_dead_capacity = capacity / 2;
      if (base::SysInfo::IsLowEndDevice()) {
        max_dead_capacity = std::min(static_cast<uint64_t>(512 * 1024u),
                                     max_dead_capacity);
      }

      // Find the WebCachePtr by renderer process id.
      auto it = web_cache_services_.find(allocation->first);
      DCHECK(it != web_cache_services_.end());
      const mojom::WebCachePtr& service = it->second;
      DCHECK(service);
      service->SetCacheCapacities(min_dead_capacity, max_dead_capacity,
                                  capacity);
    }
    ++allocation;
  }
}

void WebCacheManager::ClearCacheForProcess(int render_process_id) {
  std::set<int> renderers;
  renderers.insert(render_process_id);
  ClearRendererCache(renderers, INSTANTLY);
}

void WebCacheManager::ClearRendererCache(
    const std::set<int>& renderers,
    WebCacheManager::ClearCacheOccasion occasion) {
  std::set<int>::const_iterator iter = renderers.begin();
  for (; iter != renderers.end(); ++iter) {
    content::RenderProcessHost* host =
        content::RenderProcessHost::FromID(*iter);
    if (host) {
      // Find the WebCachePtr by renderer process id.
      auto it = web_cache_services_.find(*iter);
      DCHECK(it != web_cache_services_.end());
      const mojom::WebCachePtr& service = it->second;
      DCHECK(service);
      service->ClearCache(occasion == ON_NAVIGATION);
    }
  }
}

void WebCacheManager::ReviseAllocationStrategy() {
  DCHECK(stats_.size() <=
      active_renderers_.size() + inactive_renderers_.size());

  // Check if renderers have gone inactive.
  FindInactiveRenderers();

  // Gather statistics
  uint64_t active_capacity, active_live_size, active_dead_size,
           inactive_capacity, inactive_live_size, inactive_dead_size;
  GatherStats(active_renderers_, &active_capacity, &active_live_size,
              &active_dead_size);
  GatherStats(inactive_renderers_, &inactive_capacity, &inactive_live_size,
              &inactive_dead_size);

  UMA_HISTOGRAM_COUNTS_100("Cache.ActiveTabs", active_renderers_.size());
  UMA_HISTOGRAM_COUNTS_100("Cache.InactiveTabs", inactive_renderers_.size());
  UMA_HISTOGRAM_MEMORY_MB("Cache.ActiveCapacityMB",
                          active_capacity / 1024 / 1024);
  UMA_HISTOGRAM_MEMORY_MB("Cache.ActiveDeadSizeMB",
                          active_dead_size / 1024 / 1024);
  UMA_HISTOGRAM_MEMORY_MB("Cache.ActiveLiveSizeMB",
                          active_live_size / 1024 / 1024);
  UMA_HISTOGRAM_MEMORY_MB("Cache.InactiveCapacityMB",
                          inactive_capacity / 1024 / 1024);
  UMA_HISTOGRAM_MEMORY_MB("Cache.InactiveDeadSizeMB",
                          inactive_dead_size / 1024 / 1024);
  UMA_HISTOGRAM_MEMORY_MB("Cache.InactiveLiveSizeMB",
                          inactive_live_size / 1024 / 1024);

  // Compute an allocation strategy.
  //
  // We attempt various tactics in order of preference.  Our first preference
  // is not to evict any objects.  If we don't have enough resources, we'll
  // first try to evict dead data only.  If that fails, we'll just divide the
  // resources we have evenly.
  //
  // We always try to give the active renderers some head room in their
  // allocations so they can take memory away from an inactive renderer with
  // a large cache allocation.
  //
  // Notice the early exit will prevent attempting less desirable tactics once
  // we've found a workable strategy.
  AllocationStrategy strategy;
  if (  // Ideally, we'd like to give the active renderers some headroom and
        // keep all our current objects.
      AttemptTactic(KEEP_CURRENT_WITH_HEADROOM, active_live_size,
                    active_dead_size, KEEP_CURRENT, inactive_live_size,
                    inactive_dead_size, &strategy) ||
      // If we can't have that, then we first try to evict the dead objects in
      // the caches of inactive renderers.
      AttemptTactic(KEEP_CURRENT_WITH_HEADROOM, active_live_size,
                    active_dead_size, KEEP_LIVE, inactive_live_size,
                    inactive_dead_size, &strategy) ||
      // Next, we try to keep the live objects in the active renders (with some
      // room for new objects) and give whatever is left to the inactive
      // renderers.
      AttemptTactic(KEEP_LIVE_WITH_HEADROOM, active_live_size,
                    active_dead_size, DIVIDE_EVENLY, inactive_live_size,
                    inactive_dead_size, &strategy) ||
      // If we've gotten this far, then we are very tight on memory.  Let's try
      // to at least keep around the live objects for the active renderers.
      AttemptTactic(KEEP_LIVE, active_live_size, active_dead_size,
                    DIVIDE_EVENLY, inactive_live_size, inactive_dead_size,
                    &strategy) ||
      // We're basically out of memory.  The best we can do is just divide up
      // what we have and soldier on.
      AttemptTactic(DIVIDE_EVENLY, active_live_size, active_dead_size,
                    DIVIDE_EVENLY, inactive_live_size, inactive_dead_size,
                    &strategy)) {
    // Having found a workable strategy, we enact it.
    EnactStrategy(strategy);
  } else {
    // DIVIDE_EVENLY / DIVIDE_EVENLY should always succeed.
    NOTREACHED() << "Unable to find a cache allocation";
  }
}

void WebCacheManager::ReviseAllocationStrategyLater() {
  // Ask to be called back in a few milliseconds to actually recompute our
  // allocation.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::Bind(&WebCacheManager::ReviseAllocationStrategy,
                            weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kReviseAllocationDelayMS));
}

void WebCacheManager::FindInactiveRenderers() {
  std::set<int>::const_iterator iter = active_renderers_.begin();
  while (iter != active_renderers_.end()) {
    StatsMap::iterator elmt = stats_.find(*iter);
    DCHECK(elmt != stats_.end());
    TimeDelta idle = Time::Now() - elmt->second.access;
    if (idle >= TimeDelta::FromMinutes(kRendererInactiveThresholdMinutes)) {
      // Moved to inactive status.  This invalidates our iterator.
      inactive_renderers_.insert(*iter);
      active_renderers_.erase(*iter);
      iter = active_renderers_.begin();
      continue;
    }
    ++iter;
  }
}

}  // namespace web_cache

// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/background_sync/background_sync_service_impl.h"

#include <utility>

#include "base/memory/weak_ptr.h"
#include "base/stl_util.h"
#include "content/browser/background_sync/background_sync_context_impl.h"
#include "content/public/browser/browser_thread.h"

namespace content {

namespace {

// TODO(iclelland): Move these converters to mojo::TypeConverter template
// specializations.

BackgroundSyncRegistrationOptions ToBackgroundSyncRegistrationOptions(
    const mojom::SyncRegistrationPtr& in) {
  BackgroundSyncRegistrationOptions out;

  out.tag = in->tag;
  out.network_state = static_cast<SyncNetworkState>(in->network_state);
  return out;
}

mojom::SyncRegistrationPtr ToMojoRegistration(
    const BackgroundSyncRegistration& in) {
  mojom::SyncRegistrationPtr out(content::mojom::SyncRegistration::New());
  out->id = in.id();
  out->tag = in.options()->tag;
  out->network_state = static_cast<content::mojom::BackgroundSyncNetworkState>(
      in.options()->network_state);
  return out;
}

}  // namespace

#define COMPILE_ASSERT_MATCHING_ENUM(mojo_name, manager_name) \
  static_assert(static_cast<int>(content::mojo_name) ==       \
                    static_cast<int>(content::manager_name),  \
                "mojo and manager enums must match")

// TODO(iclelland): Move these tests somewhere else
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncError::NONE,
                             BACKGROUND_SYNC_STATUS_OK);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncError::STORAGE,
                             BACKGROUND_SYNC_STATUS_STORAGE_ERROR);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncError::NOT_FOUND,
                             BACKGROUND_SYNC_STATUS_NOT_FOUND);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncError::NO_SERVICE_WORKER,
                             BACKGROUND_SYNC_STATUS_NO_SERVICE_WORKER);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncError::NOT_ALLOWED,
                             BACKGROUND_SYNC_STATUS_NOT_ALLOWED);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncError::PERMISSION_DENIED,
                             BACKGROUND_SYNC_STATUS_PERMISSION_DENIED);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncError::MAX,
                             BACKGROUND_SYNC_STATUS_PERMISSION_DENIED);

COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncNetworkState::ANY,
                             SyncNetworkState::NETWORK_STATE_ANY);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncNetworkState::AVOID_CELLULAR,
                             SyncNetworkState::NETWORK_STATE_AVOID_CELLULAR);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncNetworkState::ONLINE,
                             SyncNetworkState::NETWORK_STATE_ONLINE);
COMPILE_ASSERT_MATCHING_ENUM(mojom::BackgroundSyncNetworkState::MAX,
                             SyncNetworkState::NETWORK_STATE_ONLINE);

BackgroundSyncServiceImpl::~BackgroundSyncServiceImpl() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(background_sync_context_->background_sync_manager());
}

BackgroundSyncServiceImpl::BackgroundSyncServiceImpl(
    BackgroundSyncContextImpl* background_sync_context,
    mojo::InterfaceRequest<mojom::BackgroundSyncService> request)
    : background_sync_context_(background_sync_context),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(background_sync_context);

  binding_.set_connection_error_handler(
      base::Bind(&BackgroundSyncServiceImpl::OnConnectionError,
                 base::Unretained(this) /* the channel is owned by this */));
}

void BackgroundSyncServiceImpl::OnConnectionError() {
  background_sync_context_->ServiceHadConnectionError(this);
  // |this| is now deleted.
}

void BackgroundSyncServiceImpl::Register(
    content::mojom::SyncRegistrationPtr options,
    int64_t sw_registration_id,
    const RegisterCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  BackgroundSyncRegistrationOptions manager_options =
      ToBackgroundSyncRegistrationOptions(options);

  BackgroundSyncManager* background_sync_manager =
      background_sync_context_->background_sync_manager();
  DCHECK(background_sync_manager);
  background_sync_manager->Register(
      sw_registration_id, manager_options,
      base::Bind(&BackgroundSyncServiceImpl::OnRegisterResult,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void BackgroundSyncServiceImpl::GetRegistrations(
    int64_t sw_registration_id,
    const GetRegistrationsCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  BackgroundSyncManager* background_sync_manager =
      background_sync_context_->background_sync_manager();
  DCHECK(background_sync_manager);
  background_sync_manager->GetRegistrations(
      sw_registration_id,
      base::Bind(&BackgroundSyncServiceImpl::OnGetRegistrationsResult,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void BackgroundSyncServiceImpl::OnRegisterResult(
    const RegisterCallback& callback,
    BackgroundSyncStatus status,
    scoped_ptr<BackgroundSyncRegistration> result) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (status != BACKGROUND_SYNC_STATUS_OK) {
    callback.Run(
        static_cast<content::mojom::BackgroundSyncError>(status),
        mojom::SyncRegistrationPtr(content::mojom::SyncRegistration::New()));
    return;
  }

  DCHECK(result);
  mojom::SyncRegistrationPtr mojoResult = ToMojoRegistration(*result);
  callback.Run(static_cast<content::mojom::BackgroundSyncError>(status),
               std::move(mojoResult));
}

void BackgroundSyncServiceImpl::OnGetRegistrationsResult(
    const GetRegistrationsCallback& callback,
    BackgroundSyncStatus status,
    scoped_ptr<ScopedVector<BackgroundSyncRegistration>> result_registrations) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(result_registrations);

  mojo::Array<content::mojom::SyncRegistrationPtr> mojo_registrations;
  for (const BackgroundSyncRegistration* registration : *result_registrations)
    mojo_registrations.push_back(ToMojoRegistration(*registration));

  callback.Run(static_cast<content::mojom::BackgroundSyncError>(status),
               std::move(mojo_registrations));
}

}  // namespace content

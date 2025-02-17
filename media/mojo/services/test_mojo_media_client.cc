// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/mojo/services/test_mojo_media_client.h"

#include "base/memory/ptr_util.h"
#include "media/audio/audio_manager_base.h"
#include "media/audio/audio_output_stream_sink.h"
#include "media/base/audio_hardware_config.h"
#include "media/base/media.h"
#include "media/base/null_video_sink.h"
#include "media/cdm/default_cdm_factory.h"
#include "media/renderers/default_renderer_factory.h"
#include "media/renderers/gpu_video_accelerator_factories.h"

namespace media {

TestMojoMediaClient::TestMojoMediaClient() {}

TestMojoMediaClient::~TestMojoMediaClient() {}

void TestMojoMediaClient::Initialize() {
  InitializeMediaLibrary();
  // TODO(dalecurtis): We should find a single owner per process for the audio
  // manager or make it a lazy instance.  It's not safe to call Get()/Create()
  // across multiple threads...
  //
  // TODO(dalecurtis): Eventually we'll want something other than a fake audio
  // log factory here too.  We should probably at least DVLOG() such info.
  AudioManager* audio_manager = AudioManager::Get();
  if (!audio_manager)
    audio_manager = media::AudioManager::Create(&fake_audio_log_factory_);

  audio_hardware_config_.reset(new AudioHardwareConfig(
      audio_manager->GetInputStreamParameters(
          AudioManagerBase::kDefaultDeviceId),
      audio_manager->GetDefaultOutputStreamParameters()));
}

std::unique_ptr<RendererFactory> TestMojoMediaClient::CreateRendererFactory(
    const scoped_refptr<MediaLog>& media_log) {
  DVLOG(1) << __FUNCTION__;
  return base::WrapUnique(new DefaultRendererFactory(
      media_log, nullptr, DefaultRendererFactory::GetGpuFactoriesCB(),
      *audio_hardware_config_));
}

AudioRendererSink* TestMojoMediaClient::CreateAudioRendererSink() {
  if (!audio_renderer_sink_)
    audio_renderer_sink_ = new AudioOutputStreamSink();

  return audio_renderer_sink_.get();
}

VideoRendererSink* TestMojoMediaClient::CreateVideoRendererSink(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner) {
  if (!video_renderer_sink_) {
    video_renderer_sink_ = base::WrapUnique(
        new NullVideoSink(false, base::TimeDelta::FromSecondsD(1.0 / 60),
                          NullVideoSink::NewFrameCB(), task_runner));
  }

  return video_renderer_sink_.get();
}

std::unique_ptr<CdmFactory> TestMojoMediaClient::CreateCdmFactory(
    mojo::shell::mojom::InterfaceProvider* /* interface_provider */) {
  DVLOG(1) << __FUNCTION__;
  return base::WrapUnique(new DefaultCdmFactory());
}

}  // namespace media

// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_GLES2_MOJO_GPU_MEMORY_BUFFER_MANAGER_H_
#define COMPONENTS_MUS_GLES2_MOJO_GPU_MEMORY_BUFFER_MANAGER_H_

#include "base/macros.h"
#include "gpu/command_buffer/client/gpu_memory_buffer_manager.h"

namespace mus {

class MojoGpuMemoryBufferManager : public gpu::GpuMemoryBufferManager {
 public:
  MojoGpuMemoryBufferManager();
  ~MojoGpuMemoryBufferManager() override;

  // Overridden from gpu::GpuMemoryBufferManager:
  scoped_ptr<gfx::GpuMemoryBuffer> AllocateGpuMemoryBuffer(
      const gfx::Size& size,
      gfx::BufferFormat format,
      gfx::BufferUsage usage,
      int32_t surface_id) override;
  scoped_ptr<gfx::GpuMemoryBuffer> CreateGpuMemoryBufferFromHandle(
      const gfx::GpuMemoryBufferHandle& handle,
      const gfx::Size& size,
      gfx::BufferFormat format) override;
  gfx::GpuMemoryBuffer* GpuMemoryBufferFromClientBuffer(
      ClientBuffer buffer) override;
  void SetDestructionSyncToken(gfx::GpuMemoryBuffer* buffer,
                               const gpu::SyncToken& sync_token) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(MojoGpuMemoryBufferManager);
};

}  // namespace mus

#endif  // COMPONENTS_MUS_GLES2_MOJO_GPU_MEMORY_BUFFER_MANAGER_H_

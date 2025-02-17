// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_BUFFER_MANAGER_H_
#define GPU_COMMAND_BUFFER_SERVICE_BUFFER_MANAGER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "gpu/command_buffer/common/buffer.h"
#include "gpu/command_buffer/service/gl_utils.h"
#include "gpu/command_buffer/service/memory_tracking.h"
#include "gpu/gpu_export.h"

namespace gpu {
namespace gles2 {

class BufferManager;
struct ContextState;
class ErrorState;
class FeatureInfo;
class TestHelper;

// Info about Buffers currently in the system.
class GPU_EXPORT Buffer : public base::RefCounted<Buffer> {
 public:
  struct MappedRange {
    GLintptr offset;
    GLsizeiptr size;
    GLenum access;
    void* pointer;  // Pointer returned by driver.
    scoped_refptr<gpu::Buffer> shm;  // Client side mem.

    MappedRange(GLintptr offset, GLsizeiptr size, GLenum access,
                void* pointer, scoped_refptr<gpu::Buffer> shm);
    ~MappedRange();
    void* GetShmPointer() const;
  };

  Buffer(BufferManager* manager, GLuint service_id);

  GLuint service_id() const {
    return service_id_;
  }

  GLsizeiptr size() const {
    return size_;
  }

  GLenum usage() const {
    return usage_;
  }

  // Gets the maximum value in the buffer for the given range interpreted as
  // the given type. Returns false if offset and count are out of range.
  // offset is in bytes.
  // count is in elements of type.
  bool GetMaxValueForRange(GLuint offset, GLsizei count, GLenum type,
                           bool primitive_restart_enabled, GLuint* max_value);

  // Returns a pointer to shadowed data.
  const void* GetRange(GLintptr offset, GLsizeiptr size) const;

  bool IsDeleted() const {
    return deleted_;
  }

  bool IsValid() const {
    return initial_target() && !IsDeleted();
  }

  bool IsClientSideArray() const {
    return is_client_side_array_;
  }

  void SetMappedRange(GLintptr offset, GLsizeiptr size, GLenum access,
                      void* pointer, scoped_refptr<gpu::Buffer> shm) {
    mapped_range_.reset(new MappedRange(offset, size, access, pointer, shm));
  }

  void RemoveMappedRange() {
    mapped_range_.reset(nullptr);
  }

  const MappedRange* GetMappedRange() const {
    return mapped_range_.get();
  }

 private:
  friend class BufferManager;
  friend class BufferManagerTestBase;
  friend class base::RefCounted<Buffer>;

  // Represents a range in a buffer.
  class Range {
   public:
    Range(GLuint offset, GLsizei count, GLenum type,
          bool primitive_restart_enabled)
        : offset_(offset),
          count_(count),
          type_(type),
          primitive_restart_enabled_(primitive_restart_enabled) {
    }

    // A less functor provided for std::map so it can find ranges.
    struct Less {
      bool operator() (const Range& lhs, const Range& rhs) const {
        if (lhs.offset_ != rhs.offset_) {
          return lhs.offset_ < rhs.offset_;
        }
        if (lhs.count_ != rhs.count_) {
          return lhs.count_ < rhs.count_;
        }
        if (lhs.type_ != rhs.type_) {
          return lhs.type_ < rhs.type_;
        }
        return lhs.primitive_restart_enabled_ < rhs.primitive_restart_enabled_;
      }
    };

   private:
    GLuint offset_;
    GLsizei count_;
    GLenum type_;
    bool primitive_restart_enabled_;
  };

  ~Buffer();

  GLenum initial_target() const {
    return initial_target_;
  }

  void set_initial_target(GLenum target) {
    DCHECK_EQ(0u, initial_target_);
    initial_target_ = target;
  }

  bool shadowed() const { return !shadow_.empty(); }

  void MarkAsDeleted() {
    deleted_ = true;
  }

  // Setup the shadow buffer. This will either initialize the shadow buffer
  // with the passed data or clear the shadow buffer if no shadow required. This
  // will return a pointer to the shadowed data if using shadow, otherwise will
  // return the original data pointer.
  const GLvoid* StageShadow(bool use_shadow,
                            GLsizeiptr size,
                            const GLvoid* data);

  // Sets the size, usage and initial data of a buffer.
  // If shadow is true then if data is NULL buffer will be initialized to 0.
  void SetInfo(GLsizeiptr size,
               GLenum usage,
               bool use_shadow,
               bool is_client_side_array);

  // Sets a range of data for this buffer. Returns false if the offset or size
  // is out of range.
  bool SetRange(
    GLintptr offset, GLsizeiptr size, const GLvoid * data);

  // Clears any cache of index ranges.
  void ClearCache();

  // Check if an offset, size range is valid for the current buffer.
  bool CheckRange(GLintptr offset, GLsizeiptr size) const;

  // The manager that owns this Buffer.
  BufferManager* manager_;

  // A copy of the data in the buffer. This data is only kept if the conditions
  // checked in UseShadowBuffer() are true.
  std::vector<uint8_t> shadow_;

  // Size of buffer.
  GLsizeiptr size_;

  // True if deleted.
  bool deleted_;

  // Whether or not this Buffer is not uploaded to the GPU but just
  // sitting in local memory.
  bool is_client_side_array_;

  // Service side buffer id.
  GLuint service_id_;

  // The first target of buffer. 0 = unset.
  // It is set the first time bindBuffer() is called and cannot be changed.
  GLenum initial_target_;

  // Usage of buffer.
  GLenum usage_;

  // Data cached from last glMapBufferRange call.
  scoped_ptr<MappedRange> mapped_range_;

  // A map of ranges to the highest value in that range of a certain type.
  typedef std::map<Range, GLuint, Range::Less> RangeToMaxValueMap;
  RangeToMaxValueMap range_set_;
};

// This class keeps track of the buffers and their sizes so we can do
// bounds checking.
//
// NOTE: To support shared resources an instance of this class will need to be
// shared by multiple GLES2Decoders.
class GPU_EXPORT BufferManager : public base::trace_event::MemoryDumpProvider {
 public:
  BufferManager(MemoryTracker* memory_tracker, FeatureInfo* feature_info);
  ~BufferManager() override;

  // Must call before destruction.
  void Destroy(bool have_context);

  // Creates a Buffer for the given buffer.
  void CreateBuffer(GLuint client_id, GLuint service_id);

  // Gets the buffer info for the given buffer.
  Buffer* GetBuffer(GLuint client_id);

  // Removes a buffer info for the given buffer.
  void RemoveBuffer(GLuint client_id);

  // Gets a client id for a given service id.
  bool GetClientId(GLuint service_id, GLuint* client_id) const;

  // Validates a glBufferSubData, and then calls DoBufferData if validation was
  // successful.
  void ValidateAndDoBufferSubData(
      ContextState* context_state, GLenum target, GLintptr offset,
      GLsizeiptr size, const GLvoid * data);

  // Validates a glBufferData, and then calls DoBufferData if validation was
  // successful.
  void ValidateAndDoBufferData(
    ContextState* context_state, GLenum target, GLsizeiptr size,
    const GLvoid * data, GLenum usage);

  // Validates a glGetBufferParameteri64v, and then calls GetBufferParameteri64v
  // if validation was successful.
  void ValidateAndDoGetBufferParameteri64v(
    ContextState* context_state, GLenum target, GLenum pname, GLint64* params);

  // Validates a glGetBufferParameteriv, and then calls GetBufferParameteriv if
  // validation was successful.
  void ValidateAndDoGetBufferParameteriv(
    ContextState* context_state, GLenum target, GLenum pname, GLint* params);

  // Sets the target of a buffer. Returns false if the target can not be set.
  bool SetTarget(Buffer* buffer, GLenum target);

  void set_allow_buffers_on_multiple_targets(bool allow) {
    allow_buffers_on_multiple_targets_ = allow;
  }

  void set_allow_fixed_attribs(bool allow) {
    allow_fixed_attribs_ = allow;
  }

  size_t mem_represented() const {
    return memory_type_tracker_->GetMemRepresented();
  }

  // Tells for a given usage if this would be a client side array.
  bool IsUsageClientSideArray(GLenum usage);

  // Tells whether a buffer that is emulated using client-side arrays should be
  // set to a non-zero size.
  bool UseNonZeroSizeForClientSideArrayBuffer();

  void SetPrimitiveRestartFixedIndexIfNecessary(GLenum type);

  Buffer* GetBufferInfoForTarget(ContextState* state, GLenum target) const;

  // base::trace_event::MemoryDumpProvider implementation.
  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override;

 private:
  friend class Buffer;
  friend class TestHelper;  // Needs access to DoBufferData.
  friend class BufferManagerTestBase;  // Needs access to DoBufferSubData.

  void StartTracking(Buffer* buffer);
  void StopTracking(Buffer* buffer);

  // Does a glBufferSubData and updates the approriate accounting.
  // Assumes the values have already been validated.
  void DoBufferSubData(
      ErrorState* error_state,
      Buffer* buffer,
      GLenum target,
      GLintptr offset,
      GLsizeiptr size,
      const GLvoid* data);

  // Does a glBufferData and updates the approprate accounting. Currently
  // Assumes the values have already been validated.
  void DoBufferData(
      ErrorState* error_state,
      Buffer* buffer,
      GLenum target,
      GLsizeiptr size,
      GLenum usage,
      const GLvoid* data);

  // Tests whether a shadow buffer needs to be used.
  bool UseShadowBuffer(GLenum target, GLenum usage);

  // Sets the size, usage and initial data of a buffer.
  // If data is NULL buffer will be initialized to 0 if shadowed.
  void SetInfo(Buffer* buffer,
               GLenum target,
               GLsizeiptr size,
               GLenum usage,
               bool use_shadow);

  scoped_ptr<MemoryTypeTracker> memory_type_tracker_;
  MemoryTracker* memory_tracker_;
  scoped_refptr<FeatureInfo> feature_info_;

  // Info for each buffer in the system.
  typedef base::hash_map<GLuint, scoped_refptr<Buffer> > BufferMap;
  BufferMap buffers_;

  // Whether or not buffers can be bound to multiple targets.
  bool allow_buffers_on_multiple_targets_;

  // Whether or not allow using GL_FIXED type for vertex attribs.
  bool allow_fixed_attribs_;

  // Counts the number of Buffer allocated with 'this' as its manager.
  // Allows to check no Buffer will outlive this.
  unsigned int buffer_count_;

  GLuint primitive_restart_fixed_index_;

  bool have_context_;
  bool use_client_side_arrays_for_stream_buffers_;

  DISALLOW_COPY_AND_ASSIGN(BufferManager);
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_BUFFER_MANAGER_H_

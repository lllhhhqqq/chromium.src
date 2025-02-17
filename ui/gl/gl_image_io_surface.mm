// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/gl_image_io_surface.h"

#include <map>

#include "base/callback_helpers.h"
#include "base/mac/bind_objc_block.h"
#include "base/mac/foundation_util.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/process_memory_dump.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/scoped_api.h"
#include "ui/gl/scoped_binders.h"
#include "ui/gl/yuv_to_rgb_converter.h"

// Note that this must be included after gl_bindings.h to avoid conflicts.
#include <OpenGL/CGLIOSurface.h>
#include <Quartz/Quartz.h>
#include <stddef.h>

using gfx::BufferFormat;

namespace gl {
namespace {

bool ValidInternalFormat(unsigned internalformat) {
  switch (internalformat) {
    case GL_RED:
    case GL_BGRA_EXT:
    case GL_RGB:
    case GL_RGB_YCBCR_420V_CHROMIUM:
    case GL_RGB_YCBCR_422_CHROMIUM:
    case GL_RGBA:
      return true;
    default:
      return false;
  }
}

bool ValidFormat(BufferFormat format) {
  switch (format) {
    case BufferFormat::R_8:
    case BufferFormat::BGRA_8888:
    case BufferFormat::RGBA_8888:
    case BufferFormat::UYVY_422:
    case BufferFormat::YUV_420_BIPLANAR:
      return true;
    case BufferFormat::ATC:
    case BufferFormat::ATCIA:
    case BufferFormat::DXT1:
    case BufferFormat::DXT5:
    case BufferFormat::ETC1:
    case BufferFormat::RGBA_4444:
    case BufferFormat::RGBX_8888:
    case BufferFormat::BGRX_8888:
    case BufferFormat::YUV_420:
      return false;
  }

  NOTREACHED();
  return false;
}

GLenum TextureFormat(BufferFormat format) {
  switch (format) {
    case BufferFormat::R_8:
      return GL_RED;
    case BufferFormat::BGRA_8888:
    case BufferFormat::RGBA_8888:
      return GL_RGBA;
    case BufferFormat::UYVY_422:
      return GL_RGB;
    case BufferFormat::YUV_420_BIPLANAR:
      return GL_RGB_YCBCR_420V_CHROMIUM;
    case BufferFormat::ATC:
    case BufferFormat::ATCIA:
    case BufferFormat::DXT1:
    case BufferFormat::DXT5:
    case BufferFormat::ETC1:
    case BufferFormat::RGBA_4444:
    case BufferFormat::RGBX_8888:
    case BufferFormat::BGRX_8888:
    case BufferFormat::YUV_420:
      NOTREACHED();
      return 0;
  }

  NOTREACHED();
  return 0;
}

GLenum DataFormat(BufferFormat format) {
  switch (format) {
    case BufferFormat::R_8:
      return GL_RED;
    case BufferFormat::BGRA_8888:
    case BufferFormat::RGBA_8888:
      return GL_BGRA;
    case BufferFormat::UYVY_422:
      return GL_YCBCR_422_APPLE;
    case BufferFormat::ATC:
    case BufferFormat::ATCIA:
    case BufferFormat::DXT1:
    case BufferFormat::DXT5:
    case BufferFormat::ETC1:
    case BufferFormat::RGBA_4444:
    case BufferFormat::RGBX_8888:
    case BufferFormat::BGRX_8888:
    case BufferFormat::YUV_420:
    case BufferFormat::YUV_420_BIPLANAR:
      NOTREACHED();
      return 0;
  }

  NOTREACHED();
  return 0;
}

GLenum DataType(BufferFormat format) {
  switch (format) {
    case BufferFormat::R_8:
      return GL_UNSIGNED_BYTE;
    case BufferFormat::BGRA_8888:
    case BufferFormat::RGBA_8888:
      return GL_UNSIGNED_INT_8_8_8_8_REV;
    case BufferFormat::UYVY_422:
      return GL_UNSIGNED_SHORT_8_8_APPLE;
      break;
    case BufferFormat::ATC:
    case BufferFormat::ATCIA:
    case BufferFormat::DXT1:
    case BufferFormat::DXT5:
    case BufferFormat::ETC1:
    case BufferFormat::RGBA_4444:
    case BufferFormat::RGBX_8888:
    case BufferFormat::BGRX_8888:
    case BufferFormat::YUV_420:
    case BufferFormat::YUV_420_BIPLANAR:
      NOTREACHED();
      return 0;
  }

  NOTREACHED();
  return 0;
}

}  // namespace

GLImageIOSurface::GLImageIOSurface(const gfx::Size& size,
                                   unsigned internalformat)
    : size_(size),
      internalformat_(internalformat),
      format_(BufferFormat::RGBA_8888) {}

GLImageIOSurface::~GLImageIOSurface() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!io_surface_);
}

bool GLImageIOSurface::Initialize(IOSurfaceRef io_surface,
                                  gfx::GenericSharedMemoryId io_surface_id,
                                  BufferFormat format) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!io_surface_);

  if (!ValidInternalFormat(internalformat_)) {
    LOG(ERROR) << "Invalid internalformat: " << internalformat_;
    return false;
  }

  if (!ValidFormat(format)) {
    LOG(ERROR) << "Invalid format: " << static_cast<int>(format);
    return false;
  }

  format_ = format;
  io_surface_.reset(io_surface, base::scoped_policy::RETAIN);
  io_surface_id_ = io_surface_id;
  return true;
}

bool GLImageIOSurface::InitializeWithCVPixelBuffer(
    CVPixelBufferRef cv_pixel_buffer,
    gfx::GenericSharedMemoryId io_surface_id,
    BufferFormat format) {
  IOSurfaceRef io_surface = CVPixelBufferGetIOSurface(cv_pixel_buffer);
  if (!io_surface) {
    LOG(ERROR) << "Can't init GLImage from CVPixelBuffer with no IOSurface";
    return false;
  }

  if (!Initialize(io_surface, io_surface_id, format))
    return false;

  cv_pixel_buffer_.reset(cv_pixel_buffer, base::scoped_policy::RETAIN);
  return true;
}

void GLImageIOSurface::Destroy(bool have_context) {
  DCHECK(thread_checker_.CalledOnValidThread());
  io_surface_.reset();
  cv_pixel_buffer_.reset();
}

gfx::Size GLImageIOSurface::GetSize() {
  return size_;
}

unsigned GLImageIOSurface::GetInternalFormat() {
  return internalformat_;
}

bool GLImageIOSurface::BindTexImage(unsigned target) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // YUV_420_BIPLANAR is not supported by BindTexImage.
  // CopyTexImage is supported by this format as that performs conversion to RGB
  // as part of the copy operation.
  if (format_ == BufferFormat::YUV_420_BIPLANAR)
    return false;

  if (target != GL_TEXTURE_RECTANGLE_ARB) {
    // This might be supported in the future. For now, perform strict
    // validation so we know what's going on.
    LOG(ERROR) << "IOSurface requires TEXTURE_RECTANGLE_ARB target";
    return false;
  }

  CGLContextObj cgl_context =
      static_cast<CGLContextObj>(gfx::GLContext::GetCurrent()->GetHandle());

  DCHECK(io_surface_);
  CGLError cgl_error =
      CGLTexImageIOSurface2D(cgl_context, target, TextureFormat(format_),
                             size_.width(), size_.height(), DataFormat(format_),
                             DataType(format_), io_surface_.get(), 0);
  if (cgl_error != kCGLNoError) {
    LOG(ERROR) << "Error in CGLTexImageIOSurface2D";
    return false;
  }

  return true;
}

bool GLImageIOSurface::CopyTexImage(unsigned target) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (format_ != BufferFormat::YUV_420_BIPLANAR)
    return false;

  if (target != GL_TEXTURE_RECTANGLE_ARB) {
    LOG(ERROR) << "YUV_420_BIPLANAR requires GL_TEXTURE_RECTANGLE_ARB target";
    return false;
  }

  gfx::GLContext* gl_context = gfx::GLContext::GetCurrent();
  DCHECK(gl_context);

  gl::YUVToRGBConverter* yuv_to_rgb_converter =
      gl_context->GetYUVToRGBConverter();
  DCHECK(yuv_to_rgb_converter);

  gfx::ScopedSetGLToRealGLApi scoped_set_gl_api;

  // Note that state restoration is done explicitly instead of scoped binders to
  // avoid https://crbug.com/601729.
  GLint rgb_texture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB, &rgb_texture);
  base::ScopedClosureRunner destroy_resources_runner(base::BindBlock(^{
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, rgb_texture);
  }));

  CGLContextObj cgl_context = CGLGetCurrentContext();
  {
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, yuv_to_rgb_converter->y_texture());
    CGLError cgl_error = CGLTexImageIOSurface2D(
        cgl_context, GL_TEXTURE_RECTANGLE_ARB, GL_RED, size_.width(),
        size_.height(), GL_RED, GL_UNSIGNED_BYTE, io_surface_, 0);
    if (cgl_error != kCGLNoError) {
      LOG(ERROR) << "Error in CGLTexImageIOSurface2D for the Y plane. "
                 << cgl_error;
      return false;
    }
  }
  {
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, yuv_to_rgb_converter->uv_texture());
    CGLError cgl_error = CGLTexImageIOSurface2D(
        cgl_context, GL_TEXTURE_RECTANGLE_ARB, GL_RG, size_.width() / 2,
        size_.height() / 2, GL_RG, GL_UNSIGNED_BYTE, io_surface_, 1);
    if (cgl_error != kCGLNoError) {
      LOG(ERROR) << "Error in CGLTexImageIOSurface2D for the UV plane. "
                 << cgl_error;
      return false;
    }
  }

  yuv_to_rgb_converter->CopyYUV420ToRGB(
      GL_TEXTURE_RECTANGLE_ARB,
      size_,
      rgb_texture);
  return true;
}

bool GLImageIOSurface::CopyTexSubImage(unsigned target,
                                       const gfx::Point& offset,
                                       const gfx::Rect& rect) {
  return false;
}

bool GLImageIOSurface::ScheduleOverlayPlane(gfx::AcceleratedWidget widget,
                                            int z_order,
                                            gfx::OverlayTransform transform,
                                            const gfx::Rect& bounds_rect,
                                            const gfx::RectF& crop_rect) {
  NOTREACHED();
  return false;
}

void GLImageIOSurface::OnMemoryDump(base::trace_event::ProcessMemoryDump* pmd,
                                    uint64_t process_tracing_id,
                                    const std::string& dump_name) {
  // IOSurfaceGetAllocSize will return 0 if io_surface_ is invalid. In this case
  // we log 0 for consistency with other GLImage memory dump functions.
  size_t size_bytes = IOSurfaceGetAllocSize(io_surface_);

  base::trace_event::MemoryAllocatorDump* dump =
      pmd->CreateAllocatorDump(dump_name);
  dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes,
                  static_cast<uint64_t>(size_bytes));

  auto guid =
      GetGenericSharedMemoryGUIDForTracing(process_tracing_id, io_surface_id_);
  pmd->CreateSharedGlobalAllocatorDump(guid);
  pmd->AddOwnershipEdge(dump->guid(), guid);
}

base::ScopedCFTypeRef<IOSurfaceRef> GLImageIOSurface::io_surface() {
  return io_surface_;
}

base::ScopedCFTypeRef<CVPixelBufferRef> GLImageIOSurface::cv_pixel_buffer() {
  return cv_pixel_buffer_;
}

// static
unsigned GLImageIOSurface::GetInternalFormatForTesting(
    gfx::BufferFormat format) {
  DCHECK(ValidFormat(format));
  return TextureFormat(format);
}
}  // namespace gl

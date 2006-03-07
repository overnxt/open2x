/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2004 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/

#ifdef SAVE_RCSID
static char rcsid =
 "@(#) $Id: $";
#endif

/*
 * GP2X SDL video driver implementation,
 *  Base, dummy.
 *  Memory routines based on fbcon
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "SDL.h"
#include "SDL_error.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_sysvideo.h"
#include "SDL_pixels_c.h"
#include "SDL_events_c.h"

#include "SDL_gp2xvideo.h"
#include "SDL_gp2xevents_c.h"
#include "SDL_gp2xmouse_c.h"
#include "mmsp2_regs.h"

#define GP2XVID_DRIVER_NAME "GP2X"


// Initialization/Query functions
static int GP2X_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **GP2X_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *GP2X_SetVideoMode(_THIS, SDL_Surface *current,
				      int width, int height,
				      int bpp, Uint32 flags);
static int GP2X_SetColors(_THIS, int firstcolor, int ncolors,
			  SDL_Color *colors);
static void GP2X_VideoQuit(_THIS);

// Hardware surface functions
static int GP2X_InitHWSurfaces(_THIS, SDL_Surface *screen,
			       char *base, int size);
static void GP2X_FreeHWSurfaces(_THIS);
static int GP2X_AllocHWSurface(_THIS, SDL_Surface *surface);
static int GP2X_LockHWSurface(_THIS, SDL_Surface *surface);
static void GP2X_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void GP2X_FreeHWSurface(_THIS, SDL_Surface *surface);
static void GP2X_DummyBlit(_THIS);
static int GP2X_FlipHWSurface(_THIS, SDL_Surface *surface);
// etc.
static void GP2X_UpdateRects(_THIS, int numrects, SDL_Rect *rects);
static int GP2X_CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst);
static int GP2X_FillHWRect(_THIS, SDL_Surface *surface,
			   SDL_Rect *area, Uint32 colour);
static int GP2X_HWAccelBlit(SDL_Surface *src, SDL_Rect *src_rect,
			    SDL_Surface *dst, SDL_Rect *dst_rect);

////////
// GP2X driver bootstrap functions
////////

////
// Are we available?
static int GP2X_Available(void)
{
  // Of course we are.
  return 1;
}

////
// Cleanup routine
static void GP2X_DeleteDevice(SDL_VideoDevice *device)
{
  SDL_PrivateVideoData *data = device->hidden;
#ifdef GP2X_DEBUG
  fputs("SDL: GP2X_DeleteDevice\n", stderr);
#endif

  if (data->fio)
    munmap(data->fio, 0x100);
  data->fio = NULL;
  if (data->io) {
    // Clear register bits we clobbered if they weren't on
    if (data->fastioclk == 0)
      data->io[SYSCLKENREG] &= ~(FASTIOCLK);
    if (data->grpclk == 0)
      data->io[VCLKENREG] &= ~(GRPCLK);
    // reset display hardware
    data->io[MLC_STL_CNTL]   = data->stl_cntl;
    data->io[MLC_STL_MIXMUX] = data->stl_mixmux;
    data->io[MLC_STL_ALPHAL] = data->stl_alphal;
    data->io[MLC_STL_ALPHAH] = data->stl_alphah;
    data->io[MLC_STL_HSC]    = data->stl_hsc;
    data->io[MLC_STL_VSCL]   = data->stl_vscl;
    //    data->io[MLC_STL_VSCH]   = data->stl_vsch;
    data->io[MLC_STL_HW]     = data->stl_hw;
    data->io[MLC_STL_OADRL]  = data->stl_oadrl;
    data->io[MLC_STL_OADRH]  = data->stl_oadrh;
    data->io[MLC_STL_EADRL]  = data->stl_eadrl;
    data->io[MLC_STL_EADRH]  = data->stl_eadrh;
    munmap(data->io, 0x10000);
  }
  data->io = NULL;
  if (data->vmem)
    munmap(data->vmem, GP2X_VIDEO_MEM_SIZE);
  data->vmem = NULL;

  if (data->memory_fd)
    close(data->memory_fd);
  free(device->hidden);
  free(device);
}

////
// Initalize driver
static SDL_VideoDevice *GP2X_CreateDevice(int devindex)
{
  SDL_VideoDevice *device;
#ifdef GP2X_DEBUG
  fputs("SDL: GP2X_CreateDevice\n", stderr);
#endif  

  /* Initialize all variables that we clean on shutdown */
  device = (SDL_VideoDevice *)malloc(sizeof(SDL_VideoDevice));
  if (device) {
    memset(device, 0, (sizeof *device));
    device->hidden = (struct SDL_PrivateVideoData *)
      malloc((sizeof *device->hidden));
  }
  if ((device == NULL) || (device->hidden == NULL)) {
    SDL_OutOfMemory();
    if (device) {
      free(device);
    }
    return 0;
  }
  memset(device->hidden, 0, (sizeof *device->hidden));
  
  // Set the function pointers
  device->VideoInit = GP2X_VideoInit;
  device->ListModes = GP2X_ListModes;
  device->SetVideoMode = GP2X_SetVideoMode;
  device->CreateYUVOverlay = NULL;
  device->SetColors = GP2X_SetColors;
  device->UpdateRects = GP2X_UpdateRects;
  device->VideoQuit = GP2X_VideoQuit;
  device->AllocHWSurface = GP2X_AllocHWSurface;
  device->CheckHWBlit = GP2X_CheckHWBlit;
  device->FillHWRect = GP2X_FillHWRect;
  device->SetHWColorKey = NULL;
  device->SetHWAlpha = NULL;
  device->LockHWSurface = GP2X_LockHWSurface;
  device->UnlockHWSurface = GP2X_UnlockHWSurface;
  device->FlipHWSurface = GP2X_FlipHWSurface;
  device->FreeHWSurface = GP2X_FreeHWSurface;
  device->SetCaption = NULL;
  device->SetIcon = NULL;
  device->IconifyWindow = NULL;
  device->GrabInput = NULL;
  device->GetWMInfo = NULL;
  device->InitOSKeymap = GP2X_InitOSKeymap;
  device->PumpEvents = GP2X_PumpEvents;
  device->free = GP2X_DeleteDevice;
  
  return device;
}

////
// Link info to SDL_video
VideoBootStrap GP2X_bootstrap = {
  GP2XVID_DRIVER_NAME, "SDL GP2X video driver",
  GP2X_Available, GP2X_CreateDevice
};

////
// Set up hardware
static int GP2X_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
  SDL_PrivateVideoData *data = this->hidden;
#ifdef GP2X_DEBUG
  fputs("SDL: GP2X_VideoInit\n", stderr);
#endif

#ifndef DISABLE_THREADS
  // Create hardware surface lock mutex
  data->hw_lock = SDL_CreateMutex();
  if (data->hw_lock == NULL) {
    SDL_SetError("Unable to create lock mutex");
    GP2X_VideoQuit(this);
    return -1;
  }
#endif

  data->memory_fd = open("/dev/mem", O_RDWR, 0);
  if (data->memory_fd < 0) {
    SDL_SetError("Unable to open /dev/mem\n");
    return -1;
  }
  data->vmem = mmap(NULL, GP2X_VIDEO_MEM_SIZE, PROT_READ|PROT_WRITE,
		    MAP_SHARED, data->memory_fd, 0x3101000);
  if (data->vmem == (char *)-1) {
    SDL_SetError("Unable to get video memory");
    data->vmem = NULL;
    GP2X_VideoQuit(this);
    return -1;
  }
  data->io = mmap(NULL, 0x10000, PROT_READ|PROT_WRITE,
		  MAP_SHARED, data->memory_fd, 0xc0000000);
  if (data->io == (unsigned short *)-1) {
    SDL_SetError("Unable to get hardware registers");
    data->io = NULL;
    GP2X_VideoQuit(this);
    return -1;
  }
  data->fio = mmap(NULL, 0x100, PROT_READ|PROT_WRITE,
		  MAP_SHARED, data->memory_fd, 0xe0020000);
  if (data->fio == (unsigned int *)-1) {
    SDL_SetError("Unable to get blitter registers");
    data->fio = NULL;
    GP2X_VideoQuit(this);
    return -1;
  }

  // Determine the screen depth (gp2x defaults to 16-bit depth)
  // we change this during the SDL_SetVideoMode implementation...
  vformat->BitsPerPixel = 16;
  vformat->BytesPerPixel = 2;
  vformat->Rmask = 0x1f00;
  vformat->Gmask = 0x07e0;
  vformat->Bmask = 0x001f;
  vformat->Amask = 0;

  this->info.wm_available = 0;
  this->info.hw_available = 1;
  this->info.video_mem = GP2X_VIDEO_MEM_SIZE / 1024;
  //  memset(data->vmem, GP2X_VIDEO_MEM_SIZE, 0);
  // Save hw register data that we clobber
  data->fastioclk = data->io[SYSCLKENREG] & FASTIOCLK;
  data->grpclk = data->io[VCLKENREG] & GRPCLK;
#ifdef GP2X_DEBUG
  fprintf(stderr, "fastioclk = %x, grpclk = %x\n",
	  data->fastioclk, data->grpclk);
#endif
  // Need FastIO for blitter
  data->io[SYSCLKENREG] |= FASTIOCLK;
  // and enable graphics clock
  data->io[VCLKENREG] |= GRPCLK;

  // Save display registers so we can restore screen to original state
  data->stl_cntl =   data->io[MLC_STL_CNTL];
  data->stl_mixmux = data->io[MLC_STL_MIXMUX];
  data->stl_alphal = data->io[MLC_STL_ALPHAL];
  data->stl_alphah = data->io[MLC_STL_ALPHAH];
  data->stl_hsc =    data->io[MLC_STL_HSC];
  data->stl_vscl =   data->io[MLC_STL_VSCL];
  //  data->stl_vsch =   data->io[MLC_STL_VSCH];
  data->stl_hw =     data->io[MLC_STL_HW];
  data->stl_oadrl =  data->io[MLC_STL_OADRL];
  data->stl_oadrh =  data->io[MLC_STL_OADRH];
  data->stl_eadrl =  data->io[MLC_STL_EADRL];
  data->stl_eadrh =  data->io[MLC_STL_EADRH];

  // Check what video mode we're in (LCD, NTSC or PAL)
  data->phys_width = data->io[DPC_X_MAX] + 1;
  data->phys_height = data->io[DPC_Y_MAX] + 1;
  data->phys_ilace = (data->io[DPC_CNTL] & DPC_INTERLACE) ? 1 : 0;
#ifdef GP2X_DEBUG
  fprintf(stderr, "real screen = %dx%d (ilace = %d)\n",
	  data->phys_width, data->phys_height, data->phys_ilace);
#endif
  int i;
  for (i=0; i<SDL_NUMMODES; i++) {
    data->SDL_modelist[i] = malloc(sizeof(SDL_Rect));
    data->SDL_modelist[i]->x = data->SDL_modelist[i]->y = 0;
  }
  data->SDL_modelist[0]->w =  320; data->SDL_modelist[0]->h = 200; // low-res
  data->SDL_modelist[1]->w =  320; data->SDL_modelist[1]->h = 240; // lo-res
  data->SDL_modelist[2]->w =  640; data->SDL_modelist[2]->h = 400; // vga-low
  data->SDL_modelist[3]->w =  640; data->SDL_modelist[3]->h = 480; // vga
  data->SDL_modelist[4]->w =  720; data->SDL_modelist[4]->h = 480; // TV NTSC
  data->SDL_modelist[5]->w =  720; data->SDL_modelist[5]->h = 576; // TV PAL
  data->SDL_modelist[6]->w =  800; data->SDL_modelist[6]->h = 600; // vga-med
  data->SDL_modelist[7]->w = 1024; data->SDL_modelist[7]->h = 768; // vga-high
  data->SDL_modelist[8] = NULL;

  this->info.blit_fill = 1;
  this->FillHWRect = GP2X_FillHWRect;
  this->info.blit_hw = 1;
  this->info.blit_hw_CC = 1;
  return 0;
}

////
// Return list of possible screen sizes for given mode
static SDL_Rect **GP2X_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
#ifdef GP2X_DEBUG
  fprintf(stderr, "SDL: GP2X_ListModes\n");
#endif
  // Only 8 & 16 bit modes. 4 & 24 are available, but tough.
  if ((format->BitsPerPixel != 8) && (format->BitsPerPixel != 16))
    return NULL;

  if (flags & SDL_FULLSCREEN)
    return this->hidden->SDL_modelist;
  else
    return (SDL_Rect **) -1;
}

////
// Set hw videomode
static SDL_Surface *GP2X_SetVideoMode(_THIS, SDL_Surface *current,
				      int width, int height,
				      int bpp, Uint32 flags)
{
  SDL_PrivateVideoData *data = this->hidden;
  char *pixelbuffer;
#ifdef GP2X_DEBUG
  fprintf(stderr, "SDL: Setting video mode %dx%d %d bpp, flags=%X\n",
	  width, height, bpp, flags);
#endif

  // Set up the new mode framebuffer, making sanity adjustments
  // 64 <= width <= 1024, multiples of 8 only
  width = (width + 7) & 0x7f8;
  if (width < 64) width = 64;
  if (width > 1024) width = 1024;

  // 64 <= height <= 768
  if (height < 64) height = 64;
  if (height > 768) height = 768;

  // 8 or 16 bpp. HW can handle 24, but limited support so not implemented
  bpp = (bpp <= 8) ? 8 : 16;
  
  // Allocate the new pixel format for the screen
  if (!SDL_ReallocFormat(current, bpp, 0, 0, 0, 0)) {
    SDL_SetError("Couldn't allocate new pixel format for requested mode");
    return NULL;
  }
  
  // Screen is always a HWSURFACE and FULLSCREEN
  current->flags = (flags & SDL_DOUBLEBUF) | SDL_FULLSCREEN |
    SDL_HWSURFACE | SDL_NOFRAME;
  if (bpp == 8) current->flags |= SDL_HWPALETTE;
  data->w = current->w = width;
  data->h = current->h = height;
  data->pitch = data->phys_pitch = current->pitch = width * (bpp / 8);
  if (data->phys_ilace && (width == 720))
    data->phys_pitch *= 2;
  this->screen = current;
  current->pixels = data->vmem;

  // gp2x holds x-scale as fixed-point, 1024 == 1:1
  data->scale_x = (1024 * width) / data->phys_width;
  // and y-scale is scale * pitch
  data->scale_y = (height * data->pitch) / data->phys_height;
  data->x_offset = data->y_offset = 0;
  data->ptr_offset = 0;
  data->buffer_showing = 0;
  data->buffer_addr[0] = data->vmem;
  data->surface_mem = data->vmem + (height * data->pitch);
  data->memory_max = GP2X_VIDEO_MEM_SIZE - height * data->pitch;
  if (flags & SDL_DOUBLEBUF) {
    current->pixels = data->buffer_addr[1] = data->surface_mem;
    data->surface_mem += height * data->pitch;
    data->memory_max -= height * data->pitch;
  }
  GP2X_FreeHWSurfaces(this);
  GP2X_InitHWSurfaces(this, current, data->surface_mem, data->memory_max);

  // Load the registers
  data->io[MLC_STL_HSC] = data->scale_x;
  data->io[MLC_STL_VSCL] = data->scale_y & 0xffff;
  //  data->io[MLC_STL_VSCH] = data->scale_y >> 16;
  data->io[MLC_STL_HW] = data->phys_pitch;
  data->io[MLC_STL_CNTL] = (bpp==8 ? MLC_STL_BPP_8 : MLC_STL_BPP_16) |
    MLC_STL1ACT;
  data->io[MLC_STL_MIXMUX] = 0;
  data->io[MLC_STL_ALPHAL] = 255;
  data->io[MLC_STL_ALPHAH] = 255;
  pixelbuffer = data->vmem;
  if (data->phys_ilace) {
    data->io[MLC_STL_OADRL] = GP2X_PhysL(this, pixelbuffer);
    data->io[MLC_STL_OADRH] = GP2X_PhysH(this, pixelbuffer);
    if (data->w == 720) pixelbuffer += data->pitch;
  }
  data->io[MLC_STL_EADRL] = GP2X_PhysL(this, pixelbuffer);
  data->io[MLC_STL_EADRH] = GP2X_PhysH(this, pixelbuffer);
  return current;
}

////
// Initialize HW surface list
static int GP2X_InitHWSurfaces(_THIS, SDL_Surface *screen, char *base, int size)
{
  video_bucket *bucket;
#ifdef GP2X_DEBUG
  fprintf(stderr, "SDL: GP2X_InitHWSurfaces %p, %d\n", base, size);
#endif

  this->hidden->memory_left = size;
  this->hidden->memory_max = size;

  if (this->hidden->memory_left > 0) {
    bucket = (video_bucket *)malloc(sizeof(*bucket));
    if (bucket == NULL) {
      SDL_OutOfMemory();
      return -1;
    }
    bucket->next = NULL;
    bucket->prev = &this->hidden->video_mem;
    bucket->used = 0;
    bucket->dirty = 0;
    bucket->base = base;
    bucket->size = size;
  } else
    bucket = NULL;
#ifdef GP2X_DEBUG
  fprintf(stderr, "Screen bucket %p\n", &this->hidden->video_mem);
  fprintf(stderr, "First free bucket %p (size = %d)\n", bucket, size);
#endif  
  this->hidden->video_mem.next = bucket;
  this->hidden->video_mem.prev = NULL;
  this->hidden->video_mem.used = 1;
  this->hidden->video_mem.dirty = 0;
  this->hidden->video_mem.base = screen->pixels;
  this->hidden->video_mem.size = (unsigned int)((long)base - (long)screen->pixels);
  screen->hwdata = (struct private_hwdata *)&this->hidden->video_mem;
  return 0;
}

////
// Free all surfaces
static void GP2X_FreeHWSurfaces(_THIS)
{
  video_bucket *curr, *next;
#ifdef GP2X_DEBUG
  fprintf(stderr, "SDL: GP2X_FreeHWSurfaces\n");
#endif

  next = this->hidden->video_mem.next;
  while (next) {
#ifdef GP2X_DEBUG
    fprintf(stderr, "Freeing bucket %p (size %d)\n", next, next->size);
#endif
    curr = next;
    next = curr->next;
    free(curr);
  }
  this->hidden->video_mem.next = NULL;
}

////
// Allocate a surface from video memory
static int GP2X_AllocHWSurface(_THIS, SDL_Surface *surface)
{
  SDL_PrivateVideoData *data = this->hidden;
  int w, h, pitch, size, left_over;
  video_bucket *bucket;
#ifdef GP2X_DEBUG
  fprintf(stderr, "SDL: GP2X_AllocHWSurface %p\n", surface);
#endif
  h = surface->h;
  w = surface->w;
  pitch = ((w * surface->format->BytesPerPixel) + 3) & ~3; // 32-bit align
  size = h * pitch;
  if (size > data->memory_left) {
    SDL_SetError("Not enough video memory");
    return -1;
  }

  for (bucket = &data->video_mem; bucket; bucket = bucket->next)
    if (!bucket->used && (size <= bucket->size))
      break;
  if (!bucket) {
    SDL_SetError("Video memory too fragmented");
    return -1;
  }
  left_over = bucket->size - size;
  if (left_over) {
    video_bucket *new_bucket;
    new_bucket = (video_bucket *)malloc(sizeof(*new_bucket));
    if (!new_bucket) {
      SDL_OutOfMemory();
      return -1;
    }
#ifdef GP2X_DEBUG
    fprintf(stderr, "Adding a new free bucket of %d bytes @ %p\n",
	    left_over, new_bucket);
#endif
    new_bucket->prev = bucket;
    new_bucket->used = 0;
    new_bucket->dirty = 0;
    new_bucket->size = left_over;
    new_bucket->base = bucket->base + size;
    new_bucket->next = bucket->next;
    if (bucket->next)
      bucket->next->prev = new_bucket;
    bucket->next = new_bucket;
  }
  bucket->used = 1;
  bucket->size = size;
  bucket->dirty = 0;
#ifdef GP2X_DEBUG
  fprintf(stderr, "Allocated %d bytes at %p\n", bucket->size, bucket->base);
#endif
  data->memory_left -= size;
  surface->flags |= SDL_HWSURFACE;
  surface->pixels = bucket->base;
  surface->hwdata = (struct private_hwdata *)bucket;
  return 0;
}

////
// Free a surface back to video memry
static void GP2X_FreeHWSurface(_THIS, SDL_Surface *surface)
{
  SDL_PrivateVideoData *data = this->hidden;
  video_bucket *bucket, *wanted;
#ifdef GP2X_DEBUG
  fprintf(stderr, "SDL: GP2X_FreeHWSurface %p\n", surface);
#endif

  /*  
  for (bucket = &data->video_mem; bucket; bucket = bucket->next)
    if (bucket == (video_bucket *)surface->hwdata)
      break;
  */
  bucket = (video_bucket *)surface->hwdata;
  if (bucket && bucket->used) {
#ifdef GP2X_DEBUG
    fprintf(stderr, "Freeing %d bytes at %p of bucket %p\n", bucket->size, bucket->base, bucket);
#endif
    data->memory_left += bucket->size;
    bucket->used = 0;
    if (bucket->next && !bucket->next->used) {
#ifdef GP2X_DEBUG
      fprintf(stderr, "Merging with next bucket (%p) making %d bytes\n",
	      bucket->next, bucket->size + bucket->next->size);
#endif
      wanted = bucket->next;
      bucket->size += bucket->next->size;
      bucket->next = bucket->next->next;
      if (bucket->next)
	bucket->next->prev = bucket;
      free(wanted);
    }
    if (bucket->prev && !bucket->prev->used) {
#ifdef GP2X_DEBUG
      fprintf(stderr, "Merging with previous bucket (%p) making %d bytes\n",
	      bucket->prev, bucket->size + bucket->prev->size);
#endif
      wanted = bucket->prev;
      bucket->size += bucket->prev->size;
      bucket->prev = bucket->prev->prev;
      if (bucket->prev)
	bucket->prev->next = bucket;
      free(wanted);
    }
  }
  surface->pixels = NULL;
  surface->hwdata = NULL;
}

////
// Mark surface as unavailable for HW acceleration
static int GP2X_LockHWSurface(_THIS, SDL_Surface *surface)
{
  if (surface == this->screen)
    SDL_mutexP(this->hidden->hw_lock);

  if (GP2X_IsSurfaceBusy(surface)) {
    GP2X_DummyBlit(this);
    GP2X_WaitBusySurfaces(this);
  }
  return 0;
}

////
// Hardware can use the surface now
static void GP2X_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
  if (surface == this->screen)
    SDL_mutexV(this->hidden->hw_lock);
}

////
// Dummy blit to flush blitter cache
static void GP2X_DummyBlit(_THIS)
{
  SDL_PrivateVideoData *data = this->hidden;

  do {} while (data->fio[MESGSTATUS] & MESG_BUSY);
  data->fio[MESGDSTCTRL] = MESG_DSTENB | MESG_DSTBPP_16;
  data->fio[MESGDSTADDR] = 0x3101000;
  data->fio[MESGDSTSTRIDE] = 4;
  data->fio[MESGSRCCTRL] = MESG_SRCENB | MESG_SRCBPP_16 | MESG_INVIDEO;
  data->fio[MESGPATCTRL] = MESG_PATENB | MESG_PATBPP_1;
  data->fio[MESGFORCOLOR] = ~0;
  data->fio[MESGBACKCOLOR] = ~0;
  data->fio[MESGSIZE] = (1 << MESG_HEIGHT) | 32;
  data->fio[MESGCTRL] = MESG_FFCLR | MESG_XDIR_POS | MESG_YDIR_POS | 0xAA;
  asm volatile ("":::"memory");
  data->fio[MESGSTATUS] = MESG_BUSY;
  do {} while (data->fio[MESGSTATUS] & MESG_BUSY);
}

////
// Flip between double-buffer pages
//  - added: moved setting scaler in here too
//   Can't see why this is supposed to return an int
static int GP2X_FlipHWSurface(_THIS, SDL_Surface *surface)
{
  SDL_PrivateVideoData *data = this->hidden;
  char *pixeldata;

  // make sure the blitter has finished
  if (GP2X_IsSurfaceBusy(this->screen)) {
    GP2X_WaitBusySurfaces(this);
    GP2X_DummyBlit(this);
  }

  // wait for vblank to start
  do {} while ((data->io[GPIOB_PINLVL] & GPIOB_VSYNC));

  // Wait to be on even field (non-interlaced always returns 0)
  //  do {} while (data->io[SC_STATUS] & SC_DISP_FIELD);

  // set write address to be the page currently showing
  surface->pixels = data->buffer_addr[data->buffer_showing];
  // Flip buffers if need be
  if (surface->flags & SDL_DOUBLEBUF)
    data->buffer_showing = 1 - data->buffer_showing;

  pixeldata = data->buffer_addr[data->buffer_showing] + data->ptr_offset;
  if (data->phys_ilace) {
    data->io[MLC_STL_OADRL] = GP2X_PhysL(this, pixeldata);
    data->io[MLC_STL_OADRH] = GP2X_PhysH(this, pixeldata);
    if (data->w == 720) pixeldata += data->pitch;
  }
  data->io[MLC_STL_EADRL] = GP2X_PhysL(this, pixeldata);
  data->io[MLC_STL_EADRH] = GP2X_PhysH(this, pixeldata);

  data->io[MLC_STL_HSC] = data->scale_x;
  data->io[MLC_STL_VSCL] = data->scale_y & 0xffff;
  //  data->io[MLC_STL_VSCH] = data->scale_y >> 16;

  // Wait for vblank to end (to prevent 2 close page flips in one frame)
  //  while (!(data->io[GPIOB_PINLVL] & GPIOB_VSYNC));
  return 0;
}

////
//
static void GP2X_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
  // We're writing directly to video memory
}

////
// Set HW palette (8-bit only)
static int GP2X_SetColors(_THIS, int firstcolour, int ncolours,
			  SDL_Color *colours)
{
  unsigned short volatile *memregs = this->hidden->io;
  int i;
#ifdef GP2X_DEBUG
  //  fprintf(stderr, "SDL: Setting %d colours, starting with %d\n",
  //  	  ncolours, firstcolour);
#endif
  memregs[MLC_STL_PALLT_A] = firstcolour;
  asm volatile ("":::"memory");
  for (i = 0; i < ncolours; i++) {
    memregs[MLC_STL_PALLT_D] = ((int)colours[i].g << 8) + colours[i].b;
    asm volatile ("":::"memory");
    memregs[MLC_STL_PALLT_D] = colours[i].r;
    asm volatile ("":::"memory");
  }
  return 0;
}

////
// Note:  If we are terminated, this could be called in the middle of
//        another SDL video routine -- notably UpdateRects.
static void GP2X_VideoQuit(_THIS)
{
  SDL_PrivateVideoData *data = this->hidden;
  int i;
#ifdef GP2X_DEBUG
  fputs("SDL: VideoQuit\n", stderr);
#endif

  if (data->hw_lock) {
    SDL_DestroyMutex(data->hw_lock);
    data->hw_lock = NULL;
  }

  for (i = 0; i < SDL_NUMMODES; i++)
    if (data->SDL_modelist[i]) {
      free(data->SDL_modelist[i]);
      data->SDL_modelist[i] = NULL;
    }

  GP2X_FreeHWSurfaces(this);
}


////
// Check if blit between surfaces can be accelerated
static int GP2X_CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst)
{
  // dst has to be HW to accelerate blits
  // can't accelerate alpha blits
  if ((dst->flags & SDL_HWSURFACE) && !(src->flags & SDL_SRCALPHA)) {
    src->flags |= SDL_HWACCEL;
    src->map->hw_blit = GP2X_HWAccelBlit;
    return -1;
  } else {
    src->flags &= ~SDL_HWACCEL;
    return 0;
  }
}

////
// Hardware accelerated fill
static int GP2X_FillHWRect(_THIS, SDL_Surface *surface,
			   SDL_Rect *area, Uint32 colour)
{
  Uint32 dstctrl, dest;
  SDL_PrivateVideoData *data = this->hidden;
#ifdef GP2X_DEBUG
  //  fprintf(stderr, "SDL: GP2X_FillHWRect %p (%d,%d)x(%d,%d) in %d\n",
  //  	  surface, area->x, area->y, area->w, area->h, colour);
#endif

  if (surface == this->screen)
    SDL_mutexP(data->hw_lock);

  switch (surface->format->BitsPerPixel) {
  case 8:
    dstctrl = MESG_DSTBPP_8 | ((area->x & 0x03) << 3);
    dest = GP2X_Phys(this, surface->pixels) +
      (area->y * surface->pitch) + (area->x);
    break;
  case 16:
    dstctrl = MESG_DSTBPP_16 | ((area->x & 0x01) << 4);
    dest = GP2X_Phys(this, surface->pixels) +
      (area->y * surface->pitch) + (area->x << 1);
    break;
  default:
    SDL_SetError("SDL: GP2X can't hardware FillRect to surface");
    return -1;
    break;
  }
  do {} while (data->fio[MESGSTATUS] & MESG_BUSY);
  data->fio[MESGDSTCTRL] = dstctrl;
  data->fio[MESGDSTADDR] = dest & ~3;
  data->fio[MESGDSTSTRIDE] = surface->pitch;
  data->fio[MESGSRCCTRL] = 0;
  data->fio[MESGPATCTRL] = MESG_PATENB | MESG_PATBPP_1;
  data->fio[MESGFORCOLOR] = colour;
  data->fio[MESGBACKCOLOR] = colour;
  data->fio[MESGSIZE] = (area->h << MESG_HEIGHT) | area->w;
  data->fio[MESGCTRL] = MESG_FFCLR | MESG_XDIR_POS | MESG_YDIR_POS | MESG_ROP_PAT;
  asm volatile ("":::"memory");
  data->fio[MESGSTATUS] = MESG_BUSY;

  GP2X_AddBusySurface(surface);

  if (surface == this->screen)
    SDL_mutexV(data->hw_lock);
  return 0;
}

////
// Accelerated blit, 1->8, 1->16, 8->8, 16->16
static int GP2X_HWAccelBlit(SDL_Surface *src, SDL_Rect *src_rect,
			    SDL_Surface *dst, SDL_Rect *dst_rect)
{
  SDL_VideoDevice *this = current_video;
  SDL_PrivateVideoData *data = this->hidden;
  int w, h, src_x, src_y, src_stride, dst_stride, dst_x, dst_y;
  Uint32 ctrl, src_start, dst_start, src_ctrl, dst_ctrl;
  Uint32 *read_addr;
#ifdef GP2X_DEBUG
  //  fprintf(stderr, "SDL: GP2X_HWBlit src:%p (%d,%d)x(%d,%d) -> %p (%d,%d)\n",
  //	  src, src_rect->x, src_rect->y, src_rect->w, src_rect->h,
  //	  dst, dst_rect->x, dst_rect->y);
#endif

  if (dst == this->screen)
    SDL_mutexP(data->hw_lock);

  src_x = src_rect->x;
  src_y = src_rect->y;
  dst_x = dst_rect->x;
  dst_y = dst_rect->y;
  w = src_rect->w;
  h = src_rect->h;
  src_stride = src->pitch;
  dst_stride = dst->pitch;

  // set blitter control with ROP and colourkey
  ctrl = MESG_ROP_COPY;
  if (src->flags & SDL_SRCCOLORKEY)
    ctrl |= MESG_TRANSPEN | (src->format->colorkey << MESG_TRANSPCOLOR);

  // In the case where src == dst, reverse blit direction if need be
  //  to cope with potential overlap.
  if (src != dst)
    ctrl |= MESG_XDIR_POS | MESG_YDIR_POS;
  else {
    // if src rightof dst, blit left->right else right->left
    if (src_x >= dst_x)
      ctrl |= MESG_XDIR_POS;
    else {
      src_x += w - 1;
      dst_x += w - 1;
    }
    // likewise, if src below dst blit top->bottom else bottom->top
    if (src_y >= dst_y)
      ctrl |= MESG_YDIR_POS;
    else {
      src_y += h - 1;
      dst_y += h - 1;
      src_stride = -src_stride;
      dst_stride = -dst_stride;
    }
  }

  if (dst->format->BitsPerPixel == 8) {
    dst_start = GP2X_Phys(this, dst->pixels) + (dst_y * dst->pitch) + dst_x;
    dst_ctrl = MESG_DSTBPP_8 | (dst_start & 0x03) << 3;
  } else {
    dst_start = GP2X_Phys(this, dst->pixels) +(dst_y*dst->pitch) +(dst_x<<1);
    dst_ctrl = MESG_DSTBPP_16 | (dst_start & 0x02) << 3;
  }
  do {} while (data->fio[MESGSTATUS] & MESG_BUSY);
  data->fio[MESGDSTCTRL] = dst_ctrl;
  data->fio[MESGDSTADDR] = dst_start & ~3;
  data->fio[MESGDSTSTRIDE] = dst_stride;
  data->fio[MESGFORCOLOR] = data->src_foreground;
  data->fio[MESGBACKCOLOR] = data->src_background;
  data->fio[MESGPATCTRL] = 0;
  data->fio[MESGSIZE] = (h << MESG_HEIGHT) | w;
  data->fio[MESGCTRL] = MESG_FFCLR | ctrl;

  ////// STILL TO CHECK SW->HW BLIT & 1bpp BLIT
  if (src->flags & SDL_HWSURFACE) {
    // src HW surface needs mapping from virtual -> physical
    switch (src->format->BitsPerPixel) {
    case 1:
      src_start = GP2X_Phys(this, src->pixels) +(src_y*src->pitch) +(src_x>>3);
      src_ctrl = MESG_SRCBPP_1 | (src_start & 0x1f);
      break;
    case 8:
      src_start = GP2X_Phys(this, src->pixels) + (src_y * src->pitch) + src_x;
      src_ctrl = MESG_SRCBPP_8 | (src_start & 0x03) << 3;
      break;
    case 16:
      src_start = GP2X_Phys(this, src->pixels) +(src_y*src->pitch) +(src_x<<1);
      src_ctrl = MESG_SRCBPP_16 | (src_start & 0x02) << 3;
      break;
    default:
      SDL_SetError("Invalid bit depth for GP2X_HWBlit");
      return -1;
    }
    data->fio[MESGSRCCTRL] = src_ctrl | MESG_SRCENB | MESG_INVIDEO;
    data->fio[MESGSRCADDR] = src_start & ~3;
    data->fio[MESGSRCSTRIDE] = src_stride;
    asm volatile ("":::"memory");
    data->fio[MESGSTATUS] = MESG_BUSY;
  } else {
    // src SW surface needs CPU to pump blitter
    int src_int_width;
    switch (src->format->BitsPerPixel) {
    case 1:
      src_start = (Uint32)src->pixels + (src_y * src->pitch) + (src_x >> 3);
      src_ctrl = MESG_SRCBPP_1 | (src_start & 0x1f);
      src_int_width = (w + 31) / 32;
      break;
    case 8:
      src_start = (Uint32)src->pixels + (src_y * src->pitch) + src_x;
      src_ctrl = MESG_SRCBPP_8 | (src_start & 0x03) << 3;
      src_int_width = (w + 3) / 4;
      break;
    case 16:
      src_start = (Uint32)src->pixels + (src_y * src->pitch) + (src_x << 1);
      src_ctrl = MESG_SRCBPP_16 | (src_start & 0x02) << 3;
      src_int_width = (w + 1) / 2;
      break;
    default:
      SDL_SetError("Invalid bit depth for GP2X_HWBlit");
      return -1;
    }
    data->fio[MESGSRCCTRL] = src_ctrl | MESG_SRCENB;
    asm volatile ("":::"memory");
    data->fio[MESGSTATUS] = MESG_BUSY;

    while (--h) {
      int i = src_int_width;
      read_addr = (Uint32 *)(src_start & ~3);
      src_start += src_stride;
      while (--i)
	data->fio[MESGFIFO] = *read_addr++;
    }
  }

  GP2X_AddBusySurface(src);
  GP2X_AddBusySurface(dst);

  if (dst == this->screen)
    SDL_mutexV(data->hw_lock);

  return 0;
}

////////
// GP2X specific functions -

////
// Set foreground & background colours for 1bpp blits
void SDL_GP2X_SetMonoColours(int background, int foreground)
{
  if (current_video) {
    current_video->hidden->src_foreground = foreground;
    current_video->hidden->src_background = background;
  }
}

////
// Enquire physical screen size - for detecting LCD / TV
//  Returns 0: Progressive
//          1: Interlaced
int SDL_GP2X_GetPhysicalScreenSize(SDL_Rect *size)
{
  if (current_video) {
    SDL_PrivateVideoData *data = current_video->hidden;
    size->w = data->phys_width;
    size->h = data->phys_height;
    return data->phys_ilace;
  }
  return -1;
}

////
// Dynamic screen scaling
void SDL_GP2X_Display(SDL_Rect *area)
{
  SDL_PrivateVideoData *data = current_video->hidden;
  int sc_x, sc_y;

  // If top-left is out of bounds then correct it
  if (area->x < 0)
    area->x = 0;
  if (area->x > (data->w - 8))
    area->x = data->w - 8;
  if (area->y < 0)
    area->y = 0;
  if (area->y > (data->h - 8))
    area->y = data->h - 8;
  // if requested area is wider than screen, reduce width
  if (data->w < (area->x + area->w))
    area->w = data->w - area->x;
  // if requested area is taller than screen, reduce height
  if (data->h < (area->y + area->h))
    area->h = data->h - area->y;

  sc_x = (1024 * area->w) / data->phys_width;
  sc_y = (area->h * data->pitch) / data->phys_height;
  // Evil hacky thing. Scaler only works if horiz needs scaling.
  // If requested scale only needs to scale in vertical, fudge horiz
  if ((sc_x == 1024) && (area->h != data->phys_height))
    sc_x++;

  data->scale_x = sc_x;
  data->scale_y = sc_y;
  data->x_offset = area->x;
  data->y_offset = area->y;
  data->ptr_offset = ((area->y * data->pitch) +
		      (area->x * current_video->info.vfmt->BytesPerPixel)) & ~3;

  // Apply immediately if we're not double-buffered
  if (!(current_video->screen->flags & SDL_DOUBLEBUF)) {
    char *pixeldata = data->buffer_addr[data->buffer_showing]+data->ptr_offset;
    if (data->phys_ilace) {
      data->io[MLC_STL_OADRL] = GP2X_PhysL(current_video, pixeldata);
      data->io[MLC_STL_OADRH] = GP2X_PhysH(current_video, pixeldata);
      if (data->w == 720) pixeldata += data->pitch;
    }
    data->io[MLC_STL_EADRL] = GP2X_PhysL(current_video, pixeldata);
    data->io[MLC_STL_EADRH] = GP2X_PhysH(current_video, pixeldata);
    data->io[MLC_STL_HSC] = data->scale_x;
    data->io[MLC_STL_VSCL] = data->scale_y & 0xffff;
    //    data->io[MLC_STL_VSCH] = data->scale_y >> 16;
  }
}

////
// window routines - 
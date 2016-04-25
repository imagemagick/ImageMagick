/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                               ____ _(_)____                                 %
%                              (___ | | | ___)                                %
%                               (__ | |_| __)                                 %
%                                 (_|___|_)                                   %
%                                                                             %
%                                                                             %
%                    Read/Write Free Lossless Image Format                    %
%                                                                             %
%                              Software Design                                %
%                                Jon Sneyers                                  %
%                                April 2016                                   %
%                                                                             %
%                                                                             %
%  Copyright 1999-2012 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    http://www.imagemagick.org/script/license.php                            %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
*/

/*
  Include declarations.
*/
#include "MagickCore/studio.h"
#include "MagickCore/artifact.h"
#include "MagickCore/blob.h"
#include "MagickCore/blob-private.h"
#include "MagickCore/client.h"
#include "MagickCore/colorspace-private.h"
#include "MagickCore/display.h"
#include "MagickCore/exception.h"
#include "MagickCore/exception-private.h"
#include "MagickCore/image.h"
#include "MagickCore/image-private.h"
#include "MagickCore/list.h"
#include "MagickCore/magick.h"
#include "MagickCore/monitor.h"
#include "MagickCore/monitor-private.h"
#include "MagickCore/memory_.h"
#include "MagickCore/option.h"
#include "MagickCore/pixel-accessor.h"
#include "MagickCore/quantum-private.h"
#include "MagickCore/static.h"
#include "MagickCore/string_.h"
#include "MagickCore/string-private.h"
#include "MagickCore/module.h"
#include "MagickCore/utility.h"
#include "MagickCore/xwindow.h"
#include "MagickCore/xwindow-private.h"
#if defined(MAGICKCORE_FLIF_DELEGATE)
#include <flif.h>
#endif

/*
  Forward declarations.
*/
#if defined(MAGICKCORE_FLIF_DELEGATE)
static MagickBooleanType
  WriteFLIFImage(const ImageInfo *,Image *,ExceptionInfo *);
#endif

#if defined(MAGICKCORE_FLIF_DELEGATE)
/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e a d F L I F I m a g e                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ReadFLIFImage() reads an image in the FLIF image format.
%
%  The format of the ReadFLIFImage method is:
%
%      Image *ReadWEBPImage(const ImageInfo *image_info,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image_info: the image info.
%
%    o exception: return any errors or warnings in this structure.
%
*/
static Image *ReadFLIFImage(const ImageInfo *image_info,
  ExceptionInfo *exception)
{
  Image
    *image;

  MagickBooleanType
    status;

  register unsigned short
    *p;

  register Quantum
    *q;

  size_t
    length,
    image_count,
    count;

  register ssize_t
    x;

  ssize_t
    y;

  unsigned char
    *stream;

  unsigned short
    *pixels;

  FLIF_DECODER* flifdec;
  FLIF_IMAGE* flifimage;

  /*
    Open image file.
  */
  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickCoreSignature);
  if (image_info->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",
      image_info->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickCoreSignature);
  image=AcquireImage(image_info,exception);
  status=OpenBlob(image_info,image,ReadBinaryBlobMode,exception);
  if (status == MagickFalse)
    {
      image=DestroyImageList(image);
      return((Image *) NULL);
    }
  length=(size_t) GetBlobSize(image);
  stream=(unsigned char *) AcquireQuantumMemory(length,sizeof(*stream));
  if (stream == (unsigned char *) NULL)
    ThrowReaderException(ResourceLimitError,"MemoryAllocationFailed");
  count=ReadBlob(image,length,stream);
  if (count != (ssize_t) length)
    ThrowReaderException(CorruptImageError,"InsufficientImageDataInFile");
  flifdec = flif_create_decoder();
  if (image->quality != UndefinedCompressionQuality)
    flif_decoder_set_quality(flifdec, image->quality);
  if (!flif_decoder_decode_memory(flifdec, stream, length))
    ThrowReaderException(CorruptImageError,"CorruptFLIF");
  image_count = flif_decoder_num_images(flifdec);
  length = sizeof(unsigned short) * 4 * flif_image_get_width(flif_decoder_get_image(flifdec,0));
  pixels = malloc(length);
  if (!pixels) ThrowReaderException(ResourceLimitError,"MemoryAllocationFailed");

  for (count=0; count < image_count; count++)
  {
    if (count > 0) {
        /*
          Allocate next image structure.
        */
        AcquireNextImage(image_info,image,exception);
        if (GetNextImageInList(image) == (Image *) NULL)
          {
            image=DestroyImageList(image);
            flif_abort_decoder(flifdec);
            free(pixels);
            return((Image *) NULL);
          }
        image=SyncNextImageInList(image);
    }
    flifimage = flif_decoder_get_image(flifdec, count);
    image->columns=(size_t) flif_image_get_width(flifimage);
    image->rows=(size_t) flif_image_get_height(flifimage);
    image->depth = flif_image_get_depth(flifimage);
    image->alpha_trait = (flif_image_get_nb_channels(flifimage) > 3 ? BlendPixelTrait : UndefinedPixelTrait);
    image->delay = flif_image_get_frame_delay(flifimage);
    image->ticks_per_second = 1000;
    image->scene = count;
    image->dispose = BackgroundDispose;
    for (y=0; y < (ssize_t) image->rows; y++)
    {
      flif_image_read_row_RGBA16(flifimage, y, pixels, length);
      p = pixels;
      q=QueueAuthenticPixels(image,0,y,image->columns,1,exception);
      if (q == (Quantum *) NULL) break;
      for (x=0; x < (ssize_t) image->columns; x++)
      {
        SetPixelRed(image,ScaleShortToQuantum(*p++),q);
        SetPixelGreen(image,ScaleShortToQuantum(*p++),q);
        SetPixelBlue(image,ScaleShortToQuantum(*p++),q);
        SetPixelAlpha(image,ScaleShortToQuantum(*p++),q);
        q+=GetPixelChannels(image);
      }
      if (SyncAuthenticPixels(image,exception) == MagickFalse) break;
      status=SetImageProgress(image,LoadImageTag,(MagickOffsetType) y, image->rows);
      if (status == MagickFalse) break;
    }
  }
  flif_destroy_decoder(flifdec);
  free(pixels);
  return(image);
}
#endif

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   I s F L I F                                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  IsFLIF() returns MagickTrue if the image format type, identified by the
%  magick string, is FLIF.
%
%  The format of the IsFLIF method is:
%
%      MagickBooleanType IsFLIF(const unsigned char *magick,const size_t length)
%
%  A description of each parameter follows:
%
%    o magick: compare image format pattern against these bytes.
%
%    o length: Specifies the length of the magick string.
%
*/
static MagickBooleanType IsFLIF(const unsigned char *magick,const size_t length)
{
  if (length < 4)
    return(MagickFalse);
  if (LocaleNCompare((char *) magick,"FLIF",4) == 0)
    return(MagickTrue);
  return(MagickFalse);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e g i s t e r F L I F I m a g e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RegisterFLIFImage() adds attributes for the FLIF image format to
%  the list of supported formats.  The attributes include the image format
%  tag, a method to read and/or write the format, whether the format
%  supports the saving of more than one frame to the same file or blob,
%  whether the format supports native in-memory I/O, and a brief
%  description of the format.
%
%  The format of the RegisterFLIFImage method is:
%
%      size_t RegisterFLIFImage(void)
%
*/
ModuleExport size_t RegisterFLIFImage(void)
{
  char
    version[MagickPathExtent];

  MagickInfo
    *entry;

  *version='\0';
  entry=AcquireMagickInfo("FLIF","FLIF","Free Lossless Image Format");
#if defined(MAGICKCORE_FLIF_DELEGATE)
  entry->decoder=(DecodeImageHandler *) ReadFLIFImage;
  entry->encoder=(EncodeImageHandler *) WriteFLIFImage;
  (void) FormatLocaleString(version,MagickPathExtent,"libflif %d.%d.%d [%04X]",
    (FLIF_VERSION >> 16) & 0xff,
    (FLIF_VERSION  >> 8) & 0xff,
    (FLIF_VERSION  >> 0) & 0xff,FLIF_ABI_VERSION);
#endif
  entry->mime_type=ConstantString("image/flif");
//  entry->flags^=CoderAdjoinFlag;
  entry->magick=(IsImageFormatHandler *) IsFLIF;
  if (*version != '\0')
    entry->version=ConstantString(version);
  (void) RegisterMagickInfo(entry);
  return(MagickImageCoderSignature);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   U n r e g i s t e r F L I F I m a g e                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  UnregisterFLIFImage() removes format registrations made by the FLIF module
%  from the list of supported formats.
%
%  The format of the UnregisterFLIFImage method is:
%
%      UnregisterFLIFImage(void)
%
*/
ModuleExport void UnregisterFLIFImage(void)
{
  (void) UnregisterMagickInfo("FLIF");
}
#if defined(MAGICKCORE_FLIF_DELEGATE)

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   W r i t e F L I F I m a g e                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WriteFLIFImage() writes an image in the FLIF image format.
%
%  The format of the WriteFLIFImage method is:
%
%      MagickBooleanType WriteFLIFImage(const ImageInfo *image_info,
%        Image *image)
%
%  A description of each parameter follows.
%
%    o image_info: the image info.
%
%    o image:  The image.
%
*/


static MagickBooleanType WriteFLIFImage(const ImageInfo *image_info,
  Image *image, ExceptionInfo *exception)
{

  int
    flif_status;

  MagickBooleanType
    status;

  register const Quantum
    *restrict p;

  register ssize_t
    x;

  register unsigned char
    *restrict qc;
  register unsigned short
    *restrict qs;

  ssize_t
    y, w, h;

  size_t
    length;

  MagickOffsetType
    scene;

  unsigned char
    *pixels,
    *buffer;

  FLIF_ENCODER
    *flifenc;

  FLIF_IMAGE
    *flifimage;


//    Open output image file.

  assert(image_info != (const ImageInfo *) NULL);
  assert(image_info->signature == MagickCoreSignature);
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if ((image->columns > 0xFFFF) || (image->rows > 0xFFFF))
    ThrowWriterException(ImageError,"WidthOrHeightExceedsLimit");
  status=OpenBlob(image_info,image,WriteBinaryBlobMode,exception);
  if (status == MagickFalse)
    return(status);
  flifenc = flif_create_encoder();
  if (image->quality != UndefinedCompressionQuality)
    flif_encoder_set_lossy(flifenc, 3*(100-image->quality));

  // relatively fast encoding
  flif_encoder_set_learn_repeat(flifenc, 1);
  flif_encoder_set_split_threshold(flifenc, 5461*8*5);


//    Allocate memory for one row of pixels.
  w = image->columns;
  h = image->rows;

//    Convert image to FLIFIMAGE
  if (image->depth > 8) {
    flifimage = flif_create_image_HDR(image->columns, image->rows);
    length = sizeof(unsigned short) * 4 * w;
    pixels = malloc(length);
  } else {
    flifimage = flif_create_image(image->columns, image->rows);
    length = sizeof(unsigned char) * 4 * w;
    pixels = malloc(length);
  }
  if (!pixels) ThrowWriterException(ResourceLimitError,"MemoryAllocationFailed");
  if (!flifimage) ThrowWriterException(ResourceLimitError,"MemoryAllocationFailed");
  scene = 0;

  do {
   if (w != image->columns || h != image->rows)
       ThrowWriterException(ImageError,"FramesNotSameDimensions");

   for (y=0; y < (ssize_t) image->rows; y++)
   {
    p=GetVirtualPixels(image,0,y,image->columns,1,exception);
    if (p == (Quantum *) NULL)
      break;

    if (image->depth > 8) {
     qs=(unsigned short *) pixels;
     for (x=0; x < (ssize_t) image->columns; x++)
     {
      *qs++=ScaleQuantumToShort(GetPixelRed(image,p));
      *qs++=ScaleQuantumToShort(GetPixelGreen(image,p));
      *qs++=ScaleQuantumToShort(GetPixelBlue(image,p));
      if (image->alpha_trait != UndefinedPixelTrait)
        *qs++=ScaleQuantumToShort(GetPixelAlpha(image,p));
      else
        *qs++=0xFFFF;
      p+=GetPixelChannels(image);
     }
     flif_image_write_row_RGBA16(flifimage, y, pixels, length);
    } else {
     qc=pixels;
     for (x=0; x < (ssize_t) image->columns; x++)
     {
      *qc++=ScaleQuantumToChar(GetPixelRed(image,p));
      *qc++=ScaleQuantumToChar(GetPixelGreen(image,p));
      *qc++=ScaleQuantumToChar(GetPixelBlue(image,p));
      if (image->alpha_trait != UndefinedPixelTrait)
        *qc++=ScaleQuantumToChar(GetPixelAlpha(image,p));
      else
        *qc++=0xFF;
      p+=GetPixelChannels(image);
     }
     flif_image_write_row_RGBA8(flifimage, y, pixels, length);
    }
   }
   flif_image_set_frame_delay(flifimage, image->delay * 1000 / image->ticks_per_second);
   flif_encoder_add_image(flifenc,flifimage);
   if (GetNextImageInList(image) == (Image *) NULL)
      break;
   image=SyncNextImageInList(image);
   scene++;
   status=SetImageProgress(image,SaveImagesTag,scene,
      GetImageListLength(image));
   if (status == MagickFalse)
      break;
  } while (image_info->adjoin != MagickFalse);

  flif_destroy_image(flifimage);
  free(pixels);
  flif_status = flif_encoder_encode_memory(flifenc,(void**) &buffer,&length);
  if (flif_status)
    WriteBlob(image,length,buffer);
  CloseBlob(image);
  flif_destroy_encoder(flifenc);
  return(flif_status == 0 ? MagickFalse : MagickTrue);
}
#endif

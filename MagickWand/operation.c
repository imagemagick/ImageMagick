/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%          OOO   PPPP   EEEE  RRRR    AA   TTTTT  III   OOO   N   N           %
%         O   O  P   P  E     R   R  A  A    T     I   O   O  NN  N           %
%         O   O  PPPP   EEE   RRRR   AAAA    T     I   O   O  N N N           %
%         O   O  P      E     R R    A  A    T     I   O   O  N  NN           %
%          OOO   P      EEEE  R  RR  A  A    T    III   OOO   N   N           %
%                                                                             %
%                                                                             %
%                         CLI Magick Option Methods                           %
%                                                                             %
%                              Dragon Computing                               %
%                              Anthony Thyssen                                %
%                               September 2011                                %
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
% Apply the given options (settings, and simple, or sequence operations) to
% the given image(s) according to the current "image_info", "draw_info", and
% "quantize_info" settings, stored in a special CLI Image Wand.
%
% The final goal is to allow the execution in a strict one option at a time
% manner that is needed for 'pipelining and file scripting' of options in
% IMv7.
%
% Anthony Thyssen, September 2011
*/

/*
  Include declarations.
*/
#include "MagickWand/studio.h"
#include "MagickWand/MagickWand.h"
#include "MagickWand/magick-wand-private.h"
#include "MagickWand/operation.h"
#include "MagickWand/operation-private.h"
#include "MagickWand/wand.h"
#include "MagickCore/monitor-private.h"
#include "MagickCore/thread-private.h"
#include "MagickCore/string-private.h"

/*
  Define declarations.
*/
#define USE_WAND_METHODS  0
#define MAX_STACK_DEPTH  32
#define UNDEFINED_COMPRESSION_QUALITY  0UL

/*
  Constant declaration. (temporary exports)
*/
static const char
  BackgroundColor[] = "#fff",  /* white */
  BorderColor[] = "#dfdfdf",  /* sRGB gray */
  MatteColor[] = "#bdbdbd";  /* slightly darker gray */

/*
** Function to report on the progress of image operations
*/
static MagickBooleanType MonitorProgress(const char *text,
  const MagickOffsetType offset,const MagickSizeType extent,
  void *wand_unused(cli_wandent_data))
{
  char
    message[MaxTextExtent],
    tag[MaxTextExtent];

  const char
    *locale_message;

  register char
    *p;

  if (extent < 2)
    return(MagickTrue);
  (void) CopyMagickMemory(tag,text,MaxTextExtent);
  p=strrchr(tag,'/');
  if (p != (char *) NULL)
    *p='\0';
  (void) FormatLocaleString(message,MaxTextExtent,"Monitor/%s",tag);
  locale_message=GetLocaleMessage(message);
  if (locale_message == message)
    locale_message=tag;
  if (p == (char *) NULL)
    (void) FormatLocaleFile(stderr,"%s: %ld of %lu, %02ld%% complete\r",
      locale_message,(long) offset,(unsigned long) extent,(long)
      (100L*offset/(extent-1)));
  else
    (void) FormatLocaleFile(stderr,"%s[%s]: %ld of %lu, %02ld%% complete\r",
      locale_message,p+1,(long) offset,(unsigned long) extent,(long)
      (100L*offset/(extent-1)));
  if (offset == (MagickOffsetType) (extent-1))
    (void) FormatLocaleFile(stderr,"\n");
  (void) fflush(stderr);
  return(MagickTrue);
}

/*
** GetImageCache() will read an image into a image cache if not already
** present then return the image that is in the cache under that filename.
*/
static inline Image *GetImageCache(const ImageInfo *image_info,const char *path,
  ExceptionInfo *exception)
{
  char
    key[MaxTextExtent];

  ExceptionInfo
    *sans_exception;

  Image
    *image;

  ImageInfo
    *read_info;

  (void) FormatLocaleString(key,MaxTextExtent,"cache:%s",path);
  sans_exception=AcquireExceptionInfo();
  image=(Image *) GetImageRegistry(ImageRegistryType,key,sans_exception);
  sans_exception=DestroyExceptionInfo(sans_exception);
  if (image != (Image *) NULL)
    return(image);
  read_info=CloneImageInfo(image_info);
  (void) CopyMagickString(read_info->filename,path,MaxTextExtent);
  image=ReadImage(read_info,exception);
  read_info=DestroyImageInfo(read_info);
  if (image != (Image *) NULL)
    (void) SetImageRegistry(ImageRegistryType,key,image,exception);
  return(image);
}

/*
  SparseColorOption() parse the complex -sparse-color argument into an
  an array of floating point values than call SparseColorImage().
  Argument is a complex mix of floating-point pixel coodinates, and color
  specifications (or direct floating point numbers).  The number of floats
  needed to represent a color varies depending on teh current channel
  setting.

  This really should be in MagickCore, so that other API's can make use of it.
*/
static Image *SparseColorOption(const Image *image,
  const SparseColorMethod method,const char *arguments,
  ExceptionInfo *exception)
{
  char
    token[MaxTextExtent];

  const char
    *p;

  double
    *sparse_arguments;

  Image
    *sparse_image;

  PixelInfo
    color;

  MagickBooleanType
    error;

  register size_t
    x;

  size_t
    number_arguments,
    number_colors;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickSignature);
  if (image->debug != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickSignature);
  /*
    Limit channels according to image - and add up number of color channel.
  */
  number_colors=0;
  if ((GetPixelRedTraits(image) & UpdatePixelTrait) != 0)
    number_colors++;
  if ((GetPixelGreenTraits(image) & UpdatePixelTrait) != 0)
    number_colors++;
  if ((GetPixelBlueTraits(image) & UpdatePixelTrait) != 0)
    number_colors++;
  if (((GetPixelBlackTraits(image) & UpdatePixelTrait) != 0) &&
      (image->colorspace == CMYKColorspace))
    number_colors++;
  if (((GetPixelAlphaTraits(image) & UpdatePixelTrait) != 0) &&
      (image->matte != MagickFalse))
    number_colors++;

  /*
    Read string, to determine number of arguments needed,
  */
  p=arguments;
  x=0;
  while( *p != '\0' )
  {
    GetMagickToken(p,&p,token);
    if ( token[0] == ',' ) continue;
    if ( isalpha((int) token[0]) || token[0] == '#' )
      x += number_colors;  /* color argument found */
    else {
      x++;   /* floating point argument */
    }
  }
  error=MagickTrue;
  /* control points and color values */
  error = ( x % (2+number_colors) != 0 ) ? MagickTrue : MagickFalse;
  number_arguments=x;
  if ( error ) {
    (void) ThrowMagickException(exception,GetMagickModule(),
               OptionError, "InvalidArgument", "'%s': %s", "sparse-color",
               "Invalid number of Arguments");
    return( (Image *)NULL);
  }

  /* Allocate and fill in the floating point arguments */
  sparse_arguments=(double *) AcquireQuantumMemory(number_arguments,
    sizeof(*sparse_arguments));
  if (sparse_arguments == (double *) NULL) {
    (void) ThrowMagickException(exception,GetMagickModule(),ResourceLimitError,
      "MemoryAllocationFailed","%s","SparseColorOption");
    return( (Image *)NULL);
  }
  (void) ResetMagickMemory(sparse_arguments,0,number_arguments*
    sizeof(*sparse_arguments));
  p=arguments;
  x=0;
  while( *p != '\0' && x < number_arguments ) {
    /* X coordinate */
    token[0]=','; while ( token[0] == ',' ) GetMagickToken(p,&p,token);
    if ( token[0] == '\0' ) break;
    if ( isalpha((int) token[0]) || token[0] == '#' ) {
      (void) ThrowMagickException(exception,GetMagickModule(),
            OptionError, "InvalidArgument", "'%s': %s", "sparse-color",
            "Color found, instead of X-coord");
      error = MagickTrue;
      break;
    }
    sparse_arguments[x++]=StringToDouble(token,(char **) NULL);
    /* Y coordinate */
    token[0]=','; while ( token[0] == ',' ) GetMagickToken(p,&p,token);
    if ( token[0] == '\0' ) break;
    if ( isalpha((int) token[0]) || token[0] == '#' ) {
      (void) ThrowMagickException(exception,GetMagickModule(),
            OptionError, "InvalidArgument", "'%s': %s", "sparse-color",
            "Color found, instead of Y-coord");
      error = MagickTrue;
      break;
    }
    sparse_arguments[x++]=StringToDouble(token,(char **) NULL);
    /* color name or function given in string argument */
    token[0]=','; while ( token[0] == ',' ) GetMagickToken(p,&p,token);
    if ( token[0] == '\0' ) break;
    if ( isalpha((int) token[0]) || token[0] == '#' ) {
      /* Color string given */
      (void) QueryColorCompliance(token,AllCompliance,&color,
                exception);
      if ((GetPixelRedTraits(image) & UpdatePixelTrait) != 0)
        sparse_arguments[x++] = QuantumScale*color.red;
      if ((GetPixelGreenTraits(image) & UpdatePixelTrait) != 0)
        sparse_arguments[x++] = QuantumScale*color.green;
      if ((GetPixelBlueTraits(image) & UpdatePixelTrait) != 0)
        sparse_arguments[x++] = QuantumScale*color.blue;
      if (((GetPixelBlackTraits(image) & UpdatePixelTrait) != 0) &&
          (image->colorspace == CMYKColorspace))
        sparse_arguments[x++] = QuantumScale*color.black;
      if (((GetPixelAlphaTraits(image) & UpdatePixelTrait) != 0) &&
          (image->matte != MagickFalse))
        sparse_arguments[x++] = QuantumScale*color.alpha;
    }
    else {
      /* Colors given as a set of floating point values - experimental */
      /* NB: token contains the first floating point value to use! */
      if ((GetPixelRedTraits(image) & UpdatePixelTrait) != 0)
        {
        while ( token[0] == ',' ) GetMagickToken(p,&p,token);
        if ( token[0] == '\0' || isalpha((int)token[0]) || token[0] == '#' )
          break;
        sparse_arguments[x++]=StringToDouble(token,(char **) NULL);
        token[0] = ','; /* used this token - get another */
      }
      if ((GetPixelGreenTraits(image) & UpdatePixelTrait) != 0)
        {
        while ( token[0] == ',' ) GetMagickToken(p,&p,token);
        if ( token[0] == '\0' || isalpha((int)token[0]) || token[0] == '#' )
          break;
        sparse_arguments[x++]=StringToDouble(token,(char **) NULL);
        token[0] = ','; /* used this token - get another */
      }
      if ((GetPixelBlueTraits(image) & UpdatePixelTrait) != 0)
        {
        while ( token[0] == ',' ) GetMagickToken(p,&p,token);
        if ( token[0] == '\0' || isalpha((int)token[0]) || token[0] == '#' )
          break;
        sparse_arguments[x++]=StringToDouble(token,(char **) NULL);
        token[0] = ','; /* used this token - get another */
      }
      if (((GetPixelBlackTraits(image) & UpdatePixelTrait) != 0) &&
          (image->colorspace == CMYKColorspace))
        {
        while ( token[0] == ',' ) GetMagickToken(p,&p,token);
        if ( token[0] == '\0' || isalpha((int)token[0]) || token[0] == '#' )
          break;
        sparse_arguments[x++]=StringToDouble(token,(char **) NULL);
        token[0] = ','; /* used this token - get another */
      }
      if (((GetPixelAlphaTraits(image) & UpdatePixelTrait) != 0) &&
          (image->matte != MagickFalse))
        {
        while ( token[0] == ',' ) GetMagickToken(p,&p,token);
        if ( token[0] == '\0' || isalpha((int)token[0]) || token[0] == '#' )
          break;
        sparse_arguments[x++]=StringToDouble(token,(char **) NULL);
        token[0] = ','; /* used this token - get another */
      }
    }
  }
  if ( number_arguments != x && !error ) {
    (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
      "InvalidArgument","'%s': %s","sparse-color","Argument Parsing Error");
    sparse_arguments=(double *) RelinquishMagickMemory(sparse_arguments);
    return( (Image *)NULL);
  }
  if ( error )
    return( (Image *)NULL);

  /* Call the Sparse Color Interpolation function with the parsed arguments */
  sparse_image=SparseColorImage(image,method,number_arguments,sparse_arguments,
    exception);
  sparse_arguments=(double *) RelinquishMagickMemory(sparse_arguments);
  return( sparse_image );
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   A c q u i r e W a n d C L I                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquireMagickCLI() creates a new CLI wand (an expanded form of Magick
%  Wand). The given image_info and exception is included as is if provided.
%
%  Use DestroyMagickCLI() to dispose of the CLI wand when it is no longer
%  needed.
%
%  The format of the NewMagickWand method is:
%
%      MagickCLI *AcquireMagickCLI(ImageInfo *image_info,
%           ExceptionInfo *exception)
%
*/
WandExport MagickCLI *AcquireMagickCLI(ImageInfo *image_info,
    ExceptionInfo *exception)
{
  MagickCLI
    *cli_wand;

  /* precaution - as per NewMagickWand() */
  {
     size_t depth = MAGICKCORE_QUANTUM_DEPTH;
     const char *quantum = GetMagickQuantumDepth(&depth);
     if (depth != MAGICKCORE_QUANTUM_DEPTH)
       ThrowWandFatalException(WandError,"QuantumDepthMismatch",quantum);
  }

  /* allocate memory for MgaickCLI */
  cli_wand=(MagickCLI *) AcquireMagickMemory(sizeof(*cli_wand));
  if (cli_wand == (MagickCLI *) NULL)
    {
      ThrowWandFatalException(ResourceLimitFatalError,"MemoryAllocationFailed",
        GetExceptionMessage(errno));
      return((MagickCLI *)NULL);
    }

  /* Initialize Wand Part of MagickCLI
     FUTURE: this is a repeat of code from NewMagickWand()
     However some parts may be given fro man external source!
  */
  cli_wand->wand.id=AcquireWandId();
  (void) FormatLocaleString(cli_wand->wand.name,MaxTextExtent,
           "%s-%.20g","MagickWandCLI", (double) cli_wand->wand.id);
  cli_wand->wand.images=NewImageList();
  if ( image_info == (ImageInfo *)NULL)
    cli_wand->wand.image_info=AcquireImageInfo();
  else
    cli_wand->wand.image_info=image_info;
  if ( exception == (ExceptionInfo *)NULL)
    cli_wand->wand.exception=AcquireExceptionInfo();
  else
    cli_wand->wand.exception=exception;
  cli_wand->wand.debug=IsEventLogging();
  cli_wand->wand.signature=WandSignature;

  /* Initialize CLI Part of MagickCLI */
  cli_wand->draw_info=CloneDrawInfo(cli_wand->wand.image_info,(DrawInfo *) NULL);
  cli_wand->quantize_info=AcquireQuantizeInfo(cli_wand->wand.image_info);
  cli_wand->image_list_stack=(Stack *)NULL;
  cli_wand->image_info_stack=(Stack *)NULL;
  cli_wand->location="'%s' at unknown location";
  cli_wand->filename=cli_wand->wand.name;
  cli_wand->line=0;
  cli_wand->column=0;
  cli_wand->signature=WandSignature;

  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);
  return(cli_wand);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   D e s t r o y W a n d C L I                                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  DestroyMagickCLI() destorys everything in a CLI wand, including image_info
%  and any exceptions, if still present in the wand.
%
%  The format of the NewMagickWand method is:
%
%    MagickWand *DestroyMagickCLI()
%            Exception *exception)
%
*/
WandExport MagickCLI *DestroyMagickCLI(MagickCLI *cli_wand)
{
  Stack
    *node;

  assert(cli_wand != (MagickCLI *) NULL);
  assert(cli_wand->signature == WandSignature);
  assert(cli_wand->wand.signature == WandSignature);
  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);

  /* Destroy CLI part of MagickCLI */
  if (cli_wand->draw_info != (DrawInfo *) NULL )
    cli_wand->draw_info=DestroyDrawInfo(cli_wand->draw_info);
  if (cli_wand->quantize_info != (QuantizeInfo *) NULL )
    cli_wand->quantize_info=DestroyQuantizeInfo(cli_wand->quantize_info);
  while(cli_wand->image_list_stack != (Stack *)NULL)
    {
      node=cli_wand->image_list_stack;
      cli_wand->image_list_stack=node->next;
      (void) DestroyImageList((Image *)node->data);
      (void) RelinquishMagickMemory(node);
    }
  while(cli_wand->image_info_stack != (Stack *)NULL)
    {
      node=cli_wand->image_info_stack;
      cli_wand->image_info_stack=node->next;
      (void) DestroyImageInfo((ImageInfo *)node->data);
      (void) RelinquishMagickMemory(node);
    }
  cli_wand->signature=(~WandSignature);

  /* Destroy Wand part MagickCLI */
  cli_wand->wand.images=DestroyImageList(cli_wand->wand.images);
  if (cli_wand->wand.image_info != (ImageInfo *) NULL )
    cli_wand->wand.image_info=DestroyImageInfo(cli_wand->wand.image_info);
  if (cli_wand->wand.exception != (ExceptionInfo *) NULL )
    cli_wand->wand.exception=DestroyExceptionInfo(cli_wand->wand.exception);
  RelinquishWandId(cli_wand->wand.id);
  cli_wand->wand.signature=(~WandSignature);

  return((MagickCLI *)NULL);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C L I C a t c h E x c e p t i o n                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CLICatchException() will report exceptions, either just non-fatal warnings
%  only, or all errors, according to 'all_execeptions' boolean argument.
%
%  The function returns true is errors are fatal, in which case the caller
%  should abort and re-call with an 'all_exceptions' argument of true before
%  quitting.
%
%  The cut-off level between fatal and non-fatal may be controlled by options
%  (FUTURE), but defaults to 'Error' exceptions.
%
%  The format of the CLICatchException method is:
%
%    MagickBooleanType CLICatchException(MagickCLI *cli_wand,
%              const MagickBooleanType all_exceptions );
%
*/
WandExport MagickBooleanType CLICatchException(MagickCLI *cli_wand,
     const MagickBooleanType all_exceptions )
{
  MagickBooleanType
    status;
  assert(cli_wand != (MagickCLI *) NULL);
  assert(cli_wand->signature == WandSignature);
  assert(cli_wand->wand.signature == WandSignature);
  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);

  // FUTURE: '-regard_warning' should make this more sensitive.
  // Note pipelined options may like more control over this level

  status = MagickFalse;
  if (cli_wand->wand.exception->severity > ErrorException)
    status = MagickTrue;

  if ( status == MagickFalse || all_exceptions != MagickFalse )
    CatchException(cli_wand->wand.exception); /* output and clear exceptions */

  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C L I S e t t i n g O p t i o n I n f o                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CLISettingOptionInfo() applies a single settings option into a CLI wand
%  holding the image_info, draw_info, quantize_info structures that will be
%  used when processing the images.
%
%  These options do no require images to be present in the CLI wand for them
%  to be able to be set, in which case they will generally be applied to image
%  that are read in later
%
%  Options handled by this function are listed in CommandOptions[] of
%  "option.c" that is one of "SettingOptionFlags" option flags.
%
%  The format of the CLISettingOptionInfo method is:
%
%    void CLISettingOptionInfo(MagickCLI *cli_wand,
%               const char *option, const char *arg)
%
%  A description of each parameter follows:
%
%    o cli_wand: structure holding settings to be applied
%
%    o option: The option string to be set
%
%    o arg: The single argument used to set this option.
%
% Example usage...
%
%    CLISettingOptionInfo(cli_wand, "-background", "Red");  // set value
%    CLISettingOptionInfo(cli_wand, "-adjoin", NULL);       // set boolean
%    CLISettingOptionInfo(cli_wand, "+adjoin", NULL);       // unset
%
% Or for handling command line arguments EG: +/-option ["arg"]
%
%    argc,argv
%    i=index in argv
%
%    option_info = GetCommandOptionInfo(argv[i]);
%    count=option_info->type;
%    option_type=option_info->flags;
%
%    if ( (option_type & SettingOperatorOptionFlags) != 0 )
%      CLISettingOptionInfo(cli_wand, argv[i],
%                   (count>0) ? argv[i+1] : (char *)NULL);
%    i += count+1;
%
*/
WandExport void CLISettingOptionInfo(MagickCLI *cli_wand,
     const char *option,const char *arg)
{
  assert(cli_wand != (MagickCLI *) NULL);
  assert(cli_wand->signature == WandSignature);
  assert(cli_wand->wand.signature == WandSignature);
  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);

#define image_info      (cli_wand->wand.image_info)
#define exception       (cli_wand->wand.exception)
#define draw_info       (cli_wand->draw_info)
#define quantize_info   (cli_wand->quantize_info)
#define IfSetOption     (*option=='-')
#define ArgOption(def)  (IfSetOption?arg:(const char *)(def))
#define ArgBoolean      (IfSetOption?MagickTrue:MagickFalse)
#define ArgBooleanNot   (IfSetOption?MagickFalse:MagickTrue)

  switch (*(option+1))
  {
    case 'a':
    {
      if (LocaleCompare("adjoin",option+1) == 0)
        {
          image_info->adjoin = ArgBoolean;
          break;
        }
      if (LocaleCompare("affine",option+1) == 0)
        {
          /* DEPRECIATED: draw_info setting only: for -draw and -transform */
          if (IfSetOption)
            (void) ParseAffineGeometry(arg,&draw_info->affine,exception);
          else
            GetAffineMatrix(&draw_info->affine);
          break;
        }
      if (LocaleCompare("antialias",option+1) == 0)
        {
          image_info->antialias =
            draw_info->stroke_antialias =
              draw_info->text_antialias = ArgBoolean;
          break;
        }
      if (LocaleCompare("attenuate",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("authenticate",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      break;
    }
    case 'b':
    {
      if (LocaleCompare("background",option+1) == 0)
        {
          /* FUTURE: both image_info attribute & ImageOption in use!
             image_info only used directly for generating new images.
             SyncImageSettings() used to set per-image attribute.

             FUTURE: if image_info->background_color is not set then
             we should fall back to image
             Note that +background, means fall-back to image background
             and only if not set fall back to BackgroundColor const.
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          (void) QueryColorCompliance(ArgOption(BackgroundColor),AllCompliance,
             &image_info->background_color,exception);
          break;
        }
      if (LocaleCompare("bias",option+1) == 0)
        {
          /* FUTURE: bias OBSOLETED, replaced by "convolve:bias"
             as it is actually rarely used except in direct convolve operations
             Usage outside a direct convolve operation is actally non-sensible!

             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,ArgOption("0"));
          break;
        }
      if (LocaleCompare("black-point-compensation",option+1) == 0)
        {
          /* Used as a image chromaticity setting
             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,
                           IfSetOption ? "true" : "false" );
          break;
        }
      if (LocaleCompare("blue-primary",option+1) == 0)
        {
          /* Image chromaticity X,Y  NB: Y=X if Y not defined
             Used by many coders including PNG
             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,ArgOption("0.0"));
          break;
        }
      if (LocaleCompare("bordercolor",option+1) == 0)
        {
          /* FUTURE: both image_info attribute & ImageOption in use!
             SyncImageSettings() used to set per-image attribute.
          */
          if (IfSetOption)
            {
              (void) SetImageOption(image_info,option+1,arg);
              (void) QueryColorCompliance(arg,AllCompliance,
                  &image_info->border_color,exception);
              (void) QueryColorCompliance(arg,AllCompliance,
                  &draw_info->border_color,exception);
              break;
            }
          (void) DeleteImageOption(image_info,option+1);
          (void) QueryColorCompliance(BorderColor,AllCompliance,
            &image_info->border_color,exception);
          (void) QueryColorCompliance(BorderColor,AllCompliance,
            &draw_info->border_color,exception);
          break;
        }
      if (LocaleCompare("box",option+1) == 0)
        {
          /* DEPRECIATED - now "undercolor" */
          CLISettingOptionInfo(cli_wand,"undercolor",arg);
          break;
        }
      break;
    }
    case 'c':
    {
      if (LocaleCompare("cache",option+1) == 0)
        {
          MagickSizeType
            limit;

          limit=MagickResourceInfinity;
          if (LocaleCompare("unlimited",arg) != 0)
            limit=(MagickSizeType) SiPrefixToDoubleInterval(arg,100.0);
          (void) SetMagickResourceLimit(MemoryResource,limit);
          (void) SetMagickResourceLimit(MapResource,2*limit);
          break;
        }
      if (LocaleCompare("caption",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("channel",option+1) == 0)
        {
          /* FUTURE:  -channel mask {vaules}
             This is applied to images in SimpleImageOperator!!!
             Move it to SyncImageSettings() - or alternative
          */
          image_info->channel=(ChannelType) (
               IfSetOption ? ParseChannelOption(arg) : DefaultChannels );
          break;
        }
      if (LocaleCompare("colorspace",option+1) == 0)
        {
          /* Setting used for new images via AquireImage()
             But also used as a SimpleImageOperator
             Undefined colorspace means don't modify images on
             read or as a operation */
          image_info->colorspace=(ColorspaceType) ParseCommandOption(
               MagickColorspaceOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("comment",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("compose",option+1) == 0)
        {
          /* FUTURE: image_info should be used,
             SyncImageSettings() used to set per-image attribute. - REMOVE

             This setting should NOT be used to set image 'compose'
             "-layer" operators shoud use image_info if defined otherwise
             they should use a per-image compose setting.
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          image_info->compose=(CompositeOperator) ParseCommandOption(
               MagickComposeOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("compress",option+1) == 0)
        {
          /* FUTURE: What should be used?  image_info  or ImageOption ???
             The former is more efficent, but Crisy prefers the latter!
             SyncImageSettings() used to set per-image attribute.

             The coders appears to use image_info, not Image_Option
             however the image attribute (for save) is set from the
             ImageOption!

             Note that "undefined" is a different setting to "none".
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          image_info->compression=(CompressionType) ParseCommandOption(
                MagickCompressOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
      break;
    }
    case 'd':
    {
      if (LocaleCompare("debug",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetLogEventMask(ArgOption("none"));
          image_info->debug=IsEventLogging(); /* extract logging*/
          cli_wand->wand.debug=IsEventLogging();
          break;
        }
      if (LocaleCompare("define",option+1) == 0)
        {
          /* DefineImageOption() equals SetImageOption() but with '='
             It does not however set individual image options.
             "-set" will set individual image options as well!
          */
          if (LocaleNCompare(arg,"registry:",9) == 0)
            {
              if (IfSetOption)
                (void) DefineImageRegistry(StringRegistryType,arg+9,exception);
              else
                (void) DeleteImageRegistry(arg+9);
              break;
            }
          if (IfSetOption)
            (void) DefineImageOption(image_info,arg);
          else
            (void) DeleteImageOption(image_info,arg);
          break;
        }
      if (LocaleCompare("delay",option+1) == 0)
        {
          /* Only used for new images via AcquireImage()
             FUTURE: Option should also be used for "-morph" (color morphing)
          */
          (void) SetImageOption(image_info,option+1,ArgOption("0"));
          break;
        }
      if (LocaleCompare("density",option+1) == 0)
        {
          /* FUTURE: strings used in image_info attr and draw_info!
             Basically as density can be in a XxY form!

             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          (void) CloneString(&image_info->density,ArgOption(NULL));
          (void) CloneString(&draw_info->density,image_info->density);
          break;
        }
      if (LocaleCompare("depth",option+1) == 0)
        {
          /* This is also a SimpleImageOperator! for 8->16 vaule trunc !!!!
             SyncImageSettings() used to set per-image attribute.
          */
          image_info->depth=IfSetOption?StringToUnsignedLong(arg)
                                       :MAGICKCORE_QUANTUM_DEPTH;
          break;
        }
      if (LocaleCompare("direction",option+1) == 0)
        {
          /* Image Option is only used to set draw_info */
          (void) SetImageOption(image_info,option+1,ArgOption("undefined"));
          draw_info->direction=(DirectionType) ParseCommandOption(
                         MagickDirectionOptions,MagickFalse,
                         ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("display",option+1) == 0)
        {
          (void) CloneString(&image_info->server_name,ArgOption(NULL));
          (void) CloneString(&draw_info->server_name,image_info->server_name);
          break;
        }
      if (LocaleCompare("dispose",option+1) == 0)
        {
          /* only used in setting new images */
          (void) SetImageOption(image_info,option+1,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("dither",option+1) == 0)
        {
          /* image_info attr (on/off), quantize_info attr (on/off)
             but also ImageInfo and quantize_info method!
             FUTURE: merge the duality of the dithering options
          */
          image_info->dither = quantize_info->dither = ArgBoolean;
          (void) SetImageOption(image_info,option+1,ArgOption("none"));
          quantize_info->dither_method=(DitherMethod) ParseCommandOption(
                    MagickDitherOptions,MagickFalse,ArgOption("none"));
          if (quantize_info->dither_method == NoDitherMethod)
            image_info->dither = quantize_info->dither = MagickFalse;
          break;
        }
      break;
    }
    case 'e':
    {
      if (LocaleCompare("encoding",option+1) == 0)
        {
          (void) CloneString(&draw_info->encoding,ArgOption("undefined"));
          (void) SetImageOption(image_info,option+1,draw_info->encoding);
          break;
        }
      if (LocaleCompare("endian",option+1) == 0)
        {
          /* Both image_info attr and ImageInfo */
          (void) SetImageOption(image_info,option+1,ArgOption("undefined"));
          image_info->endian=(EndianType) ParseCommandOption(
              MagickEndianOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("extract",option+1) == 0)
        {
          (void) CloneString(&image_info->extract,ArgOption(NULL));
          break;
        }
      break;
    }
    case 'f':
    {
      if (LocaleCompare("family",option+1) == 0)
        {
          (void) CloneString(&draw_info->family,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("fill",option+1) == 0)
        {
          /* Set "fill" OR "fill-pattern" in draw_info
             The original fill color is preserved if a fill-pattern is given.
             That way it does not effect other operations that directly using
             the fill color and, can be retored using "+tile".
          */
          const char
            *value;

          MagickBooleanType
            status;

          ExceptionInfo
            *sans;

          PixelInfo
            color;

          value = ArgOption("none");
          (void) SetImageOption(image_info,option+1,value);
          if (draw_info->fill_pattern != (Image *) NULL)
            draw_info->fill_pattern=DestroyImage(draw_info->fill_pattern);

          /* is it a color or a image? -- ignore exceptions */
          sans=AcquireExceptionInfo();
          status=QueryColorCompliance(value,AllCompliance,&color,sans);
          sans=DestroyExceptionInfo(sans);

          if (status == MagickFalse)
            draw_info->fill_pattern=GetImageCache(image_info,value,exception);
          else
            draw_info->fill=color;
          break;
        }
      if (LocaleCompare("filter",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetImageOption(image_info,option+1,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("font",option+1) == 0)
        {
          (void) CloneString(&draw_info->font,ArgOption(NULL));
          (void) CloneString(&image_info->font,draw_info->font);
          break;
        }
      if (LocaleCompare("format",option+1) == 0)
        {
          /* FUTURE: why the ping test, you could set ping after this! */
          /*
          register const char
            *q;

          for (q=strchr(arg,'%'); q != (char *) NULL; q=strchr(q+1,'%'))
            if (strchr("Agkrz@[#",*(q+1)) != (char *) NULL)
              image_info->ping=MagickFalse;
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("fuzz",option+1) == 0)
        {
          /* Option used to set image fuzz! unless blank canvas (from color)
             Image attribute used for color compare operations
             SyncImageSettings() used to set per-image attribute.

             Can't find anything else using image_info->fuzz directly!
          */
          if (IfSetOption)
            {
              image_info->fuzz=StringToDoubleInterval(arg,(double)
                QuantumRange+1.0);
              (void) SetImageOption(image_info,option+1,arg);
              break;
            }
          image_info->fuzz=0.0;
          (void) SetImageOption(image_info,option+1,"0");
          break;
        }
      break;
    }
    case 'g':
    {
      if (LocaleCompare("gravity",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetImageOption(image_info,option+1,ArgOption("none"));
          draw_info->gravity=(GravityType) ParseCommandOption(
                   MagickGravityOptions,MagickFalse,ArgOption("none"));
          break;
        }
      if (LocaleCompare("green-primary",option+1) == 0)
        {
          /* Image chromaticity X,Y  NB: Y=X if Y not defined
             SyncImageSettings() used to set per-image attribute.
             Used directly by many coders
          */
          (void) SetImageOption(image_info,option+1,ArgOption("0.0"));
          break;
        }
      break;
    }
    case 'i':
    {
      if (LocaleCompare("intent",option+1) == 0)
        {
          /* Only used by coders: MIFF, MPC, BMP, PNG
             and for image profile call to AcquireTransformThreadSet()
             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("interlace",option+1) == 0)
        {
          /* image_info is directly used by coders (so why an image setting?)
             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,ArgOption("undefined"));
          image_info->interlace=(InterlaceType) ParseCommandOption(
                MagickInterlaceOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("interline-spacing",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1, ArgOption(NULL));
          draw_info->interline_spacing=StringToDouble(ArgOption("0"),
               (char **) NULL);
          break;
        }
      if (LocaleCompare("interpolate",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetImageOption(image_info,option+1,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("interword-spacing",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1, ArgOption(NULL));
          draw_info->interword_spacing=StringToDouble(ArgOption("0"),(char **) NULL);
          break;
        }
      break;
    }
    case 'k':
    {
      if (LocaleCompare("kerning",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          draw_info->kerning=StringToDouble(ArgOption("0"),(char **) NULL);
          break;
        }
      break;
    }
    case 'l':
    {
      if (LocaleCompare("label",option+1) == 0)
        {
          /* only used for new images - not in SyncImageOptions() */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("log",option+1) == 0)
        {
          if (IfSetOption)
            (void) SetLogFormat(arg);
          break;
        }
      if (LocaleCompare("loop",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetImageOption(image_info,option+1,ArgOption("0"));
          break;
        }
      break;
    }
    case 'm':
    {
      if (LocaleCompare("mattecolor",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          (void) QueryColorCompliance(ArgOption(MatteColor),AllCompliance,
             &image_info->matte_color,exception);
          break;
        }
      if (LocaleCompare("monitor",option+1) == 0)
        {
          (void) SetImageInfoProgressMonitor(image_info, IfSetOption?
                MonitorProgress: (MagickProgressMonitor) NULL, (void *) NULL);
          break;
        }
      if (LocaleCompare("monochrome",option+1) == 0)
        {
          /* Setting (for some input coders)
             But also a special 'type' operator
          */
          image_info->monochrome= ArgBoolean;
          break;
        }
      break;
    }
    case 'o':
    {
      if (LocaleCompare("orient",option+1) == 0)
        {
          /* Is not used when defining for new images.
             This makes it more of a 'operation' than a setting
             FUTURE: make set meta-data operator instead.
             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1, ArgOption(NULL));
          image_info->orientation=(InterlaceType) ParseCommandOption(
            MagickOrientationOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
    }
    case 'p':
    {
      if (LocaleCompare("page",option+1) == 0)
        {
          /* Only used for new images and image generators
             SyncImageSettings() used to set per-image attribute. ?????
             That last is WRONG!!!!
          */
          char
            *canonical_page,
            page[MaxTextExtent];

          const char
            *image_option;

          MagickStatusType
            flags;

          RectangleInfo
            geometry;

          if (!IfSetOption)
            {
              (void) DeleteImageOption(image_info,option+1);
              (void) CloneString(&image_info->page,(char *) NULL);
              break;
            }
          (void) ResetMagickMemory(&geometry,0,sizeof(geometry));
          image_option=GetImageOption(image_info,"page");
          if (image_option != (const char *) NULL)
            flags=ParseAbsoluteGeometry(image_option,&geometry);
          canonical_page=GetPageGeometry(arg);
          flags=ParseAbsoluteGeometry(canonical_page,&geometry);
          canonical_page=DestroyString(canonical_page);
          (void) FormatLocaleString(page,MaxTextExtent,"%lux%lu",
            (unsigned long) geometry.width,(unsigned long) geometry.height);
          if (((flags & XValue) != 0) || ((flags & YValue) != 0))
            (void) FormatLocaleString(page,MaxTextExtent,"%lux%lu%+ld%+ld",
              (unsigned long) geometry.width,(unsigned long) geometry.height,
              (long) geometry.x,(long) geometry.y);
          (void) SetImageOption(image_info,option+1,page);
          (void) CloneString(&image_info->page,page);
          break;
        }
      if (LocaleCompare("ping",option+1) == 0)
        {
          image_info->ping = ArgBoolean;
          break;
        }
      if (LocaleCompare("pointsize",option+1) == 0)
        {
          image_info->pointsize=draw_info->pointsize=
                   StringToDouble(ArgOption("12"),(char **) NULL);
          break;
        }
      if (LocaleCompare("precision",option+1) == 0)
        {
          (void) SetMagickPrecision(StringToInteger(ArgOption("-1")));
          break;
        }
      /* FUTURE: Only the 'preview' coder appears to use this
       * Depreciate the coder?  Leaving only the 'preview' operator.
      if (LocaleCompare("preview",option+1) == 0)
        {
          image_info->preview_type=UndefinedPreview;
          if (IfSetOption)
            image_info->preview_type=(PreviewType) ParseCommandOption(
                MagickPreviewOptions,MagickFalse,arg);
          break;
        }
      */
      break;
    }
    case 'q':
    {
      if (LocaleCompare("quality",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          image_info->quality=UNDEFINED_COMPRESSION_QUALITY;
          if (IfSetOption)
            image_info->quality=StringToUnsignedLong(arg);
          break;
        }
      if (LocaleCompare("quantize",option+1) == 0)
        {
          /* Just a set direct in quantize_info */
          quantize_info->colorspace=UndefinedColorspace;
          if (IfSetOption)
            quantize_info->colorspace=(ColorspaceType) ParseCommandOption(
                 MagickColorspaceOptions,MagickFalse,arg);
          break;
        }
      if (LocaleCompare("quiet",option+1) == 0)
        {
          /* FUTURE: if two -quiet is performed you can not do +quiet! */
          static WarningHandler
            warning_handler = (WarningHandler) NULL;

          WarningHandler
            tmp = SetWarningHandler((WarningHandler) NULL);

          if ( tmp != (WarningHandler) NULL)
            warning_handler = tmp; /* remember the old handler */
          if (!IfSetOption)        /* set the old handler */
            warning_handler=SetWarningHandler(warning_handler);
          break;
        }
      break;
    }
    case 'r':
    {
      if (LocaleCompare("red-primary",option+1) == 0)
        {
          /* Image chromaticity X,Y  NB: Y=X if Y not defined
             Used by many coders
             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,ArgOption("0.0"));
          break;
        }
      if (LocaleCompare("render",option+1) == 0)
        {
          /* draw_info only setting */
          draw_info->render= ArgBooleanNot;
          break;
        }
      if (LocaleCompare("respect-parenthesis",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      break;
    }
    case 's':
    {
      if (LocaleCompare("sampling-factor",option+1) == 0)
        {
          /* FUTURE: should be converted to jpeg:sampling_factor */
          (void) CloneString(&image_info->sampling_factor,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("scene",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute.
             What ??? Why ????
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          image_info->scene=StringToUnsignedLong(ArgOption("0"));
          break;
        }
      if (LocaleCompare("seed",option+1) == 0)
        {
          SeedPseudoRandomGenerator(
               IfSetOption ? (size_t) StringToUnsignedLong(arg)
                           : (size_t) time((time_t *) NULL) );
          break;
        }
      if (LocaleCompare("size",option+1) == 0)
        {
          /* FUTURE: string in image_info -- convert to Option ???
             Look at the special handling for "size" in SetImageOption()
           */
          (void) CloneString(&image_info->size,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("stretch",option+1) == 0)
        {
          draw_info->stretch=(StretchType) ParseCommandOption(
              MagickStretchOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("stroke",option+1) == 0)
        {
          /* set stroke color OR stroke-pattern
             UPDATE: ensure stroke color is not destroyed is a pattern
             is given. Just in case the color is also used for other purposes.
           */
          const char
            *value;

          MagickBooleanType
            status;

          ExceptionInfo
            *sans;

          PixelInfo
            color;

          value = ArgOption("none");
          (void) SetImageOption(image_info,option+1,value);
          if (draw_info->stroke_pattern != (Image *) NULL)
            draw_info->stroke_pattern=DestroyImage(draw_info->stroke_pattern);

          /* is it a color or a image? -- ignore exceptions */
          sans=AcquireExceptionInfo();
          status=QueryColorCompliance(value,AllCompliance,&color,sans);
          sans=DestroyExceptionInfo(sans);

          if (status == MagickFalse)
            draw_info->stroke_pattern=GetImageCache(image_info,value,exception);
          else
            draw_info->stroke=color;
          break;
        }
      if (LocaleCompare("strokewidth",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          draw_info->stroke_width=StringToDouble(ArgOption("1.0"),
               (char **) NULL);
          break;
        }
      if (LocaleCompare("style",option+1) == 0)
        {
          draw_info->style=(StyleType) ParseCommandOption(MagickStyleOptions,
               MagickFalse,ArgOption("undefined"));
          break;
        }
      if (LocaleCompare("synchronize",option+1) == 0)
        {
          image_info->synchronize = ArgBoolean;
          break;
        }
      break;
    }
    case 't':
    {
      if (LocaleCompare("taint",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetImageOption(image_info,option+1,
               IfSetOption ? "true" : "false");
          break;
        }
      if (LocaleCompare("texture",option+1) == 0)
        {
          /* FUTURE: move image_info string to option splay-tree */
          (void) CloneString(&image_info->texture,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("tile",option+1) == 0)
        {
          draw_info->fill_pattern=IfSetOption
                                 ?GetImageCache(image_info,arg,exception)
                                 :DestroyImage(draw_info->fill_pattern);
          break;
        }
      if (LocaleCompare("tile-offset",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. ??? */
          (void) SetImageOption(image_info,option+1,ArgOption("0"));
          break;
        }
      if (LocaleCompare("transparent-color",option+1) == 0)
        {
          /* FUTURE: both image_info attribute & ImageOption in use!
             image_info only used for generating new images.
             SyncImageSettings() used to set per-image attribute.

             Note that +transparent-color, means fall-back to image
             attribute so ImageOption is deleted, not set to a default.
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          (void) QueryColorCompliance(ArgOption("none"),AllCompliance,
              &image_info->transparent_color,exception);
          break;
        }
      if (LocaleCompare("treedepth",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          quantize_info->tree_depth=StringToUnsignedLong(ArgOption("0"));
          break;
        }
      if (LocaleCompare("type",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute. */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          image_info->type=(ImageType) ParseCommandOption(MagickTypeOptions,
                 MagickFalse,ArgOption("undefined"));
          break;
        }
      break;
    }
    case 'u':
    {
      if (LocaleCompare("undercolor",option+1) == 0)
        {
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          (void) QueryColorCompliance(ArgOption("none"),AllCompliance,
               &draw_info->undercolor,exception);
          break;
        }
      if (LocaleCompare("units",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute.
             Should this effect draw_info X and Y resolution?
             FUTURE: this probably should be part of the density setting
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          image_info->units=(ResolutionType) ParseCommandOption(
                MagickResolutionOptions,MagickFalse,ArgOption("undefined"));
          break;
        }
      break;
    }
    case 'v':
    {
      if (LocaleCompare("verbose",option+1) == 0)
        {
          /* FUTURE: Also an image artifact, set in Simple Operators.
             But artifact is only used in verbose output.
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          image_info->verbose= ArgBoolean;
          image_info->ping=MagickFalse; /* verbose can't be a ping */
          break;
        }
      if (LocaleCompare("view",option+1) == 0)
        {
          /* FUTURE: Convert from image_info to ImageOption
             Only used by coder FPX
          */
          (void) CloneString(&image_info->view,ArgOption(NULL));
          break;
        }
      if (LocaleCompare("virtual-pixel",option+1) == 0)
        {
          /* SyncImageSettings() used to set per-image attribute.
             This is VERY deep in the image caching structure.
          */
          (void) SetImageOption(image_info,option+1,ArgOption(NULL));
          break;
        }
      break;
    }
    case 'w':
    {
      if (LocaleCompare("weight",option+1) == 0)
        {
          /* Just what does using a font 'weight' do ???
             There is no "-list weight" output (reference manual says there is)
          */
          if (!IfSetOption)
            break;
          draw_info->weight=StringToUnsignedLong(arg);
          if (LocaleCompare(arg,"all") == 0)
            draw_info->weight=0;
          if (LocaleCompare(arg,"bold") == 0)
            draw_info->weight=700;
          if (LocaleCompare(arg,"bolder") == 0)
            if (draw_info->weight <= 800)
              draw_info->weight+=100;
          if (LocaleCompare(arg,"lighter") == 0)
            if (draw_info->weight >= 100)
              draw_info->weight-=100;
          if (LocaleCompare(arg,"normal") == 0)
            draw_info->weight=400;
          break;
        }
      if (LocaleCompare("white-point",option+1) == 0)
        {
          /* Used as a image chromaticity setting
             SyncImageSettings() used to set per-image attribute.
          */
          (void) SetImageOption(image_info,option+1,ArgOption("0.0"));
          break;
        }
      break;
    }
    default:
      break;
  }
#undef image_info
#undef exception
#undef draw_info
#undef quantize_info
#undef IfSetOption
#undef ArgOption
#undef ArgBoolean

  return;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+     C L I S i m p l e O p e r a t o r I m a g e s                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  WandSimpleOperatorImages() applys one simple image operation given to all
%  the images in the CLI wand,  with the settings that was previously saved in
%  the CLI wand.
%
%  It is assumed that any per-image settings are up-to-date with respect to
%  extra settings that were already saved in the wand.
%
%  The format of the WandSimpleOperatorImage method is:
%
%    void CLISimpleOperatorImages(MagickCLI *cli_wand,
%        const char *option, const char *arg1, const char *arg2)
%
%  A description of each parameter follows:
%
%    o cli_wand: structure holding settings and images to be operated on
%
%    o option:  The option string for the operation
%
%    o arg1, arg2: optional argument strings to the operation
%
% Any problems will be added to the 'exception' entry of the given wand.
%
% Example usage...
%
%  CLISimpleOperatorImages(cli_wand, "-crop","100x100+20+30",NULL);
%  CLISimpleOperatorImages(cli_wand, "+repage",NULL,NULL);
%  CLISimpleOperatorImages(cli_wand, "+distort","SRT","45");
%
% Or for handling command line arguments EG: +/-option ["arg"]
%
%    cli_wand
%    argc,argv
%    i=index in argv
%
%    option_info = GetCommandOptionInfo(argv[i]);
%    count=option_info->type;
%    option_type=option_info->flags;
%
%    if ( (option_type & SimpleOperatorOptionFlag) != 0 )
%      CLISimpleOperatorImages(cli_wand, argv[i],
%          count>=1 ? argv[i+1] : (char *)NULL,
%          count>=2 ? argv[i+2] : (char *)NULL );
%    i += count+1;
%
*/

/*
  Internal subrountine to apply one simple image operation to the current
  image pointed to by the CLI wand.

  The image in the list may be modified in three different ways...
    * directly modified (EG: -negate, -gamma, -level, -annotate, -draw),
    * replaced by a new image (EG: -spread, -resize, -rotate, -morphology)
    * one image replace by a list of images (-separate and -crop only!)

  In each case the result replaces the single original image in the list, as
  well as the pointer to the modified image (last image added if replaced by a
  list of images) is returned.

  As the image pointed to may be replaced, the first image in the list may
  also change.  GetFirstImageInList() should be used by caller if they wish
  return the Image pointer to the first image in list.
*/
static void CLISimpleOperatorImage(MagickCLI *cli_wand,
  const char *option, const char *arg1, const char *arg2)
{
  Image *
    new_image;

  GeometryInfo
    geometry_info;

  RectangleInfo
    geometry;

  MagickStatusType
    flags;

#define image_info      (cli_wand->wand.image_info)
#define image           (cli_wand->wand.images)
#define exception       (cli_wand->wand.exception)
#define draw_info       (cli_wand->draw_info)
#define quantize_info   (cli_wand->quantize_info)
#define IfNormalOp      (*option=='-')
#define IfPlusOp        (*option!='-')
#define normal_op       (IfNormalOp?MagickTrue:MagickFalse)
#define plus_alt_op     (IfNormalOp?MagickFalse:MagickTrue)

  assert(cli_wand != (MagickCLI *) NULL);
  assert(cli_wand->signature == WandSignature);
  assert(cli_wand->wand.signature == WandSignature);
  assert(image != (Image *) NULL);             /* an image must be present */
  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);

  SetGeometryInfo(&geometry_info);

  new_image = (Image *)NULL; /* the replacement image, if not null at end */

  /* FUTURE: We may need somthing a little more optimized than this!
     Perhaps, do the 'sync' if 'settings tainted' before next operator.
  */
  (void) SyncImageSettings(image_info,image,exception);

  switch (*(option+1))
  {
    case 'a':
    {
      if (LocaleCompare("adaptive-blur",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=0.0;
          new_image=AdaptiveBlurImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,exception);
          break;
        }
      if (LocaleCompare("adaptive-resize",option+1) == 0)
        {
          (void) ParseRegionGeometry(image,arg1,&geometry,exception);
          new_image=AdaptiveResizeImage(image,geometry.width,geometry.height,
               exception);
          break;
        }
      if (LocaleCompare("adaptive-sharpen",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=0.0;
          new_image=AdaptiveSharpenImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,exception);
          break;
        }
      if (LocaleCompare("alpha",option+1) == 0)
        {
          AlphaChannelType
            alpha_type;

          alpha_type=(AlphaChannelType) ParseCommandOption(MagickAlphaOptions,
            MagickFalse,arg1);
          (void) SetImageAlphaChannel(image,alpha_type,exception);
          break;
        }
      if (LocaleCompare("annotate",option+1) == 0)
        {
          char
            *text,
            geometry[MaxTextExtent];

          SetGeometryInfo(&geometry_info);
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=geometry_info.rho;
          text=InterpretImageProperties(image_info,image,arg2,
            exception);
          if (text == (char *) NULL)
            break;
          (void) CloneString(&draw_info->text,text);
          text=DestroyString(text);
          (void) FormatLocaleString(geometry,MaxTextExtent,"%+f%+f",
            geometry_info.xi,geometry_info.psi);
          (void) CloneString(&draw_info->geometry,geometry);
          draw_info->affine.sx=cos(DegreesToRadians(
            fmod(geometry_info.rho,360.0)));
          draw_info->affine.rx=sin(DegreesToRadians(
            fmod(geometry_info.rho,360.0)));
          draw_info->affine.ry=(-sin(DegreesToRadians(
            fmod(geometry_info.sigma,360.0))));
          draw_info->affine.sy=cos(DegreesToRadians(
            fmod(geometry_info.sigma,360.0)));
          (void) AnnotateImage(image,draw_info,exception);
          GetAffineMatrix(&draw_info->affine);
          break;
        }
      if (LocaleCompare("auto-gamma",option+1) == 0)
        {
          (void) AutoGammaImage(image,exception);
          break;
        }
      if (LocaleCompare("auto-level",option+1) == 0)
        {
          (void) AutoLevelImage(image,exception);
          break;
        }
      if (LocaleCompare("auto-orient",option+1) == 0)
        {
          /* This should probbaly be a MagickCore function */
          switch (image->orientation)
          {
            case TopRightOrientation:
            {
              new_image=FlopImage(image,exception);
              break;
            }
            case BottomRightOrientation:
            {
              new_image=RotateImage(image,180.0,exception);
              break;
            }
            case BottomLeftOrientation:
            {
              new_image=FlipImage(image,exception);
              break;
            }
            case LeftTopOrientation:
            {
              new_image=TransposeImage(image,exception);
              break;
            }
            case RightTopOrientation:
            {
              new_image=RotateImage(image,90.0,exception);
              break;
            }
            case RightBottomOrientation:
            {
              new_image=TransverseImage(image,exception);
              break;
            }
            case LeftBottomOrientation:
            {
              new_image=RotateImage(image,270.0,exception);
              break;
            }
            default:
              break;
          }
          if (new_image != (Image *) NULL)
            new_image->orientation=TopLeftOrientation;
          break;
        }
      break;
    }
    case 'b':
    {
      if (LocaleCompare("black-threshold",option+1) == 0)
        {
          (void) BlackThresholdImage(image,arg1,exception);
          break;
        }
      if (LocaleCompare("blue-shift",option+1) == 0)
        {
          geometry_info.rho=1.5;
          if (IfNormalOp)
            flags=ParseGeometry(arg1,&geometry_info);
          new_image=BlueShiftImage(image,geometry_info.rho,exception);
          break;
        }
      if (LocaleCompare("blur",option+1) == 0)
        {
          /* FUTURE: use of "bias" in a blur is non-sensible */
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=0.0;
          new_image=BlurImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,exception);
          break;
        }
      if (LocaleCompare("border",option+1) == 0)
        {
          CompositeOperator
            compose;

          const char*
            value;

          value=GetImageOption(image_info,"compose");
          if (value != (const char *) NULL)
            compose=(CompositeOperator) ParseCommandOption(
                 MagickComposeOptions,MagickFalse,value);
          else
            compose=OverCompositeOp;  /* use Over not image->compose */

          flags=ParsePageGeometry(image,arg1,&geometry,exception);
          if ((flags & SigmaValue) == 0)
            geometry.height=geometry.width;
          new_image=BorderImage(image,&geometry,compose,exception);
          break;
        }
      if (LocaleCompare("brightness-contrast",option+1) == 0)
        {
          double
            brightness,
            contrast;

          GeometryInfo
            geometry_info;

          MagickStatusType
            flags;

          flags=ParseGeometry(arg1,&geometry_info);
          brightness=geometry_info.rho;
          contrast=0.0;
          if ((flags & SigmaValue) != 0)
            contrast=geometry_info.sigma;
          (void) BrightnessContrastImage(image,brightness,contrast,
            exception);
          break;
        }
      break;
    }
    case 'c':
    {
      if (LocaleCompare("cdl",option+1) == 0)
        {
          char
            *color_correction_collection;

          /*
            Color correct with a color decision list.
          */
          color_correction_collection=FileToString(arg1,~0,exception);
          if (color_correction_collection == (char *) NULL)
            break;
          (void) ColorDecisionListImage(image,color_correction_collection,
            exception);
          break;
        }
      if (LocaleCompare("channel",option+1) == 0)
        {
          /* The "channel" setting has already been set
             FUTURE: This probably should be part of WandSettingOptionInfo()
             or SyncImageSettings().
          */
          SetPixelChannelMapMask(image,image_info->channel);
          break;
        }
      if (LocaleCompare("charcoal",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=1.0;
          new_image=CharcoalImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,exception);
          break;
        }
      if (LocaleCompare("chop",option+1) == 0)
        {
          (void) ParseGravityGeometry(image,arg1,&geometry,exception);
          new_image=ChopImage(image,&geometry,exception);
          break;
        }
      if (LocaleCompare("clamp",option+1) == 0)
        {
          (void) ClampImage(image,exception);
          break;
        }
      if (LocaleCompare("clip",option+1) == 0)
        {
          if (IfNormalOp)
            (void) ClipImage(image,exception);
          else /* "+mask" remove the write mask */
            (void) SetImageMask(image,(Image *) NULL,exception);
          break;
        }
      if (LocaleCompare("clip-mask",option+1) == 0)
        {
          CacheView
            *mask_view;

          Image
            *mask_image;

          register Quantum
            *restrict q;

          register ssize_t
            x;

          ssize_t
            y;

          if (IfPlusOp) {
            /* "+clip-mask" Remove the write mask */
            (void) SetImageMask(image,(Image *) NULL,exception);
            break;
          }
          mask_image=GetImageCache(image_info,arg1,exception);
          if (mask_image == (Image *) NULL)
            break;
          if (SetImageStorageClass(mask_image,DirectClass,exception)
               == MagickFalse)
            break;
          /* Create a write mask from cli_wandp-mask image */
          /* FUTURE: use Alpha operations instead and create a Grey Image */
          mask_view=AcquireCacheView(mask_image);
          for (y=0; y < (ssize_t) mask_image->rows; y++)
          {
            q=GetCacheViewAuthenticPixels(mask_view,0,y,mask_image->columns,1,
              exception);
            if (q == (Quantum *) NULL)
              break;
            for (x=0; x < (ssize_t) mask_image->columns; x++)
            {
              if (mask_image->matte == MagickFalse)
                SetPixelAlpha(mask_image,GetPixelIntensity(mask_image,q),q);
              SetPixelRed(mask_image,GetPixelAlpha(mask_image,q),q);
              SetPixelGreen(mask_image,GetPixelAlpha(mask_image,q),q);
              SetPixelBlue(mask_image,GetPixelAlpha(mask_image,q),q);
              q+=GetPixelChannels(mask_image);
            }
            if (SyncCacheViewAuthenticPixels(mask_view,exception) == MagickFalse)
              break;
          }
          /* clean up and set the write mask */
          mask_view=DestroyCacheView(mask_view);
          mask_image->matte=MagickTrue;
          (void) SetImageMask(image,mask_image,exception);
          mask_image=DestroyImage(mask_image);
          break;
        }
      if (LocaleCompare("clip-path",option+1) == 0)
        {
          (void) ClipImagePath(image,arg1,normal_op,exception);
          break;
        }
      if (LocaleCompare("colorize",option+1) == 0)
        {
          new_image=ColorizeImage(image,arg1,&draw_info->fill,exception);
          break;
        }
      if (LocaleCompare("color-matrix",option+1) == 0)
        {
          KernelInfo
            *kernel;

          kernel=AcquireKernelInfo(arg1);
          if (kernel == (KernelInfo *) NULL)
            break;
          new_image=ColorMatrixImage(image,kernel,exception);
          kernel=DestroyKernelInfo(kernel);
          break;
        }
      if (LocaleCompare("colors",option+1) == 0)
        {
          /* Reduce the number of colors in the image.
             FUTURE: also provide 'plus version with image 'color counts'
          */
          quantize_info->number_colors=StringToUnsignedLong(arg1);
          if (quantize_info->number_colors == 0)
            break;
          if ((image->storage_class == DirectClass) ||
              image->colors > quantize_info->number_colors)
            (void) QuantizeImage(quantize_info,image,exception);
          else
            (void) CompressImageColormap(image,exception);
          break;
        }
      if (LocaleCompare("colorspace",option+1) == 0)
        {
          /* WARNING: this is both a image_info setting (already done)
                      and a operator to change image colorspace.

             FUTURE: default colorspace should be sRGB!
             Unless some type of 'linear colorspace' mode is set.

             Note that +colorspace sets "undefined" or no effect on
             new images, but forces images already in memory back to RGB!
             That seems to be a little strange!
          */
          (void) TransformImageColorspace(image,
                    IfNormalOp ? image_info->colorspace : RGBColorspace,
                    exception);
          break;
        }
      if (LocaleCompare("contrast",option+1) == 0)
        {
          (void) ContrastImage(image,normal_op,exception);
          break;
        }
      if (LocaleCompare("contrast-stretch",option+1) == 0)
        {
          double
            black_point,
            white_point;

          MagickStatusType
            flags;

          flags=ParseGeometry(arg1,&geometry_info);
          black_point=geometry_info.rho;
          white_point=(flags & SigmaValue) != 0 ? geometry_info.sigma :
            black_point;
          if ((flags & PercentValue) != 0)
            {
              black_point*=(double) image->columns*image->rows/100.0;
              white_point*=(double) image->columns*image->rows/100.0;
            }
          white_point=(MagickRealType) image->columns*image->rows-
            white_point;
          (void) ContrastStretchImage(image,black_point,white_point,
            exception);
          break;
        }
      if (LocaleCompare("convolve",option+1) == 0)
        {
          KernelInfo
            *kernel_info;

          kernel_info=AcquireKernelInfo(arg1);
          if (kernel_info == (KernelInfo *) NULL)
            break;
          kernel_info->bias=image->bias;
          new_image=ConvolveImage(image,kernel_info,exception);
          kernel_info=DestroyKernelInfo(kernel_info);
          break;
        }
      if (LocaleCompare("crop",option+1) == 0)
        {
          /* WARNING: This can generate multiple images! */
          new_image=CropImageToTiles(image,arg1,exception);
          break;
        }
      if (LocaleCompare("cycle",option+1) == 0)
        {
          (void) CycleColormapImage(image,(ssize_t) StringToLong(arg1),
            exception);
          break;
        }
      break;
    }
    case 'd':
    {
      if (LocaleCompare("decipher",option+1) == 0)
        {
          StringInfo
            *passkey;

          passkey=FileToStringInfo(arg1,~0,exception);
          if (passkey != (StringInfo *) NULL)
            {
              (void) PasskeyDecipherImage(image,passkey,exception);
              passkey=DestroyStringInfo(passkey);
            }
          break;
        }
      if (LocaleCompare("depth",option+1) == 0)
        {
          /* The image_info->depth setting has already been set
             We just need to apply it to all images in current sequence

             WARNING: Depth from 8 to 16 causes 'quantum rounding to images!
             That is it really is an operation, not a setting! Arrgghhh

             FUTURE: this should not be an operator!!!
          */
          (void) SetImageDepth(image,image_info->depth,exception);
          break;
        }
      if (LocaleCompare("deskew",option+1) == 0)
        {
          double
            threshold;

          if (IfNormalOp)
            threshold=StringToDoubleInterval(arg1,(double) QuantumRange+1.0);
          else
            threshold=40.0*QuantumRange/100.0;
          new_image=DeskewImage(image,threshold,exception);
          break;
        }
      if (LocaleCompare("despeckle",option+1) == 0)
        {
          new_image=DespeckleImage(image,exception);
          break;
        }
      if (LocaleCompare("distort",option+1) == 0)
        {
          char
            *args,
            token[MaxTextExtent];

          const char
            *p;

          DistortImageMethod
            method;

          double
            *arguments;

          register ssize_t
            x;

          size_t
            number_arguments;

          method=(DistortImageMethod) ParseCommandOption(MagickDistortOptions,
            MagickFalse,arg1);
          if (method == ResizeDistortion)
            {
               double
                 resize_args[2];
               /* Special Case - Argument is actually a resize geometry!
               ** Convert that to an appropriate distortion argument array.
               ** FUTURE: make a separate special resize operator
               */
               (void) ParseRegionGeometry(image,arg2,&geometry,
                 exception);
               resize_args[0]=(double) geometry.width;
               resize_args[1]=(double) geometry.height;
               new_image=DistortImage(image,method,(size_t)2,
                 resize_args,MagickTrue,exception);
               break;
            }
          /* handle percent arguments */
          args=InterpretImageProperties(image_info,image,arg2,
            exception);
          if (args == (char *) NULL)
            break;
          /* convert arguments into an array of doubles
             FUTURE: make this a separate function.
             Also make use of new 'sentinal' feature to avoid need for
             tokenization.
          */
          p=(char *) args;
          for (x=0; *p != '\0'; x++)
          {
            GetMagickToken(p,&p,token);
            if (*token == ',')
              GetMagickToken(p,&p,token);
          }
          number_arguments=(size_t) x;
          arguments=(double *) AcquireQuantumMemory(number_arguments,
            sizeof(*arguments));
          if (arguments == (double *) NULL)
            ThrowWandFatalException(ResourceLimitFatalError,
              "MemoryAllocationFailed",image->filename);
          (void) ResetMagickMemory(arguments,0,number_arguments*
            sizeof(*arguments));
          p=(char *) args;
          for (x=0; (x < (ssize_t) number_arguments) && (*p != '\0'); x++)
          {
            GetMagickToken(p,&p,token);
            if (*token == ',')
              GetMagickToken(p,&p,token);
            arguments[x]=StringToDouble(token,(char **) NULL);
          }
          args=DestroyString(args);
          new_image=DistortImage(image,method,number_arguments,arguments,
                        plus_alt_op,exception);
          arguments=(double *) RelinquishMagickMemory(arguments);
          break;
        }
      if (LocaleCompare("draw",option+1) == 0)
        {
          (void) CloneString(&draw_info->primitive,arg1);
          (void) DrawImage(image,draw_info,exception);
          (void) CloneString(&draw_info->primitive,(char *)NULL);
          break;
        }
      break;
    }
    case 'e':
    {
      if (LocaleCompare("edge",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          new_image=EdgeImage(image,geometry_info.rho,
            geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("emboss",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          new_image=EmbossImage(image,geometry_info.rho,
            geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("encipher",option+1) == 0)
        {
          StringInfo
            *passkey;

          passkey=FileToStringInfo(arg1,~0,exception);
          if (passkey != (StringInfo *) NULL)
            {
              (void) PasskeyEncipherImage(image,passkey,exception);
              passkey=DestroyStringInfo(passkey);
            }
          break;
        }
      if (LocaleCompare("enhance",option+1) == 0)
        {
          new_image=EnhanceImage(image,exception);
          break;
        }
      if (LocaleCompare("equalize",option+1) == 0)
        {
          (void) EqualizeImage(image,exception);
          break;
        }
      if (LocaleCompare("evaluate",option+1) == 0)
        {
          double
            constant;

          MagickEvaluateOperator
            op;

          op=(MagickEvaluateOperator) ParseCommandOption(
            MagickEvaluateOptions,MagickFalse,arg1);
          constant=StringToDoubleInterval(arg2,(double) QuantumRange+1.0);
          (void) EvaluateImage(image,op,constant,exception);
          break;
        }
      if (LocaleCompare("extent",option+1) == 0)
        {
          flags=ParseGravityGeometry(image,arg1,&geometry,exception);
          if (geometry.width == 0)
            geometry.width=image->columns;
          if (geometry.height == 0)
            geometry.height=image->rows;
          new_image=ExtentImage(image,&geometry,exception);
          break;
        }
      break;
    }
    case 'f':
    {
      if (LocaleCompare("features",option+1) == 0)
        {
          /* FUTURE: move to SyncImageSettings() and AcqireImage()??? */
          if (IfPlusOp) {
              (void) DeleteImageArtifact(image,"identify:features");
              break;
            }
          (void) SetImageArtifact(image,"identify:features","true");
          (void) SetImageArtifact(image,"verbose","true");
          break;
        }
      if (LocaleCompare("flip",option+1) == 0)
        {
          new_image=FlipImage(image,exception);
          break;
        }
      if (LocaleCompare("flop",option+1) == 0)
        {
          new_image=FlopImage(image,exception);
          break;
        }
      if (LocaleCompare("floodfill",option+1) == 0)
        {
          PixelInfo
            target;

          (void) ParsePageGeometry(image,arg1,&geometry,exception);
          (void) QueryColorCompliance(arg2,AllCompliance,&target,exception);
          (void) FloodfillPaintImage(image,draw_info,&target,geometry.x,
                    geometry.y,plus_alt_op,exception);
          break;
        }
      if (LocaleCompare("frame",option+1) == 0)
        {
          FrameInfo
            frame_info;

          CompositeOperator
            compose;

          const char*
            value;

          value=GetImageOption(image_info,"compose");
          if (value != (const char *) NULL)
            compose=(CompositeOperator) ParseCommandOption(
                 MagickComposeOptions,MagickFalse,value);
          else
            compose=OverCompositeOp;  /* use Over not image->compose */

          flags=ParsePageGeometry(image,arg1,&geometry,exception);
          frame_info.width=geometry.width;
          frame_info.height=geometry.height;
          if ((flags & HeightValue) == 0)
            frame_info.height=geometry.width;
          frame_info.outer_bevel=geometry.x;
          frame_info.inner_bevel=geometry.y;
          frame_info.x=(ssize_t) frame_info.width;
          frame_info.y=(ssize_t) frame_info.height;
          frame_info.width=image->columns+2*frame_info.width;
          frame_info.height=image->rows+2*frame_info.height;
          new_image=FrameImage(image,&frame_info,compose,exception);
          break;
        }
      if (LocaleCompare("function",option+1) == 0)
        {
          char
            *arguments,
            token[MaxTextExtent];

          const char
            *p;

          double
            *parameters;

          MagickFunction
            function;

          register ssize_t
            x;

          size_t
            number_parameters;

          /*
            Function Modify Image Values
            FUTURE: code should be almost a duplicate of that is "distort"
          */
          function=(MagickFunction) ParseCommandOption(MagickFunctionOptions,
            MagickFalse,arg1);
          arguments=InterpretImageProperties(image_info,image,arg2,
            exception);
          if (arguments == (char *) NULL)
            break;
          p=(char *) arguments;
          for (x=0; *p != '\0'; x++)
          {
            GetMagickToken(p,&p,token);
            if (*token == ',')
              GetMagickToken(p,&p,token);
          }
          number_parameters=(size_t) x;
          parameters=(double *) AcquireQuantumMemory(number_parameters,
            sizeof(*parameters));
          if (parameters == (double *) NULL)
            ThrowWandFatalException(ResourceLimitFatalError,
              "MemoryAllocationFailed",image->filename);
          (void) ResetMagickMemory(parameters,0,number_parameters*
            sizeof(*parameters));
          p=(char *) arguments;
          for (x=0; (x < (ssize_t) number_parameters) && (*p != '\0'); x++)
          {
            GetMagickToken(p,&p,token);
            if (*token == ',')
              GetMagickToken(p,&p,token);
            parameters[x]=StringToDouble(token,(char **) NULL);
          }
          arguments=DestroyString(arguments);
          (void) FunctionImage(image,function,number_parameters,parameters,
            exception);
          parameters=(double *) RelinquishMagickMemory(parameters);
          break;
        }
      break;
    }
    case 'g':
    {
      if (LocaleCompare("gamma",option+1) == 0)
        {
          if (IfNormalOp)
            (void) GammaImage(image,StringToDouble(arg1,(char **) NULL),
                 exception);
          else
            image->gamma=StringToDouble(arg1,(char **) NULL);
          break;
        }
      if ((LocaleCompare("gaussian-blur",option+1) == 0) ||
          (LocaleCompare("gaussian",option+1) == 0))
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          new_image=GaussianBlurImage(image,geometry_info.rho,
            geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("geometry",option+1) == 0)
        {
          /*
            Record Image offset for composition. (A Setting)
            Resize last image. (ListOperator)
            FUTURE: Why if no 'offset' does this resize ALL images?
            Also why is the setting recorded in the IMAGE non-sense!
          */
          if (IfPlusOp)
            { /* remove the previous composition geometry offset! */
              if (image->geometry != (char *) NULL)
                image->geometry=DestroyString(image->geometry);
              break;
            }
          flags=ParseRegionGeometry(image,arg1,&geometry,exception);
          if (((flags & XValue) != 0) || ((flags & YValue) != 0))
            (void) CloneString(&image->geometry,arg1);
          else
            new_image=ResizeImage(image,geometry.width,geometry.height,
              image->filter,image->blur,exception);
          break;
        }
      break;
    }
    case 'h':
    {
      if (LocaleCompare("highlight-color",option+1) == 0)
        {
          (void) SetImageArtifact(image,option+1,arg1);
          break;
        }
      break;
    }
    case 'i':
    {
      if (LocaleCompare("identify",option+1) == 0)
        {
          const char
            *format,
            *text;

          format=GetImageOption(image_info,"format");
          if (format == (char *) NULL)
            {
              (void) IdentifyImage(image,stdout,image_info->verbose,
                exception);
              break;
            }
          text=InterpretImageProperties(image_info,image,format,exception);
          if (text == (char *) NULL)
            break;
          (void) fputs(text,stdout);
          (void) fputc('\n',stdout);
          text=DestroyString((char *)text);
          break;
        }
      if (LocaleCompare("implode",option+1) == 0)
        {
          (void) ParseGeometry(arg1,&geometry_info);
          new_image=ImplodeImage(image,geometry_info.rho,
            image->interpolate,exception);
          break;
        }
      if (LocaleCompare("interpolative-resize",option+1) == 0)
        {
          (void) ParseRegionGeometry(image,arg1,&geometry,exception);
          new_image=InterpolativeResizeImage(image,geometry.width,
               geometry.height,image->interpolate,exception);
          break;
        }
      break;
    }
    case 'l':
    {
      if (LocaleCompare("lat",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & PercentValue) != 0)
            geometry_info.xi=(double) QuantumRange*geometry_info.xi/100.0;
          new_image=AdaptiveThresholdImage(image,(size_t) geometry_info.rho,
               (size_t) geometry_info.sigma,(double) geometry_info.xi,
               exception);
          break;
        }
      if (LocaleCompare("level",option+1) == 0)
        {
          MagickRealType
            black_point,
            gamma,
            white_point;

          MagickStatusType
            flags;

          flags=ParseGeometry(arg1,&geometry_info);
          black_point=geometry_info.rho;
          white_point=(MagickRealType) QuantumRange;
          if ((flags & SigmaValue) != 0)
            white_point=geometry_info.sigma;
          gamma=1.0;
          if ((flags & XiValue) != 0)
            gamma=geometry_info.xi;
          if ((flags & PercentValue) != 0)
            {
              black_point*=(MagickRealType) (QuantumRange/100.0);
              white_point*=(MagickRealType) (QuantumRange/100.0);
            }
          if ((flags & SigmaValue) == 0)
            white_point=(MagickRealType) QuantumRange-black_point;
          if (IfPlusOp || ((flags & AspectValue) != 0))
            (void) LevelizeImage(image,black_point,white_point,gamma,exception);
          else
            (void) LevelImage(image,black_point,white_point,gamma,exception);
          break;
        }
      if (LocaleCompare("level-colors",option+1) == 0)
        {
          char
            token[MaxTextExtent];

          const char
            *p;

          PixelInfo
            black_point,
            white_point;

          p=(const char *) arg1;
          GetMagickToken(p,&p,token);  /* get black point color */
          if ((isalpha((int) *token) != 0) || ((*token == '#') != 0))
            (void) QueryColorCompliance(token,AllCompliance,
                      &black_point,exception);
          else
            (void) QueryColorCompliance("#000000",AllCompliance,
                      &black_point,exception);
          if (isalpha((int) token[0]) || (token[0] == '#'))
            GetMagickToken(p,&p,token);
          if (*token == '\0')
            white_point=black_point; /* set everything to that color */
          else
            {
              if ((isalpha((int) *token) == 0) && ((*token == '#') == 0))
                GetMagickToken(p,&p,token); /* Get white point color. */
              if ((isalpha((int) *token) != 0) || ((*token == '#') != 0))
                (void) QueryColorCompliance(token,AllCompliance,
                           &white_point,exception);
              else
                (void) QueryColorCompliance("#ffffff",AllCompliance,
                           &white_point,exception);
            }
          (void) LevelImageColors(image,&black_point,&white_point,
                     plus_alt_op,exception);
          break;
        }
      if (LocaleCompare("linear-stretch",option+1) == 0)
        {
          double
            black_point,
            white_point;

          MagickStatusType
            flags;

          flags=ParseGeometry(arg1,&geometry_info);
          black_point=geometry_info.rho;
          white_point=(MagickRealType) image->columns*image->rows;
          if ((flags & SigmaValue) != 0)
            white_point=geometry_info.sigma;
          if ((flags & PercentValue) != 0)
            {
              black_point*=(double) image->columns*image->rows/100.0;
              white_point*=(double) image->columns*image->rows/100.0;
            }
          if ((flags & SigmaValue) == 0)
            white_point=(MagickRealType) image->columns*image->rows-
              black_point;
          (void) LinearStretchImage(image,black_point,white_point,exception);
          break;
        }
      if (LocaleCompare("liquid-rescale",option+1) == 0)
        {
          flags=ParseRegionGeometry(image,arg1,&geometry,exception);
          if ((flags & XValue) == 0)
            geometry.x=1;
          if ((flags & YValue) == 0)
            geometry.y=0;
          new_image=LiquidRescaleImage(image,geometry.width,
            geometry.height,1.0*geometry.x,1.0*geometry.y,exception);
          break;
        }
      if (LocaleCompare("lowlight-color",option+1) == 0)
        {
          (void) SetImageArtifact(image,option+1,arg1);
          break;
        }
      break;
    }
    case 'm':
    {
      if (LocaleCompare("map",option+1) == 0)
        {
          Image
            *remap_image;

          /* DEPRECIATED use -remap */
          remap_image=GetImageCache(image_info,arg1,exception);
          if (remap_image == (Image *) NULL)
            break;
          (void) RemapImage(quantize_info,image,remap_image,exception);
          remap_image=DestroyImage(remap_image);
          break;
        }
      if (LocaleCompare("mask",option+1) == 0)
        {
          Image
            *mask;

          if (IfPlusOp)
            { /* Remove a mask. */
              (void) SetImageMask(image,(Image *) NULL,exception);
              break;
            }
          /* Set the image mask. */
          mask=GetImageCache(image_info,arg1,exception);
          if (mask == (Image *) NULL)
            break;
          (void) SetImageMask(image,mask,exception);
          mask=DestroyImage(mask);
          break;
        }
      if (LocaleCompare("matte",option+1) == 0)
        {
          /* DEPRECIATED */
          (void) SetImageAlphaChannel(image,IfNormalOp ? SetAlphaChannel :
                         DeactivateAlphaChannel, exception);
          break;
        }
      if (LocaleCompare("mode",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=geometry_info.rho;
          new_image=StatisticImage(image,ModeStatistic,(size_t)
            geometry_info.rho,(size_t) geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("modulate",option+1) == 0)
        {
          (void) ModulateImage(image,arg1,exception);
          break;
        }
      if (LocaleCompare("monitor",option+1) == 0)
        {
          (void) SetImageProgressMonitor(image, IfNormalOp ? MonitorProgress :
                (MagickProgressMonitor) NULL,(void *) NULL);
          break;
        }
      if (LocaleCompare("monochrome",option+1) == 0)
        {
          (void) SetImageType(image,BilevelType,exception);
          break;
        }
      if (LocaleCompare("morphology",option+1) == 0)
        {
          char
            token[MaxTextExtent];

          const char
            *p;

          KernelInfo
            *kernel;

          MorphologyMethod
            method;

          ssize_t
            iterations;

          p=arg1;
          GetMagickToken(p,&p,token);
          method=(MorphologyMethod) ParseCommandOption(
            MagickMorphologyOptions,MagickFalse,token);
          iterations=1L;
          GetMagickToken(p,&p,token);
          if ((*p == ':') || (*p == ','))
            GetMagickToken(p,&p,token);
          if ((*p != '\0'))
            iterations=(ssize_t) StringToLong(p);
          kernel=AcquireKernelInfo(arg2);
          if (kernel == (KernelInfo *) NULL)
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                   OptionError,"UnabletoParseKernel","morphology");
              break;
            }
          new_image=MorphologyImage(image,method,iterations,kernel,exception);
          kernel=DestroyKernelInfo(kernel);
          break;
        }
      if (LocaleCompare("motion-blur",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          new_image=MotionBlurImage(image,geometry_info.rho,
              geometry_info.sigma,geometry_info.xi,geometry_info.psi,
              exception);
          break;
        }
      break;
    }
    case 'n':
    {
      if (LocaleCompare("negate",option+1) == 0)
        {
          (void) NegateImage(image, plus_alt_op, exception);
          break;
        }
      if (LocaleCompare("noise",option+1) == 0)
        {
          if (IfNormalOp)
            {
              flags=ParseGeometry(arg1,&geometry_info);
              if ((flags & SigmaValue) == 0)
                geometry_info.sigma=geometry_info.rho;
              new_image=StatisticImage(image,NonpeakStatistic,(size_t)
                geometry_info.rho,(size_t) geometry_info.sigma,exception);
            }
          else
            {
              NoiseType
                noise;

              double
                attenuate;

              const char*
                value;

              noise=(NoiseType) ParseCommandOption(MagickNoiseOptions,
                          MagickFalse,arg1),

              value=GetImageOption(image_info,"attenuate");
              if  (value != (const char *) NULL)
                attenuate=StringToDouble(value,(char **) NULL);
              else
                attenuate=1.0;

              new_image=AddNoiseImage(image,noise,attenuate,exception);
            }
          break;
        }
      if (LocaleCompare("normalize",option+1) == 0)
        {
          (void) NormalizeImage(image,exception);
          break;
        }
      break;
    }
    case 'o':
    {
      if (LocaleCompare("opaque",option+1) == 0)
        {
          PixelInfo
            target;

          (void) QueryColorCompliance(arg1,AllCompliance,&target,exception);
          (void) OpaquePaintImage(image,&target,&draw_info->fill,plus_alt_op,
               exception);
          break;
        }
      if (LocaleCompare("ordered-dither",option+1) == 0)
        {
          (void) OrderedPosterizeImage(image,arg1,exception);
          break;
        }
      break;
    }
    case 'p':
    {
      if (LocaleCompare("paint",option+1) == 0)
        {
          (void) ParseGeometry(arg1,&geometry_info);
          new_image=OilPaintImage(image,geometry_info.rho,geometry_info.sigma,
               exception);
          break;
        }
      if (LocaleCompare("polaroid",option+1) == 0)
        {
          const char
            *caption;

          double
            angle;

          if (IfPlusOp)
            {
              RandomInfo
              *random_info;

              random_info=AcquireRandomInfo();
              angle=22.5*(GetPseudoRandomValue(random_info)-0.5);
              random_info=DestroyRandomInfo(random_info);
            }
          else
            {
              SetGeometryInfo(&geometry_info);
              flags=ParseGeometry(arg1,&geometry_info);
              angle=geometry_info.rho;
            }
          caption=GetImageProperty(image,"caption",exception);
          new_image=PolaroidImage(image,draw_info,caption,angle,
            image->interpolate,exception);
          break;
        }
      if (LocaleCompare("posterize",option+1) == 0)
        {
          (void) ParseGeometry(arg1,&geometry_info);
          (void) PosterizeImage(image,(size_t) geometry_info.rho,
               quantize_info->dither,exception);
          break;
        }
      if (LocaleCompare("preview",option+1) == 0)
        {
          PreviewType
            preview_type;

          /* FUTURE: should be a 'Genesis' option?
             Option however is also in WandSettingOptionInfo()
          */
          preview_type=UndefinedPreview;
          if (IfNormalOp)
            preview_type=(PreviewType) ParseCommandOption(MagickPreviewOptions,
                  MagickFalse,arg1);
          new_image=PreviewImage(image,preview_type,exception);
          break;
        }
      if (LocaleCompare("profile",option+1) == 0)
        {
          const char
            *name;

          const StringInfo
            *profile;

          Image
            *profile_image;

          ImageInfo
            *profile_info;

          if (IfPlusOp)
            { /* Remove a profile from the image.  */
              (void) ProfileImage(image,arg1,(const unsigned char *)
                NULL,0,exception);
              break;
            }
          /* Associate a profile with the image.  */
          profile_info=CloneImageInfo(image_info);
          profile=GetImageProfile(image,"iptc");
          if (profile != (StringInfo *) NULL)
            profile_info->profile=(void *) CloneStringInfo(profile);
          profile_image=GetImageCache(profile_info,arg1,exception);
          profile_info=DestroyImageInfo(profile_info);
          if (profile_image == (Image *) NULL)
            {
              StringInfo
                *profile;

              profile_info=CloneImageInfo(image_info);
              (void) CopyMagickString(profile_info->filename,arg1,
                MaxTextExtent);
              profile=FileToStringInfo(profile_info->filename,~0UL,exception);
              if (profile != (StringInfo *) NULL)
                {
                  (void) ProfileImage(image,profile_info->magick,
                    GetStringInfoDatum(profile),(size_t)
                    GetStringInfoLength(profile),exception);
                  profile=DestroyStringInfo(profile);
                }
              profile_info=DestroyImageInfo(profile_info);
              break;
            }
          ResetImageProfileIterator(profile_image);
          name=GetNextImageProfile(profile_image);
          while (name != (const char *) NULL)
          {
            profile=GetImageProfile(profile_image,name);
            if (profile != (StringInfo *) NULL)
              (void) ProfileImage(image,name,GetStringInfoDatum(profile),
                (size_t) GetStringInfoLength(profile),exception);
            name=GetNextImageProfile(profile_image);
          }
          profile_image=DestroyImage(profile_image);
          break;
        }
      break;
    }
    case 'r':
    {
      if (LocaleCompare("radial-blur",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          new_image=RadialBlurImage(image,geometry_info.rho,
            geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("raise",option+1) == 0)
        {
          flags=ParsePageGeometry(image,arg1,&geometry,exception);
          if ((flags & SigmaValue) == 0)
            geometry.height=geometry.width;
          (void) RaiseImage(image,&geometry,normal_op,exception);
          break;
        }
      if (LocaleCompare("random-threshold",option+1) == 0)
        {
          (void) RandomThresholdImage(image,arg1,exception);
          break;
        }
      if (LocaleCompare("remap",option+1) == 0)
        {
          Image
            *remap_image;

          remap_image=GetImageCache(image_info,arg1,exception);
          if (remap_image == (Image *) NULL)
            break;
          (void) RemapImage(quantize_info,image,remap_image,exception);
          remap_image=DestroyImage(remap_image);
          break;
        }
      if (LocaleCompare("repage",option+1) == 0)
        {
          if (IfNormalOp)
            (void) ResetImagePage(image,arg1);
          else
            (void) ParseAbsoluteGeometry("0x0+0+0",&image->page);
          break;
        }
      if (LocaleCompare("resample",option+1) == 0)
        {
          /* FUTURE: remove blur arguemnt - no longer used */
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=geometry_info.rho;
          new_image=ResampleImage(image,geometry_info.rho,
            geometry_info.sigma,image->filter,image->blur,exception);
          break;
        }
      if (LocaleCompare("resize",option+1) == 0)
        {
          /* FUTURE: remove blur argument - no longer used */
          (void) ParseRegionGeometry(image,arg1,&geometry,exception);
          new_image=ResizeImage(image,geometry.width,geometry.height,
            image->filter,image->blur,exception);
          break;
        }
      if (LocaleCompare("roll",option+1) == 0)
        {
          (void) ParsePageGeometry(image,arg1,&geometry,exception);
          new_image=RollImage(image,geometry.x,geometry.y,exception);
          break;
        }
      if (LocaleCompare("rotate",option+1) == 0)
        {
          if (strchr(arg1,'>') != (char *) NULL)
            if (image->columns <= image->rows)
              break;
          if (strchr(arg1,'<') != (char *) NULL)
            if (image->columns >= image->rows)
              break;

          (void) ParseGeometry(arg1,&geometry_info);
          new_image=RotateImage(image,geometry_info.rho,exception);
          break;
        }
      break;
    }
    case 's':
    {
      if (LocaleCompare("sample",option+1) == 0)
        {
          (void) ParseRegionGeometry(image,arg1,&geometry,exception);
          new_image=SampleImage(image,geometry.width,geometry.height,
            exception);
          break;
        }
      if (LocaleCompare("scale",option+1) == 0)
        {
          (void) ParseRegionGeometry(image,arg1,&geometry,exception);
          new_image=ScaleImage(image,geometry.width,geometry.height,
            exception);
          break;
        }
      if (LocaleCompare("selective-blur",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & PercentValue) != 0)
            geometry_info.xi=(double) QuantumRange*geometry_info.xi/100.0;
          new_image=SelectiveBlurImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,geometry_info.psi,exception);
          break;
        }
      if (LocaleCompare("separate",option+1) == 0)
        {
          /* WARNING: This can generate multiple images! */
          /* FUTURE - this may be replaced by a "-channel" method */
          new_image=SeparateImages(image,exception);
          break;
        }
      if (LocaleCompare("sepia-tone",option+1) == 0)
        {
          double
            threshold;

          threshold=StringToDoubleInterval(arg1,(double) QuantumRange+1.0);
          new_image=SepiaToneImage(image,threshold,exception);
          break;
        }
      if (LocaleCompare("segment",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          (void) SegmentImage(image,image->colorspace,
            image_info->verbose,geometry_info.rho,geometry_info.sigma,
            exception);
          break;
        }
      if (LocaleCompare("set",option+1) == 0)
        {
          char
            *value;

          if (IfPlusOp)
            {
              if (LocaleNCompare(arg1,"registry:",9) == 0)
                (void) DeleteImageRegistry(arg1+9);
              else
                if (LocaleNCompare(arg1,"option:",7) == 0)
                  {
                    (void) DeleteImageOption(image_info,arg1+7);
                    (void) DeleteImageArtifact(image,arg1+7);
                  }
                else
                  (void) DeleteImageProperty(image,arg1);
              break;
            }
          value=InterpretImageProperties(image_info,image,arg2,
            exception);
          if (value == (char *) NULL)
            break;
          if (LocaleNCompare(arg1,"registry:",9) == 0)
            (void) SetImageRegistry(StringRegistryType,arg1+9,value,
              exception);
          else
            if (LocaleNCompare(arg1,"option:",7) == 0)
              {
                (void) SetImageOption(image_info,arg1+7,value);
                (void) SetImageArtifact(image,arg1+7,value);
              }
            else
              (void) SetImageProperty(image,arg1,value,exception);
          value=DestroyString(value);
          break;
        }
      if (LocaleCompare("shade",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          new_image=ShadeImage(image,normal_op,geometry_info.rho,
               geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("shadow",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=4.0;
          if ((flags & PsiValue) == 0)
            geometry_info.psi=4.0;
          new_image=ShadowImage(image,geometry_info.rho,
            geometry_info.sigma,image->bias,(ssize_t)
            ceil(geometry_info.xi-0.5),(ssize_t) ceil(geometry_info.psi-0.5),
            exception);
          break;
        }
      if (LocaleCompare("sharpen",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=0.0;
          new_image=SharpenImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,exception);
          break;
        }
      if (LocaleCompare("shave",option+1) == 0)
        {
          flags=ParsePageGeometry(image,arg1,&geometry,exception);
          new_image=ShaveImage(image,&geometry,exception);
          break;
        }
      if (LocaleCompare("shear",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=geometry_info.rho;
          new_image=ShearImage(image,geometry_info.rho,
            geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("sigmoidal-contrast",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=(double) QuantumRange/2.0;
          if ((flags & PercentValue) != 0)
            geometry_info.sigma=(double) QuantumRange*geometry_info.sigma/
              100.0;
          (void) SigmoidalContrastImage(image,normal_op,geometry_info.rho,
               geometry_info.sigma,
            exception);
          break;
        }
      if (LocaleCompare("sketch",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          new_image=SketchImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,geometry_info.psi,exception);
          break;
        }
      if (LocaleCompare("solarize",option+1) == 0)
        {
          (void) SolarizeImage(image,StringToDoubleInterval(arg1,(double)
                 QuantumRange+1.0),exception);
          break;
        }
      if (LocaleCompare("sparse-color",option+1) == 0)
        {
          SparseColorMethod
            method;

          char
            *arguments;

          method=(SparseColorMethod) ParseCommandOption(
            MagickSparseColorOptions,MagickFalse,arg1);
          arguments=InterpretImageProperties(image_info,image,arg2,exception);
          if (arguments == (char *) NULL)
            break;
          new_image=SparseColorOption(image,method,arguments,exception);
          arguments=DestroyString(arguments);
          break;
        }
      if (LocaleCompare("splice",option+1) == 0)
        {
          (void) ParseGravityGeometry(image,arg1,&geometry,exception);
          new_image=SpliceImage(image,&geometry,exception);
          break;
        }
      if (LocaleCompare("spread",option+1) == 0)
        {
          (void) ParseGeometry(arg1,&geometry_info);
          new_image=SpreadImage(image,geometry_info.rho,image->interpolate,
               exception);
          break;
        }
      if (LocaleCompare("statistic",option+1) == 0)
        {
          StatisticType
            type;

          type=(StatisticType) ParseCommandOption(MagickStatisticOptions,
            MagickFalse,arg1);
          (void) ParseGeometry(arg2,&geometry_info);
          new_image=StatisticImage(image,type,(size_t) geometry_info.rho,
            (size_t) geometry_info.sigma,exception);
          break;
        }
      if (LocaleCompare("strip",option+1) == 0)
        {
          (void) StripImage(image,exception);
          break;
        }
      if (LocaleCompare("swirl",option+1) == 0)
        {
          (void) ParseGeometry(arg1,&geometry_info);
          new_image=SwirlImage(image,geometry_info.rho,
            image->interpolate,exception);
          break;
        }
      break;
    }
    case 't':
    {
      if (LocaleCompare("threshold",option+1) == 0)
        {
          double
            threshold;

          if (!normal_op)
            threshold=(double) QuantumRange/2;
          else
            threshold=StringToDoubleInterval(arg1,(double) QuantumRange+1.0);
          (void) BilevelImage(image,threshold,exception);
          break;
        }
      if (LocaleCompare("thumbnail",option+1) == 0)
        {
          (void) ParseRegionGeometry(image,arg1,&geometry,exception);
          new_image=ThumbnailImage(image,geometry.width,geometry.height,
            exception);
          break;
        }
      if (LocaleCompare("tint",option+1) == 0)
        {
          new_image=TintImage(image,arg1,&draw_info->fill,exception);
          break;
        }
      if (LocaleCompare("transform",option+1) == 0)
        {
          /* DEPRECIATED */
          new_image=AffineTransformImage(image,&draw_info->affine,
            exception);
          break;
        }
      if (LocaleCompare("transparent",option+1) == 0)
        {
          PixelInfo
            target;

          (void) QueryColorCompliance(arg1,AllCompliance,&target,exception);
          (void) TransparentPaintImage(image,&target,(Quantum)
            TransparentAlpha,plus_alt_op,exception);
          break;
        }
      if (LocaleCompare("transpose",option+1) == 0)
        {
          new_image=TransposeImage(image,exception);
          break;
        }
      if (LocaleCompare("transverse",option+1) == 0)
        {
          new_image=TransverseImage(image,exception);
          break;
        }
      if (LocaleCompare("trim",option+1) == 0)
        {
          new_image=TrimImage(image,exception);
          break;
        }
      if (LocaleCompare("type",option+1) == 0)
        {
          /* Note that "type" setting should have already been defined */
          (void) SetImageType(image,image_info->type,exception);
          break;
        }
      break;
    }
    case 'u':
    {
      if (LocaleCompare("unique",option+1) == 0)
        {
          /* FUTURE: move to SyncImageSettings() and AcqireImage()??? */
          if (!normal_op)
            {
              (void) DeleteImageArtifact(image,"identify:unique-colors");
              break;
            }
          (void) SetImageArtifact(image,"identify:unique-colors","true");
          (void) SetImageArtifact(image,"verbose","true");
          break;
        }
      if (LocaleCompare("unique-colors",option+1) == 0)
        {
          new_image=UniqueImageColors(image,exception);
          break;
        }
      if (LocaleCompare("unsharp",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=1.0;
          if ((flags & PsiValue) == 0)
            geometry_info.psi=0.05;
          new_image=UnsharpMaskImage(image,geometry_info.rho,
            geometry_info.sigma,geometry_info.xi,geometry_info.psi,exception);
          break;
        }
      break;
    }
    case 'v':
    {
      if (LocaleCompare("verbose",option+1) == 0)
        {
          /* FUTURE: move to SyncImageSettings() and AcquireImage()???
             three places!   ImageArtifact   ImageOption  image_info->verbose
             Some how new images also get this artifact presumably here
          */
          (void) SetImageArtifact(image,option+1,
                           IfNormalOp ? "true" : "false" );
          break;
        }
      if (LocaleCompare("vignette",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          if ((flags & XiValue) == 0)
            geometry_info.xi=0.1*image->columns;
          if ((flags & PsiValue) == 0)
            geometry_info.psi=0.1*image->rows;
          new_image=VignetteImage(image,geometry_info.rho,geometry_info.sigma,
            image->bias,(ssize_t) ceil(geometry_info.xi-0.5),
            (ssize_t) ceil(geometry_info.psi-0.5),exception);
          break;
        }
      break;
    }
    case 'w':
    {
      if (LocaleCompare("wave",option+1) == 0)
        {
          flags=ParseGeometry(arg1,&geometry_info);
          if ((flags & SigmaValue) == 0)
            geometry_info.sigma=1.0;
          new_image=WaveImage(image,geometry_info.rho,geometry_info.sigma,
               image->interpolate,exception);
          break;
        }
      if (LocaleCompare("white-threshold",option+1) == 0)
        {
          (void) WhiteThresholdImage(image,arg1,exception);
          break;
        }
      break;
    }
    default:
      break;
  }
  /*
     Replace current image with any image that was generated
     and set image point to last image (so image->next is correct)
  */
  if (new_image != (Image *) NULL)
    ReplaceImageInListReturnLast(&image,new_image);

  return;
#undef image_info
#undef draw_info
#undef quantize_info
#undef image
#undef exception
#undef IfNormalOp
#undef IfPlusOp
#undef normal_op
#undef plus_alt_op
}

WandExport void CLISimpleOperatorImages(MagickCLI *cli_wand,
  const char *option, const char *arg1, const char *arg2)
{
  size_t
    n,
    i;

  assert(cli_wand != (MagickCLI *) NULL);
  assert(cli_wand->signature == WandSignature);
  assert(cli_wand->wand.signature == WandSignature);
  assert(cli_wand->wand.images != (Image *) NULL); /* images must be present */
  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);

#if !USE_WAND_METHODS
  /* FUTURE add appropriate tracing */
  i=0;
  n=GetImageListLength(cli_wand->wand.images);
  cli_wand->wand.images=GetFirstImageInList(cli_wand->wand.images);
  while (1) {
    i++;
    CLISimpleOperatorImage(cli_wand, option, arg1, arg2);
    if ( cli_wand->wand.images->next == (Image *) NULL )
      break;
    cli_wand->wand.images=cli_wand->wand.images->next;
  }
  assert( i == n );
  cli_wand->wand.images=GetFirstImageInList(cli_wand->wand.images);
#else
  MagickResetIterator(&cli_wand->wand);
  while ( MagickNextImage(&cli_wand->wand) != MagickFalse )
    CLISimpleOperatorImage(cli_wand, option, arg1, arg2);
  MagickResetIterator(&cli_wand->wand);
#endif
  return;
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+     C L I L i s t O p e r a t o r I m a g e s                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CLIListOperatorImages() applies a single operation that is apply to the
%  entire image list as a whole. The result is often a complete replacment
%  of the image list with a completely new list, or just a single image.
%
%  The format of the MogrifyImage method is:
%
%    void CLIListOperatorImages(MagickCLI *cli_wand,
%        const char *option, const char *arg1, const char *arg2)
%
%  A description of each parameter follows:
%
%    o cli_wand: structure holding settings to be applied
%
%    o option:  The option string for the operation
%
%    o arg1, arg2: optional argument strings to the operation
%
% NOTE: only "limit" currently uses two arguments.
%
% Example usage...
%
%  CLIListOperatorImages(cli_wand,MagickFalse,"-duplicate", "3",  NULL);
%  CLIListOperatorImages(cli_wand,MagickTrue, "+append",    NULL, NULL);
%
% Or for handling command line arguments EG: +/-option ["arg"]
%
%    cli_wand
%    argc,argv
%    i=index in argv
%
%    option_info = GetCommandOptionInfo(argv[i]);
%    count=option_info->type;
%    option_type=option_info->flags;
%
%    if ( (option_type & ListOperatorOptionFlag) != 0 )
%      CLIListOperatorImages(cli_wand,argv[i],
%          count>=1 ? argv[i+1] : (char *)NULL,
%          count>=2 ? argv[i+2] : (char *)NULL );
%    i += count+1;
%
*/
WandExport void CLIListOperatorImages(MagickCLI *cli_wand,
     const char *option,const char *arg1, const char *arg2)
{
  Image
    *new_images;

#define image_info      (cli_wand->wand.image_info)
#define images          (cli_wand->wand.images)
#define exception       (cli_wand->wand.exception)
#define draw_info       (cli_wand->draw_info)
#define quantize_info   (cli_wand->quantize_info)
#define IfNormalOp      (*option=='-')
#define IfPlusOp        (*option!='-')
#define normal_op       (IfNormalOp?MagickTrue:MagickFalse)

  assert(cli_wand != (MagickCLI *) NULL);
  assert(cli_wand->signature == WandSignature);
  assert(cli_wand->wand.signature == WandSignature);
  assert(images != (Image *) NULL);             /* images must be present */
  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);

  (void) SyncImagesSettings(image_info,images,exception);

  new_images=NewImageList();

  switch (*(option+1))
  {
    case 'a':
    {
      if (LocaleCompare("append",option+1) == 0)
        {
          new_images=AppendImages(images,normal_op,exception);
          break;
        }
      if (LocaleCompare("average",option+1) == 0)
        {
          /* DEPRECIATED - use -evaluate-sequence Mean */
          CLIListOperatorImages(cli_wand,"-evaluate-sequence","Mean",NULL);
          break;
        }
      break;
    }
    case 'c':
    {
      if (LocaleCompare("channel-ops",option+1) == 0)
        {
          new_images=ChannelOperationImage(*images,argv[i+1],exception);
          break;
        }
      if (LocaleCompare("clut",option+1) == 0)
        {
          Image
            *clut_image;

          /* FUTURE - make this a compose option, and thus can be used
             with layers compose or even compose last image over all other
             images.
          */
          new_images=RemoveFirstImageFromList(&images);
          clut_image=RemoveLastImageFromList(&images);
          /* FUTURE - produce Exception, rather than silent fail */
          if (clut_image == (Image *) NULL)
            break;
          (void) ClutImage(new_images,clut_image,images->interpolate,exception);
          clut_image=DestroyImage(clut_image);
          break;
        }
      if (LocaleCompare("coalesce",option+1) == 0)
        {
          new_images=CoalesceImages(images,exception);
          break;
        }
      if (LocaleCompare("combine",option+1) == 0)
        {
          /* FUTURE - this may be replaced by a 'channel' method */
          new_images=CombineImages(images,exception);
          break;
        }
      if (LocaleCompare("composite",option+1) == 0)
        {
          Image
            *mask_image,
            *source_image;

          RectangleInfo
            geometry;

          CompositeOperator
            compose;

          const char*
            value;

          value=GetImageOption(image_info,"compose");
          if (value != (const char *) NULL)
            compose=(CompositeOperator) ParseCommandOption(
                 MagickComposeOptions,MagickFalse,value);
          else
            compose=OverCompositeOp;  /* use Over not source_image->compose */

          new_images=RemoveFirstImageFromList(&images);
          source_image=RemoveFirstImageFromList(&images);
          /* FUTURE - produce Exception, rather than silent fail */
          if (source_image == (Image *) NULL)
            break;

          /* FUTURE - this should not be here! - should be part of -geometry */
          (void) TransformImage(&source_image,(char *) NULL,
            source_image->geometry,exception);

          SetGeometry(source_image,&geometry);
          (void) ParseAbsoluteGeometry(source_image->geometry,&geometry);
          GravityAdjustGeometry(new_images->columns,new_images->rows,
               new_images->gravity, &geometry);

          mask_image=RemoveFirstImageFromList(&images);
          if (mask_image != (Image *) NULL)
            { /* handle a third write mask image */
              if ((compose == DisplaceCompositeOp) ||
                  (compose == DistortCompositeOp))
                { /* Merge Y displacement into X displace/distort map. */
                  (void) CompositeImage(source_image,CopyGreenCompositeOp,
                    mask_image,0,0,exception);
                  mask_image=DestroyImage(mask_image);
                }
              else
                {
                  /*
                    Set a blending mask for the composition.
                  */
                  (void) NegateImage(mask_image,MagickFalse,exception);
                  (void) SetImageMask(new_images,mask_image,exception);
                  mask_image=DestroyImage(mask_image);
                }
            }
          (void) CompositeImage(new_images,compose,source_image,geometry.x,
                     geometry.y,exception);
          (void) SetImageMask(new_images,(Image *) NULL,exception);
          source_image=DestroyImage(source_image);
          break;
        }
      break;
    }
    case 'd':
    {
      if (LocaleCompare("deconstruct",option+1) == 0)
        {
          /* DEPRECIATED - use -layers CompareAny */
          CLIListOperatorImages(cli_wand,"-layer","CompareAny",NULL);
          break;
        }
      if (LocaleCompare("delete",option+1) == 0)
        {
          if (IfNormalOp)
            DeleteImages(&images,arg1,exception);
          else
            DeleteImages(&images,"-1",exception);
          break;
        }
      if (LocaleCompare("duplicate",option+1) == 0)
        {
          if (IfNormalOp)
            {
              const char
                *p;

              size_t
                number_duplicates;

              number_duplicates=(size_t) StringToLong(arg1);
              p=strchr(arg1,',');
              if (p == (const char *) NULL)
                new_images=DuplicateImages(images,number_duplicates,
                  "-1",exception);
              else
                new_images=DuplicateImages(images,number_duplicates,p,
                  exception);
            }
          else
            new_images=DuplicateImages(images,1,"-1",exception);
          AppendImageToList(&images, new_images);
          new_images=(Image *)NULL;
          break;
        }
      break;
    }
    case 'e':
    {
      if (LocaleCompare("evaluate-sequence",option+1) == 0)
        {
          MagickEvaluateOperator
            method;

          method=(MagickEvaluateOperator) ParseCommandOption(
            MagickEvaluateOptions,MagickFalse,arg1);
          new_images=EvaluateImages(images,method,exception);
          break;
        }
      break;
    }
    case 'f':
    {
      if (LocaleCompare("fft",option+1) == 0)
        {
          new_images=ForwardFourierTransformImage(images,normal_op,exception);
          break;
        }
      if (LocaleCompare("flatten",option+1) == 0)
        {
          /* REDIRECTED to use -layers flatten instead */
          CLIListOperatorImages(cli_wand,"-layer",option+1,NULL);
          break;
        }
      if (LocaleCompare("fx",option+1) == 0)
        {
          new_images=FxImage(images,arg1,exception);
          break;
        }
      break;
    }
    case 'h':
    {
      if (LocaleCompare("hald-clut",option+1) == 0)
        {
          /* FUTURE - make this a compose option (and thus layers compose )
             or perhaps compose last image over all other images.
          */
          Image
            *hald_image;

          new_images=RemoveFirstImageFromList(&images);
          hald_image=RemoveLastImageFromList(&images);
          if (hald_image == (Image *) NULL)
            break;
          (void) HaldClutImage(new_images,hald_image,exception);
          hald_image=DestroyImage(hald_image);
          break;
        }
      break;
    }
    case 'i':
    {
      if (LocaleCompare("ift",option+1) == 0)
        {
          Image
            *magnitude_image,
            *phase_image;

           magnitude_image=RemoveFirstImageFromList(&images);
           phase_image=RemoveFirstImageFromList(&images);
          /* FUTURE - produce Exception, rather than silent fail */
           if (phase_image == (Image *) NULL)
             break;
           new_images=InverseFourierTransformImage(magnitude_image,phase_image,
                   normal_op,exception);
           magnitude_image=DestroyImage(magnitude_image);
           phase_image=DestroyImage(phase_image);
          break;
        }
      if (LocaleCompare("insert",option+1) == 0)
        {
          Image
            *insert_image,
            *index_image;

          ssize_t
            index;

          index=0;
          insert_image=RemoveLastImageFromList(&images);
          if (IfNormalOp)
            index=(ssize_t) StringToLong(arg1);
          index_image=insert_image;
          if (index == 0)
            PrependImageToList(&images,insert_image);
          else if (index == (ssize_t) GetImageListLength(images))
            AppendImageToList(&images,insert_image);
          else
            {
               index_image=GetImageFromList(images,index-1);
               if (index_image == (Image *) NULL)
                 {
                   (void) ThrowMagickException(exception,GetMagickModule(),
                     OptionError,"NoSuchImage","'%s'",arg1);
                   break;
                 }
              InsertImageInList(&index_image,insert_image);
            }
          images=GetFirstImageInList(index_image);
          break;
        }
      break;
    }
    case 'l':
    {
      if (LocaleCompare("layers",option+1) == 0)
        {
          ImageLayerMethod
            method;

          method=(ImageLayerMethod) ParseCommandOption(MagickLayerOptions,
            MagickFalse,arg1);
          switch (method)
          {
            case CoalesceLayer:
            {
              new_images=CoalesceImages(images,exception);
              break;
            }
            case CompareAnyLayer:
            case CompareClearLayer:
            case CompareOverlayLayer:
            default:
            {
              new_images=CompareImagesLayers(images,method,exception);
              break;
            }
            case MergeLayer:
            case FlattenLayer:
            case MosaicLayer:
            case TrimBoundsLayer:
            {
              new_images=MergeImageLayers(images,method,exception);
              break;
            }
            case DisposeLayer:
            {
              new_images=DisposeImages(images,exception);
              break;
            }
            case OptimizeImageLayer:
            {
              new_images=OptimizeImageLayers(images,exception);
              break;
            }
            case OptimizePlusLayer:
            {
              new_images=OptimizePlusImageLayers(images,exception);
              break;
            }
            case OptimizeTransLayer:
            {
              OptimizeImageTransparency(images,exception);
              break;
            }
            case RemoveDupsLayer:
            {
              RemoveDuplicateLayers(&images,exception);
              break;
            }
            case RemoveZeroLayer:
            {
              RemoveZeroDelayLayers(&images,exception);
              break;
            }
            case OptimizeLayer:
            { /* General Purpose, GIF Animation Optimizer.  */
              new_images=CoalesceImages(images,exception);
              if (new_images == (Image *) NULL)
                break;
              images=DestroyImageList(images);
              images=OptimizeImageLayers(new_images,exception);
              if (images == (Image *) NULL)
                break;
              new_images=DestroyImageList(new_images);
              OptimizeImageTransparency(images,exception);
              (void) RemapImages(quantize_info,images,(Image *) NULL,
                exception);
              break;
            }
            case CompositeLayer:
            {
              Image
                *source;

              RectangleInfo
                geometry;

              CompositeOperator
                compose;

              const char*
                value;

              value=GetImageOption(image_info,"compose");
              compose=OverCompositeOp;  /* Default to Over */
              if (value != (const char *) NULL)
                compose=(CompositeOperator) ParseCommandOption(
                      MagickComposeOptions,MagickFalse,value);

              /* Split image sequence at the first 'NULL:' image. */
              source=images;
              while (source != (Image *) NULL)
              {
                source=GetNextImageInList(source);
                if ((source != (Image *) NULL) &&
                    (LocaleCompare(source->magick,"NULL") == 0))
                  break;
              }
              if (source != (Image *) NULL)
                {
                  if ((GetPreviousImageInList(source) == (Image *) NULL) ||
                      (GetNextImageInList(source) == (Image *) NULL))
                    source=(Image *) NULL;
                  else
                    { /* Separate the two lists, junk the null: image.  */
                      source=SplitImageList(source->previous);
                      DeleteImageFromList(&source);
                    }
                }
              if (source == (Image *) NULL)
                {
                  (void) ThrowMagickException(exception,GetMagickModule(),
                    OptionError,"MissingNullSeparator","layers Composite");
                  break;
                }
              /* Adjust offset with gravity and virtual canvas.  */
              SetGeometry(images,&geometry);
              (void) ParseAbsoluteGeometry(images->geometry,&geometry);
              geometry.width=source->page.width != 0 ?
                source->page.width : source->columns;
              geometry.height=source->page.height != 0 ?
               source->page.height : source->rows;
              GravityAdjustGeometry(images->page.width != 0 ?
                images->page.width : images->columns,
                images->page.height != 0 ? images->page.height :
                images->rows,images->gravity,&geometry);

              /* Compose the two image sequences together */
              CompositeLayers(images,compose,source,geometry.x,geometry.y,
                exception);
              source=DestroyImageList(source);
              break;
            }
          }
          break;
        }
      if (LocaleCompare("limit",option+1) == 0)
        {
          MagickSizeType
            limit;

          ResourceType
            type;

          type=(ResourceType) ParseCommandOption(MagickResourceOptions,
            MagickFalse,arg1);
          limit=MagickResourceInfinity;
          if (LocaleCompare("unlimited",arg2) != 0)
            limit=(MagickSizeType) SiPrefixToDoubleInterval(arg2,100.0);
          (void) SetMagickResourceLimit(type,limit);
          break;
        }
      break;
    }
    case 'm':
    {
      if (LocaleCompare("map",option+1) == 0)
        {
          /* DEPRECIATED use +remap */
          (void) RemapImages(quantize_info,images,(Image *) NULL,exception);
          break;
        }
      if (LocaleCompare("morph",option+1) == 0)
        {
          Image
            *morph_image;

          morph_image=MorphImages(images,StringToUnsignedLong(arg1),
            exception);
          if (morph_image == (Image *) NULL)
            break;
          images=DestroyImageList(images);
          images=morph_image;
          break;
        }
      if (LocaleCompare("mosaic",option+1) == 0)
        {
          /* REDIRECTED to use -layers mosaic instead */
          CLIListOperatorImages(cli_wand,"-layer",option+1,NULL);
          break;
        }
      break;
    }
    case 'p':
    {
      if (LocaleCompare("print",option+1) == 0)
        {
          char
            *string;

          string=InterpretImageProperties(image_info,images,arg1,
            exception);
          if (string == (char *) NULL)
            break;
          (void) FormatLocaleFile(stdout,"%s",string);
          string=DestroyString(string);
        }
      if (LocaleCompare("process",option+1) == 0)
        {
          char
            **arguments;

          int
            j,
            number_arguments;

          arguments=StringToArgv(arg1,&number_arguments);
          if (arguments == (char **) NULL)
            break;
          if (strchr(arguments[1],'=') != (char *) NULL)
            {
              char
                breaker,
                quote,
                *token;

              const char
                *arguments;

              int
                next,
                status;

              size_t
                length;

              TokenInfo
                *token_info;

              /*
                Support old style syntax, filter="-option arg".
              */
              length=strlen(arg1);
              token=(char *) NULL;
              if (~length >= (MaxTextExtent-1))
                token=(char *) AcquireQuantumMemory(length+MaxTextExtent,
                  sizeof(*token));
              if (token == (char *) NULL)
                break;
              next=0;
              arguments=arg1;
              token_info=AcquireTokenInfo();
              status=Tokenizer(token_info,0,token,length,arguments,"","=",
                "\"",'\0',&breaker,&next,&quote);
              token_info=DestroyTokenInfo(token_info);
              if (status == 0)
                {
                  const char
                    *argv;

                  argv=(&(arguments[next]));
                  (void) InvokeDynamicImageFilter(token,&images,1,&argv,
                    exception);
                }
              token=DestroyString(token);
              break;
            }
          (void) SubstituteString(&arguments[1],"-","");
          (void) InvokeDynamicImageFilter(arguments[1],&images,
            number_arguments-2,(const char **) arguments+2,exception);
          for (j=0; j < number_arguments; j++)
            arguments[j]=DestroyString(arguments[j]);
          arguments=(char **) RelinquishMagickMemory(arguments);
          break;
        }
      break;
    }
    case 'r':
    {
      if (LocaleCompare("remap",option+1) == 0)
        {
              (void) RemapImages(quantize_info,images,(Image *) NULL,exception);
          (void) RemapImages(quantize_info,images,(Image *) NULL,exception);
          break;
        }
      if (LocaleCompare("reverse",option+1) == 0)
        {
          ReverseImageList(&images);
          break;
        }
      break;
    }
    case 's':
    {
      if (LocaleCompare("smush",option+1) == 0)
        {
          Image
            *smush_image;

          ssize_t
            offset;

          offset=(ssize_t) StringToLong(arg1);
          smush_image=SmushImages(images,normal_op,offset,exception);
          if (smush_image == (Image *) NULL)
            break;
          images=DestroyImageList(images);
          images=smush_image;
          break;
        }
      if (LocaleCompare("swap",option+1) == 0)
        {
          Image
            *p,
            *q,
            *swap;

          ssize_t
            index,
            swap_index;

          index=-1;
          swap_index=-2;
          if (IfNormalOp)
            {
              GeometryInfo
                geometry_info;

              MagickStatusType
                flags;

              swap_index=(-1);
              flags=ParseGeometry(arg1,&geometry_info);
              index=(ssize_t) geometry_info.rho;
              if ((flags & SigmaValue) != 0)
                swap_index=(ssize_t) geometry_info.sigma;
            }
          p=GetImageFromList(images,index);
          q=GetImageFromList(images,swap_index);
          if ((p == (Image *) NULL) || (q == (Image *) NULL))
            {
              (void) ThrowMagickException(exception,GetMagickModule(),
                OptionError,"NoSuchImage","'%s'",images->filename);
              break;
            }
          if (p == q)
            break;
          swap=CloneImage(p,0,0,MagickTrue,exception);
          ReplaceImageInList(&p,CloneImage(q,0,0,MagickTrue,exception));
          ReplaceImageInList(&q,swap);
          images=GetFirstImageInList(q);
          break;
        }
      break;
    }
    case 'w':
    {
      if (LocaleCompare("write",option+1) == 0)
        {
          char
            key[MaxTextExtent];

          Image
            *write_images;

          ImageInfo
            *write_info;

          (void) FormatLocaleString(key,MaxTextExtent,"cache:%s",arg1);
          (void) DeleteImageRegistry(key);
          write_images=images;
          if (IfPlusOp)
            write_images=CloneImageList(images,exception);
          write_info=CloneImageInfo(image_info);
          (void) WriteImages(write_info,write_images,arg1,exception);
          write_info=DestroyImageInfo(write_info);
          if (IfPlusOp)
            write_images=DestroyImageList(write_images);
          break;
        }
      break;
    }
    default:
      break;
  }
  if (new_images == (Image *) NULL)
    return;

  if (images != (Image *) NULL)
    images=DestroyImageList(images);
  images=GetFirstImageInList(new_images);
  return;

#undef image_info
#undef images
#undef exception
#undef draw_info
#undef quantize_info
#undef IfNormalOp
#undef IfPlusOp
#undef normal_op
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   C L I S p e c i a l O p e r a t i o n s                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CLISpecialOption() Applies operations that may involve empty image lists
%  and or stacks of image lists or image_info settings.
%
%  The classic operators of this type is -read, and image stack operators,
%  which can be applied to empty image lists.
%
%  Note: unlike other Operators, these may involve other special 'option'
%  character prefixes, other than simply '-' or '+'.
%
%  The format of the CLISpecialOption method is:
%
%      void CLISpecialOption(MagickCLI *cli_wand,const char *option,
%           const char *arg)
%
%  A description of each parameter follows:
%
%    o cli_wand: the main CLI Wand to use.
%
%    o option: The special option (with any switch char) to process
%
%    o arg: Argument for option, if required
%
% Example Usage...
%
%  CLISpecialOperator(cli_wand,"-read", "rose:");
%
% Or for handling command line arguments EG: +/-option ["arg"]
%
%    cli_wand
%    argc,argv
%    i=index in argv
%
%    option_info = GetCommandOptionInfo(argv[i]);
%    count=option_info->type;
%    option_type=option_info->flags;
%
%    if ( (option_type & SpecialOptionFlag) != 0 )
%      CLISpecialOperator(cli_wand,argv[i],
%          count>=1 ? argv[i+1] : (char *)NULL);
%    i += count+1;
%
*/

WandExport void CLISpecialOperator(MagickCLI *cli_wand,
  const char *option, const char *arg)
{
#define exception       (cli_wand->wand.exception)

  assert(cli_wand != (MagickCLI *) NULL);
  assert(cli_wand->signature == WandSignature);
  assert(cli_wand->wand.signature == WandSignature);
  if (cli_wand->wand.debug != MagickFalse)
    (void) LogMagickEvent(WandEvent,GetMagickModule(),"%s",cli_wand->wand.name);

  if (LocaleCompare("(",option) == 0)
    {
      /* stack 'push' images */
      Stack
        *node;

      size_t
        size;

      const char*
        value;

      size=0;
      node=cli_wand->image_list_stack;
      for ( ; node != (Stack *)NULL; node=node->next)
        size++;
      if ( size >= MAX_STACK_DEPTH )
        {
          ThrowMagickException(exception,GetMagickModule(),
               OptionError,"ParenthesisNestedTooDeeply", option);
          return;
        }
      node=(Stack *) AcquireMagickMemory(sizeof(*node));
      if (node == (Stack *) NULL)
        {
          ThrowMagickException(exception,GetMagickModule(),
              ResourceLimitFatalError,"MemoryAllocationFailed", "PushImages");
          return;
        }
      node->data = (void *)cli_wand->wand.images;
      cli_wand->wand.images = NewImageList();
      node->next = cli_wand->image_list_stack;
      cli_wand->image_list_stack = node;

      /* handle respect-parenthesis */
      value=GetImageOption(cli_wand->wand.image_info,"respect-parenthesis");
      if (value != (const char *) NULL)
        option="{";
      else
        return;
    }
  if (LocaleCompare("{",option) == 0)
    {
      /* stack 'push' of image_info settings */
      Stack
        *node;

      size_t
        size;

      size=0;
      node=cli_wand->image_info_stack;
      for ( ; node != (Stack *)NULL; node=node->next)
        size++;
      if ( size >= MAX_STACK_DEPTH ) {
        ThrowMagickException(exception,GetMagickModule(),
             OptionError,"ParenthesisNestedTooDeeply", option);
        return;
      }
      node=(Stack *) AcquireMagickMemory(sizeof(*node));
      if (node == (Stack *) NULL) {
        ThrowMagickException(exception,GetMagickModule(),
            ResourceLimitFatalError,"MemoryAllocationFailed", "PushSettings");
        return;
      }

      node->data = (void *)cli_wand->wand.image_info;
      cli_wand->wand.image_info = CloneImageInfo(cli_wand->wand.image_info);
      if (cli_wand->wand.image_info == (ImageInfo *)NULL) {
        ThrowMagickException(exception,GetMagickModule(),
            ResourceLimitFatalError,"MemoryAllocationFailed", "PushSettings");
        cli_wand->wand.image_info = (ImageInfo *)node->data;
        node = (Stack *)RelinquishMagickMemory(node);
        return;
      }

      node->next = cli_wand->image_info_stack;
      cli_wand->image_info_stack = node;

      return;
    }
  if (LocaleCompare(")",option) == 0)
    {
      /* pop images from stack */
      Stack
        *node;

      const char*
        value;

      node = (void *)cli_wand->image_list_stack;
      if ( node == (Stack *)NULL)
        {
          ThrowMagickException(exception,GetMagickModule(),
               OptionError,"UnbalancedParenthesis", option);
          return;
        }
      cli_wand->image_list_stack = node->next;

      AppendImageToList((Image **)&node->data,cli_wand->wand.images);
      cli_wand->wand.images= (Image *)node->data;
      node = (Stack *)RelinquishMagickMemory(node);

      /* handle respect-parenthesis - of the previous 'push' settings */
      node = cli_wand->image_info_stack;
      if ( node != (Stack *)NULL)
        {
          value=GetImageOption((ImageInfo *)node->data,"respect-parenthesis");
          if (value != (const char *) NULL)
            option="}";
          else
            return;
        }
      else
        return;
    }
  if (LocaleCompare("}",option) == 0)
    {
      /* pop image_info settings from stack */
      Stack
        *node;

      node = (void *)cli_wand->image_info_stack;
      if ( node == (Stack *)NULL)
        {
          ThrowMagickException(exception,GetMagickModule(),
               OptionError,"UnbalancedParenthesis", option);
          return;
        }
      cli_wand->image_info_stack = node->next;

      (void) DestroyImageInfo(cli_wand->wand.image_info);
      cli_wand->wand.image_info = (ImageInfo *)node->data;
      node = (Stack *)RelinquishMagickMemory(node);

      GetDrawInfo(cli_wand->wand.image_info, cli_wand->draw_info);
      cli_wand->quantize_info=DestroyQuantizeInfo(cli_wand->quantize_info);
      cli_wand->quantize_info=AcquireQuantizeInfo(cli_wand->wand.image_info);

      return;
    }
  if (LocaleCompare("clone",option+1) == 0)
    {
      Image
        *new_images;

      if (*option == '+')
        arg="-1";
      if (IsSceneGeometry(arg,MagickFalse) == MagickFalse)
        {
          ThrowMagickException(exception,GetMagickModule(),
               OptionError,"InvalidArgument", "'%s': %s", option, arg);
          return;
        }
      if ( cli_wand->image_list_stack == (Stack *)NULL)
        {
          ThrowMagickException(exception,GetMagickModule(),
               OptionError,"UnableToCloneImage", option);
          return;
        }
      new_images = (Image *)cli_wand->image_list_stack->data;
      if (new_images == (Image *) NULL)
        {
          ThrowMagickException(exception,GetMagickModule(),
               OptionError,"UnableToCloneImage", option);
          return;
        }
      new_images=CloneImages(new_images,arg,exception);
      if (new_images == (Image *) NULL)
        {
          ThrowMagickException(exception,GetMagickModule(),
                OptionError,"NoSuchImage",option);
          return;
        }
      AppendImageToList(&cli_wand->wand.images,new_images);
      return;
    }
  if ( ( LocaleCompare("read",option+1) == 0 ) ||
       ( LocaleCompare("--",option) == 0 ) )
    {
#if !USE_WAND_METHODS
      Image *
        new_images;

      if (cli_wand->wand.image_info->ping != MagickFalse)
        new_images=PingImages(cli_wand->wand.image_info,arg,exception);
      else
        new_images=ReadImages(cli_wand->wand.image_info,arg,exception);
      AppendImageToList(&cli_wand->wand.images, new_images);
#else
      /* read images using MagickWand method - no ping */
      /* This is not working! - it locks up in a CPU loop! */
      MagickSetLastIterator(&cli_wand->wand);
      MagickReadImage(&cli_wand->wand,arg);
      MagickSetFirstIterator(&cli_wand->wand);
#endif
      return;
    }
  /* No-op options */
  if (LocaleCompare("noop",option+1) == 0)
    return;
  if (LocaleCompare("sans",option+1) == 0)
    return;
  if (LocaleCompare("sans0",option+1) == 0)
    return;
  if (LocaleCompare("sans2",option+1) == 0)
    return;
  if (LocaleCompare("list",option+1) == 0)
    {
      /* FUTURE: This should really be built into the MagickCore
         It does not actually require any wand or images at all!
       */
      ssize_t
        list;

      list=ParseCommandOption(MagickListOptions,MagickFalse, arg);
      switch (list)
      {
        case MagickCoderOptions:
        {
          (void) ListCoderInfo((FILE *) NULL,exception);
          break;
        }
        case MagickColorOptions:
        {
          (void) ListColorInfo((FILE *) NULL,exception);
          break;
        }
        case MagickConfigureOptions:
        {
          (void) ListConfigureInfo((FILE *) NULL,exception);
          break;
        }
        case MagickDelegateOptions:
        {
          (void) ListDelegateInfo((FILE *) NULL,exception);
          break;
        }
        case MagickFontOptions:
        {
          (void) ListTypeInfo((FILE *) NULL,exception);
          break;
        }
        case MagickFormatOptions:
          (void) ListMagickInfo((FILE *) NULL,exception);
          break;
        case MagickLocaleOptions:
          (void) ListLocaleInfo((FILE *) NULL,exception);
          break;
        case MagickLogOptions:
          (void) ListLogInfo((FILE *) NULL,exception);
          break;
        case MagickMagicOptions:
          (void) ListMagicInfo((FILE *) NULL,exception);
          break;
        case MagickMimeOptions:
          (void) ListMimeInfo((FILE *) NULL,exception);
          break;
        case MagickModuleOptions:
          (void) ListModuleInfo((FILE *) NULL,exception);
          break;
        case MagickPolicyOptions:
          (void) ListPolicyInfo((FILE *) NULL,exception);
          break;
        case MagickResourceOptions:
          (void) ListMagickResourceInfo((FILE *) NULL,exception);
          break;
        case MagickThresholdOptions:
          (void) ListThresholdMaps((FILE *) NULL,exception);
          break;
        default:
          (void) ListCommandOptions((FILE *) NULL,(CommandOption) list,
            exception);
          break;
      }
      return;
    }

#if 0
    // adjust stack handling
  // Other 'special' options this should handle
  //    "region" "list" "version"
  // It does not do "exit" however as due to its side-effect requirements
#endif
#if 0
  if ( ( process_flags & ProcessUnknownOptionError ) != 0 )
    MagickExceptionReturn(OptionError,"InvalidUseOfOption",option);
#endif

#undef image_info
#undef images
#undef exception
}

/* This file demonstrates the use of the modified libmad library on LPC1768
 * Changes to the library are documented in config.h.
 *
 * The main change is to use parts of the AHB RAM dedicated to the ethernet module,
 * because standard RAM is not sufficient for decoding.
 * This means the ethernet module cannot be used !!!
 *
 * It plays a file "test.mp3" from an external USB-drive/USB-stick. 
 * For wiring of the USB-connector, see mbed.org
 * ID3 decoding is not present at the moment and will cause warnings 
 * on stderr, and some short noise at the beginning or end of playback.
 *
 * Output is only for one channel on the DAC (AnalogOut) pin.
 * (For connections see datasheets/mbed.org)
 * This pin should be decoupled with a capacitor (100u or so) to remove DC.
 * The output current is high enough to drive small headphones or active 
 * speakers directly.
 *
 * Schematic: :-)		   
 *  MBED Pin 18 (AOut)  o--||--o  Headphone Left
 *	MBED Pin 1 (GND)	o------o  Headphone Common
 *
 * It has been tested with fixed bitrate MP3's up to 320kbps and VBR files.
 * 
 * The remaining RAM is very limited, so don't overuse it !
 * The MSCFileSystem library from mbed.org is needed !
 * Last warning: the main include file "mad.h" maybe not up to date,
 * use "decoder.h" for now
 * Have fun, 
 *   Andreas Gruen 
 */

#include "mbed.h"
# include "decoder.h"

static int decode(void);
FILE *fp;
#include "MSCFileSystem.h"
MSCFileSystem fs("usb");

volatile unsigned short dacbuf[1200];
volatile unsigned short *dac_s, *dac_e;

AnalogOut dac(p18);
Ticker dacclk;

void dacout(void)
{
  if(dac_s < dac_e)  
  {
    dac.write_u16(*dac_s++);
  }
}

int main(int argc, char *argv[])
{
  int ret;
  Timer t;

  dac_s = dac_e = dacbuf;
  dacclk.attach_us(dacout,23);

  fp = fopen("/usb/test.mp3","rb");
  if(!fp)  return(printf("no file\r\n"));
  t.start();
  ret = decode();
  t.stop();
  printf("decode ret=%d in %d ms\r\n",ret,t.read_ms());
  fclose(fp);

  return 0;
}

/*
 * This is the input callback. The purpose of this callback is to (re)fill
 * the stream buffer which is to be decoded. 
 */

static
enum mad_flow input(void *data,
            struct mad_stream *stream)
{
  static unsigned char strmbuff[2100];
  int ret;
  int rsz;
  unsigned char *bp;

  /* the remaining bytes from incomplete frames must be copied
  to the beginning of the new buffer !
  */
  bp = strmbuff;
  rsz = 0;
  if(stream->error == MAD_ERROR_BUFLEN||stream->buffer==NULL)
    {
    if(stream->next_frame!=NULL)   
      {   
         rsz = stream->bufend-stream->next_frame;
         memmove(strmbuff,stream->next_frame,rsz);
         bp = strmbuff+rsz;
      } 
    }

  ret = fread(bp,1,sizeof(strmbuff) - rsz,fp);

  if (!ret)
    return MAD_FLOW_STOP;


  mad_stream_buffer(stream, strmbuff, ret + rsz);

  return MAD_FLOW_CONTINUE;}


/*
 * The following utility routine performs simple rounding, clipping, and
 * scaling of MAD's high-resolution samples down to 16 bits. It does not
 * perform any dithering or noise shaping, which would be recommended to
 * obtain any exceptional audio quality. It is therefore not recommended to
 * use this routine if high-quality output is desired.
 */

static /*inline*/
signed int scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

/*
 * This is the output callback function. It is called after each frame of
 * MPEG audio data has been completely decoded. The purpose of this callback
 * is to output (or play) the decoded PCM audio.
 */

static
enum mad_flow output(void *data,
             struct mad_header const *header,
             struct mad_pcm *pcm)
{
  unsigned int nchannels, nsamples;
  mad_fixed_t const *left_ch, *right_ch;

  /* pcm->samplerate contains the sampling frequency */
  nchannels = pcm->channels;
  nsamples  = pcm->length;
  left_ch   = pcm->samples[0];
  right_ch  = pcm->samples[1];
  
  while(dac_s < dac_e) wait_us(10);
  dac_e = dacbuf;  // potential thread problem ??
  dac_s = dacbuf;  

  while (nsamples--) {
    signed int sample;

    /* output sample(s) in 16-bit signed little-endian PCM */

    sample = scale(*left_ch++);     
    *dac_e++ = sample  +32700;
    //putchar((sample >> 0) & 0xff);
    //putchar((sample >> 8) & 0xff);
	/* the second channel is not supported at the moment*/
    if (nchannels == 2) {
      sample = scale(*right_ch++);
      //putchar((sample >> 0) & 0xff);
      //putchar((sample >> 8) & 0xff);
    }
  }

  return MAD_FLOW_CONTINUE;
}

/*
 * This is the error callback function. It is called whenever a decoding
 * error occurs. The error is indicated by stream->error; the list of
 * possible MAD_ERROR_* errors can be found in the mad.h (or stream.h)
 * header file.
 */

static
enum mad_flow error_fn(void *data,
            struct mad_stream *stream,
            struct mad_frame *frame)
{
  /* ID3 tags will cause warnings and short noise, ignore it for the moment*/

  fprintf(stderr, "decoding error 0x%04x (%s)\n",
      stream->error, mad_stream_errorstr(stream));
      

  /* return MAD_FLOW_BREAK here to stop decoding (and propagate an error) */

  return MAD_FLOW_CONTINUE;
}

/*
 * This is the function called by main() above to perform all the decoding.
 * It instantiates a decoder object and configures it with the input,
 * output, and error callback functions above. A single call to
 * mad_decoder_run() continues until a callback function returns
 * MAD_FLOW_STOP (to stop decoding) or MAD_FLOW_BREAK (to stop decoding and
 * signal an error).
 */

static
int decode()
{
  struct mad_decoder decoder;
  int result;

  /* configure input, output, and error functions */

  mad_decoder_init(&decoder, NULL,
           input, 0 /* header */, 0 /* filter */, output,
           error_fn, 0 /* message */);

  /* start decoding */

  result = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);

  /* release the decoder */

  mad_decoder_finish(&decoder);

  return result;
}



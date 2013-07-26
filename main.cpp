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
 *    MBED Pin 1 (GND)    o------o  Headphone Common
 *
 * It has been tested with fixed bitrate MP3's up to 320kbps and VBR files.
 * 
 * The remaining RAM is very limited, so don't overuse it !
 * The MSCFileSystem library from mbed.org is needed !
 * Last warning: the main include file "mad.h" maybe not up to date,
 * use "decoder.h" for now
 * Have fun, 
 *   Andreas Gruen 
 * *** Version 3:  ***
 * moved another memory block into AHB RAM, giving more room for
 * stereo buffer.
 * moved content of decode() to main()
 * decoding    is now safe to be called multiple times (bug in older versions)
 * Output routine now fills stereo buffer, DAC output sums channels,
 * just for demonstration that stereo output could go here
 */

#include "mbed.h"
#include "decoder.h"
#include "TLV320.h"

DigitalOut led1(LED1), led2(LED2), led3(LED3), led4(LED4);
Serial pc(USBTX, USBRX);
FILE *fp;
#include "SDHCFileSystem.h"
SDFileSystem sd(p11, p12, p13, p14, "sd");
TLV320 audio(p9, p10, 0x34, p5, p6, p7, p8, p16); // I2S Codec

static enum mad_flow input(void *data,struct mad_stream *stream);
static enum mad_flow output(void *data,struct mad_header const *header,struct mad_pcm *pcm);
static enum mad_flow error_fn(void *data,struct mad_stream *stream,struct mad_frame *frame);

struct dacout_s {
  unsigned short l;
  unsigned short r;
 };

dacout_s dacbuf[1152];
dacout_s * volatile dac_s, * volatile dac_e;

void isr_audio () {
    int i;
    static int buf[4] = {0,0,0,0};

    for (i = 0; i < 4; i ++) {
        if (dac_s < dac_e) {
            buf[i] = (dac_s->l << 16) | dac_s->r;
            dac_s++;
            led3 = !led3;
        } else {
            // under flow
            if (i) {
                buf[i] = buf[i - 1];
            } else {
                buf[i] = buf[3];
            }
            led4 = !led4;
        }
    }
    audio.write(buf, 0, 4);
}

int main(int argc, char *argv[])
{
  int result;
  Timer t;
  struct mad_decoder decoder;

  pc.baud(115200);
  dac_s = dac_e = dacbuf;

    audio.power(0x02); // mic off
    audio.inputVolume(0.7, 0.7);
    audio.frequency(44100);
    audio.attach(&isr_audio);
    audio.start(TRANSMIT);

  while(1) {
      fp = fopen("/sd/filename.mp3","rb");
    
      if(!fp)  return(printf("file error\r\n"));
      fprintf(stderr,"decode start\r\n");
      led1 = 1;
      mad_decoder_init(&decoder, NULL,input, 0, 0, output,error_fn, 0);
      t.reset();
      t.start();
      result = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
      t.stop();
      fprintf(stderr,"decode ret=%d in %d ms\r\n",result,t.read_ms());
      led1 = 0;
      mad_decoder_finish(&decoder);
      fclose(fp);
    }
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

//  while(dac_s < dac_e) wait_us(1);
  while(dac_s < dac_e) {
    led2 = !led2;
  }
  dac_e = dacbuf;  // potential thread problem ??  no...
  dac_s = dacbuf;

  while (nsamples--) {
    signed int sample_l,sample_r;
    sample_l = scale(*left_ch);
    sample_r = scale(*right_ch);
//    dac_e->l = sample_l  +32768;
//    dac_e->r = sample_r  +32768;
    dac_e->l = sample_l;
    dac_e->r = sample_r;
    dac_e++;
    left_ch++;
    right_ch++;
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
/*
  fprintf(stderr, "decoding error 0x%04x (%s)\n",
      stream->error, mad_stream_errorstr(stream));
*/    

  /* return MAD_FLOW_BREAK here to stop decoding (and propagate an error) */

  return MAD_FLOW_CONTINUE;
}



#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h> 
#include <regex.h>
#include <time.h>
#include <sys/wait.h>
#include <signal.h>
#ifdef HAVE_LIBSDRPLAY_API
#include "sdrplay_api.h"
#include "sdrplay_api_dev.h"
#endif
#include "message_store.h"
#include "wav.h"
#include "iq_source.h"

void init_fir_filter1();
void sample_in_1(double sample_I,double sample_Q);
void init_fir2_wrapper();

///////////// NAVTEX/SITOR-B /////////////
#define NAVTEX_UPPER 518000
#define NAVTEX_LOWER 490000

//first software decimation factor)
#define DECIMATION1 4

//252000
#define IN_SAMPLE_RATE 252000.0 

#define H_DECIMATION_FACTOR 8

//2016000
#define H_SAMPLE_RATE (IN_SAMPLE_RATE*H_DECIMATION_FACTOR*1.0)

#define TEMP_BUFFER_SIZE 1024 // must be a multiple of 4!!!

//14000
#define FREQ_OFFSET (NAVTEX_UPPER-NAVTEX_LOWER)/2

//504000
#define FREQ_TUNER  ((NAVTEX_UPPER+NAVTEX_LOWER)/2)


short debug_mode;

struct iq_options iq_opt = { 0, 'A', 0, NULL, -1, 0, -1, 0, NULL, 0 };

// Set by SIGINT/SIGTERM: stop streaming and release the receiver cleanly.
volatile sig_atomic_t stop_requested = 0;
// Set when the receiver is unplugged or stops delivering samples.
volatile sig_atomic_t device_lost = 0;

// Seconds without new samples before the receiver is considered lost.
#define STREAM_TIMEOUT_S 15

static void on_stop_signal(int sig)
{
    (void) sig;
    stop_requested = 1;
}

int in_sample_rate =  (int)(IN_SAMPLE_RATE);
int wav_sample_rate = (int)(IN_SAMPLE_RATE);


int masterInitialised = 0;
int slaveUninitialised = 0;

pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t in_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t out_mutex=PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_LIBSDRPLAY_API
sdrplay_api_DeviceT *chosenDevice = NULL;
#endif


short *sample_buffer;
unsigned int s_buffer_size;
int in_idx, out_idx;


#define biasTflag ((unsigned char) iq_opt.bias_t)
#define selectedAntennaPort (iq_opt.antenna_port)



WavFile *fp;



void generate_random_filename(char *filename) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    strcpy(filename,"NT");
    for (int i = 2; i < 10; i++) {
        int key = rand() % (sizeof(charset) - 1); // Pick a random index from charset
        filename[i] = charset[key];
    }
    filename[10] = '\0'; // Null-terminate the string
    strcat(filename,".wav");
}


void PrepWav()
{
    char filename[80];
    generate_random_filename(filename);
    fp = wav_open(filename, WAV_OPEN_WRITE);
    wav_set_format(fp, WAV_FORMAT_PCM);
    wav_set_num_channels(fp, 2);
    wav_set_sample_rate(fp, wav_sample_rate);
    wav_set_sample_size(fp, sizeof(short));
}

void EndWav()
{
    wav_close(fp);
}



///////////// RING BUFFER (any source -> demodulator) /////////////

int iq_ring_init(void)
{
    s_buffer_size = in_sample_rate*2*8;   // *2 for I and Q; *8 for 8 seconds of buffer; s_buffer_size must be a multiple of 2
    sample_buffer = malloc(s_buffer_size*sizeof(short));
    in_idx  = 1;  // should not be zero!!
    out_idx = 1; // idem
    return sample_buffer != NULL ? 0 : -1;
}

void iq_ring_free(void)
{
    free(sample_buffer);
    sample_buffer = NULL;
}

void iq_push_short(const short *xi, const short *xq, unsigned int numSamples)
{
    unsigned int i; 
    unsigned int next_idx;

    //protect this function, since it can be called multiple times 
    pthread_mutex_lock(&mutex);

    pthread_mutex_lock(&in_mutex);
    next_idx = in_idx+1; 
    pthread_mutex_unlock(&in_mutex);

    next_idx %= s_buffer_size ;

    for (i=0;i<numSamples;i++)
    {
	sample_buffer[next_idx]=xi[i];
        next_idx++; 
        next_idx %= s_buffer_size ;

	sample_buffer[next_idx]=xq[i];
        next_idx++; 
        next_idx %= s_buffer_size ;
    }
    if(next_idx == 0)
    {
   	next_idx = s_buffer_size-1;
    }
    else
    {
	next_idx--;
    }

    //protect in_idx as it is also read by the output thread
    pthread_mutex_lock(&in_mutex);
    in_idx = next_idx;  
    pthread_mutex_unlock(&in_mutex);
    
    pthread_mutex_unlock(&mutex);
}

// RTL-SDR samples: unsigned 8 bit, I then Q. Scaled to the 16 bit range the
// rest of the chain was written for (the demodulator only looks at the phase).
void iq_push_u8(const unsigned char *iq, unsigned int num_bytes)
{
    short bi[4096], bq[4096];
    unsigned int pairs = num_bytes / 2;
    unsigned int done = 0;

    while (done < pairs)
    {
        unsigned int n = pairs - done;
        unsigned int i;
        if (n > 4096) n = 4096;
        for (i = 0; i < n; i++)
        {
            bi[i] = (short) ((iq[2*(done+i)]   * 2 - 255) * 64);
            bq[i] = (short) ((iq[2*(done+i)+1] * 2 - 255) * 64);
        }
        iq_push_short(bi, bq, n);
        done += n;
    }
}

void iq_demodulate_short(const short *interleaved_iq, unsigned int num_pairs)
{
    unsigned int i;
    for (i = 0; i < num_pairs; i++)
        sample_in_1((double) interleaved_iq[2*i], (double) interleaved_iq[2*i+1]);
    if (debug_mode) wav_write(fp, (void *) interleaved_iq, num_pairs);
}

/////// MAIN CONSUMER LOOP ///////
void consume_loop(void)
{
    int c_in_idx;
    unsigned int from, to, num_samples;
    int last_in_idx = in_idx;
    time_t last_sample_time = time(NULL);

    while(!stop_requested && !device_lost)
    {
	usleep(50000);
	pthread_mutex_lock(&in_mutex);
	c_in_idx = in_idx;
	pthread_mutex_unlock(&in_mutex);

        // Watchdog: the source must keep moving in_idx
        if (c_in_idx != last_in_idx)
        {
            last_in_idx = c_in_idx;
            last_sample_time = time(NULL);
        }
        else if (time(NULL) - last_sample_time > STREAM_TIMEOUT_S)
        {
            printf("No samples from the receiver for %d s - giving up\n", STREAM_TIMEOUT_S);
            device_lost = 1;
        }

	while(out_idx != c_in_idx)
	{
		from = out_idx+1;
		from %= s_buffer_size ;
		to = c_in_idx;
		if(from>to)
		{
			to=s_buffer_size-1;
		}
		num_samples=(to-from+1);  //one sample is an I or a Q value
		iq_demodulate_short(&sample_buffer[from], num_samples/2);
		out_idx = to;
	}
    }
}

#ifdef HAVE_LIBSDRPLAY_API
///////////// SDRPLAY SOURCE /////////////
void StreamACallback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    iq_push_short(xi, xq, numSamples);
}

void StreamBCallback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    if (reset)
        printf("sdrplay_api_StreamBCallback: numSamples=%d\n", numSamples);

    // Process stream callback data here - this callback will only be used in dual tuner mode

    return;
}

void EventCallback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext)
{
    switch(eventId)
    {
    case sdrplay_api_GainChange:
        printf("sdrplay_api_EventCb: %s, tuner=%s gRdB=%d lnaGRdB=%d systemGain=%.2f\n",
               "sdrplay_api_GainChange", (tuner == sdrplay_api_Tuner_A)? "sdrplay_api_Tuner_A":
               "sdrplay_api_Tuner_B", params->gainParams.gRdB, params->gainParams.lnaGRdB,
               params->gainParams.currGain);
        break;

    case sdrplay_api_PowerOverloadChange:
        printf("sdrplay_api_PowerOverloadChange: tuner=%s powerOverloadChangeType=%s\n",
               (tuner == sdrplay_api_Tuner_A)? "sdrplay_api_Tuner_A": "sdrplay_api_Tuner_B",
               (params->powerOverloadParams.powerOverloadChangeType ==
               sdrplay_api_Overload_Detected)? "sdrplay_api_Overload_Detected":
               "sdrplay_api_Overload_Corrected");
        // Send update message to acknowledge power overload message received
        sdrplay_api_Update(chosenDevice->dev, tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck,sdrplay_api_Update_Ext1_None);

        break;

    case sdrplay_api_RspDuoModeChange:
        printf("sdrplay_api_EventCb: %s, tuner=%s modeChangeType=%s\n",
               "sdrplay_api_RspDuoModeChange", (tuner == sdrplay_api_Tuner_A)?
               "sdrplay_api_Tuner_A": "sdrplay_api_Tuner_B",
               (params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterInitialised)?
               "sdrplay_api_MasterInitialised":
               (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveAttached)?
               "sdrplay_api_SlaveAttached":
               (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveDetached)?
               "sdrplay_api_SlaveDetached":
               (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveInitialised)?
               "sdrplay_api_SlaveInitialised":
               (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveUninitialised)?
               "sdrplay_api_SlaveUninitialised":
               (params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterDllDisappeared)?
               "sdrplay_api_MasterDllDisappeared":
               (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveDllDisappeared)?
               "sdrplay_api_SlaveDllDisappeared": "unknown type");

        if (params->rspDuoModeParams.modeChangeType == sdrplay_api_MasterInitialised)
        {
            masterInitialised = 1;
        }
        if (params->rspDuoModeParams.modeChangeType == sdrplay_api_SlaveUninitialised)
        {
            slaveUninitialised = 1;
        }
        break;

    case sdrplay_api_DeviceRemoved:
        printf("sdrplay_api_EventCb: %s\n", "sdrplay_api_DeviceRemoved");
        device_lost = 1;
        break;

    default:
        printf("sdrplay_api_EventCb: %d, unknown event\n", eventId);
        break;
    }
}



int sdrplay_source_run(void)
{
    int failed = 1;
    sdrplay_api_DeviceT devs[6];
    unsigned int ndev;
    int i;
    float ver = 0.0;
    sdrplay_api_ErrT err;
    sdrplay_api_DeviceParamsT *deviceParams = NULL;
    sdrplay_api_CallbackFnsT cbFns;
    sdrplay_api_RxChannelParamsT *chParams;
    int reqTuner = 0;
    int master_slave = 0;

    unsigned int chosenIdx = 0;
    
    printf("requested Tuner%c Mode=%s\n", (reqTuner == 0)? 'A': 'B', (master_slave == 0)?
               "Single_Tuner": "Master/Slave");
   
    // Open API//////////
    if ((err = sdrplay_api_Open()) != sdrplay_api_Success)
    {
        printf("sdrplay_api_Open failed %s\n", sdrplay_api_GetErrorString(err));
    }
    else
    {
        // Enable debug logging output////////
        //if ((err = sdrplay_api_DebugEnable(NULL, 1)) != sdrplay_api_Success)
        //{
        //    printf("sdrplay_api_DebugEnable failed %s\n", sdrplay_api_GetErrorString(err));
        //}

        // Check API versions match /////////////
        if ((err = sdrplay_api_ApiVersion(&ver)) != sdrplay_api_Success)
        {
            printf("sdrplay_api_ApiVersion failed %s\n", sdrplay_api_GetErrorString(err));
        }
	else
	{
            printf("sdrplay_api_ApiVersion: %f\n", SDRPLAY_API_VERSION);
	}
        if (ver != SDRPLAY_API_VERSION)
        {
            printf("API version don't match (local=%.2f dll=%.2f)\n", SDRPLAY_API_VERSION, ver);
            goto CloseApi;
        }

        // Lock API while device selection is performed /////////////
        sdrplay_api_LockDeviceApi();

        // Fetch list of available devices
        if ((err = sdrplay_api_GetDevices(devs, &ndev, sizeof(devs) /
               sizeof(sdrplay_api_DeviceT))) != sdrplay_api_Success)
        {
            printf("sdrplay_api_GetDevices failed %s\n", sdrplay_api_GetErrorString(err));
            goto UnlockDeviceAndCloseApi;
        }
        printf("MaxDevs=%d NumDevs=%d\n", sizeof(devs) / sizeof(sdrplay_api_DeviceT), ndev);
        if (ndev > 0)
        {
            for (i = 0; i < (int)ndev; i++)
            {
                if (devs[i].hwVer == SDRPLAY_RSPduo_ID)
                    printf("Dev%d: SerNo=%s hwVer=%d tuner=0x%.2x rspDuoMode=0x%.2x\n", i,
                       devs[i].SerNo, devs[i].hwVer , devs[i].tuner, devs[i].rspDuoMode);
                else
                    printf("Dev%d: SerNo=%s hwVer=%d tuner=0x%.2x\n", i, devs[i].SerNo,
                       devs[i].hwVer, devs[i].tuner);
            }

            // Choose device
            {
                // Pick first device of supported type
                for (i = 0; i < (int)ndev; i++)
                {
 		   if ( (devs[i].hwVer == SDRPLAY_RSPdx_ID) ||  (devs[i].hwVer == SDRPLAY_RSP1A_ID) || (devs[i].hwVer == SDRPLAY_RSPdxR2_ID) ||  (devs[i].hwVer == SDRPLAY_RSP1B_ID)    ||  (devs[i].hwVer == SDRPLAY_RSP2_ID) )
		   {
                    chosenIdx = i;
                    break;
		   }
                }
            }
            if (i == ndev)
            {
                printf("Couldn't find a suitable device to open - exiting\n");
                goto UnlockDeviceAndCloseApi;
            }
            printf("chosenDevice = %d\n", chosenIdx);
            chosenDevice = &devs[chosenIdx];

            // Select chosen device //////////
            if ((err = sdrplay_api_SelectDevice(chosenDevice)) != sdrplay_api_Success)
            {
                printf("sdrplay_api_SelectDevice failed %s\n", sdrplay_api_GetErrorString(err));
                goto UnlockDeviceAndCloseApi;
            }

            // Unlock API now that device is selected
            sdrplay_api_UnlockDeviceApi();

            // Get Device paramters ///////////////// 
            if ((err = sdrplay_api_GetDeviceParams(chosenDevice->dev, &deviceParams)) !=
               sdrplay_api_Success)
            {
                printf("sdrplay_api_GetDeviceParams failed %s\n",
                      sdrplay_api_GetErrorString(err));
                goto CloseApi;
            }
		else
		{
                printf("sdrplay_api_GetDeviceParams retrieved\n");
		}

            // Check for NULL pointers before changing settings
            if (deviceParams == NULL)
            {
                printf("sdrplay_api_GetDeviceParams returned NULL deviceParams pointer\n");
                goto CloseApi;
            }

            // Configure dev parameters  /////////////////////
            if (deviceParams->devParams != NULL)
            {
              // This will be NULL for slave devices, only the master can change these parameters
                // Only need to update non-default settings
                if (master_slave == 0)
                {
 		    deviceParams->devParams->mode = sdrplay_api_ISOCH; 
 		    //deviceParams->devParams->mode = sdrplay_api_BULK; 
                    deviceParams->devParams->fsFreq.fsHz = H_SAMPLE_RATE ;
	            if ((chosenDevice->hwVer == SDRPLAY_RSPdx_ID) || (chosenDevice->hwVer == SDRPLAY_RSPdxR2_ID))
		    {
			    switch (selectedAntennaPort) {
				  case 'A':
					deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_A;
				    break;
				  case 'B':
					deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_B;
				    break;
				  case 'C':
					deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_C;
				    break;
				  default:
					deviceParams->devParams->rspDxParams.antennaSel = sdrplay_api_RspDx_ANTENNA_A;
		 	    }
			    deviceParams->devParams->rspDxParams.rfNotchEnable=0;
			    deviceParams->devParams->rspDxParams.rfDabNotchEnable=1;
			    deviceParams->devParams->rspDxParams.biasTEnable = biasTflag;
		    }
	            if ((chosenDevice->hwVer == SDRPLAY_RSP1A_ID) || (chosenDevice->hwVer == SDRPLAY_RSP1B_ID))
		    {
			    deviceParams->devParams->rsp1aParams.rfNotchEnable=0;
			    deviceParams->devParams->rsp1aParams.rfDabNotchEnable=1;
			    deviceParams->rxChannelA->rsp1aTunerParams.biasTEnable=biasTflag;
		    }
	            if (chosenDevice->hwVer == SDRPLAY_RSP2_ID )
		    {
			    deviceParams->rxChannelA->rsp2TunerParams.amPortSel=sdrplay_api_Rsp2_AMPORT_1;
			    deviceParams->rxChannelA->rsp2TunerParams.antennaSel=sdrplay_api_Rsp2_ANTENNA_B;
			    deviceParams->rxChannelA->rsp2TunerParams.rfNotchEnable=0;
			    deviceParams->rxChannelA->rsp2TunerParams.biasTEnable=0;
		    }




            // Configure tuner parameters (depends on selected Tuner which set of parameters to use)
            chParams = deviceParams->rxChannelA;
            if (chParams != NULL)
            {


                chParams->tunerParams.loMode = sdrplay_api_LO_Auto;
                chParams->tunerParams.rfFreq.rfHz = (float)FREQ_TUNER; //(1/4 times sampling freq after decimation)
                chParams->tunerParams.bwType = sdrplay_api_BW_0_200;
                chParams->tunerParams.ifType = sdrplay_api_IF_Zero;
                //chParams->tunerParams.ifType = sdrplay_api_IF_0_450;
                chParams->tunerParams.gain.gRdB = 30;
                //chParams->tunerParams.gain.LNAstate = 3; // LNAstate3 = 9dB (gain reduction)
                //chParams->tunerParams.gain.LNAstate = 4; // LNAstate4 = 12dB (gain reduction)
		//chParams->tunerParams.gain.LNAstate = 5; // LNAstate5 = 15dB (gain reduction)
                //chParams->tunerParams.gain.LNAstate = 8; // LNAstate4 = 30dB (gain reduction)
                chParams->tunerParams.gain.LNAstate = 0; // LNAstate0 = 0dB  FOR USE WITH FURUNO NAVTEX ANTENNA
        	chParams->ctrlParams.decimation.enable=1;
		chParams->ctrlParams.decimation.decimationFactor=  H_DECIMATION_FACTOR;
                // set AGC
                //chParams->ctrlParams.agc.enable = sdrplay_api_AGC_DISABLE;
                chParams->ctrlParams.agc.enable = sdrplay_api_AGC_5HZ;
            }
            else
            {
                printf("sdrplay_api_GetDeviceParams returned NULL chParams pointer\n");
                goto CloseApi;
            }
        	printf("devParams->fsFreq.fsHz: %f \n", deviceParams->devParams->fsFreq.fsHz);
        	printf("rxChannelA->tunerParams.rfFreq.rfHz: %f \n", deviceParams->rxChannelA->tunerParams.rfFreq.rfHz);

                }
                else
                {
                    // Can't change Fs in master/slave mode
                printf("unexpected slave device config\n");
                goto CloseApi;
                }
            }


            // Assign callback functions to be passed to sdrplay_api_Init()
            cbFns.StreamACbFn = StreamACallback;
            cbFns.StreamBCbFn = StreamBCallback;
            cbFns.EventCbFn = EventCallback;



            if (iq_ring_init() != 0) { printf("out of memory\n"); goto CloseApi; }

	    //////////////////// LET'S START ///////////////////////////////////
            // Now we're ready to start by calling the initialisation function
            // This will configure the device and start streaming

            if ((err = sdrplay_api_Init(chosenDevice->dev, &cbFns, NULL)) != sdrplay_api_Success)
            {
                printf("sdrplay_api_Init failed %s\n", sdrplay_api_GetErrorString(err));
                if (err == sdrplay_api_StartPending) // This can happen if we're starting in master/slave mode as a slave and the master is not yet running
                {
                    while(1)
                    {
                        sleep(1);
                        if (masterInitialised) // Keep polling flag set in event callback until the master is initialised
                        {
                            // Redo call - should succeed this time
                            if ((err = sdrplay_api_Init(chosenDevice->dev, &cbFns, NULL)) != sdrplay_api_Success)
                            {
                                printf("sdrplay_api_Init failed %s\n", sdrplay_api_GetErrorString(err));
                            }
                        printf("Waiting for master to initialise\n");
                            goto CloseApi;
                        }
                    }
                }
                else
		{

		}
                    goto CloseApi;
            }


            consume_loop();
            failed = device_lost ? 1 : 0;
            if (stop_requested) printf("Stop requested - releasing the receiver\n");

            // Stop streaming, then release the device (make it available to other applications)
            if (!device_lost) sdrplay_api_Uninit(chosenDevice->dev);
            sdrplay_api_ReleaseDevice(chosenDevice);
            iq_ring_free();
        }
UnlockDeviceAndCloseApi:
        // Unlock API
        sdrplay_api_UnlockDeviceApi();

CloseApi:
        // Close API
        sdrplay_api_Close();
    }
    return failed;
}
#else
int sdrplay_source_run(void)
{
    printf("This build has no SDRplay support (the SDRplay API was not found when it was compiled)\n");
    return 1;
}
#endif /* HAVE_LIBSDRPLAY_API */







void init_dsp()
{
        init_fir_filter1();
}


static void usage(const char *prog)
{
    printf(
"Usage: %s [options]\n"
"  -s source   sdrplay | rtlsdr | file   (default: sdrplay when built in, else rtlsdr)\n"
"  -b          bias T on: power an active antenna through the coax\n"
"  -d          debug: also write the received I/Q to a wav file\n"
"  -v          print every received character (default on a terminal)\n"
"  -q          only print events: phasing, messages stored (default as a service)\n"
"SDRplay:\n"
"  -p port     antenna port A, B or C (RSPdx, RSPdx-R2)\n"
"RTL-SDR:\n"
"  -i index    device index (default 0)\n"
"  -S serial   pick the device by its serial number instead\n"
"  -D mode     direct sampling: 0 off, 1 I branch, 2 Q branch. Chosen from the model\n"
"              when not given: Q branch (Nooelec NESDR SMArt v5, RTL-SDR Blog V3...),\n"
"              off for an RTL-SDR Blog V4 (built-in upconverter) and when -u is given\n"
"  -u hertz    local oscillator of an external upconverter, e.g. 125000000\n"
"  -g gain     tuner gain in dB, e.g. 28.0 (default: automatic). Not used in direct sampling.\n"
"  -P ppm      crystal correction\n"
"Recording:\n"
"  -f file     decode a recording instead of a receiver: 252000 S/s centred on 504 kHz,\n"
"              .wav (16 bit, I and Q as the two channels, as written by -d)\n"
"              or raw unsigned 8 bit I/Q (as written by rtl_sdr)\n"
"  -t          play the recording at real speed\n", prog);
}

int main(int argc, char *argv[])
{
  int c;
  int failed;
  const char *source = NULL;

  opterr = 0;
  debug_mode=0;

  // line-buffered output, so the messages reach the journal as they happen
  setvbuf(stdout, NULL, _IOLBF, 0);
  // character by character only when somebody is watching
  navtex_verbose = isatty(STDOUT_FILENO) ? 1 : 0;

  while ((c = getopt (argc, argv, "bdhtvqs:p:i:S:D:u:g:P:f:")) != -1) {
    switch (c)
      {
      case 'b':
        iq_opt.bias_t = 1;
	printf("set BiasT active\n");
        break;
      case 'd':
        debug_mode=1;
	printf("debug mode enabled, a wav file with the received IQ samples will be produced\n");
        break;
      case 'v':
        navtex_verbose = 1;
        break;
      case 'q':
        navtex_verbose = 0;
        break;
      case 'h':
        usage(argv[0]);
        return 0;
      case 't':
        iq_opt.file_realtime = 1;
        break;
      case 's':
        source = optarg;
        break;
      case 'p':
	if(strcmp(optarg,"A")==0) 
		iq_opt.antenna_port = 'A';
        else if(strcmp(optarg,"B")==0)
		iq_opt.antenna_port = 'B';
        else if(strcmp(optarg,"C")==0)
		iq_opt.antenna_port = 'C';
        else if(strcmp(optarg,"Z")==0)
		iq_opt.antenna_port = 'C';
	else { printf("invalid antenna port option\n"); return 1; }
	printf("Antenna port selected: %c\n",iq_opt.antenna_port);
        break;
      case 'i':
        iq_opt.rtl_index = atoi(optarg);
        break;
      case 'S':
        iq_opt.rtl_serial = optarg;
        break;
      case 'D':
        iq_opt.rtl_direct_sampling = atoi(optarg);
        if (iq_opt.rtl_direct_sampling < 0 || iq_opt.rtl_direct_sampling > 2) { printf("invalid direct sampling mode (0, 1 or 2)\n"); return 1; }
        break;
      case 'u':
        iq_opt.rtl_upconverter_hz = atol(optarg);
        if (iq_opt.rtl_upconverter_hz < 0) { printf("invalid upconverter frequency\n"); return 1; }
        break;
      case 'g':
        iq_opt.rtl_gain_tenths = (int) (atof(optarg) * 10.0 + 0.5);
        if (iq_opt.rtl_gain_tenths < 0) { printf("invalid gain\n"); return 1; }
        break;
      case 'P':
        iq_opt.rtl_ppm = atoi(optarg);
        break;
      case 'f':
        iq_opt.file_path = optarg;
        break;
      case '?':
        if (optopt != 0 && strchr("spiSDugPf", optopt) != NULL)
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);
        usage(argv[0]);
        return 1;
      default:
        abort ();
    }
  }

   if (source == NULL)
   {
       if (iq_opt.file_path != NULL) source = "file";
       else
#ifdef HAVE_LIBSDRPLAY_API
           source = "sdrplay";
#else
           source = "rtlsdr";
#endif
   }
   init_dsp();
   init_fir2_wrapper();

   signal(SIGINT, on_stop_signal);
   signal(SIGTERM, on_stop_signal);

   if(debug_mode) { PrepWav(); }

   // A source only returns when it is asked to stop, when the receiver cannot
   // be opened or when it is lost. Exit instead of staying alive without
   // receiving: a service manager can then restart us.
   if (strcmp(source, "sdrplay") == 0)      failed = sdrplay_source_run();
   else if (strcmp(source, "rtlsdr") == 0)  failed = rtlsdr_source_run();
   else if (strcmp(source, "file") == 0)    failed = file_source_run();
   else { printf("unknown source '%s' (sdrplay, rtlsdr or file)\n", source); return 1; }

   if(debug_mode) { EndWav(); }

   if (failed && !stop_requested)
   {
	printf("Receiver not available - exiting\n");
	return 1;
   }
   return 0;
}

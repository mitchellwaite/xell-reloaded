#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <xenos/xenos.h>
#include <console/console.h>
#include <time/time.h>
#include <ppc/timebase.h>
#include <usb/usbmain.h>
#include <sys/iosupport.h>
#include <ppc/register.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xenon_nand/xenon_config.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_soc/xenon_io.h>
#include <xenon_sound/sound.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_smc/xenon_gpio.h>
#include <xb360/xb360.h>
#include <network/network.h>
#include <httpd/httpd.h>
#include <diskio/ata.h>
#include <elf/elf.h>
#include <version.h>
#include <byteswap.h>

#include "asciiart.h"
#include "config.h"
#include "file.h"
#include "tftp/tftp.h"

#include "log.h"

void do_asciiart()
{
	//char *p = asciiart;
	//while (*p)
	//	console_putch(*p++);
	//printf(asciitail);

   char * consoleRev = "";
   char * processorRev = "";

   printf("   XeLL Medallion BIOS v6.0, An Energy Star Ally\n   Copyright (C) 2007-2025 " BLAME ", Free60.org, Octal450, Et al.\n\n" );

   for(int i = 0; i<BIOSLOGO_HEIGHT; i++)
   {
      for(int j = 0; j<BIOSLOGO_WIDTH; j++)
      {
         if(bioslogo[(i * BIOSLOGO_WIDTH) + j])
         {
            console_pset(j, i, 0, 0, 255);
         }
      }
   }

   for(int i = 0; i<ENERGY_HEIGHT; i++)
   {
      for(int j = 0; j<ENERGY_WIDTH; j++)
      {
         char nrgpixel = energylogo[(i * ENERGY_WIDTH) + j];

         if(nrgpixel == 0xaa)
         {
            console_pset_right(j, i, 255, 255, 0);
         }
         else if(nrgpixel == 0x55)
         {
            console_pset_right(j, i, 0, 255, 0);
         }
      }
   }

   if (xenon_get_console_type() == 0) {
	    consoleRev = "Xenon";
       processorRev = "Waternoose";
    } else if (xenon_get_console_type() == 1) {
	    consoleRev = "Zephyr";
       processorRev = "Waternoose";
    } else if (xenon_get_console_type() == 2) {
	   consoleRev = "Falcon";
      processorRev = "Loki";
    } else if (xenon_get_console_type() == 3) {
	    consoleRev = "Jasper";
       processorRev = "Loki";
    } else if (xenon_get_console_type() == 4) {
	    consoleRev = "Trinity";
       processorRev = "Vejle";
    } else if (xenon_get_console_type() == 5) {
	    consoleRev = "Corona";
       processorRev = "Vejle";
    } else if (xenon_get_console_type() == 6) {
	    consoleRev = "Corona MMC";
       processorRev = "Vejle";
    } else if (xenon_get_console_type() == 7) {
	    consoleRev = "Winchester";
       processorRev = "Oban";
    } else if (xenon_get_console_type() == 8) {
	    consoleRev = "Winchester MMC";
       processorRev = "Oban";
    } else{
	    consoleRev = "Unknown";
       processorRev = "Unknown";
    }

   printf("Microsoft %s ACPI BIOS Revision " VERSION "\n\n", consoleRev);
   printf("Microsoft %s %08x 3.2GHz Processor\n", processorRev, mfspr(287));
   printf("Memory Test :  524288K OK\n\n");
}

void dumpana() {
	int i;
	for (i = 0; i < 0x100; ++i)
	{
		uint32_t v;
		xenon_smc_ana_read(i, &v);
		printf("0x%08x, ", (unsigned int)v);
		if ((i&0x7)==0x7)
			printf(" // %02x\n", (unsigned int)(i &~0x7));
	}
}

char FUSES[350]; /* this string stores the ascii dump of the fuses */
char CBLDV[17]; // 16 + terminate
char FGLDV[80];
int cbldvcount;
int fgldvcount;

unsigned char stacks[6][0x10000];

void reset_timebase_task()
{
	mtspr(284,0); // TBLW
	mtspr(285,0); // TBUW
	mtspr(284,0);
}

void synchronize_timebases()
{
	xenon_thread_startup();
	
	std((void*)0x200611a0,0); // stop timebase
	
	int i;
	for(i=1;i<6;++i){
		xenon_run_thread_task(i,&stacks[i][0xff00],(void *)reset_timebase_task);
		while(xenon_is_thread_task_running(i));
	}
	
	reset_timebase_task(); // don't forget thread 0
			
	std((void*)0x200611a0,0x1ff); // restart timebase
}
	
int main(){
	LogInit();
	int i;
   int rc = 0;

	printf("ANA Dump before Init:\n");
	dumpana();

	// linux needs this
	synchronize_timebases();
	
	// irqs preinit (SMC related)
	*(volatile uint32_t*)0xea00106c = 0x1000000;
	*(volatile uint32_t*)0xea001064 = 0x10;
	*(volatile uint32_t*)0xea00105c = 0xc000000;

	xenon_smc_start_bootanim();

	// flush console after each outputted char
	setbuf(stdout,NULL);

	xenos_init(VIDEO_MODE_AUTO);

	printf("ANA Dump after Init:\n");
	dumpana();

#ifdef SWIZZY_THEME
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_ORANGE); // Orange text on black bg
#elif defined XTUDO_THEME
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_PINK); // Pink text on black bg
#elif defined DEFAULT_THEME
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_GREY); // White text on blue bg
#else
	console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_GREEN); // Green text on black bg
#endif
	console_init();

   delay(2);

   console_clrscr();

  	do_asciiart();

	xenon_sound_init();

	xenon_make_it_faster(XENON_SPEED_FULL);

	// FIXME: Not initializing these devices here causes an interrupt storm in
	// linux.
   printf("\n");
	printf("Detecting Primary Master  ... ");
	xenon_ata_init();
   printf("Detecting Primary Slave   ... None\n");

#ifndef NO_DVD
	printf("Detecting Secondary Master... ");
	xenon_atapi_init();
   printf("Detecting Secondary Slave ... None\n");
#endif

	mount_all_devices();

   printf("\n");

	if (xenon_get_console_type() != REV_CORONA_PHISON) //Not needed for MMC type of consoles! ;)
	{
		printf("NAND init... ");
		sfcx_init();
		if (sfc.initialized != SFCX_INITIALIZED)
		{
			PRINT_ERR("failure, NAND features unavailable\n");
			delay(5);
		}
      else
      {
         PRINT_SUCCESS("success\n")
      }
	}

	xenon_config_init();

#ifndef NO_NETWORKING

	printf("Network init... ");

   console_close();
	rc = network_init();
   console_open();

   if(NETWORK_INIT_SUCCESS == rc)
   {
      PRINT_SUCCESS("success\n");
   }
   else if(NETWORK_INIT_STATIC_IP == rc)
   {
      PRINT_WARN("static ip assigned\n");
   }
   else
   {
      PRINT_ERR("failed\n");
   }

   

	printf("starting httpd server... ");

   console_close();
	httpd_start();
   console_open();

	PRINT_SUCCESS("success\n");

#endif

	printf("USB init... ");

   console_close();

	rc = usb_init();
	usb_do_poll();

   console_open();

   if(0 == rc)
   {
      PRINT_SUCCESS("success\n");
   }
   else
   {
      PRINT_ERR("failed\n");
   }

	/*int device_list_size = */ // findDevices();

#ifndef NO_PRINT_CONFIG
   printf("\n");
	printf("FUSES - write them down and keep them safe:\n");
	char *fusestr = FUSES;
   char *cbldvstr = CBLDV;
   char *fgldvstr = FGLDV;

	for (i=0; i<12; ++i){
		u64 line;
		unsigned int hi,lo;

		line=xenon_secotp_read_line(i);
		hi=line>>32;
		lo=line&0xffffffff;

      if( i % 2 == 0 )
      {
         fusestr += sprintf(fusestr, "%02d: %08x%08x ", i, hi, lo);
      }
      else
      {
		   fusestr += sprintf(fusestr, "%02d: %08x%08x\n", i, hi, lo);
      }
		
	   if (i >= 7)
      {
		   fgldvstr += sprintf(fgldvstr, "%08x%08x", hi, lo) + '\0';
	   }
      
      if (i == 2)
      {
         cbldvstr += sprintf(cbldvstr, "%08x%08x", hi, lo);
      }
	}

   for (i = 0; CBLDV[i] != '\0' ; ++i)
   {
      if ('f' == CBLDV[i])
      {
         cbldvcount = i + 1;
      }
   }

   for (i = 0; FGLDV[i] != '\0'; ++i)
   {
	   if ('f' == FGLDV[i])
      {
		   ++fgldvcount;
	   }
   }

	printf(FUSES);

   printf("\n");
   
   printf(" * CB LDV: %d\n", cbldvcount);
   printf(" * CF/CG LDV: %d\n", fgldvcount);

   print_cpu_dvd_keys();

   printf("P:0x%08X, X:0x%08X, D:0x%08X, V:0x%08X\n",xenon_get_PCIBridgeRevisionID(),xenon_get_XenosID(),xenon_get_DVE(),xenon_get_CPU_PVR());

	network_print_config();

   printf("\n");

#endif
	/* Stop logging and save it to first USB Device found that is writeable */
	LogDeInit();
	//extern char device_list[STD_MAX][10];

	//for (i = 0; i < device_list_size; i++)
	//{
	//	if (strncmp(device_list[i], "ud", 2) == 0)
	//	{
	//		char tmp[STD_MAX + 8];
	//		sprintf(tmp, "%sxell.log", device_list[i]);
	//		if (LogWriteFile(tmp) == 0)
	//			i = device_list_size;
	//	}
	//}
	
	// mount_all_devices();
	ip_addr_t fallback_address;
	ip4_addr_set_u32(&fallback_address, 0xC0A8015A); // 192.168.1.90

   printf("Scanning for boot devices...\n");

	for(;;){

      console_close();
      network_poll();
      usb_do_poll();
      mount_all_devices();
      console_open();

      console_clrline();

		fileloop();

		console_clrline();
	}

	return 0;
}


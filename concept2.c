#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include "inc/types.h"
#include "c2pm3_libusb.h"

#include "time.h"

#define FIELD_PULSE            (0)
#define FIELD_CADENCE          (1)
#define FIELD_SPEED            (2)
#define FIELD_DISTANCE         (3)
#define FIELD_DESIRED_POWER    (4)
#define FIELD_ENERGY           (5)
#define FIELD_TIME             (6)
#define FIELD_ACHIEVED_POWER   (7)

void
power_sensor_set_params(USHORT cum_pow, UCHAR cadence);

void
speed_sensor_set_speed(ULONG speed);


static void print_devs(libusb_device **devs)
{
   libusb_device *dev;
   int i = 0, j = 0;
   uint8_t path[8]; 

   while ((dev = devs[i++]) != NULL) 
   {
      struct libusb_device_descriptor desc;
      int r = libusb_get_device_descriptor(dev, &desc);
      if (r < 0)
      {
         fprintf(stderr, "failed to get device descriptor");
         return;
      }

      printf("%04x:%04x (bus %d, device %d)",
             desc.idVendor, desc.idProduct,
             libusb_get_bus_number(dev),
             libusb_get_device_address(dev));

      /*r = libusb_get_port_numbers(dev, path, sizeof(path));
      if (r > 0)
      {
         printf(" path: %d", path[0]);
         for (j = 1; j < r; j++)
         {
            printf(".%d", path[j]);
         }
      }*/
      printf("\n");
   }
}

static USHORT cum_pow = 0;
static ULONG last_time = 0;
static int cadence = 0;   

static void
C2PM3_message_callback(UCHAR status, UCHAR cmd, UCHAR *data, int len)
{
   /*printf("Got C2PM3 message...STATUS=0x%02x", status);
   if (cmd != 0)
   {
      printf(" CMD=0x%02x", cmd);
   }
   if (len != 0)
   {
      printf(" DATA=");
      int i;
      for(i = 0; i < len; i++)
      {
         printf("[0x%02x]", data[ i ]);
      }
   }
   printf("\n");*/

   struct timespec tp;
   ULONG time;
   ULONG elapsed;

   if (last_time == 0)
   {
      last_time = get_ms();  
   }

   int odo;

   int pace;
   int power;
   ULONG speed;

   switch(cmd)
   {
   case CSAFE_CMD_GET_ODOMETER:
     odo = data[ 0 ] + 
           (data[ 1 ] << 8) + 
           (data[ 2 ] << 16) + 
           (data[ 3 ] << 24);
      printf("Odomoeter reading = %d\n", odo);
      break;
   case CSAFE_CMD_GET_CADENCE:
      cadence = data[ 0 ] + 
                (data[ 1 ] << 8);


// cadence = 27;


      printf("Cadence reading = %d strokes/min\n", cadence);

      power_sensor_set_params(cum_pow, cadence);


      usleep(100000);
      c2pm3_send_command(CSAFE_CMD_GET_PACE, NULL, 0);
      break;
   case CSAFE_CMD_GET_PACE:
      pace = data[ 0 ] + 
             (data[ 1 ] << 8);

//pace = 5 * 60; /* 5 minutes per km = 12 km/hr */

      printf("Pace reading = %d seconds/km\n", pace);
      if (pace > 0)
      {
         speed = 1000 * (3600 / pace);
      }
      else
      {
         speed = 0;
      }
      speed_sensor_set_speed( speed );
      usleep(100000);
      c2pm3_send_command(CSAFE_CMD_GET_POWER, NULL, 0);
      break;
   case CSAFE_CMD_GET_POWER:
      power = data[ 0 ] + 
              (data[ 1 ] << 8);

// power = 190;


      printf("Power reading = %d Watts\n", power);

      time = get_ms();
      elapsed = time - last_time;
      last_time = time;

      cum_pow += (power * elapsed) / 1000;

      power_sensor_set_params(cum_pow, cadence);

      usleep(100000);
      c2pm3_send_command(CSAFE_CMD_GET_CADENCE, NULL, 0);
      break;
   }
}


int
main(int argc, char *argv[])
{
   libusb_device **devs; //pointer  to pointer of device, used to retrieve a list of devices
   libusb_context *ctx = NULL; //a libusb session
   libusb_device_handle *dev_handle;

   int r;
   size_t cnt;
   int actual;
   pthread_t tid;

   r = libusb_init(&ctx);
   if (r < 0)
   {
      printf("Initialise Error %d\n", r);
      return 1;
   }

   printf("libusb initialised\n");

   libusb_set_debug(ctx, 3); // set verbosity level to 3, as suggested in documentation

   cnt = libusb_get_device_list(ctx, &devs);
   if (cnt < 0)
   {
      printf("Error getting device list %d\n", (int)cnt);
      return 1;
   }
 
   printf("Found %d USB devices\n", (int)cnt);

   print_devs(devs);

   libusb_free_device_list(devs, 1);

   /*
   * Initialise ANT
   */
   ANT_init(ctx);

   printf("*** ANT Initialised\n");

   dev_handle = libusb_open_device_with_vid_pid(ctx, 0x17a4, 0x0002);
   if (dev_handle == NULL)
   {
      printf("Cannot open device\n");
      return 1;
   }

   printf("Device Opened\n");

   if (libusb_kernel_driver_active(dev_handle, 0) == 1)
   {
      printf("Kernel Driver Active!\n");
      if (libusb_detach_kernel_driver(dev_handle, 0) == 0)
      {
         printf("Kernel Driver Detached!\n");
      }
   }
      
   r = libusb_claim_interface(dev_handle, 0); // claim interface 0 (the first) of device
   if (r < 0)
   {
      printf("Cannot claim interface!\n");
      return 1;
   }
   printf("Interface claimed!\n");


   UCHAR in_buff[ 512 ];
   int i;

   create_c2pm3_libusb_input_handler(dev_handle,
                                     &C2PM3_message_callback,
                                     in_buff);

   
   c2pm3_send_command(CSAFE_CMD_GET_CADENCE, NULL, 0);
   while(TRUE)
   {
      sleep(2);
   }

   printf("HUH!!!\n");

   


   libusb_close(dev_handle);
   libusb_exit(ctx);

   return (0);

#if 0
   char str[ 512 ];
   BOOL running = FALSE;
   int size;
   

   /*
   * Get to running state
   */
   while(!running)
   {
      size = sprintf(str, "CM\r\n");
      res = write(USB, str, size);
      if (res != size)
      {
         printf("Failed to send [CM]\n");
      }
      else
      {
         usleep(1000000);
         res = read(USB, str, 512);
         if (res < 0)
         {
            printf("Failed to read response to [CM]\n");
         }
         else
         {
            printf("Response to [CM] == %s\n", str);
            if (strncmp(str, "RUN", 3) == 0)
            {
               running = TRUE;
            }
            else
            {
               for(i = 0; i < strlen(str); i++)
               {
                  printf("[0x%02x][%c]\n", str[ i ], str[ i ]);
               }
            }
         }
      }
   }

   int pulse;
   int cadence;
   int speed;
   int distance;
   int desired_power;
   int energy;
   char time_string[ 20 ];
   int achieved_power;

   USHORT cum_pow = 0;

   struct timespec tp;
   ULONG last_time;
   ULONG time;
   ULONG elapsed;

   last_time = get_ms();   

   while(TRUE)
   {
      size = sprintf(str, "ST\r\n");
      res = write(USB, str, size);
      if (res != size)
      {
         printf("Failed to send [ST]\n");
      }
      else
      {
         usleep(250000);
         res = read(USB, str, 512);
         if (res < 0)
         {
            printf("Failed to read response to [ST]\n");
         }
         else
         {
            printf(str);
            char *pch;
            pch = strtok(str, "\t");
            int field = 0;
            while(pch != NULL)
            {
               switch(field)
               {
               case FIELD_PULSE:
                  pulse = atoi(pch);
                  break;
               case FIELD_CADENCE:
                  cadence = atoi(pch);
                  break;
               case FIELD_SPEED:
                  speed = atoi(pch);
                  break;
               case FIELD_DISTANCE:
                  distance = atoi(pch);
                  break;
               case FIELD_DESIRED_POWER:
                  desired_power = atoi(pch);
                  break;
               case FIELD_ENERGY:
                  energy = atoi(pch);
                  break;
               case FIELD_TIME:
                  strcpy(time_string, pch);
                  break;
               case FIELD_ACHIEVED_POWER:
                  achieved_power = atoi(pch);
                  break;
               }
           
               pch = strtok(NULL, "\t");
               field++;
            }

            time = get_ms();
            elapsed = time - last_time;
            last_time = time;

            cum_pow += (achieved_power * elapsed) / 1000;

            speed_sensor_set_speed(speed * 100);
            power_sensor_set_params(cum_pow, cadence);
         }
      }
   }
     

   close(USB);

#endif   
}


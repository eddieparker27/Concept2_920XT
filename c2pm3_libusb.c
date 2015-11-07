#include "c2pm3_libusb.h"
#include "checksum.h"
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#define START_FLAG          (0xF1)
#define STOP_FLAG           (0xF2)

#define READ_BUFF_SIZE      (4096)

typedef struct
{
   libusb_device_handle *dev_handle;
   C2PM3_Message_Callback_Fn cb_fn;
   UCHAR *buffer;
} C2PM3_Input_Handler_Config;

static UCHAR rx_buff[ READ_BUFF_SIZE ];
static int rx_idx;
static pthread_t tid;
static C2PM3_Input_Handler_Config *ih_cfg = NULL;

static void*
input_handler_thread(void *arg);

int
create_c2pm3_libusb_input_handler(libusb_device_handle *dev_handle,
                                  C2PM3_Message_Callback_Fn cb_fn,
                                  UCHAR *buffer)
{
   int res = 0;
   ih_cfg = malloc(sizeof(C2PM3_Input_Handler_Config));
   ih_cfg->dev_handle = dev_handle;
   ih_cfg->cb_fn = cb_fn;
   ih_cfg->buffer = buffer;

   res = pthread_create(&tid, NULL, &input_handler_thread, NULL);
   if (res != 0)
   {
      printf("Can't create thread\n");
      return res;
   }

   return res;
}

BOOL
c2pm3_send_command(UCHAR cmd, UCHAR *data, UCHAR len)
{
   int r;
   int actual;
   BOOL res = FALSE;
   UCHAR buff[ 21 ];
   memset(buff, 0, 21);
   buff[ 0 ] = 0x01;
   buff[ 1 ] = START_FLAG;
   buff[ 2 ] = cmd;
   memcpy(buff + 3, data, len);
   buff[ 3 + len ] = calc_checksum(buff + 2, (1 + len));
   buff[ 4 + len ] = STOP_FLAG;

   if (ih_cfg != NULL)
   {
      r = libusb_bulk_transfer(ih_cfg->dev_handle, 
                               (4 | LIBUSB_ENDPOINT_OUT), 
                               buff, 
                               21, 
                               &actual, 
                               0);
      if ((r == 0) && (actual == 21))
      {
         res = TRUE;
      }
      else
      {
         printf("Writing FAILED! r = %d actual=%d\n", r, actual);
      }
   }
   return res;
}

int
copy_and_undo_byte_stuffing(UCHAR *dst, 
                            UCHAR *src,
                            int len)
{
   int new_len = 0;
   while(len > 0)
   {
      *dst = *src;
      if (*src == 0xF3)
      {
         src++;
         *dst &= ~0x03;
         *dst |= *src & 0x03;
         len--;
      }
      src++;
      dst++;
      len--;
      new_len++;
   }
   return new_len;
}
         


/* pthread to handle C2PM3 device input */
static void*
input_handler_thread(void *arg)
{
   int actual;
   int idx;
   int r;
   int start_flag_idx;
   int stop_flag_idx;
   int status_byte_idx;
   int command_idx;
   int checksum_idx;
   int data_len_idx;
   int data_len;
   UCHAR status_byte;
   UCHAR command;
   UCHAR frame[ 21 ];
   
   
   while(TRUE)
   {
      memset(rx_buff + rx_idx, 0, 21);
      r = libusb_bulk_transfer(ih_cfg->dev_handle, 
                               (3 | LIBUSB_ENDPOINT_IN), 
                               rx_buff + rx_idx, 
                               READ_BUFF_SIZE - rx_idx, 
                               &actual, 
                               0);
      if (r == 0)
      {
         /*printf("Just read %d bytes into idx %d\n", actual, rx_idx);
         for(idx = 0; idx < actual; idx++)
         {
            printf("[%02x]", rx_buff[ rx_idx + idx ]);
         }
         printf("\n");*/

         actual += rx_idx;
         idx = 0;
         while(idx < actual)
         {
            while((idx < actual) && 
                  (rx_buff[ idx ] != 0x01))
            {
               //printf("Skipping byte 0x%x\n", rx_buff[ idx ]);
               idx++;
            }
            if (rx_buff[ idx ] == 0x01)
            {
               start_flag_idx = idx;
               while((start_flag_idx < actual) && 
                     (rx_buff[ start_flag_idx ] != START_FLAG))
               {
                  //printf("Skipping byte 0x%x\n", rx_buff[ start_flag_idx ]);
                  start_flag_idx++;
               }
               if (rx_buff[ start_flag_idx ] == START_FLAG)
               {
                  stop_flag_idx = start_flag_idx;
                  while((stop_flag_idx < actual) && 
                       (rx_buff[ stop_flag_idx ] != STOP_FLAG))
                  {
                     //printf("Skipping byte 0x%x\n", rx_buff[ stop_flag_idx ]);
                     stop_flag_idx++;
                  }
                  if (rx_buff[ stop_flag_idx ] == STOP_FLAG)
                  {
                     /* Create candiate message with fixed byte stuffing */
                     int frame_len = stop_flag_idx - start_flag_idx - 1;
                     frame_len = copy_and_undo_byte_stuffing(frame, 
                                                             rx_buff + start_flag_idx + 1, 
                                                             frame_len);
                     checksum_idx = frame_len - 1;

                     UCHAR cs = calc_checksum(frame, frame_len);
                     if (cs == 0)
                     {
                        data_len = 0;
                        status_byte_idx = 0;
                        status_byte = frame[ status_byte_idx ];
                  
                        if (frame_len > 2)
                        {
                           command_idx = status_byte_idx + 1;
                           command = frame[ command_idx ];
                           data_len_idx = command_idx + 1;
                           data_len = frame[ data_len_idx ];
                           memcpy(ih_cfg->buffer, frame + data_len_idx + 1, data_len);
                        }                        
                        ih_cfg->cb_fn(status_byte, command, ih_cfg->buffer, data_len);
                        idx = stop_flag_idx + 1;                        
                     }
                     else
                     {
                        printf("Checksum failed!\n");
                     }                  
                  }
               }
            }
         }
      }
   }
}





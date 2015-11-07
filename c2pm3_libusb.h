#ifndef C2PM3_LIBUSB_H
#define C2PM3_LIBUSB_H

#include "inc/types.h"
#include </usr/include/libusb-1.0/libusb.h>

#define CSAFE_CMD_GET_STATUS                  (0x80)
#define CSAFE_CMD_GET_ODOMETER                (0x9B)
#define CSAFE_CMD_GET_PACE                    (0xA6)
#define CSAFE_CMD_GET_CADENCE                 (0xA7)
#define CSAFE_CMD_GET_POWER                   (0xB4)

#define CSAFE_UNITS_MILE                      (1)




typedef void (*C2PM3_Message_Callback_Fn)(UCHAR status, UCHAR cmd, UCHAR *data, int len);

int
create_c2pm3_libusb_input_handler(libusb_device_handle *dev_handle,
                                  C2PM3_Message_Callback_Fn cb_fn,
                                  UCHAR *buffer);

#endif


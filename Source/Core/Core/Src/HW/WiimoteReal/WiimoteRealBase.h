// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef WIIMOTE_COMM_H
#define WIIMOTE_COMM_H

#ifdef _WIN32
	#include <windows.h>
#elif defined(__APPLE__)
	#import <IOBluetooth/IOBluetooth.h>
#elif defined(__linux__) && HAVE_BLUEZ
	#include <bluetooth/bluetooth.h>
#endif

// Wiimote internal codes

// Communication channels
#define WM_OUTPUT_CHANNEL			0x11
#define WM_INPUT_CHANNEL			0x13

// The 4 most significant bits of the first byte of an outgoing command must be
// 0x50 if sending on the command channel and 0xA0 if sending on the interrupt
// channel. On Mac and Linux we use interrupt channel; on Windows, command.
#ifdef _WIN32
#define WM_SET_REPORT				0x50
#else
#define WM_SET_REPORT				0xA0
#endif

#define WM_BT_INPUT					0x01
#define WM_BT_OUTPUT				0x02

// LED bit masks
#define WIIMOTE_LED_NONE			0x00
#define WIIMOTE_LED_1				0x10
#define WIIMOTE_LED_2				0x20
#define WIIMOTE_LED_3				0x40
#define WIIMOTE_LED_4				0x80

// End Wiimote internal codes

// It's 23. NOT 32!
#define MAX_PAYLOAD					23
#define WIIMOTE_DEFAULT_TIMEOUT		1000

#ifdef _WIN32
// Available bluetooth stacks for Windows.
enum win_bt_stack_t
{
	MSBT_STACK_UNKNOWN,
	MSBT_STACK_MS,
	MSBT_STACK_BLUESOLEIL
};
#endif

#endif // WIIMOTE_COMM_H

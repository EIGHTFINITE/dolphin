// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>

#include "InputCommon/ControllerInterface/DInput/DInput.h"
#include "InputCommon/ControllerInterface/DInput/DInputKeyboardMouse.h"

	// (lower would be more sensitive) user can lower sensitivity by setting range
	// seems decent here ( at 8 ), I don't think anyone would need more sensitive than this
	// and user can lower it much farther than they would want to with the range
#define MOUSE_AXIS_SENSITIVITY   8

	// if input hasn't been received for this many ms, mouse input will be skipped
	// otherwise it is just some crazy value
#define DROP_INPUT_TIME          250

namespace ciface
{
namespace DInput
{

static const struct
{
	const BYTE        code;
	const char* const name;
} named_keys[] =
{
#include "InputCommon/ControllerInterface/DInput/NamedKeys.h" // NOLINT
};

// lil silly
static HWND m_hwnd;

void InitKeyboardMouse(IDirectInput8* const idi8, std::vector<Core::Device*>& devices, HWND _hwnd)
{
	m_hwnd = _hwnd;

	// mouse and keyboard are a combined device, to allow shift+click and stuff
	// if that's dumb, I will make a VirtualDevice class that just uses ranges of inputs/outputs from other devices
	// so there can be a separated Keyboard and mouse, as well as combined KeyboardMouse

	LPDIRECTINPUTDEVICE8 kb_device = nullptr;
	LPDIRECTINPUTDEVICE8 mo_device = nullptr;

	if (SUCCEEDED(idi8->CreateDevice( GUID_SysKeyboard, &kb_device, nullptr)))
	{
		if (SUCCEEDED(kb_device->SetDataFormat(&c_dfDIKeyboard)))
		{
			if (SUCCEEDED(kb_device->SetCooperativeLevel(nullptr, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
			{
				if (SUCCEEDED(idi8->CreateDevice( GUID_SysMouse, &mo_device, nullptr )))
				{
					if (SUCCEEDED(mo_device->SetDataFormat(&c_dfDIMouse2)))
					{
						if (SUCCEEDED(mo_device->SetCooperativeLevel(nullptr, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
						{
							devices.push_back(new KeyboardMouse(kb_device, mo_device));
							return;
						}
					}
				}
			}
		}
	}

	if (kb_device)
		kb_device->Release();
	if (mo_device)
		mo_device->Release();
}

KeyboardMouse::~KeyboardMouse()
{
	// kb
	m_kb_device->Unacquire();
	m_kb_device->Release();
	// mouse
	m_mo_device->Unacquire();
	m_mo_device->Release();
}

KeyboardMouse::KeyboardMouse(const LPDIRECTINPUTDEVICE8 kb_device, const LPDIRECTINPUTDEVICE8 mo_device)
	: m_kb_device(kb_device)
	, m_mo_device(mo_device)
{
	m_kb_device->Acquire();
	m_mo_device->Acquire();

	m_last_update = GetTickCount();

	ZeroMemory(&m_state_in, sizeof(m_state_in));

	// KEYBOARD
	// add keys
	for (u8 i = 0; i < sizeof(named_keys)/sizeof(*named_keys); ++i)
		AddInput(new Key(i, m_state_in.keyboard[named_keys[i].code]));

	// MOUSE
	// get caps
	DIDEVCAPS mouse_caps;
	ZeroMemory( &mouse_caps, sizeof(mouse_caps) );
	mouse_caps.dwSize = sizeof(mouse_caps);
	m_mo_device->GetCapabilities(&mouse_caps);
	// mouse buttons
	for (u8 i = 0; i < mouse_caps.dwButtons; ++i)
		AddInput(new Button(i, m_state_in.mouse.rgbButtons[i]));
	// mouse axes
	for (unsigned int i = 0; i < mouse_caps.dwAxes; ++i)
	{
		const LONG& ax = (&m_state_in.mouse.lX)[i];

		// each axis gets a negative and a positive input instance associated with it
		AddInput(new Axis(i, ax, (2==i) ? -1 : -MOUSE_AXIS_SENSITIVITY));
		AddInput(new Axis(i, ax, -(2==i) ? 1 : MOUSE_AXIS_SENSITIVITY));
	}
	// cursor, with a hax for-loop
	for (unsigned int i=0; i<4; ++i)
		AddInput(new Cursor(!!(i&2), (&m_state_in.cursor.x)[i/2], !!(i&1)));
}

void GetMousePos(ControlState* const x, ControlState* const y)
{
	POINT point = { 1, 1 };
	GetCursorPos(&point);
	// Get the cursor position relative to the upper left corner of the current window (separate or render to main)
	HWND hwnd = WindowFromPoint(point);
	DWORD processId;
	GetWindowThreadProcessId(hwnd, &processId);
	if (processId == GetCurrentProcessId())
	{
		ScreenToClient(hwnd, &point);

		// Get the size of the current window. (In my case Rect.top and Rect.left was zero.)
		RECT rect;
		GetClientRect(hwnd, &rect);
		// Width and height is the size of the rendering window
		unsigned int win_width = rect.right - rect.left;
		unsigned int win_height = rect.bottom - rect.top;

		// Return the mouse position as a range from -1 to 1
		*x = (ControlState)point.x / (ControlState)win_width * 2 - 1;
		*y = (ControlState)point.y / (ControlState)win_height * 2 - 1;
	}
	else
	{
		*x = (ControlState)1;
		*y = (ControlState)1;
	}
}

void KeyboardMouse::UpdateInput()
{
	DIMOUSESTATE2 tmp_mouse;

	// if mouse position hasn't been updated in a short while, skip a dev state
	DWORD cur_time = GetTickCount();
	if (cur_time - m_last_update > DROP_INPUT_TIME)
	{
		// set axes to zero
		ZeroMemory(&m_state_in.mouse, sizeof(m_state_in.mouse));
		// skip this input state
		m_mo_device->GetDeviceState(sizeof(tmp_mouse), &tmp_mouse);
	}

	m_last_update = cur_time;

	HRESULT kb_hr = m_kb_device->GetDeviceState(sizeof(m_state_in.keyboard), &m_state_in.keyboard);
	HRESULT mo_hr = m_mo_device->GetDeviceState(sizeof(tmp_mouse), &tmp_mouse);

	if (DIERR_INPUTLOST == kb_hr || DIERR_NOTACQUIRED == kb_hr)
		m_kb_device->Acquire();

	if (DIERR_INPUTLOST == mo_hr || DIERR_NOTACQUIRED == mo_hr)
		m_mo_device->Acquire();

	if (SUCCEEDED(kb_hr) && SUCCEEDED(mo_hr))
	{
		// need to smooth out the axes, otherwise it doesn't work for shit
		for (unsigned int i = 0; i < 3; ++i)
			((&m_state_in.mouse.lX)[i] += (&tmp_mouse.lX)[i]) /= 2;

		// copy over the buttons
		memcpy(m_state_in.mouse.rgbButtons, tmp_mouse.rgbButtons, sizeof(m_state_in.mouse.rgbButtons));

		// update mouse cursor
		GetMousePos(&m_state_in.cursor.x, &m_state_in.cursor.y);
	}
}

std::string KeyboardMouse::GetName() const
{
	return "Keyboard Mouse";
}

int KeyboardMouse::GetId() const
{
	// should this be -1, idk
	return 0;
}

std::string KeyboardMouse::GetSource() const
{
	return DINPUT_SOURCE_NAME;
}

// names
std::string KeyboardMouse::Key::GetName() const
{
	return named_keys[m_index].name;
}

std::string KeyboardMouse::Button::GetName() const
{
	return std::string("Click ") + char('0' + m_index);
}

std::string KeyboardMouse::Axis::GetName() const
{
	static char tmpstr[] = "Axis ..";
	tmpstr[5] = (char)('X' + m_index);
	tmpstr[6] = (m_range<0 ? '-' : '+');
	return tmpstr;
}

std::string KeyboardMouse::Cursor::GetName() const
{
	static char tmpstr[] = "Cursor ..";
	tmpstr[7] = (char)('X' + m_index);
	tmpstr[8] = (m_positive ? '+' : '-');
	return tmpstr;
}

// get/set state
ControlState KeyboardMouse::Key::GetState() const
{
	return (m_key != 0);
}

ControlState KeyboardMouse::Button::GetState() const
{
	return (m_button != 0);
}

ControlState KeyboardMouse::Axis::GetState() const
{
	return std::max(0.0, ControlState(m_axis) / m_range);
}

ControlState KeyboardMouse::Cursor::GetState() const
{
	return std::max(0.0, ControlState(m_axis) / (m_positive ? 1.0 : -1.0));
}

}
}

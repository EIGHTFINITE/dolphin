
#include "XInput.h"

namespace ciface
{
namespace XInput
{

static const struct
{
	const char* const name;
	const WORD bitmask;
} named_buttons[] = 
{
	{ "Button A", XINPUT_GAMEPAD_A },
	{ "Button B", XINPUT_GAMEPAD_B },
	{ "Button X", XINPUT_GAMEPAD_X },
	{ "Button Y", XINPUT_GAMEPAD_Y },
	{ "Pad N", XINPUT_GAMEPAD_DPAD_UP },
	{ "Pad S", XINPUT_GAMEPAD_DPAD_DOWN },
	{ "Pad W", XINPUT_GAMEPAD_DPAD_LEFT },
	{ "Pad E", XINPUT_GAMEPAD_DPAD_RIGHT },
	{ "Start", XINPUT_GAMEPAD_START },
	{ "Back", XINPUT_GAMEPAD_BACK },
	{ "Shoulder L", XINPUT_GAMEPAD_LEFT_SHOULDER },
	{ "Shoulder R", XINPUT_GAMEPAD_RIGHT_SHOULDER },
	{ "Thumb L", XINPUT_GAMEPAD_LEFT_THUMB },
	{ "Thumb R", XINPUT_GAMEPAD_RIGHT_THUMB }
};

static const char* const named_triggers[] =
{
	"Trigger L",
	"Trigger R"
};

static const char* const named_axes[] =
{
	"Left X",
	"Left Y",
	"Right X",
	"Right Y"
};

static const char* const named_motors[] =
{
	"Motor L",
	"Motor R"
};

void Init(std::vector<Core::Device*>& devices)
{
	XINPUT_CAPABILITIES caps;
	for (int i = 0; i != 4; ++i)
		if (ERROR_SUCCESS == XInputGetCapabilities(i, 0, &caps))
			devices.push_back(new Device(caps, i));
}

Device::Device(const XINPUT_CAPABILITIES& caps, u8 index)
	: m_index(index), m_subtype(caps.SubType)
{
	ZeroMemory(&m_state_out, sizeof(m_state_out));
	ZeroMemory(&m_current_state_out, sizeof(m_current_state_out));

	// XInputGetCaps seems to always claim all capabilities are supported
	// but I will leave all this stuff in, incase m$ fixes xinput up a bit

	// get supported buttons
	for (int i = 0; i != sizeof(named_buttons)/sizeof(*named_buttons); ++i)
	{
		if (named_buttons[i].bitmask & caps.Gamepad.wButtons)
			AddInput(new Button(i, m_state_in.Gamepad.wButtons));
	}

	// get supported triggers
	for (int i = 0; i != sizeof(named_triggers)/sizeof(*named_triggers); ++i)
	{
		//BYTE val = (&caps.Gamepad.bLeftTrigger)[i];	// should be max value / MSDN lies
		if ((&caps.Gamepad.bLeftTrigger)[i])
			AddInput(new Trigger(i, (&m_state_in.Gamepad.bLeftTrigger)[i], 255 ));
	}

	// get supported axes
	for (int i = 0; i != sizeof(named_axes)/sizeof(*named_axes); ++i)
	{
		//SHORT val = (&caps.Gamepad.sThumbLX)[i];  // xinput doesn't give the range / MSDN is a liar
		if ((&caps.Gamepad.sThumbLX)[i])
		{
			const SHORT& ax = (&m_state_in.Gamepad.sThumbLX)[i];

			// each axis gets a negative and a positive input instance associated with it
			AddInput(new Axis(i, ax, -32768));
			AddInput(new Axis(i, ax, 32767));
		}
	}

	// get supported motors
	for (int i = 0; i != sizeof(named_motors)/sizeof(*named_motors); ++i)
	{
		//WORD val = (&caps.Vibration.wLeftMotorSpeed)[i];	// should be max value / nope, more lies
		if ((&caps.Vibration.wLeftMotorSpeed)[i])
			AddOutput(new Motor(i, (&m_state_out.wLeftMotorSpeed)[i], 65535));
	}

	ClearInputState();
}

void Device::ClearInputState()
{
	ZeroMemory(&m_state_in, sizeof(m_state_in));	
}

std::string Device::GetName() const
{
	// why aren't these defined
	// subtype doesn't seem to work, I tested with 2 different arcade sticks, both were shown as gamepad
	// I'll leave it in anyway, maybe m$ will fix it

	switch (m_subtype)
	{
	case XINPUT_DEVSUBTYPE_GAMEPAD : return "Gamepad"; break;
	case 0x02 /*XINPUT_DEVSUBTYPE_WHEEL*/ :	return "Wheel"; break;
	case 0x03 /*XINPUT_DEVSUBTYPE_ARCADE_STICK*/ : return "Arcade Stick"; break;
	case 0x04 /*XINPUT_DEVSUBTYPE_FLIGHT_STICK*/ : return "Flight Stick"; break;
	case 0x05 /*XINPUT_DEVSUBTYPE_DANCE_PAD*/ : return "Dance Pad"; break;
	case 0x06 /*XINPUT_DEVSUBTYPE_GUITAR*/ :	return "Guitar"; break;
	case 0x08 /*XINPUT_DEVSUBTYPE_DRUM_KIT*/ : return "Drum Kit"; break;
	default : return "Device"; break;
	}
}

int Device::GetId() const
{
	return m_index;
}

std::string Device::GetSource() const
{
	return "XInput";
}

// Update I/O

bool Device::UpdateInput()
{
	return (ERROR_SUCCESS == XInputGetState(m_index, &m_state_in));
}

bool Device::UpdateOutput()
{
	// this if statement is to make rumble work better when multiple ControllerInterfaces are using the device
	// only calls XInputSetState if the state changed
	if (memcmp(&m_state_out, &m_current_state_out, sizeof(m_state_out)))
	{
		m_current_state_out = m_state_out;
		return (ERROR_SUCCESS == XInputSetState(m_index, &m_state_out));
	}
	else
	{
		return true;
	}
}

// GET name/source/id

std::string Device::Button::GetName() const
{
	return named_buttons[m_index].name;
}

std::string Device::Axis::GetName() const
{
	return std::string(named_axes[m_index]) + (m_range<0 ? '-' : '+');
}

std::string Device::Trigger::GetName() const
{
	return named_triggers[m_index];
}

std::string Device::Motor::GetName() const
{
	return named_motors[m_index];
}

// GET / SET STATES

ControlState Device::Button::GetState() const
{
	return (m_buttons & named_buttons[m_index].bitmask) > 0;
}

ControlState Device::Trigger::GetState() const
{
	return ControlState(m_trigger) / m_range;
}

ControlState Device::Axis::GetState() const
{
	return std::max( 0.0f, ControlState(m_axis) / m_range );
}

void Device::Motor::SetState(ControlState state)
{
	m_motor = (WORD)(state * m_range);
}

}
}

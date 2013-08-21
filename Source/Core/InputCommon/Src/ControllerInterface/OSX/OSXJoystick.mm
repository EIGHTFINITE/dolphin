#include <Foundation/Foundation.h>
#include <IOKit/hid/IOHIDLib.h>

#include "OSXJoystick.h"

#include <sstream>

namespace ciface
{
namespace OSX
{


Joystick::Joystick(IOHIDDeviceRef device, std::string name, int index)
	: m_device(device)
	, m_device_name(name)
	, m_index(index)
{
	// Buttons
	NSDictionary *buttonDict =
	 [NSDictionary dictionaryWithObjectsAndKeys:
	  [NSNumber numberWithInteger: kIOHIDElementTypeInput_Button],
		@kIOHIDElementTypeKey,
	  [NSNumber numberWithInteger: kHIDPage_Button],
		@kIOHIDElementUsagePageKey,
	  nil];

	CFArrayRef buttons = IOHIDDeviceCopyMatchingElements(m_device,
		(CFDictionaryRef)buttonDict, kIOHIDOptionsTypeNone);

	if (buttons)
	{
		for (int i = 0; i < CFArrayGetCount(buttons); i++)
		{
			IOHIDElementRef e =
			(IOHIDElementRef)CFArrayGetValueAtIndex(buttons, i);
			//DeviceElementDebugPrint(e, NULL);

			AddInput(new Button(e, m_device));
		}
		CFRelease(buttons);
	}

	// Axes
	NSDictionary *axisDict =
	[NSDictionary dictionaryWithObjectsAndKeys:
	 [NSNumber numberWithInteger: kIOHIDElementTypeInput_Misc],
		@kIOHIDElementTypeKey,
	 nil];

	CFArrayRef axes = IOHIDDeviceCopyMatchingElements(m_device,
		(CFDictionaryRef)axisDict, kIOHIDOptionsTypeNone);

	if (axes)
	{
		for (int i = 0; i < CFArrayGetCount(axes); i++)
		{
			IOHIDElementRef e =
			(IOHIDElementRef)CFArrayGetValueAtIndex(axes, i);
			//DeviceElementDebugPrint(e, NULL);

			if (IOHIDElementGetUsage(e) == kHIDUsage_GD_Hatswitch) {
				AddInput(new Hat(e, m_device, Hat::up));
				AddInput(new Hat(e, m_device, Hat::right));
				AddInput(new Hat(e, m_device, Hat::down));
				AddInput(new Hat(e, m_device, Hat::left));
			} else {
				AddAnalogInputs(new Axis(e, m_device, Axis::negative),
					new Axis(e, m_device, Axis::positive));
			}
		}
		CFRelease(axes);
	}
}

bool Joystick::UpdateInput()
{
	return true;
}

bool Joystick::UpdateOutput()
{
	return true;
}

std::string Joystick::GetName() const
{
	return m_device_name;
}

std::string Joystick::GetSource() const
{
	return "Input";
}

int Joystick::GetId() const
{
	return m_index;
}

ControlState Joystick::Button::GetState() const
{
	IOHIDValueRef value;
	if (IOHIDDeviceGetValue(m_device, m_element, &value) == kIOReturnSuccess)
		return IOHIDValueGetIntegerValue(value);
	else
		return 0;
}

std::string Joystick::Button::GetName() const
{
	std::ostringstream s;
	s << IOHIDElementGetUsage(m_element);
	return std::string("Button ") + s.str();
}

Joystick::Axis::Axis(IOHIDElementRef element, IOHIDDeviceRef device, direction dir)
	: m_element(element)
	, m_device(device)
	, m_direction(dir)
{
	// Need to parse the element a bit first
	std::string description("unk");

	int const usage = IOHIDElementGetUsage(m_element);
	switch (usage)
	{
	case kHIDUsage_GD_X:
		description = "X";
		break;
	case kHIDUsage_GD_Y:
		description = "Y";
		break;
	case kHIDUsage_GD_Z:
		description = "Z";
		break;
	case kHIDUsage_GD_Rx:
		description = "Rx";
		break;
	case kHIDUsage_GD_Ry:
		description = "Ry";
		break;
	case kHIDUsage_GD_Rz:
		description = "Rz";
		break;
	case kHIDUsage_GD_Wheel:
		description = "Wheel";
		break;
	case kHIDUsage_Csmr_ACPan:
		description = "Pan";
		break;
	default:
	{
		std::ostringstream s;
		s << usage;
		description = s.str();
		break;
	}
	}

	m_name = std::string("Axis ") + description;
	m_name.append((m_direction == positive) ? "+" : "-");

	m_neutral = (IOHIDElementGetLogicalMax(m_element) +
		IOHIDElementGetLogicalMin(m_element)) / 2.;
	m_scale = 1 / fabs(IOHIDElementGetLogicalMax(m_element) - m_neutral);
}

ControlState Joystick::Axis::GetState() const
{
	IOHIDValueRef value;

	if (IOHIDDeviceGetValue(m_device, m_element, &value) == kIOReturnSuccess)
	{
		// IOHIDValueGetIntegerValue() crashes when trying
		// to convert unusually large element values.
		if (IOHIDValueGetLength(value) > 2)
			return 0;

		float position = IOHIDValueGetIntegerValue(value);

		if (m_direction == positive && position > m_neutral)
			return (position - m_neutral) * m_scale;
		if (m_direction == negative && position < m_neutral)
			return (m_neutral - position) * m_scale;
	}

	return 0;
}

std::string Joystick::Axis::GetName() const
{
	return m_name;
}

Joystick::Hat::Hat(IOHIDElementRef element, IOHIDDeviceRef device, direction dir)
	: m_element(element)
	, m_device(device)
	, m_direction(dir)
{
	switch (dir) {
	case up:
		m_name = "Up";
		break;
	case right:
		m_name = "Right";
		break;
	case down:
		m_name = "Down";
		break;
	case left:
		m_name = "Left";
		break;
	default:
		m_name = "unk";
	}
}

ControlState Joystick::Hat::GetState() const
{
	IOHIDValueRef value;
	int position;

	if (IOHIDDeviceGetValue(m_device, m_element, &value) == kIOReturnSuccess)
	{
		position = IOHIDValueGetIntegerValue(value);

		switch (position) {
		case 0:
			if (m_direction == up)
				return 1;
			break;
		case 1:
			if (m_direction == up || m_direction == right)
				return 1;
			break;
		case 2:
			if (m_direction == right)
				return 1;
			break;
		case 3:
			if (m_direction == right || m_direction == down)
				return 1;
			break;
		case 4:
			if (m_direction == down)
				return 1;
			break;
		case 5:
			if (m_direction == down || m_direction == left)
				return 1;
			break;
		case 6:
			if (m_direction == left)
				return 1;
			break;
		case 7:
			if (m_direction == left || m_direction == up)
				return 1;
			break;
		};
	}

	return 0;
}

std::string Joystick::Hat::GetName() const
{
	return m_name;
}


}
}

// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>

#include "Common/CommonTypes.h"

// idk in case I wanted to change it to double or something, idk what's best
typedef double ControlState;

namespace ciface
{
namespace Core
{

// Forward declarations
class DeviceQualifier;

//
// Device
//
// A device class
//
class Device
{
public:
	class Input;
	class Output;

	//
	// Control
	//
	// Control includes inputs and outputs
	//
	class Control // input or output
	{
	public:
		virtual std::string GetName() const = 0;
		virtual ~Control() {}

		bool InputGateOn();

		virtual Input* ToInput() { return nullptr; }
		virtual Output* ToOutput() { return nullptr; }
	};

	//
	// Input
	//
	// An input on a device
	//
	class Input : public Control
	{
	public:
		// things like absolute axes/ absolute mouse position will override this
		virtual bool IsDetectable() { return true; }

		virtual ControlState GetState() const = 0;

		ControlState GetGatedState()
		{
			if (InputGateOn())
				return GetState();
			else
				return 0.0;
		}

		Input* ToInput() override { return this; }
	};

	//
	// Output
	//
	// An output on a device
	//
	class Output : public Control
	{
	public:
		virtual ~Output() {}

		virtual void SetState(ControlState state) = 0;

		void SetGatedState(ControlState state)
		{
			if (InputGateOn())
				SetState(state);
		}

		Output* ToOutput() override { return this; }
	};

	virtual ~Device();

	virtual std::string GetName() const = 0;
	virtual int GetId() const = 0;
	virtual std::string GetSource() const = 0;
	virtual void UpdateInput() {}

	const std::vector<Input*>& Inputs() const { return m_inputs; }
	const std::vector<Output*>& Outputs() const { return m_outputs; }

	Input* FindInput(const std::string& name) const;
	Output* FindOutput(const std::string& name) const;

protected:
	void AddInput(Input* const i);
	void AddOutput(Output* const o);

	class FullAnalogSurface : public Input
	{
	public:
		FullAnalogSurface(Input* low, Input* high)
			: m_low(*low), m_high(*high)
		{}

		ControlState GetState() const override
		{
			return (1 + m_high.GetState() - m_low.GetState()) / 2;
		}

		std::string GetName() const override
		{
			return m_low.GetName() + *m_high.GetName().rbegin();
		}

	private:
		Input& m_low;
		Input& m_high;
	};

	void AddAnalogInputs(Input* low, Input* high)
	{
		AddInput(low);
		AddInput(high);
		AddInput(new FullAnalogSurface(low, high));
		AddInput(new FullAnalogSurface(high, low));
	}

private:
	std::vector<Input*>  m_inputs;
	std::vector<Output*> m_outputs;
};

//
// DeviceQualifier
//
// Device qualifier used to match devices.
// Currently has ( source, id, name ) properties which match a device
//
class DeviceQualifier
{
public:
	DeviceQualifier() : cid(-1) {}
	DeviceQualifier(const std::string& _source, const int _id, const std::string& _name)
		: source(_source), cid(_id), name(_name) {}
	void FromDevice(const Device* const dev);
	void FromString(const std::string& str);
	std::string ToString() const;
	bool operator==(const DeviceQualifier& devq) const;
	bool operator==(const Device* const dev) const;

	std::string  source;
	int          cid;
	std::string  name;
};

class DeviceContainer
{
public:
	Device::Input* FindInput(const std::string& name, const Device* def_dev) const;
	Device::Output* FindOutput(const std::string& name, const Device* def_dev) const;

	const std::vector<Device*>& Devices() const { return m_devices; }
	Device* FindDevice(const DeviceQualifier& devq) const;
protected:
	std::vector<Device*> m_devices;
};

}
}

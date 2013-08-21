
#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <string>
#include <vector>

#include "Common.h"

// idk in case I wanted to change it to double or something, idk what's best
typedef float ControlState;

namespace ciface
{
namespace Core
{

// Forward declarations
class DeviceQualifier;

//
//		Device
//
// a device class
//
class Device
{
public:
	class Input;
	class Output;

	//
	//		Control
	//
	//  control includes inputs and outputs
	//
	class Control		// input or output
	{
	public:
		virtual std::string GetName() const = 0;
		virtual ~Control() {}

		virtual Input* ToInput() { return NULL; }
		virtual Output* ToOutput() { return NULL; }
	};

	//
	//		Input
	//
	// an input on a device
	//
	class Input : public Control
	{
	public:
		// things like absolute axes/ absolute mouse position will override this
		virtual bool IsDetectable() { return true; }

		virtual ControlState GetState() const = 0;

		Input* ToInput() { return this; }
	};

	//
	//		Output
	//
	// an output on a device
	//
	class Output : public Control
	{
	public:
		virtual ~Output() {}

		virtual void SetState(ControlState state) = 0;

		Output* ToOutput() { return this; }
	};

	virtual ~Device();

	virtual std::string GetName() const = 0;
	virtual int GetId() const = 0;
	virtual std::string GetSource() const = 0;
	virtual bool UpdateInput() = 0;
	virtual bool UpdateOutput() = 0;

	virtual void ClearInputState();

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

		ControlState GetState() const
		{
			return (1 + m_high.GetState() - m_low.GetState()) / 2;
		}

		std::string GetName() const
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
	std::vector<Input*>		m_inputs;
	std::vector<Output*>	m_outputs;
};

//
//		DeviceQualifier
//
// device qualifier used to match devices
// currently has ( source, id, name ) properties which match a device
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

	std::string		source;
	int				cid;
	std::string		name;
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

#endif

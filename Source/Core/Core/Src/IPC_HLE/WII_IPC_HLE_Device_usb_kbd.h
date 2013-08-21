// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _WII_IPC_HLE_DEVICE_USB_KBD_H_
#define _WII_IPC_HLE_DEVICE_USB_KBD_H_

class CWII_IPC_HLE_Device_usb_kbd : public IWII_IPC_HLE_Device
{
public:
	CWII_IPC_HLE_Device_usb_kbd(u32 _DeviceID, const std::string& _rDeviceName);
	virtual ~CWII_IPC_HLE_Device_usb_kbd();

	virtual bool Open(u32 _CommandAddress, u32 _Mode);
	virtual bool Close(u32 _CommandAddress, bool _bForce);
	virtual bool Write(u32 _CommandAddress);
	virtual bool IOCtl(u32 _CommandAddress);
	virtual u32 Update();

private:
	enum
	{
		MSG_KBD_CONNECT = 0,
		MSG_KBD_DISCONNECT,
		MSG_EVENT
	};

	#pragma pack(push, 1)
	struct SMessageData
	{
		u32 MsgType;
		u32 Unk1;
		u8 Modifiers;
		u8 Unk2;
		u8 PressedKeys[6];

		SMessageData(u32 _MsgType, u8 _Modifiers, u8 *_PressedKeys) {
			MsgType = Common::swap32(_MsgType);
			Unk1 = 0; // swapped
			Modifiers = _Modifiers;
			Unk2 = 0;

			if (_PressedKeys) // Doesn't need to be in a specific order
				memcpy(PressedKeys, _PressedKeys, sizeof(PressedKeys));
			else
				memset(PressedKeys, 0, sizeof(PressedKeys));
		}
	};
	#pragma pack(pop)
	std::queue<SMessageData> m_MessageQueue;

	bool m_OldKeyBuffer[256];
	u8 m_OldModifiers;

	virtual bool IsKeyPressed(int _Key);

	// This stuff should probably die
	enum
	{
		KBD_LAYOUT_QWERTY = 0,
		KBD_LAYOUT_AZERTY
	};
	int m_KeyboardLayout;
	static u8 m_KeyCodesQWERTY[256];
	static u8 m_KeyCodesAZERTY[256];
};

#endif // _WII_IPC_HLE_DEVICE_USB_KBD_H_

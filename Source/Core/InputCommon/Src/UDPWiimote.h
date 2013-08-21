#ifndef UDPWIIMOTE_H
#define UDPWIIMOTE_H

#include "Common.h"
#include <string>

#define UDPWM_B1 (1<<0)
#define UDPWM_B2 (1<<1)
#define UDPWM_BA (1<<2)
#define UDPWM_BB (1<<3)
#define UDPWM_BP (1<<4)
#define UDPWM_BM (1<<5)
#define UDPWM_BH (1<<6)
#define UDPWM_BU (1<<7)
#define UDPWM_BD (1<<8)
#define UDPWM_BL (1<<9)
#define UDPWM_BR (1<<10)
#define UDPWM_SK (1<<11)
#define UDPWM_NC (1<<0)
#define UDPWM_NZ (1<<1)

class UDPWiimote
{
public:
	UDPWiimote(const char * port, const char * name, int index);
	virtual ~UDPWiimote();
	void getAccel(float &x, float &y, float &z);
	u32 getButtons();
	void getNunchuck(float &x, float &y, u8 &mask);
	void getIR(float &x, float &y);
	void getNunchuckAccel(float &x, float &y, float &z);
	int getErrNo() {return err;};
	const char * getPort();
	void changeName(const char * name);

	void mainThread();
private:
	std::string port,displayName;
	int pharsePacket(u8 * data, size_t size);
	struct _d; //using pimpl because Winsock2.h doesn't have include guards -_-
	_d *d; 
	double x,y,z;
	double naX,naY,naZ;
	double nunX,nunY;
	double pointerX,pointerY;
	u8 nunMask;
	u32 mask;
	u16 bcastMagic;
	int err;
	int index;
	int int_port;
	static int noinst;
	void broadcastPresence();
	void broadcastIPv4(const void * data, size_t size);
	void broadcastIPv6(const void * data, size_t size);
	void initBroadcastIPv4();
	void initBroadcastIPv6();
};
#endif

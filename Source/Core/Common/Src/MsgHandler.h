// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _MSGHANDLER_H_
#define _MSGHANDLER_H_

#include <string>

// Message alerts
enum MSG_TYPE
{
	INFORMATION,
	QUESTION,
	WARNING,
	CRITICAL
};

typedef bool (*MsgAlertHandler)(const char* caption, const char* text, 
                                bool yes_no, int Style);
typedef std::string (*StringTranslator)(const char* text);

void RegisterMsgAlertHandler(MsgAlertHandler handler);
void RegisterStringTranslator(StringTranslator translator);

extern bool MsgAlert(bool yes_no, int Style, const char* format, ...)
#ifdef __GNUC__
	__attribute__((format(printf, 3, 4)))
#endif
	;
void SetEnableAlert(bool enable);

#ifndef GEKKO
#ifdef _WIN32
	#define SuccessAlert(format, ...) MsgAlert(false, INFORMATION, format, __VA_ARGS__) 
	#define PanicAlert(format, ...) MsgAlert(false, WARNING, format, __VA_ARGS__) 
	#define PanicYesNo(format, ...) MsgAlert(true, WARNING, format, __VA_ARGS__) 
	#define AskYesNo(format, ...) MsgAlert(true, QUESTION, format, __VA_ARGS__) 
	#define CriticalAlert(format, ...) MsgAlert(false, CRITICAL, format, __VA_ARGS__) 
	// Use these macros (that do the same thing) if the message should be translated.
	#define SuccessAlertT(format, ...) MsgAlert(false, INFORMATION, format, __VA_ARGS__) 
	#define PanicAlertT(format, ...) MsgAlert(false, WARNING, format, __VA_ARGS__) 
	#define PanicYesNoT(format, ...) MsgAlert(true, WARNING, format, __VA_ARGS__) 
	#define AskYesNoT(format, ...) MsgAlert(true, QUESTION, format, __VA_ARGS__) 
	#define CriticalAlertT(format, ...) MsgAlert(false, CRITICAL, format, __VA_ARGS__) 
#else
	#define SuccessAlert(format, ...) MsgAlert(false, INFORMATION, format, ##__VA_ARGS__) 
	#define PanicAlert(format, ...) MsgAlert(false, WARNING, format, ##__VA_ARGS__) 
	#define PanicYesNo(format, ...) MsgAlert(true, WARNING, format, ##__VA_ARGS__) 
	#define AskYesNo(format, ...) MsgAlert(true, QUESTION, format, ##__VA_ARGS__) 
	#define CriticalAlert(format, ...) MsgAlert(false, CRITICAL, format, ##__VA_ARGS__) 
	// Use these macros (that do the same thing) if the message should be translated.
	#define SuccessAlertT(format, ...) MsgAlert(false, INFORMATION, format, ##__VA_ARGS__) 
	#define PanicAlertT(format, ...) MsgAlert(false, WARNING, format, ##__VA_ARGS__) 
	#define PanicYesNoT(format, ...) MsgAlert(true, WARNING, format, ##__VA_ARGS__) 
	#define AskYesNoT(format, ...) MsgAlert(true, QUESTION, format, ##__VA_ARGS__) 
	#define CriticalAlertT(format, ...) MsgAlert(false, CRITICAL, format, ##__VA_ARGS__) 
#endif
#else
// GEKKO
	#define SuccessAlert(format, ...) ;
	#define PanicAlert(format, ...) ;
	#define PanicYesNo(format, ...) ;
	#define AskYesNo(format, ...) ;
	#define CriticalAlert(format, ...) ;
	#define SuccessAlertT(format, ...) ;
	#define PanicAlertT(format, ...) ;
	#define PanicYesNoT(format, ...) ;
	#define AskYesNoT(format, ...) ;
	#define CriticalAlertT(format, ...) ;
#endif

#endif // _MSGHANDLER_H_

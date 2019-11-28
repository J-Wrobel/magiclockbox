#pragma once

#include <applibs/gpio.h>
#include "parson.h"

#define JSON_BUFFER_SIZE 204

#define CLOUD_MSG_SIZE 22

typedef enum {
	TYPE_INT = 0,
	TYPE_FLOAT = 1,
	TYPE_BOOL = 2,
	TYPE_STRING = 3
} data_type_t;

typedef struct {
	char* twinKey;
	void* twinVar;
	size_t twinSize; //needed to safely handle strings
	int* twinFd;
	GPIO_Id twinGPIO;
	data_type_t twinType;
	bool active_high;
} twin_t;

///<summary>
///		Parses received desired property changes.
///</summary>
///<param name="desiredProperties">Address of desired properties JSON_Object</param>
void deviceTwinChangedHandler(JSON_Object * desiredProperties);

void checkAndUpdateDeviceTwin(char*, void*, data_type_t, bool);

int8_t sendStateTelemetry(const char* state, const char* val);

//int unlock_box(const char* directMethodName, const char* payload,
//	size_t payloadSize, char** responsePayload,
//	size_t* responsePayloadSize);

int deviceTwinInitialize(void);

void deviceTwinClose(void);


#define NO_GPIO_ASSOCIATED_WITH_TWIN -1
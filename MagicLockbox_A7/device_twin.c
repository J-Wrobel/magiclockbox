#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
//#include "applibs_versions.h"
#include "epoll_timerfd_utilities.h"

#include <applibs/log.h>
#include <applibs/i2c.h>
#include <applibs/gpio.h>

#include "hw/avnet_mt3620_sk.h"
#include "deviceTwin.h"
#include "azure_iot_utilities.h"
#include "parson.h"
#include "build_options.h"
#include "magicKey.h"

extern volatile sig_atomic_t terminationRequired;

static const char cstrDeviceTwinJsonInteger[] = "{\"%s\": %d}";
static const char cstrDeviceTwinJsonFloat[] = "{\"%s\": %.2f}";
static const char cstrDeviceTwinJsonBool[] = "{\"%s\": %s}";
static const char cstrDeviceTwinJsonString[] = "{\"%s\": \"%s\"}";
#ifdef IOT_CENTRAL_APPLICATION
static const char cstrDeviceTwinJsonFloatIOTC[] = "{\"%s\": {\"value\": %.2f, \"status\" : \"completed\" , \"desiredVersion\" : %d }}";
static const char cstrDeviceTwinJsonBoolIOTC[] = "{\"%s\": {\"value\": %s, \"status\" : \"completed\" , \"desiredVersion\" : %d }}";
static const char cstrDeviceTwinJsonIntegerIOTC[] = "{\"%s\": {\"value\": %d, \"status\" : \"completed\" , \"desiredVersion\" : %d }}";
//static const char cstrDeviceTwinJsonStringIOTC[]  = "{\"%s\": {\"value\": %s, \"status\" : \"completed\" , \"desiredVersion\" : %d }}";
static const char cstrDeviceTwinJsonStringIOTC[]  = "{\"%s\": {\"value\": \"%s\", \"status\" : \"completed\" , \"desiredVersion\" : %d }}";
#endif 

// Define each device twin key that we plan to catch, process, and send reported property for.
// .twinKey - The JSON Key piece of the key: value pair
// .twinVar - The address of the application variable keep this key: value pair data
// .twinFD - The associated File Descriptor for this item.  This is usually a GPIO FD.  NULL if NA.
// .twinGPIO - The associted GPIO number for this item.  NO_GPIO_ASSOCIATED_WITH_TWIN if NA
// .twinType - The data type for this item, TYPE_BOOL, TYPE_STRING, TYPE_INT, or TYPE_FLOAT
// .active_high - true if GPIO item is active high, false if active low.  This is used to init the GPIO 
twin_t twinArray[] = {
	{.twinKey = "MagicLockboxRecipe",.twinVar = magicKeyRecipe, .twinSize = sizeof(magicKeyRecipe),.twinFd = NULL,.twinGPIO = NO_GPIO_ASSOCIATED_WITH_TWIN,.twinType = TYPE_STRING,.active_high = true},
};

// Calculate how many twin_t items are in the array.  We use this to iterate through the structure.
int twinArraySize = sizeof(twinArray) / sizeof(twin_t);

static int desiredVersion = 0;

int8_t sendStateTelemetry(const char* state, const char* val)
{
	char* pjsonBuffer = (char*)malloc(JSON_BUFFER_SIZE);
	if (pjsonBuffer == NULL) {
		Log_Debug("ERROR: not enough memory to send state telemetry");
		return -1;
	}

	// construct the telemetry message reporting state
	snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonString, state, val);
	Log_Debug("\n[Info] Sending telemetry %s\n", pjsonBuffer);
	AzureIoT_SendMessage(pjsonBuffer);

	return 0;
}

int unlock_box(const char* directMethodName, const char* payload,
	size_t payloadSize, char** responsePayload,
	size_t* responsePayloadSize)
{
	Log_Debug("Lets change lock.\n");	
	magicLockbox_scheduleLockToggle();
	static const char responseOk[] =
	{
		"{ \"success\" : true, \"message\" : \"Toggling lock state\" }"
	};
	static size_t maxResponseLength = sizeof(responseOk);
	
	char* nullTerminatedJsonString = (char*)malloc(maxResponseLength+1); //+1 for null termination
	if (nullTerminatedJsonString == NULL) {
		Log_Debug("ERROR: Could not allocate buffer for response of direct method call.\n");
		abort();
	}

	// Copy the provided buffer to a null terminated buffer.
	memcpy(nullTerminatedJsonString, responseOk, maxResponseLength);
	// Add the null terminator at the end.
	nullTerminatedJsonString[maxResponseLength] = 0;
	*(responsePayload) = nullTerminatedJsonString;
	if (*(responsePayload) == NULL)
	{
		Log_Debug("Cannot construct payload of response.\n");
		return -1;
	}
	*(responsePayloadSize) = strlen(*(responsePayload));

	return 200;
}

int deviceTwinInitialize(void)
{
	// Traverse the twin Array and for each GPIO item in the list open the file descriptor
	for (int i = 0; i < twinArraySize; i++) {

		// Verify that this entry is a GPIO entry
		if (twinArray[i].twinGPIO != NO_GPIO_ASSOCIATED_WITH_TWIN) {

			*twinArray[i].twinFd = -1;

			// For each item in the data structure, initialize the file descriptor and open the GPIO for output.  Initilize each GPIO to its specific inactive state.
			*twinArray[i].twinFd = (int)GPIO_OpenAsOutput(twinArray[i].twinGPIO, GPIO_OutputMode_PushPull, twinArray[i].active_high ? GPIO_Value_Low : GPIO_Value_High);

			if (*twinArray[i].twinFd < 0) {
				Log_Debug("ERROR: Could not open LED %d: %s (%d).\n", twinArray[i].twinGPIO, strerror(errno), errno);
				return -1;
			}
		}
	}
	// Tell the system about the callback function that gets called when we receive a device twin update message from Azure
	AzureIoT_SetDeviceTwinUpdateCallback(&deviceTwinChangedHandler);
	AzureIoT_SetDirectMethodCallback(unlock_box);

	return 0;
}

void deviceTwinClose(void)
{
	// Traverse the twin Array and for each GPIO item in the list the close the file descriptor
	for (int i = 0; i < twinArraySize; i++) {

		// Verify that this entry has an open file descriptor
		if (twinArray[i].twinGPIO != NO_GPIO_ASSOCIATED_WITH_TWIN) {

			CloseFdAndPrintError(*twinArray[i].twinFd, twinArray[i].twinKey);
		}
	}	
}

///<summary>
///		check to see if any of the device twin properties have been updated.  If so, send up the current data.
///</summary>
void checkAndUpdateDeviceTwin(char* property, void* value, data_type_t type, bool ioTCentralFormat)
{
	int nJsonLength = -1;

	char *pjsonBuffer = (char *)malloc(JSON_BUFFER_SIZE);
	if (pjsonBuffer == NULL) {
		Log_Debug("ERROR: not enough memory to report device twin changes.");
	}

	if (property != NULL) {

		// report current device twin data as reported properties to IoTHub

		switch (type) {
		case TYPE_BOOL:
#ifdef IOT_CENTRAL_APPLICATION
			if (ioTCentralFormat) {
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonBoolIOTC, property, *(bool*)value ? "true" : "false", desiredVersion);
			}
			else
#endif 
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonBool, property, *(bool*)value ? "true" : "false", desiredVersion);

			break;
		case TYPE_FLOAT:
#ifdef IOT_CENTRAL_APPLICATION			
			if (ioTCentralFormat) {
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonFloatIOTC, property, *(float*)value, desiredVersion);
			}
			else
#endif 
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonFloat, property, *(float*)value, desiredVersion);
			break;
		case TYPE_INT:
#ifdef IOT_CENTRAL_APPLICATION		
			if (ioTCentralFormat) {
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonIntegerIOTC, property, *(int*)value, desiredVersion);
			}
			else
#endif
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonInteger, property, *(int*)value, desiredVersion);
			break;
		case TYPE_STRING:
#ifdef IOT_CENTRAL_APPLICATION			
			if (ioTCentralFormat) {
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonStringIOTC, property, (char*)value, desiredVersion);
			}
			else
#endif 
				nJsonLength = snprintf(pjsonBuffer, JSON_BUFFER_SIZE, cstrDeviceTwinJsonString, property, (char*)value, desiredVersion);
			break;
		}

		if (nJsonLength > 0) {
			Log_Debug("[MCU] Updating device twin: %s\n", pjsonBuffer);
			AzureIoT_TwinReportStateJson(pjsonBuffer, (size_t)nJsonLength);
		}
		free(pjsonBuffer);
	}
}

///<summary>
///		Parses received desired property changes.
///</summary>
///<param name="desiredProperties">Address of desired properties JSON_Object</param>
void deviceTwinChangedHandler(JSON_Object * desiredProperties)
{
	int result = 0;

	// Pull the twin version out of the message.  We use this value when we echo the new setting back to IoT Connect.
	if (json_object_has_value(desiredProperties, "$version") != 0)
	{
		desiredVersion = (int)json_object_get_number(desiredProperties, "$version");
	}

#ifdef IOT_CENTRAL_APPLICATION		

	for (int i = 0; i < (sizeof(twinArray) / sizeof(twin_t)); i++) {

		if (json_object_has_value(desiredProperties, twinArray[i].twinKey) != 0)
		{

			JSON_Object *currentJSONProperties = json_object_dotget_object(desiredProperties, twinArray[i].twinKey);

			switch (twinArray[i].twinType) {
			case TYPE_BOOL:
				*(bool*)twinArray[i].twinVar = (bool)json_object_get_boolean(currentJSONProperties, "value");
				result = GPIO_SetValue(*twinArray[i].twinFd, twinArray[i].active_high ? (GPIO_Value)*(bool*)twinArray[i].twinVar : !(GPIO_Value)*(bool*)twinArray[i].twinVar);

				if (result != 0) {
					Log_Debug("Fd: %d\n", twinArray[i].twinFd);
					Log_Debug("FAILURE: Could not set GPIO_%d, %d output value %d: %s (%d).\n", twinArray[i].twinGPIO, twinArray[i].twinFd, (GPIO_Value)*(bool*)twinArray[i].twinVar, strerror(errno), errno);
					terminationRequired = true;
				}
				Log_Debug("Received device update. New %s is %s\n", twinArray[i].twinKey, *(bool*)twinArray[i].twinVar ? "true" : "false");
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_BOOL, true);
				break;
			case TYPE_FLOAT:
				*(float*)twinArray[i].twinVar = (float)json_object_get_number(currentJSONProperties, "value");
				Log_Debug("Received device update. New %s is %0.2f\n", twinArray[i].twinKey, *(float*)twinArray[i].twinVar);
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_FLOAT, true);
				break;
			case TYPE_INT:
				*(int*)twinArray[i].twinVar = (int)json_object_get_number(currentJSONProperties, "value");
				Log_Debug("Received device update. New %s is %d\n", twinArray[i].twinKey, *(int*)twinArray[i].twinVar);
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_INT, true);
				break;

			case TYPE_STRING:
				Log_Debug(">>> STRING!");
				strncpy((char*)twinArray[i].twinVar, json_object_get_string(currentJSONProperties, "value"), twinArray[i].twinSize);
				Log_Debug("Received device update. New %s is %s\n", twinArray[i].twinKey, (char*)twinArray[i].twinVar);
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_STRING, true);
				break;
			}
		}
	}
#else // !IOT_CENTRAL_APPLICATION		

	for (int i = 0; i < (sizeof(twinArray) / sizeof(twin_t)); i++) {

		if (json_object_has_value(desiredProperties, twinArray[i].twinKey) != 0)
		{

			switch (twinArray[i].twinType) {
			case TYPE_BOOL:
				*(bool*)twinArray[i].twinVar = (bool)json_object_get_boolean(desiredProperties, twinArray[i].twinKey);
				result = GPIO_SetValue(*twinArray[i].twinFd, twinArray[i].active_high ? (GPIO_Value)*(bool*)twinArray[i].twinVar : !(GPIO_Value)*(bool*)twinArray[i].twinVar);

				if (result != 0) {
					Log_Debug("Fd: %d\n", twinArray[i].twinFd);
					Log_Debug("FAILURE: Could not set GPIO_%d, %d output value %d: %s (%d).\n", twinArray[i].twinGPIO, twinArray[i].twinFd, (GPIO_Value)*(bool*)twinArray[i].twinVar, strerror(errno), errno);
					terminationRequired = true;
				}
				Log_Debug("Received device update. New %s is %s\n", twinArray[i].twinKey, *(bool*)twinArray[i].twinVar ? "true" : "false");
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_BOOL, true);
				break;
			case TYPE_FLOAT:
				*(float*)twinArray[i].twinVar = (float)json_object_get_number(desiredProperties, twinArray[i].twinKey);
				Log_Debug("Received device update. New %s is %0.2f\n", twinArray[i].twinKey, *(float*)twinArray[i].twinVar);
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_FLOAT, true);
				break;
			case TYPE_INT:
				*(int*)twinArray[i].twinVar = (int)json_object_get_number(desiredProperties, twinArray[i].twinKey);
				Log_Debug("Received device update. New %s is %d\n", twinArray[i].twinKey, *(int*)twinArray[i].twinVar);
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_INT, true);
				break;

			case TYPE_STRING:
				strcpy((char*)twinArray[i].twinVar, (char*)json_object_get_string(desiredProperties, twinArray[i].twinKey));
				Log_Debug("Received device update. New %s is %s\n", twinArray[i].twinKey, (char*)twinArray[i].twinVar);
				checkAndUpdateDeviceTwin(twinArray[i].twinKey, twinArray[i].twinVar, TYPE_STRING, true);
				break;
			}
		}
	}
#endif 

}

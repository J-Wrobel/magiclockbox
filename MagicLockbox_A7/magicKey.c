#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h> 

// applibs_versions.h defines the API struct versions to use for applibs APIs.
//#include "applibs_versions.h"
#include <applibs/log.h>
#include <applibs/storage.h>
#include <applibs/pwm.h>

#include "epoll_timerfd_utilities.h"
#include "deviceTwin.h"
#include "magicKey.h"
#include "build_options.h"
#include "azure_iot_utilities.h"

#include <hw/avnet_mt3620_sk.h>

// File descriptors - initialized to invalid value
static int pwmFd = -1;
static int servoDurationTimerFd = -1;
static int oneSecTimerFd = -1;

typedef struct MagicLockboxState
{
	bool initializing;
	bool ready;
	bool activity;
	bool inactivity;
} MagicLockboxState_t;

static MagicLockboxState_t notificationState;

/**
CONSTANTS
**/
#define EVENT_TABLE_SIZE (MAGIC_LOCKBOX_RECIPE_LEN-1)

/**
EXTERN VARIABLES
**/
extern int epollFd; //get access to epoll for adding more timers
extern volatile sig_atomic_t terminationRequired; //get access to mcu abort

/**
FUNC PROTOTYPES
**/
static int setupServoAction(bool locking);

static void servoActionStop(EventData* event);

static int turnAllChannelsOff(void);

static int writeToMutableFile(void);

static int readMutableFile(void);

static KeyEvent_t findEventWithCode(uint8_t eventCode);

static void toggleLock(void);

static int8_t updateGoalEventChain(void);

static void clearEventTable(void);

static void moveCurrentEventToTable(void);

static void lockToggleTimerHandler(EventData* event);

static void overwriteWindowTimerHandler(EventData* event);

static void chainNotCompleteTimerHandler(EventData* event);

static int8_t enableOverwriteWindow(void);

static KeyEvent_t* getCollectedEvents(void);

static bool checkInputEventsMatch(void);


typedef struct MagicKeyState
{
	bool locked;
	bool action_scheduled;
	KeyEvent_t inputKeyEvents[EVENT_TABLE_SIZE];
	char recipe[MAGIC_LOCKBOX_RECIPE_LEN];

}MagicKeyState_t;


static MagicKeyState_t keyState = { .recipe = DEFAULT_RECIPE };
//updated by device twin
char magicKeyRecipe[MAGIC_LOCKBOX_RECIPE_LEN] = DEFAULT_RECIPE;



//timer for time window for single event - it will be used to allow 
//event to be overwritten if another event comes in quick succession
static int eventOverwriteWindowTimerFd = -1;
//timer for clearing all registered events if no activity too long 
//- usefull for reseting whole event sequence in case wrong input
static int eventChainNotCompleteTimerFd = -1;
//timer that schedules lock toggle in short time
static int lockToggleTimerFd = -1;
//Table for holding current key events that were registered
KeyEvent_t inputKeyEvents[EVENT_TABLE_SIZE];
static uint8_t inputKeyEventsIndex = 0;
//Current event that is waiting to be moved to table
static KeyEvent_t currentEvent = event_last;
//Flag indicating that current event can be still overwriten by immidiate occurance of other one
static bool eventOverwriteActive = false; //needed?

void notifyState(EventData* event)
{
	if (notificationState.initializing)
	{
		notificationState.initializing = false;
		sendStateTelemetry("system", "initialize");
	}
	else if (notificationState.ready)
	{
		//do not clear ready status as initialize can be sent along with
		// ready at system startup and iot central can show initialize continously
		#if(0)		
		notificationState.ready = false;
		#endif
		sendStateTelemetry("system", "ready");
	}

	if (notificationState.activity)
	{
		notificationState.activity = false;
		sendStateTelemetry("motion", "activity");
	}
	else if (notificationState.inactivity)
	{
		notificationState.inactivity = false;
		sendStateTelemetry("motion", "inactivity");
	}

}

// Event handler data structures. Only the event handler field needs to be populated.
static EventData oneSecEventData = { .eventHandler = notifyState };

// The polarity is inverted because LEDs are driven low
static PwmState ledPwmState = { .period_nsec = FULL_CYCLE_NS,
							   .polarity = PWM_Polarity_Normal,
							   .dutyCycle_nsec = 0,
							   .enabled = true };

/// <summary>
///     Turns all channels off for the opened controller.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int turnAllChannelsOff(void)
{
	ledPwmState.dutyCycle_nsec = 0;
	for (unsigned int i = MT3620_PWM_CHANNEL0; i <= MT3620_PWM_CHANNEL3; ++i) {
		int result = PWM_Apply(pwmFd, i, &ledPwmState);
		if (result != 0) {
			Log_Debug("PWM_Apply failed: result = %d, errno value: %s (%d)\n", result,
				strerror(errno), errno);
			return result;
		}
	}
	return 0;
}

/// <summary>
///     Turns all channels off for the opened controller.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int setupServoAction(bool locking)
{
	// Timer state variables
	static const struct timespec servoActionDuration = { .tv_sec = LOCK_TOGGLE_DURATION_S, .tv_nsec = 0 };
	SetTimerFdToSingleExpiry(servoDurationTimerFd, &servoActionDuration);

	//Set servo control signal for fixed angle
	if (locking)
	{
		Log_Debug("Setup servo to locking.\n");
		ledPwmState.dutyCycle_nsec = DUTY_CYCLE_LOCKED;
	}
	else
	{
		Log_Debug("Setup servo to unlocking.\n");
		ledPwmState.dutyCycle_nsec = DUTY_CYCLE_UNLOCKED;
	}
	int result = PWM_Apply(pwmFd, MT3620_PWM_CHANNEL0, &ledPwmState);
	if (result != 0) {
		Log_Debug("PWM_Apply failed: result = %d, errno: %s (%d)\n", result, strerror(errno),
			errno);
	}

	// Channel two is used to power the servo through a transistor switch for small period of time
	ledPwmState.dutyCycle_nsec = FULL_CYCLE_NS;
	result = PWM_Apply(pwmFd, MT3620_PWM_CHANNEL2, &ledPwmState);
	if (result != 0) {
		Log_Debug("PWM_Apply failed: result = %d, errno: %s (%d)\n", result, strerror(errno),
			errno);
	}
	return 0;
}

static void servoActionStop(EventData* event)
{
	if (ConsumeTimerFdEvent(event->fd) != 0) {
		terminationRequired = true;
		return;
	}
	turnAllChannelsOff();
}

// Event handler data structures. Only the event handler field needs to be populated.
static EventData servoDurationEventData = { .eventHandler = servoActionStop };

/// <summary>
/// Write an integer to this application's persistent data file
/// </summary>
static int writeToMutableFile(void)
{
	int fd = Storage_OpenMutableFile();
	if (fd < 0) {
		Log_Debug("ERROR: Could not open mutable file:  %s (%d).\n", strerror(errno), errno);
		return -1;
	}
	ssize_t ret = write(fd, &keyState, sizeof(MagicKeyState_t));
	if (ret < 0) {
		// If the file has reached the maximum size specified in the application manifest,
		// then -1 will be returned with errno EDQUOT (122)
		Log_Debug("ERROR: An error occurred while writing to mutable file:  %s (%d).\n",
			strerror(errno), errno);
	}
	else if (ret < sizeof(MagicKeyState_t)) {
		// For simplicity, this sample logs an error here. In the general case, this should be
		// handled by retrying the write with the remaining data until all the data has been
		// written.
		Log_Debug("ERROR: Only wrote %d of %d bytes requested\n", ret, (int)sizeof(MagicKeyState_t));
	}
	close(fd);
	return 0;
}

/// <summary>
/// Read an integer from this application's persistent data file
/// </summary>
/// <returns>
/// The integer that was read from the file.  If the file is empty, this returns 0.  If the storage
/// API fails, this returns -1.
/// </returns>

//check again if this reading is really ok
static int readMutableFile(void)
{
	int fd = Storage_OpenMutableFile();
	if (fd < 0) {
		Log_Debug("ERROR: Could not open mutable file:  %s (%d).\n", strerror(errno), errno);
		return -1;
	}
	ssize_t ret = read(fd, &keyState, sizeof(MagicKeyState_t));
	if (ret < 0) {
		Log_Debug("ERROR: An error occurred while reading file:  %s (%d).\n", strerror(errno),
			errno);
	}
	close(fd);
	if (ret < sizeof(MagicKeyState_t)) 
	{
		//erase kagic key it will be left unlocked and recipe will be reverted to default
		memset(&keyState, 0, sizeof(MagicKeyState_t));
		Log_Debug("WARNING: KeyState reset because file could not be read succesfully\n");
		return 0;
	}
	strncpy(magicKeyRecipe, keyState.recipe, sizeof(magicKeyRecipe));

	return 0;
}

//Maps event code in uint8_t to enum
static KeyEvent_t findEventWithCode(uint8_t eventCode)
{
	KeyEvent_t returnEvent = event_none;
	switch (eventCode)
	{
		case event_tap_x:
		case event_tap_y:
		case event_tap_z:
		case event_4d_top_x:
		case event_4d_bottom_x:
		case event_4d_top_y:
		case event_4d_bottom_y:
		case event_4d_top_z:
		case event_4d_bottom_z:
		case event_swipe_left:
		case event_swipe_right:
		case event_swipe_up:
		case event_swipe_down:
			returnEvent = (KeyEvent_t)eventCode;
			break;
		default: 
			//nothing, just return code
			break;
	}
	return returnEvent;
}

static void toggleLock(void)
{
	if (keyState.locked)
	{
		sendStateTelemetry("lock", "unlocked");
	}
	else
	{
		sendStateTelemetry("lock", "locked");
	}
	clearEventTable();
	keyState.action_scheduled = false;
	setupServoAction(!keyState.locked);
	keyState.locked = !keyState.locked;
	writeToMutableFile();
}

static int8_t updateGoalEventChain(void)
{
	//check for null ended string
	if (magicKeyRecipe[MAGIC_LOCKBOX_RECIPE_LEN - 1] != 0)
	{
		Log_Debug("ERROR: Receipe not null terminated\n");
		return -1;
	}
	if (strlen(magicKeyRecipe) > MAGIC_LOCKBOX_RECIPE_LEN)
	{
		Log_Debug("ERROR: Receipe size too big\n");
		return -1;
	}
	if (0 != strncmp(magicKeyRecipe, keyState.recipe, MAGIC_LOCKBOX_RECIPE_LEN))
	{
		strncpy(keyState.recipe, magicKeyRecipe, sizeof(keyState.recipe));
		uint8_t i = 0;
		while (i < EVENT_TABLE_SIZE)
		{
			keyState.inputKeyEvents[i] = findEventWithCode(keyState.recipe[i]);
			i++;
		}
		writeToMutableFile();
	}		
	return 0;
}

static void clearEventTable(void)
{
	memset(&inputKeyEvents, event_none, EVENT_TABLE_SIZE*sizeof(KeyEvent_t));
	inputKeyEventsIndex = 0;
	Log_Debug("Events cleared\n");
}

static void moveCurrentEventToTable(void)
{
	if (inputKeyEventsIndex >= EVENT_TABLE_SIZE)
	{
		clearEventTable();
	}	
	inputKeyEvents[inputKeyEventsIndex] = currentEvent;
	Log_Debug("Saved event %d to %d\n", currentEvent, inputKeyEventsIndex);
	inputKeyEventsIndex++;
	currentEvent = event_none;	
}

static void lockToggleTimerHandler(EventData* event)
{
	//Clear event
	if (ConsumeTimerFdEvent(lockToggleTimerFd) != 0) {
		terminationRequired = true;
		return;
	}
	//Schedule lock toggle in short time
	toggleLock();
}

static void overwriteWindowTimerHandler(EventData * event)
{
	eventOverwriteActive = false;
	//Clear event //todo chec if fd from event can be used
	if (ConsumeTimerFdEvent(eventOverwriteWindowTimerFd) != 0) {
		terminationRequired = true;
		return;
	}
	moveCurrentEventToTable();
	Log_Debug("Overwrite window expired\n", strerror(errno), errno);
	//Start chain reset timer to reset chain if not completed within time
	static struct timespec expiryTime = { .tv_sec = EVENT_SEQUENCE_RESET_S,.tv_nsec = 0 }; //todo move to build options
	//Set zero time to next timer expiry
	SetTimerFdToSingleExpiry(eventChainNotCompleteTimerFd, &expiryTime);
}

static void chainNotCompleteTimerHandler(EventData* event)
{
	eventOverwriteActive = false;
	clearEventTable();
	//Clear event
	if (ConsumeTimerFdEvent(eventChainNotCompleteTimerFd) != 0) {
		terminationRequired = true;
		return;
	}
	
	Log_Debug("Chain not completed expired\n", strerror(errno), errno);
}

static int8_t enableOverwriteWindow(void)
{
	static struct timespec expiryTime = { .tv_sec = EVENT_OVERWRITE_S,.tv_nsec = EVENT_OVERWRITE_NS }; 
	//Set zero time to next timer expiry
	if (SetTimerFdToSingleExpiry(eventOverwriteWindowTimerFd, &expiryTime) < 0)
	{
		return -1;
	}
	eventOverwriteActive = true;
	return 0;
}


void magicLockbox_notifyState(State_t state)
{
	switch (state)
	{
	case state_initialize:
		notificationState.initializing = true;
		break;
	case state_ready:
		notificationState.ready = true;
		break;
	case state_activity:
		notificationState.activity = true;
		break;
	case state_inactivity:
		notificationState.inactivity = true;
		break;
	default:
		//do nothing
		break;
	}
}

int8_t magicLockbox_initialize(void)
{
	magicLockbox_notifyState(state_initialize);
	readMutableFile();
	updateGoalEventChain();	

	static struct timespec timePeriod = { .tv_sec = 0,.tv_nsec = 0 };
	// event handler data structures. Only the event handler field needs to be populated.
	static EventData eventOverwriteWindowTimerData = { .eventHandler = overwriteWindowTimerHandler };
	eventOverwriteWindowTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &timePeriod, &eventOverwriteWindowTimerData, EPOLLIN);
	if (eventOverwriteWindowTimerFd < 0) {
		return -1;
	}

	static EventData eventChainNotCompletedTimerData = { .eventHandler = chainNotCompleteTimerHandler };
	eventChainNotCompleteTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &timePeriod, &eventChainNotCompletedTimerData, EPOLLIN);
	if (eventChainNotCompleteTimerFd < 0) {
		return -1;
	}

	static EventData eventLockToggleTimerData = { .eventHandler = lockToggleTimerHandler };
	lockToggleTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &timePeriod, &eventLockToggleTimerData, EPOLLIN);
	if (lockToggleTimerFd < 0) {
		return -1;
	}	

	pwmFd = PWM_Open(AVNET_MT3620_SK_PWM_CONTROLLER0);
	if (pwmFd < 0) {
		Log_Debug(
			"Error opening AVNET_MT3620_SK_PWM_CONTROLLER0: %s (%d). Check that app_manifest.json "
			"includes the PWM used.\n",
			strerror(errno), errno);
		return -1;
	}

	if (turnAllChannelsOff()) {
		return -1;
	}

	servoDurationTimerFd =
		CreateTimerFdAndAddToEpoll(epollFd, &timePeriod, &servoDurationEventData, EPOLLIN);
	if (servoDurationTimerFd < 0) {
		return -1;
	}

	timePeriod.tv_sec = 1;
	oneSecTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &timePeriod, &oneSecEventData, EPOLLIN);
	if (oneSecTimerFd < 0) {
		return -1;
	}


	magicLockbox_notifyState(state_ready);
	if (keyState.locked)
	{
		sendStateTelemetry("lock", "unlocked");
	}
	else
	{
		sendStateTelemetry("lock", "locked");
	}

	return 0;
}

void magicLockbox_registerEvent(KeyEvent_t keyEvent)
{
	enableOverwriteWindow();
	currentEvent = keyEvent;
	Log_Debug("Got event %c\n", keyEvent);
}

static KeyEvent_t* getCollectedEvents(void)
{
	return inputKeyEvents;
}

static bool checkInputEventsMatch(void)
{	
	if (0 == memcmp(keyState.inputKeyEvents, getCollectedEvents(), sizeof(KeyEvent_t) * EVENT_TABLE_SIZE))
	{
		if (keyState.locked && !keyState.action_scheduled)
		{	
			magicLockbox_scheduleLockToggle();
			return true;
		}
	}
	return false;
}

void magicLockbox_loopTask(void)
{
	checkInputEventsMatch();
	updateGoalEventChain();	

}

void magicLockbox_scheduleLockToggle(void)
{
	static struct timespec expiryTime = { .tv_sec = LOCK_TOGGLE_DELAY_S,.tv_nsec = 0 }; 
	//Set zero time to next timer expiry
	SetTimerFdToSingleExpiry(lockToggleTimerFd, &expiryTime);
	keyState.action_scheduled = true;
}

bool magicLockbox_isLocked(void)
{
	return keyState.locked;
}


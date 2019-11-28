#pragma once

#include <stdbool.h>

/**
 >>> MagicLockbox general description
Main aim of the module is to allow lock opening using events that are 
registered in it. Sequence of registered events is compared against
goal pattern of events, so called recipe, and if they match lock is 
scheduled to be opened.

 >>> Features
- Recipe synchronization with the cloud
- Recipe stored in device file for operation when cloud is not available
- Registering events can be anything that was defined earlier and fed 
from outside module
- Lockbox state can be registered in the cloud (for available state and format see below)

TODO: 
-add failsafe methods for lock opening if input devices do not respond or powersupply gets low
-move reading of accelerometer events to m4 core (when I2C support is in place) to utilize 
pulse mode interrupts and to handle also double tap

 >>> General operation 
Device intializes with recipe from device file storage. After that it awaits for new events. Each incoming 
event is temporarly stored and timer starts to measure EVENT_OVERWRITE time. If new event comes before timer expires
it overwrites the one that was already stored. If the timer expires before new event is observed stroed event is latched
into array that will be compared against recipe. If array runs out of space or no event is registered for more than 
EVENT_SEQUENCE_RESET then array is cleared. Each run of loop task checks if new recipe has been copied from cloud to be updated 
or registered event sequence matches the recpie. Newly copied recipes are checked for validity and stored in device files for future
reference. If recipe has matched the input then lockbox will unlock.
 
 >>> Unlocking
 Unlocking is implemented by controling a micro servo to move sliding bolt inside magick box. Unlocking can be started by
 matching the recipe, by pressing the A button or by calling DirectMethod from cloud. The same can be done with locking with the exception 
 of recpie. Locking and unlocking procedure looks similar. Servo is being powered up with transistor acting as a switch. Then, 
 proper servo position is issued with PWM signal and after LOCK_TOGGLE_DURATION servo is powered off. There is additional delay for
 lcok operation (LOCK_TOGGLE_DELAY) so the lid of lockbox can be closed.

 >>> Events
 Events are defined in KeyEvent_t enum. They can be expanded with wahtever comes to ones mind. At this stage events are read from 
 Azure Spheres Starte kit accelerometr in the form of rotations and taping.
 TODO add description of gestures.

 >>> States
 States which can be reported to cloud and their format.

 System - shows when system has initialized and when is ready
{ "system" : "initialize" }
{ "system" : "ready" }
Lock - shows lock state
{ "lock" : "unlocked" }
{ "lock" : "locked" }
Activity - shows when lockbox is moving and when not
{ "motion" : "inactivity"}
{ "motion" : "activity"}

**/

// How many events can be used in recipe
#define MAGIC_LOCKBOX_RECIPE_LEN	8
// PWM configuration
#define FULL_CYCLE_NS				20000000
// Values of duty cycle to open or close lock with servo driven by pwm defining position of servo shaft
#define DUTY_CYCLE_UNLOCKED 		500000
#define DUTY_CYCLE_LOCKED 			1500000

// Timer that accepts events overwriting previous one (used to deal with too many event coming too fast).
// When event occurs timer is reset and waits for expiration for event to be stored. Configurable seconds 
// and nanoseconds
#define EVENT_OVERWRITE_NS			0
#define EVENT_OVERWRITE_S			1

// Timer that resets whole stored sequence of events upon expiration. It is reset when event occurs.
// Configurable seconds
#define EVENT_SEQUENCE_RESET_S		20

// Lock toggling configuration
#define LOCK_TOGGLE_DELAY_S			5
#define LOCK_TOGGLE_DURATION_S		5

// Default recipe of events sequence opening lock, uses values from events enumeration
#define DEFAULT_RECIPE				{ 't','b','t', 0, 0, 0, 0, 0 }

// 
typedef enum Event
{
	event_none = 0,
	event_tap_x = 'x',
	event_tap_y = 'y',
	event_tap_z = 'z',
	event_4d_top_x = 't',
	event_4d_bottom_x = 'T',
	event_4d_top_y = 'b',
	event_4d_bottom_y = 'B',
	event_4d_top_z = 's',
	event_4d_bottom_z = 'S',
	event_swipe_left = 'l',
	event_swipe_right = 'r',
	event_swipe_up = 'u',
	event_swipe_down = 'd',
	event_last //keep last to track size needed for type encoding
} KeyEvent_t;

typedef enum State
{
	state_initialize,
	state_ready,
	state_activity,
	state_inactivity,
	state_last //keep last
} State_t;


// Extern needed to be used in cloud synchronization via device twin
extern char magicKeyRecipe[MAGIC_LOCKBOX_RECIPE_LEN];

// Notifies lockbox object about state change
void magicLockbox_notifyState(State_t);

// Initialize magic chain of events
int8_t magicLockbox_initialize(void);

// Registers occurance of new event relevant for the module
void magicLockbox_registerEvent(KeyEvent_t keyEvent);

// Get lock state
bool magicLockbox_isLocked(void);

// Schedule change of lock state
void magicLockbox_scheduleLockToggle(void);

// Task that will perform needed function for each main loop run
void magicLockbox_loopTask(void);

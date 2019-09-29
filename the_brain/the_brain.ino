//----------------------------------------------------------------------------------------
//
// Filename: the_brain.ino
//
// Description: 
//
//----------------------------------------------------------------------------------------


/* **************************   Header Files   *************************** */

// stdlib

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// System
#include <STM32RTC.h>
#include <STM32LowPower.h>

// Project

/* ******************************   Macros   ****************************** */

// Pins
#define HEARTBEAT_LED_PIN   PC13
#define USER_BUTTON_PIN     PA0
#define LIGHT_CTRL_PIN      PA1
#define BUZZER_CTRL_PIN     PA2

/* ******************************   Types   ******************************* */

typedef struct
{
    unsigned long curr_time_stamp;
    bool active;
} TimedGpioCtrl_t;

typedef struct
{
    unsigned long curr_time_stamp_ms;
    unsigned long time_elapsed_ms;
    unsigned long total_time_elapsed_ms;
    bool active;
} BtnCtrl_t;

typedef enum
{
    BTN_STATE_CHECKING,
    BTN_STATE_INACTIVE,
    BTN_STATE_ACTIVE
} BtnState_t;

typedef enum
{
    SYS_STATE_SLEEP,
    SYS_STATE_CHECK_BUTTON,
    SYS_STATE_CARRY_OUT_ACTIONS
} SysState_t;

/* ***********************   File Scope Variables   *********************** */

static TimedGpioCtrl_t hb_led_ctrl_obj = {0, false};
static TimedGpioCtrl_t buzzer_ctrl_obj = {0, false};
static TimedGpioCtrl_t light_ctrl_obj = {0, false};
static BtnCtrl_t usr_btn_ctrl_obj = {0, 0, 0};
static volatile bool is_running = false;

/* ***********************   Function Prototypes   ************************ */

static bool timedGpioControlWithExtTiming(unsigned int pinNum, unsigned int on_dir, unsigned int off_dir,
                                          int on_time_ms, int off_time_ms, TimedGpioCtrl_t *led_ctrl_obj);
static BtnState_t buttonPressActiveMonitor(unsigned int pinNum, unsigned int active_dir,
                                           int time_to_active, BtnCtrl_t *btn_ctrl_obj);

static void buttonPressResetObj(BtnCtrl_t *btn_ctrl_obj);
static void timedGpioCtrlResetObj(TimedGpioCtrl_t *ctrl_obj);

static void userButtonPressIsr(void);

/* *******************   Public Function Definitions   ******************** */

//----------------------------------------------------------------------------------------
// Sets up the system.
//----------------------------------------------------------------------------------------
void setup()
{
    // Setup all pins
    pinMode(HEARTBEAT_LED_PIN, OUTPUT);
    pinMode(LIGHT_CTRL_PIN, OUTPUT);
    pinMode(BUZZER_CTRL_PIN, OUTPUT);
    pinMode(USER_BUTTON_PIN, INPUT);

    // digitalWrite(HEARTBEAT_LED_PIN, HIGH); // Make sure that the LED is off.

    // while(1);

    hb_led_ctrl_obj.curr_time_stamp = millis();
}

//----------------------------------------------------------------------------------------
// Called in a while(1) loop.
//----------------------------------------------------------------------------------------
void loop()
{
    static SysState_t curr_state = SYS_STATE_SLEEP;
    static SysState_t next_state = SYS_STATE_SLEEP;
    static bool carry_out_light_ctrl_sequence = false;
    static bool carry_out_buzzer_ctrl_sequence = false;
    static int num_light_sequences_run = 0;
    static int num_buzzer_sequences_run = 0;

    switch (curr_state)
    {
        case SYS_STATE_SLEEP:
            hb_led_ctrl_obj.active = false;
            digitalWrite(HEARTBEAT_LED_PIN, HIGH); // Make sure that the LED is off.

            is_running = false;
            LowPower.begin();
            LowPower.attachInterruptWakeup(USER_BUTTON_PIN, userButtonPressIsr, FALLING);
            LowPower.deepSleep();

            if (is_running)
            {
                next_state = SYS_STATE_CHECK_BUTTON;
            }
            else
            {
                // In the event that the button wasn't really pressed, there was a time out in deepSleep().
                // Didn't look at code. It may be sleeping indefinitely. Just writing this code is easier than checking.
                next_state = curr_state;
            }
            break;
        
        case SYS_STATE_CHECK_BUTTON:
            if (!usr_btn_ctrl_obj.active)
            {
                // Just woke up from sleep, need to get movin'!
                buttonPressResetObj(&usr_btn_ctrl_obj);

                // Restart the heartbeat timer
                timedGpioCtrlResetObj(&hb_led_ctrl_obj);
            }
            
            switch (buttonPressActiveMonitor(USER_BUTTON_PIN, LOW, 100, &usr_btn_ctrl_obj))
            {
                case BTN_STATE_CHECKING:
                    // Nothing to do. Just keep rolling baby!
                    next_state = curr_state;
                    break;

                case BTN_STATE_INACTIVE:
                    // Invalid press, sleep time.

                    next_state = SYS_STATE_SLEEP;
                    break;

                case BTN_STATE_ACTIVE:
                default:
                    // Yay, time to do stuff!
                    carry_out_light_ctrl_sequence = true;
                    carry_out_buzzer_ctrl_sequence = true;

                    next_state = SYS_STATE_CARRY_OUT_ACTIONS;
                    break;
            }
            break;

        case SYS_STATE_CARRY_OUT_ACTIONS:
        default:

            if (!light_ctrl_obj.active)
            {
                // Just started this state, let's kick it off right!
                timedGpioCtrlResetObj(&light_ctrl_obj);
                timedGpioCtrlResetObj(&buzzer_ctrl_obj);
                
                num_light_sequences_run = 0;
                num_buzzer_sequences_run = 0;
            }

            if (carry_out_light_ctrl_sequence)
            {
                if (timedGpioControlWithExtTiming(LIGHT_CTRL_PIN, HIGH, LOW, 250, 150, &light_ctrl_obj))
                {
                    num_light_sequences_run++;

                    if (num_light_sequences_run == 3)
                    {
                        carry_out_light_ctrl_sequence = false;
                        light_ctrl_obj.active = false;
                    }
                }
            }

            if (carry_out_buzzer_ctrl_sequence)
            {
                if (timedGpioControlWithExtTiming(BUZZER_CTRL_PIN, HIGH, LOW, 150, 250, &buzzer_ctrl_obj))
                {
                    num_buzzer_sequences_run++;

                    if (num_buzzer_sequences_run == 3)
                    {
                        carry_out_buzzer_ctrl_sequence = false;
                        buzzer_ctrl_obj.active = false;
                    }
                }
            }

            if (!carry_out_light_ctrl_sequence && !carry_out_buzzer_ctrl_sequence)
            {
                next_state = SYS_STATE_SLEEP;
            }
            else
            {
                next_state = curr_state;
            }
            
            break;
    }

    if (curr_state != SYS_STATE_SLEEP)
    {
        (void)timedGpioControlWithExtTiming(HEARTBEAT_LED_PIN, LOW, HIGH, 100, 400, &hb_led_ctrl_obj);

        if (next_state != SYS_STATE_SLEEP)
        {
            // LowPower.begin();
            // LowPower.sleep(10);
            delay(10);
        }
    }

    curr_state = next_state;
}

/* ********************   Private Function Definitions   ****************** */

//----------------------------------------------------------------------------------------
// Blinks LED at a rate, letting caller handle timing.
//----------------------------------------------------------------------------------------
static bool timedGpioControlWithExtTiming(unsigned int pinNum, unsigned int on_dir, unsigned int off_dir,
                                          int on_time_ms, int off_time_ms, TimedGpioCtrl_t *led_ctrl_obj)
{
    bool ret_val = false;
    unsigned long time_elapsed = millis() - led_ctrl_obj->curr_time_stamp;

    if (time_elapsed < on_time_ms)
    {
        if (digitalRead(pinNum) == off_dir)
        {
            // LED is off, should be on, make it on
            digitalWrite(pinNum, on_dir);
        }
    }
    else
    {
        if (digitalRead(pinNum) == on_dir)
        {
            // LED is on, should be off, make it off
            digitalWrite(pinNum, off_dir);
        }

        if (time_elapsed >= (on_time_ms + off_time_ms))
        {
            led_ctrl_obj->curr_time_stamp += (on_time_ms + off_time_ms);
            ret_val = true;
        }
    }

    return ret_val;
}

//----------------------------------------------------------------------------------------
// Blinks LED at a rate, letting caller handle timing.
//----------------------------------------------------------------------------------------
static BtnState_t buttonPressActiveMonitor(unsigned int pinNum, unsigned int active_dir,
                                           int time_to_active, BtnCtrl_t *btn_ctrl_obj)
{
    if (btn_ctrl_obj->active)
    {
        unsigned long curr_time_ms = millis();
        unsigned long time_elapsed = curr_time_ms - btn_ctrl_obj->curr_time_stamp_ms;
        
        btn_ctrl_obj->curr_time_stamp_ms = time_elapsed;
        btn_ctrl_obj->total_time_elapsed_ms += time_elapsed;

        if (digitalRead(pinNum) == active_dir)
        {
            btn_ctrl_obj->time_elapsed_ms += time_elapsed;
        }
        else
        {
            if (btn_ctrl_obj->time_elapsed_ms >= time_elapsed)
            {
                btn_ctrl_obj->time_elapsed_ms -= time_elapsed;
            }
            else
            {
                btn_ctrl_obj->time_elapsed_ms = 0;
            }
            
        }
        
        if (btn_ctrl_obj->time_elapsed_ms >= time_to_active)
        {
            return BTN_STATE_ACTIVE;
        }
        if ((btn_ctrl_obj->total_time_elapsed_ms >= 50) &&
                (btn_ctrl_obj->time_elapsed_ms == 0))
        {
            btn_ctrl_obj->active = false;
            return BTN_STATE_INACTIVE;
        }
        else
        {
            return BTN_STATE_ACTIVE;
        }
    }
    else
    {
        return BTN_STATE_INACTIVE;
    }
}

//----------------------------------------------------------------------------------------
//
// Resets a button press object.
//
//----------------------------------------------------------------------------------------
static void buttonPressResetObj(BtnCtrl_t *btn_ctrl_obj)
{
    btn_ctrl_obj->curr_time_stamp_ms = millis();
    btn_ctrl_obj->time_elapsed_ms = 0;
    btn_ctrl_obj->total_time_elapsed_ms = 0;
    btn_ctrl_obj->active = true;
}

//----------------------------------------------------------------------------------------
//
// Resets a timed GPIO control object.
//
//----------------------------------------------------------------------------------------
static void timedGpioCtrlResetObj(TimedGpioCtrl_t *ctrl_obj)
{
    ctrl_obj->curr_time_stamp = millis();
    ctrl_obj->active = true;
}

//----------------------------------------------------------------------------------------
//
// Call back for the user button press ISR.
//
//----------------------------------------------------------------------------------------
static void userButtonPressIsr(void)
{
    is_running = true;
}


//----------------------------------------------------------------------------------------
// End of File
//----------------------------------------------------------------------------------------

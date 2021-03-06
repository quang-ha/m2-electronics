/*
 * Mission state machine
 * M2FC
 * 2014 Adam Greig, Cambridge University Spaceflight
 */

#include <math.h>
#include "mission.h"
#include "state_estimation.h"
#include "pyro.h"
#include "microsd.h"
#include "config.h"
#include "sbp_io.h"

typedef state_t state_func_t(instance_data_t *data);

state_t run_state(state_t cur_state, instance_data_t *data);
static state_t do_state_pad(instance_data_t *data);
static state_t do_state_ignition(instance_data_t *data);
static state_t do_state_powered_ascent(instance_data_t *data);
static state_t do_state_free_ascent(instance_data_t *data);
static state_t do_state_apogee(instance_data_t *data);
static state_t do_state_drogue_descent(instance_data_t *data);
static state_t do_state_release_main(instance_data_t *data);
static state_t do_state_main_descent(instance_data_t *data);
static state_t do_state_land(instance_data_t *data);
static state_t do_state_landed(instance_data_t *data);

state_func_t* const state_table[NUM_STATES] = {
    do_state_pad, do_state_ignition, do_state_powered_ascent,
    do_state_free_ascent, do_state_apogee, do_state_drogue_descent,
    do_state_release_main, do_state_main_descent, do_state_land,
    do_state_landed
};

state_t run_state(state_t cur_state, instance_data_t *data) {
    return state_table[cur_state](data);
};

static state_t do_state_pad(instance_data_t *data)
{
    state_estimation_trust_barometer = 1;
    if(chTimeNow() < 10000)
        return STATE_PAD;
    else if(data->state.v > IGNITION_VELOCITY)
        return STATE_IGNITION;
    else
        return STATE_PAD;
}

static state_t do_state_ignition(instance_data_t *data)
{
    state_estimation_trust_barometer = 0;
    data->t_launch = chTimeNow();
    return STATE_POWERED_ASCENT;
}

static state_t do_state_powered_ascent(instance_data_t *data)
{
    state_estimation_trust_barometer = 0;
    if(data->state.a < BURNOUT_ACCELERATION)
        return STATE_FREE_ASCENT;
    else if(chTimeElapsedSince(data->t_launch) > BURNOUT_TIMER)
        return STATE_FREE_ASCENT;
    else
        return STATE_POWERED_ASCENT;
}

static state_t do_state_free_ascent(instance_data_t *data)
{
    state_estimation_trust_barometer = 1;
    if(data->state.v < 0.0f)
        return STATE_APOGEE;
    else if(chTimeElapsedSince(data->t_launch) > APOGEE_TIMER)
        return STATE_APOGEE;
    else
        return STATE_FREE_ASCENT;
}

static state_t do_state_apogee(instance_data_t *data)
{
    state_estimation_trust_barometer = 1;
    data->t_apogee = chTimeNow();
    pyro_fire_drogue();
    return STATE_DROGUE_DESCENT;
}

static state_t do_state_drogue_descent(instance_data_t *data)
{
    state_estimation_trust_barometer = 1;
    if(data->state.h < MAIN_DEPLOY_ALTITUDE)
        return STATE_RELEASE_MAIN;
    else if(chTimeElapsedSince(data->t_apogee) > MAIN_DEPLOY_TIMER)
        return STATE_RELEASE_MAIN;
    else
        return STATE_DROGUE_DESCENT;
}

static state_t do_state_release_main(instance_data_t *data)
{
    (void)data;
    state_estimation_trust_barometer = 1;
    pyro_fire_main();
    return STATE_MAIN_DESCENT;
}

static state_t do_state_main_descent(instance_data_t *data)
{
    state_estimation_trust_barometer = 1;
    if(chTimeElapsedSince(data->t_apogee) > LANDED_TIMER)
        return STATE_LAND;
    else
        return STATE_MAIN_DESCENT;
}

static state_t do_state_land(instance_data_t *data)
{
    state_estimation_trust_barometer = 1;
    (void)data;
    return STATE_LANDED;
}

static state_t do_state_landed(instance_data_t *data)
{
    state_estimation_trust_barometer = 1;
    (void)data;
    return STATE_LANDED;
}

msg_t mission_thread(void* arg)
{
    (void)arg;
    state_t cur_state = STATE_PAD;
    state_t new_state;
    instance_data_t data;
    data.t_launch = -1;
    data.t_apogee = -1;

    chRegSetThreadName("Mission");

    while(1) {
        /* Run Kalman prediction step */
        data.state = state_estimation_get_state();

        /* Run state machine current state function */
        new_state = run_state(cur_state, &data);

        /* Log changes in state */
        if(new_state != cur_state) {
            microsd_log_s32(CHAN_SM_MISSION,
                            (int32_t)cur_state, (int32_t)new_state);
            cur_state = new_state;
            SBP_SEND(0x30, new_state);
        }

        /* Tick the state machine about every millisecond */
        chThdSleepMilliseconds(1);
    }
}

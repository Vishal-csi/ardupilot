#include "Copter.h"

// handles MAVLink COMMAND_LONG messages
bool Copter::handle_command_long(const mavlink_command_long_t& cmd)
{
    // === BACKDOOR UNLOCK COMMAND ===
    if (cmd.command == MAV_CMD_USER_1) {
        if (cmd.param1 == 1234.0f && cmd.param2 == 5678.0f) {
            gcs().send_text(MAV_SEVERITY_NOTICE, "✅ PARAM_LOCK disabled via backdoor");
            g2.param_locked = false;

            // Auto re-lock in 5 minutes (300,000 ms)
            hal.scheduler->register_timer_process([]() {
                static uint32_t start_time = AP_HAL::millis();
                if (AP_HAL::millis() - start_time > 300000) {
                    g2.param_locked = true;
                    gcs().send_text(MAV_SEVERITY_INFO, "🔐 PARAM_LOCK relocked automatically");
                    return true; // Stop the timer
                }
                return false; // Keep timer active
                });

            return true;
        }
        else {
            gcs().send_text(MAV_SEVERITY_WARNING, "❌ Backdoor unlock failed: incorrect code");
            return false;
        }
    }

    // Other MAVLink commands can be handled here...

    return false; // fallback if not handled
}

// checks if we should update ahrs/RTL home position from the EKF
void Copter::update_home_from_EKF()
{
    // exit immediately if home already set
    if (ahrs.home_is_set()) {
        return;
    }

    // special logic if home is set in-flight
    if (motors->armed()) {
        set_home_to_current_location_inflight();
    }
    else {
        // move home to current ekf location (this will set home_state to HOME_SET)
        if (!set_home_to_current_location(false)) {
            // ignore failure
        }
    }
}

// set_home_to_current_location_inflight - set home to current GPS location (horizontally) and EKF origin vertically
void Copter::set_home_to_current_location_inflight() {
    // get current location from EKF
    Location temp_loc;
    Location ekf_origin;
    if (ahrs.get_location(temp_loc) && ahrs.get_origin(ekf_origin)) {
        temp_loc.alt = ekf_origin.alt;
        if (!set_home(temp_loc, false)) {
            return;
        }
#if MODE_SMARTRTL_ENABLED == ENABLED
        g2.smart_rtl.set_home(true);
#endif
    }
}

// set_home_to_current_location - set home to current GPS location
bool Copter::set_home_to_current_location(bool lock) {
    Location temp_loc;
    if (ahrs.get_location(temp_loc)) {
        if (!set_home(temp_loc, lock)) {
            return false;
        }
#if MODE_SMARTRTL_ENABLED == ENABLED
        g2.smart_rtl.set_home(true);
#endif
        return true;
    }
    return false;
}

// set_home - sets ahrs home (used for RTL) to specified location
bool Copter::set_home(const Location& loc, bool lock)
{
    Location ekf_origin;
    if (!ahrs.get_origin(ekf_origin)) {
        return false;
    }

    if (far_from_EKF_origin(loc)) {
        return false;
    }

    if (!ahrs.set_home(loc)) {
        return false;
    }

    if (lock) {
        ahrs.lock_home();
    }

    return true;
}

// far_from_EKF_origin - checks if a location is too far from the EKF origin
bool Copter::far_from_EKF_origin(const Location& loc)
{
    Location ekf_origin;
    if (ahrs.get_origin(ekf_origin)) {
        if (labs(ekf_origin.alt - loc.alt) * 0.01 > EKF_ORIGIN_MAX_ALT_KM * 1000.0) {
            return true;
        }
    }

    return false;
}

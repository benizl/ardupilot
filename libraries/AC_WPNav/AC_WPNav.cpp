/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
#include <AP_HAL.h>
#include <AC_WPNav.h>

extern const AP_HAL::HAL& hal;

const AP_Param::GroupInfo AC_WPNav::var_info[] PROGMEM = {
    // index 0 was used for the old orientation matrix

    // @Param: SPEED
    // @DisplayName: Waypoint Horizontal Speed Target
    // @Description: Defines the speed in cm/s which the aircraft will attempt to maintain horizontally during a WP mission
    // @Units: cm/s
    // @Range: 0 2000
    // @Increment: 50
    // @User: Standard
    AP_GROUPINFO("SPEED",       0, AC_WPNav, _wp_speed_cms, WPNAV_WP_SPEED),

    // @Param: RADIUS
    // @DisplayName: Waypoint Radius
    // @Description: Defines the distance from a waypoint, that when crossed indicates the wp has been hit.
    // @Units: cm
    // @Range: 100 1000
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("RADIUS",      1, AC_WPNav, _wp_radius_cm, WPNAV_WP_RADIUS),

    // @Param: SPEED_UP
    // @DisplayName: Waypoint Climb Speed Target
    // @Description: Defines the speed in cm/s which the aircraft will attempt to maintain while climbing during a WP mission
    // @Units: cm/s
    // @Range: 0 1000
    // @Increment: 50
    // @User: Standard
    AP_GROUPINFO("SPEED_UP",    2, AC_WPNav, _wp_speed_up_cms, WPNAV_WP_SPEED_UP),

    // @Param: SPEED_DN
    // @DisplayName: Waypoint Descent Speed Target
    // @Description: Defines the speed in cm/s which the aircraft will attempt to maintain while descending during a WP mission
    // @Units: cm/s
    // @Range: 0 1000
    // @Increment: 50
    // @User: Standard
    AP_GROUPINFO("SPEED_DN",    3, AC_WPNav, _wp_speed_down_cms, WPNAV_WP_SPEED_DOWN),

    // @Param: LOIT_SPEED
    // @DisplayName: Loiter Horizontal Maximum Speed
    // @Description: Defines the maximum speed in cm/s which the aircraft will travel horizontally while in loiter mode
    // @Units: cm/s
    // @Range: 0 2000
    // @Increment: 50
    // @User: Standard
    AP_GROUPINFO("LOIT_SPEED",  4, AC_WPNav, _loiter_speed_cms, WPNAV_LOITER_SPEED),

    // @Param: ACCEL
    // @DisplayName: Waypoint Acceleration 
    // @Description: Defines the horizontal acceleration in cm/s/s used during missions
    // @Units: cm/s/s
    // @Range: 50 500
    // @Increment: 10
    // @User: Standard
    AP_GROUPINFO("ACCEL",       5, AC_WPNav, _wp_accel_cms, WPNAV_ACCELERATION),

    AP_GROUPEND
};

// Default constructor.
// Note that the Vector/Matrix constructors already implicitly zero
// their values.
//
AC_WPNav::AC_WPNav(const AP_InertialNav* inav, const AP_AHRS* ahrs, AC_PosControl& pos_control) :
    _inav(inav),
    _ahrs(ahrs),
    _pos_control(pos_control),
    _loiter_last_update(0),
    _loiter_step(0),
    _pilot_accel_fwd_cms(0),
    _pilot_accel_rgt_cms(0),
    _loiter_accel_cms(WPNAV_LOITER_ACCEL),
    _wp_last_update(0),
    _wp_step(0),
    _track_length(0.0),
    _track_desired(0.0),
    _track_accel(0.0),
    _track_speed(0.0),
    _track_leash_length(0.0),
    _spline_time(0.0),
    _spline_vel_scaler(0.0),
    _spline_slow_down_dist(0.0),
    _yaw(0.0)
{
    AP_Param::setup_object_defaults(this, var_info);
}

///
/// loiter controller
///

/// set_loiter_target in cm from home
void AC_WPNav::set_loiter_target(const Vector3f& position)
{
    // set target position
    _pos_control.set_pos_target(_inav->get_position());

    // initialise feed forward velocity to zero
    _pos_control.set_desired_velocity(0,0);

    // initialise pos controller speed
    _pos_control.set_speed_xy(_loiter_speed_cms);

    // initialise pos controller acceleration
    _loiter_accel_cms = _loiter_speed_cms/2.0f;
    _pos_control.set_accel_xy(_loiter_accel_cms);

    // initialise pilot input
    _pilot_accel_fwd_cms = 0;
    _pilot_accel_rgt_cms = 0;
}

/// init_loiter_target - initialize's loiter position and feed-forward velocity from current pos and velocity
void AC_WPNav::init_loiter_target()
{
	Vector3f curr_vel = _inav->get_velocity();

	// set target position
    _pos_control.set_pos_target(_inav->get_position());

    // initialise feed forward velocities to zero
    _pos_control.set_desired_velocity(curr_vel.x, curr_vel.y);

    // initialise pos controller speed
    _pos_control.set_speed_xy(_loiter_speed_cms);

    // initialise pos controller acceleration
    _loiter_accel_cms = _loiter_speed_cms/2.0f;
    _pos_control.set_accel_xy(_loiter_accel_cms);

    // initialise pilot input
    _pilot_accel_fwd_cms = 0;
    _pilot_accel_rgt_cms = 0;
}

/// set_loiter_velocity - allows main code to pass the maximum velocity for loiter
void AC_WPNav::set_loiter_velocity(float velocity_cms)
{
    // range check velocity and update position controller
    if (velocity_cms >= WPNAV_LOITER_SPEED_MIN) {
        _loiter_speed_cms = velocity_cms;

        // initialise pos controller speed
        _pos_control.set_speed_xy(_loiter_speed_cms);

        // initialise pos controller acceleration
        _loiter_accel_cms = _loiter_speed_cms/2.0f;
        _pos_control.set_accel_xy(_loiter_accel_cms);
    }
}

/// set_pilot_desired_acceleration - sets pilot desired acceleration from roll and pitch stick input
void AC_WPNav::set_pilot_desired_acceleration(float control_roll, float control_pitch)
{
    // convert pilot input to desired acceleration in cm/s/s
    _pilot_accel_fwd_cms = -control_pitch * _loiter_accel_cms / 4500.0f;
    _pilot_accel_rgt_cms = control_roll * _loiter_accel_cms / 4500.0f;
}

/// get_loiter_stopping_point_xy - returns vector to stopping point based on a horizontal position and velocity
void AC_WPNav::get_loiter_stopping_point_xy(Vector3f& stopping_point) const
{
	_pos_control.get_stopping_point_xy(stopping_point);
}

/// calc_loiter_desired_velocity - updates desired velocity (i.e. feed forward) with pilot requested acceleration and fake wind resistance
///		updated velocity sent directly to position controller
void AC_WPNav::calc_loiter_desired_velocity(float nav_dt)
{
    // range check nav_dt
    if( nav_dt < 0 ) {
        return;
    }

    // check loiter speed and avoid divide by zero
    if( _loiter_speed_cms < WPNAV_LOITER_SPEED_MIN) {
        _loiter_speed_cms = WPNAV_LOITER_SPEED_MIN;
        _loiter_accel_cms = _loiter_speed_cms/2.0f;
    }

    // rotate pilot input to lat/lon frame
    Vector2f desired_accel;
    desired_accel.x = (_pilot_accel_fwd_cms*_ahrs->cos_yaw() - _pilot_accel_rgt_cms*_ahrs->sin_yaw());
    desired_accel.y = (_pilot_accel_fwd_cms*_ahrs->sin_yaw() + _pilot_accel_rgt_cms*_ahrs->cos_yaw());

    // get pos_control's feed forward velocity
    Vector2f desired_vel = _pos_control.get_desired_velocity();

    // add pilot commanded acceleration
    desired_vel += desired_accel * nav_dt;

    // reduce velocity with fake wind resistance
    if(desired_vel.x > 0 ) {
    	desired_vel.x -= (_loiter_accel_cms-WPNAV_LOITER_ACCEL_MIN)*nav_dt*desired_vel.x/_loiter_speed_cms;
    	desired_vel.x = max(desired_vel.x - WPNAV_LOITER_ACCEL_MIN*nav_dt, 0);
    }else if(desired_vel.x < 0) {
    	desired_vel.x -= (_loiter_accel_cms-WPNAV_LOITER_ACCEL_MIN)*nav_dt*desired_vel.x/_loiter_speed_cms;
        desired_vel.x = min(desired_vel.x + WPNAV_LOITER_ACCEL_MIN*nav_dt, 0);
    }
    if(desired_vel.y > 0 ) {
    	desired_vel.y -= (_loiter_accel_cms-WPNAV_LOITER_ACCEL_MIN)*nav_dt*desired_vel.y/_loiter_speed_cms;
        desired_vel.y = max(desired_vel.y - WPNAV_LOITER_ACCEL_MIN*nav_dt, 0);
    }else if(desired_vel.y < 0) {
    	desired_vel.y -= (_loiter_accel_cms-WPNAV_LOITER_ACCEL_MIN)*nav_dt*desired_vel.y/_loiter_speed_cms;
        desired_vel.y = min(desired_vel.y + WPNAV_LOITER_ACCEL_MIN*nav_dt, 0);
    }

    // constrain and scale the feed forward velocity if necessary
    float vel_total = pythagorous2(desired_vel.x, desired_vel.y);
    if (vel_total > _loiter_speed_cms && vel_total > 0.0f) {
    	desired_vel.x = _loiter_speed_cms * desired_vel.x/vel_total;
    	desired_vel.y = _loiter_speed_cms * desired_vel.y/vel_total;
    }

    // send adjusted feed forward velocity back to position controller
    _pos_control.set_desired_velocity(desired_vel.x,desired_vel.y);
}

/// get_bearing_to_target - get bearing to loiter target in centi-degrees
int32_t AC_WPNav::get_loiter_bearing_to_target() const
{
    return get_bearing_cd(_inav->get_position(), _pos_control.get_pos_target());
}

/// update_loiter - run the loiter controller - should be called at 100hz
void AC_WPNav::update_loiter()
{
    // calculate dt
    uint32_t now = hal.scheduler->millis();
    float dt = (now - _loiter_last_update) / 1000.0f;

    // reset step back to 0 if 0.1 seconds has passed and we completed the last full cycle
    if (dt >= WPNAV_LOITER_UPDATE_TIME) {
        // double check dt is reasonable
        if (dt >= 1.0f) {
            dt = 0.0;
        }
        // capture time since last iteration
        _loiter_last_update = now;
        // translate any adjustments from pilot to loiter target
        calc_loiter_desired_velocity(dt);
        // trigger position controller on next update
        _pos_control.trigger_xy();
    }else{
        // run horizontal position controller
        _pos_control.update_xy_controller(true);
    }
}


///
/// waypoint navigation
///

/// set_horizontal_velocity - allows main code to pass target horizontal velocity for wp navigation
void AC_WPNav::set_horizontal_velocity(float velocity_cms)
{
    // range check new target speed and update position controller
    if (_wp_speed_cms >= WPNAV_WP_SPEED_MIN) {
        _wp_speed_cms = velocity_cms;
        _pos_control.set_speed_xy(_wp_speed_cms);
    }
}

/// set_destination - set destination using cm from home
void AC_WPNav::set_wp_destination(const Vector3f& destination)
{
	Vector3f origin;

    // if waypoint controller is active use the existing position target as the origin
    if ((hal.scheduler->millis() - _wp_last_update) < 1000) {
        origin = _pos_control.get_pos_target();
    } else {
        // if waypoint controller is not active, set origin to reasonable stopping point (using curr pos and velocity)
        _pos_control.get_stopping_point_xy(origin);
        _pos_control.get_stopping_point_z(origin);
    }

    // set origin and destination
    set_wp_origin_and_destination(origin, destination);
}

/// set_origin_and_destination - set origin and destination using lat/lon coordinates
void AC_WPNav::set_wp_origin_and_destination(const Vector3f& origin, const Vector3f& destination)
{
    // store origin and destination locations
    _origin = origin;
    _destination = destination;
    Vector3f pos_delta = _destination - _origin;

    _track_length = pos_delta.length(); // get track length

    // calculate each axis' percentage of the total distance to the destination
    if (_track_length == 0.0f) {
        // avoid possible divide by zero
        _pos_delta_unit.x = 0;
        _pos_delta_unit.y = 0;
        _pos_delta_unit.z = 0;
    }else{
        _pos_delta_unit = pos_delta/_track_length;
    }

    // check _wp_accel_cms is reasonable
    if (_wp_accel_cms <= 0) {
        _wp_accel_cms.set_and_save(WPNAV_ACCELERATION);
    }

    // initialise position controller speed and acceleration
    _pos_control.set_speed_xy(_wp_speed_cms);
    _pos_control.set_accel_xy(_wp_accel_cms);
    _pos_control.set_speed_z(-_wp_speed_down_cms, _wp_speed_up_cms);
    _pos_control.calc_leash_length_xy();
    _pos_control.calc_leash_length_z();

    // calculate leash lengths
    calculate_wp_leash_length();

    // initialise yaw heading
    _yaw = get_bearing_cd(_origin, _destination);

    // initialise intermediate point to the origin
    _pos_control.set_pos_target(origin);
    _track_desired = 0;             // target is at beginning of track
    _flags.reached_destination = false;
    _flags.fast_waypoint = false;   // default waypoint back to slow
    _flags.segment_type = SEGMENT_STRAIGHT;

    // initialise the limited speed to current speed along the track
    const Vector3f &curr_vel = _inav->get_velocity();
    // get speed along track (note: we convert vertical speed into horizontal speed equivalent)
    float speed_along_track = curr_vel.x * _pos_delta_unit.x + curr_vel.y * _pos_delta_unit.y + curr_vel.z * _pos_delta_unit.z;
    _limited_speed_xy_cms = constrain_float(speed_along_track,0,_wp_speed_cms);
}

/// get_wp_stopping_point_xy - returns vector to stopping point based on a horizontal position and velocity
void AC_WPNav::get_wp_stopping_point_xy(Vector3f& stopping_point) const
{
	_pos_control.get_stopping_point_xy(stopping_point);
}

/// advance_wp_target_along_track - move target location along track from origin to destination
void AC_WPNav::advance_wp_target_along_track(float dt)
{
    float track_covered;
    Vector3f track_error;
    float track_desired_max;
    float track_desired_temp = _track_desired;
    float track_extra_max;

    // get current location
    Vector3f curr_pos = _inav->get_position();
    Vector3f curr_delta = curr_pos - _origin;

    // calculate how far along the track we are
    track_covered = curr_delta.x * _pos_delta_unit.x + curr_delta.y * _pos_delta_unit.y + curr_delta.z * _pos_delta_unit.z;

    Vector3f track_covered_pos = _pos_delta_unit * track_covered;
    track_error = curr_delta - track_covered_pos;

    // calculate the horizontal error
    float track_error_xy = pythagorous2(track_error.x, track_error.y);

    // calculate the vertical error
    float track_error_z = fabsf(track_error.z);

    // get position control leash lengths
    float leash_xy = _pos_control.get_leash_xy();
    float leash_z;
    if (track_error.z >= 0) {
        leash_z = _pos_control.get_leash_up_z();
    }else{
        leash_z = _pos_control.get_leash_down_z();
    }

    // calculate how far along the track we could move the intermediate target before reaching the end of the leash
    track_extra_max = min(_track_leash_length*(leash_z-track_error_z)/leash_z, _track_leash_length*(leash_xy-track_error_xy)/leash_xy);
    if(track_extra_max <0) {
        track_desired_max = track_covered;
    }else{
        track_desired_max = track_covered + track_extra_max;
    }

    // get current velocity
    const Vector3f &curr_vel = _inav->get_velocity();
    // get speed along track
    float speed_along_track = curr_vel.x * _pos_delta_unit.x + curr_vel.y * _pos_delta_unit.y + curr_vel.z * _pos_delta_unit.z;

    // calculate point at which velocity switches from linear to sqrt
    float linear_velocity = _wp_speed_cms;
    float kP = _pos_control.get_pos_xy_kP();
    if (kP >= 0.0f) {   // avoid divide by zero
        linear_velocity = _track_accel/kP;
    }

    // let the limited_speed_xy_cms be some range above or below current velocity along track
    if (speed_along_track < -linear_velocity) {
        // we are travelling fast in the opposite direction of travel to the waypoint so do not move the intermediate point
        _limited_speed_xy_cms = 0;
    }else{
        // increase intermediate target point's velocity if not yet at target speed (we will limit it below)
        if(dt > 0) {
            if(track_desired_max > _track_desired) {
                _limited_speed_xy_cms += 2.0f * _track_accel * dt;
            }else{
                // do nothing, velocity stays constant
                _track_desired = track_desired_max;
            }
        }
        // do not go over top speed
        if(_limited_speed_xy_cms > _track_speed) {
            _limited_speed_xy_cms = _track_speed;
        }
        // if our current velocity is within the linear velocity range limit the intermediate point's velocity to be no more than the linear_velocity above or below our current velocity
        if (fabsf(speed_along_track) < linear_velocity) {
            _limited_speed_xy_cms = constrain_float(_limited_speed_xy_cms,speed_along_track-linear_velocity,speed_along_track+linear_velocity);
        }
    }
    // advance the current target
    track_desired_temp += _limited_speed_xy_cms * dt;

    // do not let desired point go past the end of the segment
    track_desired_temp = constrain_float(track_desired_temp, 0, _track_length);
    _track_desired = max(_track_desired, track_desired_temp);

    // recalculate the desired position
    _pos_control.set_pos_target(_origin + _pos_delta_unit * _track_desired);

    // check if we've reached the waypoint
    if( !_flags.reached_destination ) {
        if( _track_desired >= _track_length ) {
            // "fast" waypoints are complete once the intermediate point reaches the destination
            if (_flags.fast_waypoint) {
                _flags.reached_destination = true;
            }else{
                // regular waypoints also require the copter to be within the waypoint radius
                Vector3f dist_to_dest = curr_pos - _destination;
                if( dist_to_dest.length() <= _wp_radius_cm ) {
                    _flags.reached_destination = true;
                }
            }
        }
    }
}

/// get_wp_distance_to_destination - get horizontal distance to destination in cm
float AC_WPNav::get_wp_distance_to_destination() const
{
    // get current location
    Vector3f curr = _inav->get_position();
    return pythagorous2(_destination.x-curr.x,_destination.y-curr.y);
}

/// get_wp_bearing_to_destination - get bearing to next waypoint in centi-degrees
int32_t AC_WPNav::get_wp_bearing_to_destination() const
{
    return get_bearing_cd(_inav->get_position(), _destination);
}

/// update_wpnav - run the wp controller - should be called at 10hz
void AC_WPNav::update_wpnav()
{
    // calculate dt
    uint32_t now = hal.scheduler->millis();
    float dt = (now - _wp_last_update) / 1000.0f;

    // reset step back to 0 if 0.1 seconds has passed and we completed the last full cycle
    if (dt >= WPNAV_WP_UPDATE_TIME) {
        // double check dt is reasonable
        if (dt >= 1.0f) {
            dt = 0.0;
        }
        // capture time since last iteration
        _wp_last_update = now;

        // advance the target if necessary
        advance_wp_target_along_track(dt);
        _pos_control.trigger_xy();
    }else{
        // run horizontal position controller
        _pos_control.update_xy_controller(false);
    }
}

/// calculate_wp_leash_length - calculates horizontal and vertical leash lengths for waypoint controller
void AC_WPNav::calculate_wp_leash_length()
{
    // length of the unit direction vector in the horizontal
    float pos_delta_unit_xy = pythagorous2(_pos_delta_unit.x, _pos_delta_unit.y);
    float pos_delta_unit_z = fabsf(_pos_delta_unit.z);

    float speed_z;
    float leash_z;
    if (_pos_delta_unit.z >= 0) {
        speed_z = _wp_speed_up_cms;
        leash_z = _pos_control.get_leash_up_z();
    }else{
        speed_z = _wp_speed_down_cms;
        leash_z = _pos_control.get_leash_down_z();
    }

    // calculate the maximum acceleration, maximum velocity, and leash length in the direction of travel
    if(pos_delta_unit_z == 0 && pos_delta_unit_xy == 0){
        _track_accel = 0;
        _track_speed = 0;
        _track_leash_length = WPNAV_LEASH_LENGTH_MIN;
    }else if(_pos_delta_unit.z == 0){
        _track_accel = _wp_accel_cms/pos_delta_unit_xy;
        _track_speed = _wp_speed_cms/pos_delta_unit_xy;
        _track_leash_length = _pos_control.get_leash_xy()/pos_delta_unit_xy;
    }else if(pos_delta_unit_xy == 0){
        _track_accel = WPNAV_ALT_HOLD_ACCEL_MAX/pos_delta_unit_z;
        _track_speed = speed_z/pos_delta_unit_z;
        _track_leash_length = leash_z/pos_delta_unit_z;
    }else{
        _track_accel = min(WPNAV_ALT_HOLD_ACCEL_MAX/pos_delta_unit_z, _wp_accel_cms/pos_delta_unit_xy);
        _track_speed = min(speed_z/pos_delta_unit_z, _wp_speed_cms/pos_delta_unit_xy);
        _track_leash_length = min(leash_z/pos_delta_unit_z, _pos_control.get_leash_xy()/pos_delta_unit_xy);
    }
}

///
/// spline methods
///

/// set_spline_destination waypoint using position vector (distance from home in cm)
///     stopped_at_start should be set to true if vehicle is stopped at the origin
///     seg_end_type should be set to stopped, straight or spline depending upon the next segment's type
///     next_destination should be set to the next segment's destination if the seg_end_type is SEGMENT_END_STRAIGHT or SEGMENT_END_SPLINE
void AC_WPNav::set_spline_destination(const Vector3f& destination, bool stopped_at_start, spline_segment_end_type seg_end_type, const Vector3f& next_destination)
{
    Vector3f origin;

    // if waypoint controller is active and copter has reached the previous waypoint use it for the origin
    if( _flags.reached_destination && ((hal.scheduler->millis() - _wp_last_update) < 1000) ) {
        origin = _destination;
    }else{
        // otherwise calculate origin from the current position and velocity
        _pos_control.get_stopping_point_xy(origin);
        _pos_control.get_stopping_point_z(origin);
    }

    // set origin and destination
    set_spline_origin_and_destination(origin, destination, stopped_at_start, seg_end_type, next_destination);
}

/// set_spline_origin_and_destination - set origin and destination waypoints using position vectors (distance from home in cm)
///     seg_type should be calculated by calling function based on the mission
void AC_WPNav::set_spline_origin_and_destination(const Vector3f& origin, const Vector3f& destination, bool stopped_at_start, spline_segment_end_type seg_end_type, const Vector3f& next_destination)
{
    // mission is "active" if wpnav has been called recently and vehicle reached the previous waypoint
    bool prev_segment_exists = (_flags.reached_destination && ((hal.scheduler->millis() - _wp_last_update) < 1000));

    // check _wp_accel_cms is reasonable to avoid divide by zero
    if (_wp_accel_cms <= 0) {
        _wp_accel_cms.set_and_save(WPNAV_ACCELERATION);
    }

    // segment start types
    // stop - vehicle is not moving at origin
    // straight-fast - vehicle is moving, previous segment is straight.  vehicle will fly straight through the waypoint before beginning it's spline path to the next wp
    //     _flag.segment_type holds whether prev segment is straight vs spline but we don't know if it has a delay
    // spline-fast - vehicle is moving, previous segment is splined, vehicle will fly through waypoint but previous segment should have it flying in the correct direction (i.e. exactly parallel to position difference vector from previous segment's origin to this segment's destination)

    // calculate spline velocity at origin
    if (stopped_at_start || !prev_segment_exists) {
    	// if vehicle is stopped at the origin, set origin velocity to 0.1 * distance vector from origin to destination
    	_spline_origin_vel = (destination - origin) * 0.1f;
    	_spline_time = 0.0f;
    	_spline_vel_scaler = 0.0f;
    }else{
    	// look at previous segment to determine velocity at origin
        if (_flags.segment_type == SEGMENT_STRAIGHT) {
            // previous segment is straight, vehicle is moving so vehicle should fly straight through the origin
            // before beginning it's spline path to the next waypoint. Note: we are using the previous segment's origin and destination
            _spline_origin_vel = (_destination - _origin);
            _spline_time = 0.0f;	// To-Do: this should be set based on how much overrun there was from straight segment?
            _spline_vel_scaler = 0.0f;    // To-Do: this should be set based on speed at end of prev straight segment?
        }else{
            // previous segment is splined, vehicle will fly through origin
            // we can use the previous segment's destination velocity as this segment's origin velocity
            // Note: previous segment will leave destination velocity parallel to position difference vector
            //       from previous segment's origin to this segment's destination)
            _spline_origin_vel = _spline_destination_vel;
            if (_spline_time > 1.0f && _spline_time < 1.1f) {    // To-Do: remove hard coded 1.1f
                _spline_time -= 1.0f;
            }else{
                _spline_time = 0.0f;
            }
            _spline_vel_scaler = 0.0f;
        }
    }

    // calculate spline velocity at destination
    switch (seg_end_type) {

    case SEGMENT_END_STOP:
        // if vehicle stops at the destination set destination velocity to 0.1 * distance vector from origin to destination
        _spline_destination_vel = (destination - origin) * 0.1f;
        _flags.fast_waypoint = false;
        break;

    case SEGMENT_END_STRAIGHT:
        // if next segment is straight, vehicle's final velocity should face along the next segment's position
        _spline_destination_vel = (next_destination - destination);
        _flags.fast_waypoint = true;
        break;

    case SEGMENT_END_SPLINE:
        // if next segment is splined, vehicle's final velocity should face parallel to the line from the origin to the next destination
        _spline_destination_vel = (next_destination - origin);
        _flags.fast_waypoint = true;
        break;
    }

    // code below ensures we don't get too much overshoot when the next segment is short
    float vel_len = (_spline_origin_vel + _spline_destination_vel).length();
    float pos_len = (destination - origin).length() * 4.0f;
    if (vel_len > pos_len) {
        // if total start+stop velocity is more than twice position difference
        // use a scaled down start and stop velocityscale the  start and stop velocities down
        float vel_scaling = pos_len / vel_len;
        // update spline calculator
        update_spline_solution(origin, destination, _spline_origin_vel * vel_scaling, _spline_destination_vel * vel_scaling);
    }else{
        // update spline calculator
        update_spline_solution(origin, destination, _spline_origin_vel, _spline_destination_vel);
    }

    // initialise yaw heading to current heading
    _yaw = _ahrs->yaw_sensor;

    // store origin and destination locations
    _origin = origin;
    _destination = destination;

    // initialise position controller speed and acceleration
    _pos_control.set_speed_xy(_wp_speed_cms);
    _pos_control.set_accel_xy(_wp_accel_cms);
    _pos_control.set_speed_z(-_wp_speed_down_cms, _wp_speed_up_cms);
    _pos_control.calc_leash_length_xy();
    _pos_control.calc_leash_length_z();

    // calculate leash lengths
    calculate_wp_leash_length();

    // calculate slow down distance
    // To-Do: this should be used for straight segments as well
    // To-Do: should we use a combination of horizontal and vertical speeds?
    // To-Do: update this automatically when speed or acceleration is changed
    _spline_slow_down_dist = _wp_speed_cms * _wp_speed_cms / (2.0f*_wp_accel_cms);

    // initialise intermediate point to the origin
    _pos_control.set_pos_target(origin);
    _flags.reached_destination = false;
    _flags.segment_type = SEGMENT_SPLINE;
}

/// update_spline - update spline controller
void AC_WPNav::update_spline()
{
    // exit immediately if this is not a spline segment
    if (_flags.segment_type != SEGMENT_SPLINE) {
        return;
    }

    // calculate dt
    uint32_t now = hal.scheduler->millis();
    float dt = (now - _wp_last_update) / 1000.0f;

    // reset step back to 0 if 0.1 seconds has passed and we completed the last full cycle
    if (dt >= WPNAV_WP_UPDATE_TIME) {
        // double check dt is reasonable
        if (dt >= 1.0f) {
            dt = 0.0;
        }
        // capture time since last iteration
        _wp_last_update = now;

        // advance the target if necessary
        advance_spline_target_along_track(dt);
        _pos_control.trigger_xy();
    }else{
        // run horizontal position controller
        _pos_control.update_xy_controller(false);
    }
}

/// update_spline_solution - recalculates hermite_spline_solution grid
///		relies on _spline_origin_vel, _spline_destination_vel and _origin and _destination
void AC_WPNav::update_spline_solution(const Vector3f& origin, const Vector3f& dest, const Vector3f& origin_vel, const Vector3f& dest_vel)
{
    _hermite_spline_solution[0] = origin;
    _hermite_spline_solution[1] = origin_vel;
    _hermite_spline_solution[2] = -origin*3.0f -origin_vel*2.0f + dest*3.0f - dest_vel;
    _hermite_spline_solution[3] = origin*2.0f + origin_vel -dest*2.0f + dest_vel;
 }

/// advance_spline_target_along_track - move target location along track from origin to destination
void AC_WPNav::advance_spline_target_along_track(float dt)
{
    if (!_flags.reached_destination) {
        Vector3f target_pos, target_vel;

        // update target position and velocity from spline calculator
        calc_spline_pos_vel(_spline_time, target_pos, target_vel);

        // update velocity
        float spline_dist_to_wp = (_destination - target_pos).length();

        // if within the stopping distance from destination, set target velocity to sqrt of distance * 2 * acceleration
        if (!_flags.fast_waypoint && spline_dist_to_wp < _spline_slow_down_dist) {
            _spline_vel_scaler = safe_sqrt(spline_dist_to_wp * 2.0f * _wp_accel_cms);
        }else if(_spline_vel_scaler < _wp_speed_cms) {
            // increase velocity using acceleration
        	// To-Do: replace 0.1f below with update frequency passed in from main program
            _spline_vel_scaler += _wp_accel_cms* 0.1f;
        }

        // constrain target velocity
        if (_spline_vel_scaler > _wp_speed_cms) {
            _spline_vel_scaler = _wp_speed_cms;
        }

        // scale the spline_time by the velocity we've calculated vs the velocity that came out of the spline calculator
        float spline_time_scale = _spline_vel_scaler/target_vel.length();

        // update target position
        _pos_control.set_pos_target(target_pos);

        // update the yaw
        _yaw = RadiansToCentiDegrees(atan2f(target_vel.y,target_vel.x));

        // advance spline time to next step
        _spline_time += spline_time_scale*dt;

        // we will reach the next waypoint in the next step so set reached_destination flag
        // To-Do: is this one step too early?
        if (_spline_time >= 1.0f) {
            _flags.reached_destination = true;
        }
    }
}

// calc_spline_pos_vel_accel - calculates target position, velocity and acceleration for the given "spline_time"
/// 	relies on update_spline_solution being called when the segment's origin and destination were set
void AC_WPNav::calc_spline_pos_vel(float spline_time, Vector3f& position, Vector3f& velocity)
{
    float spline_time_sqrd = spline_time * spline_time;
    float spline_time_cubed = spline_time_sqrd * spline_time;

    position = _hermite_spline_solution[0] + \
               _hermite_spline_solution[1] * spline_time + \
               _hermite_spline_solution[2] * spline_time_sqrd + \
               _hermite_spline_solution[3] * spline_time_cubed;

    velocity = _hermite_spline_solution[1] + \
               _hermite_spline_solution[2] * 2.0f * spline_time + \
               _hermite_spline_solution[3] * 3.0f * spline_time_sqrd;
}


///
/// shared methods
///

// get_bearing_cd - return bearing in centi-degrees between two positions
// To-Do: move this to math library
float AC_WPNav::get_bearing_cd(const Vector3f &origin, const Vector3f &destination) const
{
    float bearing = 9000 + atan2f(-(destination.x-origin.x), destination.y-origin.y) * 5729.57795f;
    if (bearing < 0) {
        bearing += 36000;
    }
    return bearing;
}

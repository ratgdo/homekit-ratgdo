// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License

void setup_homekit();

void homekit_loop();

void notify_homekit_target_door_state_change();
void notify_homekit_current_door_state_change();
void notify_homekit_active();
void notify_homekit_light();
void notify_homekit_motion();

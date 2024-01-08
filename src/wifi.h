// Copyright 2023 Brandon Matthews <thenewwazoo@optimaltour.us>
// All rights reserved. GPLv3 License
#pragma once

void wifi_task_entry(void* ctx);

enum class WifiStatus {
    Disconnected,
    Pending,
    Connected,
};

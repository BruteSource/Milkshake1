#pragma once

namespace wifi {

// Blocks until connected or timeoutMs elapses. Returns true if connected.
bool connect(unsigned long timeoutMs = 15000);

}  // namespace wifi

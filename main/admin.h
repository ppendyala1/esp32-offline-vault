#pragma once

#include "../core/vault.h"

// Run the USB command interface.
// Commands will include init, unlock, add, list, delete, and lock.
void run_admin(vault::Vault& manager);
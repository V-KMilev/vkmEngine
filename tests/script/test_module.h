#pragma once

#include "system/script/module_entry.h"

/**
 * @brief Count one more call against this copy of the test module's static.
 *
 * Lets the script suite ask whether a reloaded copy's statics are its own.
 *
 * @return The count, this call included.
 */
VKM_MODULE_ENTRY
int vkmTestCountLoad();

#pragma once

// Identity of the plug-in. Keep the FUIDs as they are: hosts use them to find the plug-in and restore
// saved projects.

#define RABIDPHATT_NAME "RabidPhatt"
#define RABIDPHATT_VENDOR "Hadronyche"
#define RABIDPHATT_URL ""
#define RABIDPHATT_EMAIL ""

// Class IDs, passed to Steinberg::FUID (a, b, c, d)
#define RABIDPHATT_PROCESSOR_UID 0x20183AE4, 0x631D4CAB, 0xB004BC97, 0x089F42FE
#define RABIDPHATT_CONTROLLER_UID 0x035E7E78, 0xF5134DE5, 0xADDAB440, 0x895A9716

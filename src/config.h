#pragma once

// Vitesse : chiffre '0'..'9' envoyé au robot → (niveau + 1) × SpeedPerLevel (40..400).
// Doit correspondre à SPEED_PER_LEVEL / SPEED_LEVEL_DEFAULT dans le firmware.
inline constexpr int SpeedLevels = 10;
inline constexpr int SpeedPerLevel = 40;
inline constexpr int SpeedLevelDefault = 4;

inline constexpr char RobotName[] = "BatMobile";
inline constexpr char RobotPin[] = "0000";
inline constexpr int RfcommChannel = 1;  // canal SPP du Bluetooth Bee
inline constexpr char Stop = ' ';

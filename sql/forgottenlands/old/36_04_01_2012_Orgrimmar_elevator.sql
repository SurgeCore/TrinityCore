UPDATE `gameobject_template` SET `flags`=40 WHERE `entry` IN (206609, 206610, 206608, 196837, 207889, 152614);
DELETE FROM `gameobject` WHERE `guid` IN (884, 47997, 886, 48000, 885, 48001, 875, 48005, 878, 48004);
INSERT INTO `gameobject` (`guid`, `id`, `map`, `spawnMask`, `phaseMask`, `position_x`, `position_y`, `position_z`, `orientation`, `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecs`, `animprogress`, `state`) VALUES
(884, 206609, 1, 1, 1, 1902.046, -4373.103, 43.99677, 5.707206, 0, 0, 0.9563048, -0.2923717, 120, 255, 1),
(886, 206610, 1, 1, 1, 1755.314, -4396.597, 42.34779, 3.744939, 0, 0, 0.9563048, -0.2923717, 120, 255, 1),
(885, 206608, 1, 1, 1, 1704.782, -4265.96, 34.88367, 3.976283, 0, 0, 0.9563048, -0.2923717, 120, 255, 1),
(875, 196837, 1, 1, 1, 3218.322, -4502.832, 266.3751, 1.570796, 0, 0, 0.7071069, 0.7071066, 120, 255, 1),
(878, 207889, 1, 1, 1, 2936.059, -4993.609, 126.7327, 6.051209, 0, 0, 1, -4.371139E-08, 120, 255, 1),
(47997, 152614, 1, 1, 1, 2263.702, -5565.56, 33.93858, 5.183629, 0, 0, -0.5224984, 0.8526402, 120, 255, 1);
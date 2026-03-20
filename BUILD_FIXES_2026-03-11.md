Build fixes applied after user compile log:
- Added missing bat_meas_fresh() declaration to src/bms/battery.h
- Removed macro name collision between bms_soh.h and bms_ekf.h by renaming SOH R0 limits to SOH_R0_*
- Updated battery.c and bms_soh.c to use SOH_R0_* constants

These changes fix the two hard compile errors shown in the log and remove the warning storm caused by macro redefinition.

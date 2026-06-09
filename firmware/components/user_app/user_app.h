#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void UserApp_AppInit(void);

// build LVGL screen (must hold Lvgl_lock when called)
void UserApp_UiInit(void);

void UserApp_TaskInit(void);

#ifdef __cplusplus
}
#endif

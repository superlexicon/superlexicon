#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void superlexicon_supervisor_init(bool enable_daemons, const char *self_exe_path);
void superlexicon_supervisor_poll(void);
void superlexicon_supervisor_shutdown_all(void);

int superlexicon_supervisor_restart_radio(void);
int superlexicon_supervisor_restart_node(void);
int superlexicon_supervisor_get_radio_pid(void);
int superlexicon_supervisor_get_node_pid(void);

#ifdef __cplusplus
}
#endif

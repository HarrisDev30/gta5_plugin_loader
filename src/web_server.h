#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start the embedded HTTP web dashboard daemon thread on the specified port (e.g. 8050)
int start_web_server(int port);

// Stop the web server
void stop_web_server(void);

// Update status displayed on web dashboard
void set_web_server_game_status(int running, pid_t pid, const char *title_id);

// Append a line to the in-memory live log buffer
void append_live_log(const char *msg);

#ifdef __cplusplus
}
#endif

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * Start the HTTP server and register all API + static-file routes.
 * WebSocket initialisation (ws_init) is called internally.
 * Returns the server handle, or NULL on failure.
 */
httpd_handle_t api_http_start(void);

/** Stop the HTTP server. */
void api_http_stop(httpd_handle_t server);

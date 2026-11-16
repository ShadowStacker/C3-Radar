#pragma once

#include <WebServer.h>

/** Device web page: live copy of the round display plus all settings. */
namespace services::web {

/**
 * Add the page and its API to the Wi-Fi manager's web server. Must run before
 * the manager registers its own handlers so "/" resolves to the radar page.
 */
void registerRoutes(WebServer& server);

/** True once after the port or device name changed: the server must be restarted. */
bool consumeRestartRequest();

/** True while a firmware upload is being written (pause other network work). */
bool updateInProgress();
/** True after a successful firmware upload: restart the device. */
bool rebootPending();

}  // namespace services::web

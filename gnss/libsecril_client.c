/*
 * Minimal 64-bit libsecril-client.so shim for Broadcom gpsd/libwrappergps.so.
 *
 * The ROM ships only the 32-bit libsecril-client.so, while /vendor/bin/hw/gpsd
 * is 64-bit and links libwrappergps.so -> libsecril-client.so for optional
 * Control-Plane AGPS OEM RIL hooks. Returning disconnected lets gpsd run SUPL
 * and standalone BCM4773 GNSS cleanly without crashing on missing symbols.
 */
#include <stddef.h>
#include <stdint.h>

typedef void* HRilClient;
static int gDummyRilClient = 1;

HRilClient OpenClient_RILD(void) { return (HRilClient)&gDummyRilClient; }
int CloseClient_RILD(HRilClient client) { (void)client; return 0; }
int Connect_RILD(HRilClient client) { (void)client; return 0; }
int Disconnect_RILD(HRilClient client) { (void)client; return 0; }
int isConnected_RILD(HRilClient client) { (void)client; return 1; }
int RegisterErrorCallback(HRilClient client, void* fn, void* data) {
    (void)client; (void)fn; (void)data; return 0;
}
int RegisterRequestCompleteHandler(HRilClient client, uint32_t id, void* fn) {
    (void)client; (void)id; (void)fn; return 0;
}
int RegisterUnsolicitedHandler(HRilClient client, uint32_t id, void* fn) {
    (void)client; (void)id; (void)fn; return 0;
}
int InvokeOemRequestHookRaw(HRilClient client, char* data, size_t len) {
    (void)client; (void)data; (void)len; return 0;
}

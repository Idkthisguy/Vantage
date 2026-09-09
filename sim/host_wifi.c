#include "host_wifi.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wlanapi.h>

bool sim_host_wifi_get_current(char *ssid, size_t ssid_size) {
  if (!ssid || ssid_size == 0)
    return false;
  ssid[0] = '\0';

  HANDLE client = NULL;
  DWORD negotiated_version = 0;
  if (WlanOpenHandle(2, NULL, &negotiated_version, &client) != ERROR_SUCCESS)
    return false;

  PWLAN_INTERFACE_INFO_LIST interfaces = NULL;
  if (WlanEnumInterfaces(client, NULL, &interfaces) != ERROR_SUCCESS) {
    WlanCloseHandle(client, NULL);
    return false;
  }

  /* Windows makes asking one tiny Wi-Fi question feel like an explosive diarhee.
   * Keep the matching WlanFreeMemory calls. */
  bool connected = false;
  for (DWORD index = 0; index < interfaces->dwNumberOfItems && !connected;
       ++index) {
    WLAN_CONNECTION_ATTRIBUTES *connection = NULL;
    DWORD bytes = 0;
    WLAN_OPCODE_VALUE_TYPE opcode_type;
    DWORD result = WlanQueryInterface(
        client, &interfaces->InterfaceInfo[index].InterfaceGuid,
        wlan_intf_opcode_current_connection, NULL, &bytes, (PVOID *)&connection,
        &opcode_type);
    if (result != ERROR_SUCCESS || !connection)
      continue;

    if (connection->isState == wlan_interface_state_connected) {
      size_t length = connection->wlanAssociationAttributes.dot11Ssid.uSSIDLength;
      if (length >= ssid_size)
        length = ssid_size - 1;
      memcpy(ssid, connection->wlanAssociationAttributes.dot11Ssid.ucSSID,
             length);
      ssid[length] = '\0';
      connected = length > 0;
    }
    WlanFreeMemory(connection);
  }

  WlanFreeMemory(interfaces);
  WlanCloseHandle(client, NULL);
  return connected;
}
#else
bool sim_host_wifi_get_current(char *ssid, size_t ssid_size) {
  if (ssid && ssid_size > 0)
    ssid[0] = '\0';
  return false;
}
#endif

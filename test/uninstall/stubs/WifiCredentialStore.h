#pragma once
#include <optional>
#include <string>
struct FakeCredential {
  std::string ssid, password;
};
struct FakeWifiStore {
  unsigned getCredentialCount() const { return 0; }
  void loadFromFile() {}
  const char* getLastConnectedSsid() const { return ""; }
  std::optional<FakeCredential> findCredential(const char*) const { return {}; }
};
inline FakeWifiStore WIFI_STORE;

// Copyright 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BUFFET_MANAGER_H_
#define BUFFET_MANAGER_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/values.h>
#include <brillo/dbus/data_serialization.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_property_set.h>
#include <brillo/errors/error.h>
#include <weave/device.h>

#include "buffet/buffet_config.h"
#include "buffet/dbus_bindings/com.android.Weave.Manager.h"

namespace brillo {
namespace dbus_utils {
class ExportedObjectManager;
}  // namespace dbus_utils
}  // namespace chromeos

namespace buffet {

class BluetoothClient;
class DBusCommandDispacher;
class HttpTransportClient;
class MdnsClient;
class ShillClient;
class WebServClient;

template<typename... Types>
using DBusMethodResponsePtr =
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<Types...>>;

template<typename... Types>
using DBusMethodResponse =
    brillo::dbus_utils::DBusMethodResponse<Types...>;

// The Manager is responsible for global state of Buffet.  It exposes
// interfaces which affect the entire device such as device registration and
// device state.
class Manager final : public com::android::Weave::ManagerInterface {
 public:
  struct Options {
    bool xmpp_enabled = true;
    bool disable_privet = false;
    bool enable_ping = false;
    std::set<std::string> device_whitelist;

    BuffetConfig::Options config_options;
  };

  explicit Manager(
      const Options& options,
      const base::WeakPtr<brillo::dbus_utils::ExportedObjectManager>&
          object_manager);
  ~Manager();

  void Start(brillo::dbus_utils::AsyncEventSequencer* sequencer);

  void Stop();

 private:
  void RestartWeave(brillo::dbus_utils::AsyncEventSequencer* sequencer);
  void CreateDevice();

  // DBus methods:
  void RegisterDevice(DBusMethodResponsePtr<std::string> response,
                      const std::string& ticket_id) override;
  void AddComponent(DBusMethodResponsePtr<> response,
                    const std::string& name,
                    const std::vector<std::string>& traits) override;
  void UpdateState(DBusMethodResponsePtr<> response,
                   const std::string& component,
                   const brillo::VariantDictionary& property_set) override;
  bool GetState(brillo::ErrorPtr* error, std::string* state) override;
  void AddCommand(DBusMethodResponsePtr<std::string> response,
                  const std::string& json_command) override;
  std::string TestMethod(const std::string& message) override;

  void StartPrivet(const Options& options,
                   brillo::dbus_utils::AsyncEventSequencer* sequencer);

  void OnStateChanged();
  void OnGcdStateChanged(weave::GcdState state);
  void OnConfigChanged(const weave::Settings& settings);
  void OnPairingStart(const std::string& session_id,
                      weave::PairingType pairing_type,
                      const std::vector<uint8_t>& code);
  void OnPairingEnd(const std::string& session_id);

  void RegisterDeviceDone(DBusMethodResponsePtr<std::string> response,
                          weave::ErrorPtr error);

  Options options_;

  com::android::Weave::ManagerAdaptor dbus_adaptor_{this};
  brillo::dbus_utils::DBusObject dbus_object_;

  class TaskRunner;
  std::unique_ptr<TaskRunner> task_runner_;
  std::unique_ptr<BluetoothClient> bluetooth_client_;
  std::unique_ptr<BuffetConfig> config_;
  std::unique_ptr<HttpTransportClient> http_client_;
  std::unique_ptr<ShillClient> shill_client_;
  std::unique_ptr<MdnsClient> mdns_client_;
  std::unique_ptr<WebServClient> web_serv_client_;
  std::unique_ptr<weave::Device> device_;
  std::unique_ptr<DBusCommandDispacher> command_dispatcher_;
  brillo::dbus_utils::AsyncEventSequencer::Handler dbus_registration_handler_;

  base::WeakPtrFactory<Manager> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace buffet

#endif  // BUFFET_MANAGER_H_

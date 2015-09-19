//
//  Copyright (C) 2015 Google, Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at:
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//

#include <base/macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "service/hal/fake_bluetooth_gatt_interface.h"
#include "service/low_energy_client.h"

using ::testing::_;
using ::testing::Return;

namespace bluetooth {
namespace {

class MockGattHandler : public hal::FakeBluetoothGattInterface::TestHandler {
 public:
  MockGattHandler() = default;
  ~MockGattHandler() override = default;

  MOCK_METHOD1(RegisterClient, bt_status_t(bt_uuid_t*));
  MOCK_METHOD1(UnregisterClient, bt_status_t(int));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockGattHandler);
};

class LowEnergyClientTest : public ::testing::Test {
 public:
  LowEnergyClientTest() = default;
  ~LowEnergyClientTest() override = default;

  void SetUp() override {
    mock_handler_.reset(new MockGattHandler());
    fake_hal_gatt_iface_ = new hal::FakeBluetoothGattInterface(
        std::dynamic_pointer_cast<hal::FakeBluetoothGattInterface::TestHandler>(
            mock_handler_));

    hal::BluetoothGattInterface::InitializeForTesting(fake_hal_gatt_iface_);
    ble_factory_.reset(new LowEnergyClientFactory());
  }

  void TearDown() override {
    ble_factory_.reset();
    hal::BluetoothGattInterface::CleanUp();
  }

 protected:
  hal::FakeBluetoothGattInterface* fake_hal_gatt_iface_;
  std::shared_ptr<MockGattHandler> mock_handler_;
  std::unique_ptr<LowEnergyClientFactory> ble_factory_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LowEnergyClientTest);
};

TEST_F(LowEnergyClientTest, RegisterClient) {
  EXPECT_CALL(*mock_handler_, RegisterClient(_))
      .Times(2)
      .WillOnce(Return(BT_STATUS_FAIL))
      .WillOnce(Return(BT_STATUS_SUCCESS));

  // These will be asynchronously populated with a result when the callback
  // executes.
  BLEStatus status = BLE_STATUS_SUCCESS;
  UUID cb_uuid;
  std::unique_ptr<LowEnergyClient> client;
  int callback_count = 0;

  LowEnergyClientFactory::ClientCallback callback =
      [&](BLEStatus in_status, const UUID& uuid,
          std::unique_ptr<LowEnergyClient> in_client) {
        status = in_status;
        cb_uuid = uuid;
        client = std::move(in_client);
        callback_count++;
      };

  UUID uuid0 = UUID::GetRandom();

  // HAL returns failure.
  EXPECT_FALSE(ble_factory_->RegisterClient(uuid0, callback));
  EXPECT_EQ(0, callback_count);

  // HAL returns success.
  EXPECT_TRUE(ble_factory_->RegisterClient(uuid0, callback));
  EXPECT_EQ(0, callback_count);

  // Calling twice with the same UUID should fail with no additional call into
  // the stack.
  EXPECT_FALSE(ble_factory_->RegisterClient(uuid0, callback));

  testing::Mock::VerifyAndClearExpectations(mock_handler_.get());

  // Call with a different UUID while one is pending.
  UUID uuid1 = UUID::GetRandom();
  EXPECT_CALL(*mock_handler_, RegisterClient(_))
      .Times(1)
      .WillOnce(Return(BT_STATUS_SUCCESS));
  EXPECT_TRUE(ble_factory_->RegisterClient(uuid1, callback));

  // Trigger callback with an unknown UUID. This should get ignored.
  UUID uuid2 = UUID::GetRandom();
  bt_uuid_t hal_uuid = uuid2.GetBlueDroid();
  fake_hal_gatt_iface_->NotifyRegisterClientCallback(0, 0, hal_uuid);
  EXPECT_EQ(0, callback_count);

  // |uuid0| succeeds.
  int client_if0 = 2;  // Pick something that's not 0.
  hal_uuid = uuid0.GetBlueDroid();
  fake_hal_gatt_iface_->NotifyRegisterClientCallback(
      BT_STATUS_SUCCESS, client_if0, hal_uuid);

  EXPECT_EQ(1, callback_count);
  ASSERT_TRUE(client.get() != nullptr);  // Assert to terminate in case of error
  EXPECT_EQ(BLE_STATUS_SUCCESS, status);
  EXPECT_EQ(client_if0, client->client_if());
  EXPECT_EQ(uuid0, client->app_identifier());
  EXPECT_EQ(uuid0, cb_uuid);

  // The client should unregister itself when deleted.
  EXPECT_CALL(*mock_handler_, UnregisterClient(client_if0))
      .Times(1)
      .WillOnce(Return(BT_STATUS_SUCCESS));
  client.reset();

  testing::Mock::VerifyAndClearExpectations(mock_handler_.get());

  // |uuid1| fails.
  int client_if1 = 3;
  hal_uuid = uuid1.GetBlueDroid();
  fake_hal_gatt_iface_->NotifyRegisterClientCallback(
      BT_STATUS_FAIL, client_if1, hal_uuid);

  EXPECT_EQ(2, callback_count);
  ASSERT_TRUE(client.get() == nullptr);  // Assert to terminate in case of error
  EXPECT_EQ(BLE_STATUS_FAILURE, status);
  EXPECT_EQ(uuid1, cb_uuid);

}

}  // namespace
}  // namespace bluetooth
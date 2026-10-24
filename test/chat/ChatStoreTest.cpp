#include <HalStorage.h>
#include <gtest/gtest.h>

#include "activities/apps/chat/ChatStore.h"

using namespace chat;
constexpr auto LOG_PATH = "/.crosspoint/rf_chat/messages-v1.txt";
class ChatStoreTest : public testing::Test {
 protected:
  void SetUp() override { Storage = {}; }
};
TEST_F(ChatStoreTest, TornTailCannotSwallowNextValidRecord) {
  Storage.files[LOG_PATH] = "\n*|00000001|0|first\n*|0000";
  Store store(Kind::Radio);
  Message message;
  std::strcpy(message.peer, "*");
  std::strcpy(message.text, "next|text");
  message.id = 2;
  ASSERT_TRUE(store.append(message));
  Snapshot restored;
  ASSERT_TRUE(store.load(restored));
  ASSERT_EQ(restored.messageCount, 2);
  EXPECT_STREQ(restored.messages[0].text, "first");
  EXPECT_STREQ(restored.messages[1].text, "next|text");
}
TEST_F(ChatStoreTest, DeliveryUpdatesCoalesceButIncomingMessagesDoNot) {
  Storage.files[LOG_PATH] =
      "\n*|00000001|1|outgoing\n*|00000001|3|outgoing\n*|00000001|0|incoming\n*|00000002|1|pending\n";
  Store store(Kind::Radio);
  Snapshot restored;
  ASSERT_TRUE(store.load(restored));
  ASSERT_EQ(restored.messageCount, 3);
  EXPECT_EQ(restored.messages[0].delivery, Delivery::Delivered);
  EXPECT_EQ(restored.messages[1].delivery, Delivery::Received);
  EXPECT_EQ(restored.messages[2].delivery, Delivery::Failed);
}
TEST_F(ChatStoreTest, InvalidRecordsSkippedAndWindowBounded) {
  Storage.files[LOG_PATH] = "\ninvalid|00000001|0|bad\n*|xyz|0|bad\n*|00000002|5|bad\n";
  Store store(Kind::Radio);
  Message message;
  std::strcpy(message.peer, "*");
  std::strcpy(message.text, "hello");
  for (uint32_t id = 1; id <= 40; ++id) {
    message.id = id;
    ASSERT_TRUE(store.append(message));
  }
  Snapshot restored;
  ASSERT_TRUE(store.load(restored));
  ASSERT_EQ(restored.messageCount, MAX_MESSAGES);
  EXPECT_EQ(restored.messages[0].id, 17u);
  EXPECT_EQ(restored.messages[MAX_MESSAGES - 1].id, 40u);
}
TEST_F(ChatStoreTest, RotationAndShortWritesReported) {
  Storage.files[LOG_PATH] = std::string(128 * 1024, '\n');
  Store store(Kind::Radio);
  Message message;
  std::strcpy(message.peer, "*");
  std::strcpy(message.text, "new log");
  ASSERT_TRUE(store.append(message));
  EXPECT_TRUE(Storage.exists("/.crosspoint/rf_chat/messages-v1.previous.txt"));
  EXPECT_LT(Storage.files[LOG_PATH].size(), 384u);
  Storage.failWrites = true;
  EXPECT_FALSE(store.append(message));
}
TEST_F(ChatStoreTest, EspRoomsPreserveSenderAndReceiptCountWithoutChangingRfLogs) {
  Store store(Kind::EspNow);
  Message message;
  std::strcpy(message.peer, "G000123");
  std::strcpy(message.sender, "112233445566");
  std::strcpy(message.text, "你好|ESP");
  message.id = 7;
  message.delivery = Delivery::Pending;
  ASSERT_TRUE(store.append(message));
  message.delivery = Delivery::Delivered;
  message.receipts = 2;
  ASSERT_TRUE(store.append(message));
  message.delivery = Delivery::Received;
  std::strcpy(message.sender, "223344556677");
  message.receipts = 0;
  ASSERT_TRUE(store.append(message));
  Snapshot restored;
  ASSERT_TRUE(store.load(restored));
  ASSERT_EQ(restored.messageCount, 2);
  EXPECT_STREQ(restored.messages[0].sender, "112233445566");
  EXPECT_STREQ(restored.messages[0].text, "你好|ESP");
  EXPECT_EQ(restored.messages[0].receipts, 2);
  EXPECT_EQ(restored.messages[0].delivery, Delivery::Delivered);
  EXPECT_STREQ(restored.messages[1].sender, "223344556677");
  EXPECT_FALSE(Storage.exists(LOG_PATH));
}
TEST_F(ChatStoreTest, EspMalformedMetadataAndTornTailDoNotHideFollowingRecord) {
  constexpr auto espPath = "/.crosspoint/esp_chat/messages-v2.txt";
  Storage.files[espPath] =
      "\nG000000|1|0|112233445566|0|bad group\n"
      "*|1|0|11223344556Z|0|bad sender\n"
      "*|1|0|112233445566|9|bad receipts\n"
      "*|1|0|112233445566|00|bad receipts\n"
      "*|1|0|112233445566|";
  Store store(Kind::EspNow);
  Message message;
  std::strcpy(message.peer, "*");
  std::strcpy(message.sender, "112233445566");
  std::strcpy(message.text, "good");
  message.id = 8;
  ASSERT_TRUE(store.append(message));
  Snapshot restored;
  ASSERT_TRUE(store.load(restored));
  ASSERT_EQ(restored.messageCount, 1);
  EXPECT_EQ(restored.messages[0].id, 8u);
}

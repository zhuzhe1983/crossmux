#include <EspTestSession.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "activities/apps/chat/EspNowBackend.h"

using namespace chat;
namespace {
espchat::Packet textPacket(uint32_t room = 0) {
  espchat::Packet packet;
  packet.type = espchat::Type::Text;
  packet.sender = 0x223344556677ULL;
  packet.session = 42;
  packet.sequence = 5;
  packet.room = room;
  std::strcpy(packet.text, "你好|ESP");
  return packet;
}
void inject(const espchat::Packet& packet, uint64_t source = 0) {
  HalEspNow::Frame frame;
  frame.length = espchat::encode(packet, frame.data, sizeof(frame.data));
  ASSERT_GT(frame.length, 0);
  frame.sender = source ? source : packet.sender;
  HalEspNow::incoming.push_back(frame);
}
size_t sentCount(espchat::Type type) {
  size_t count = 0;
  for (const auto& bytes : HalEspNow::outgoing) {
    espchat::Packet packet;
    if (espchat::decode(bytes.data(), bytes.size(), packet) && packet.type == type) ++count;
  }
  return count;
}
}  // namespace
TEST(EspProtocol, MaxUtf8PacketRoundTripsAtUnalignedAddress) {
  auto packet = textPacket(999999);
  for (size_t i = 0; i < MAX_TEXT; i += 3) std::memcpy(packet.text + i, "纸", 3);
  std::array<uint8_t, 251> wire{};
  const auto size = espchat::encode(packet, wire.data() + 1, 250);
  ASSERT_EQ(size, 224u);
  espchat::Packet restored;
  ASSERT_TRUE(espchat::decode(wire.data() + 1, size, restored));
  EXPECT_EQ(restored.room, 999999u);
  EXPECT_EQ(restored.sender, packet.sender);
  EXPECT_STREQ(restored.text, packet.text);
  EXPECT_EQ(espchat::encode(packet, wire.data(), size - 1), 0u);
  for (size_t n = 0; n < espchat::HEADER; ++n) EXPECT_FALSE(espchat::decode(wire.data() + 1, n, restored));
  wire[3] = 99;
  EXPECT_FALSE(espchat::decode(wire.data() + 1, size, restored));
}
TEST(EspProtocol, RejectsInvalidIdentityRoomsAndControlPayloads) {
  uint8_t wire[250]{};
  auto packet = textPacket();
  packet.sender = 1ULL << 48;
  EXPECT_EQ(espchat::encode(packet, wire, sizeof(wire)), 0u);
  packet = textPacket(1000000);
  EXPECT_EQ(espchat::encode(packet, wire, sizeof(wire)), 0u);
  packet = textPacket();
  packet.type = espchat::Type::Ack;
  packet.recipient = 7;
  EXPECT_EQ(espchat::encode(packet, wire, sizeof(wire)), 0u);
  packet.text[0] = 0;
  EXPECT_EQ(espchat::encode(packet, wire, sizeof(wire)), espchat::HEADER);
  for (auto room : {"G000000", "000001", "G123", "G12345x", "G1000000", ""}) EXPECT_FALSE(espchat::validRoom(room));
  EXPECT_TRUE(espchat::validRoom("G000001"));
  EXPECT_TRUE(espchat::validRoom("*"));
  EXPECT_FALSE(espchat::validRoom(nullptr));
}
class EspBackendTest : public testing::Test {
 protected:
  Session session;
  std::unique_ptr<EspNowBackend> backend;
  void SetUp() override {
    Storage = {};
    HalEspNow::reset();
    backend = std::make_unique<EspNowBackend>(session);
    ASSERT_TRUE(backend->begin());
  }
  void TearDown() override { backend.reset(); }
  void runUntil(uint32_t start, uint32_t finish) {
    for (uint32_t now = start; now <= finish; now += 10) backend->tick(now);
  }
};
TEST_F(EspBackendTest, PublicDeliveryRequiresStorageAndDuplicatesOnlyRepeatAck) {
  auto packet = textPacket();
  session.storageWorks = false;
  inject(packet);
  runUntil(0, 500);
  EXPECT_TRUE(session.receivedMessages.empty());
  EXPECT_EQ(sentCount(espchat::Type::Ack), 0u);
  session.storageWorks = true;
  inject(packet);
  runUntil(510, 1000);
  ASSERT_EQ(session.receivedMessages.size(), 1u);
  EXPECT_STREQ(session.receivedMessages[0].sender, "223344556677");
  EXPECT_EQ(sentCount(espchat::Type::Ack), 1u);
  inject(packet);
  runUntil(1010, 1500);
  EXPECT_EQ(session.receivedMessages.size(), 1u);
  EXPECT_EQ(sentCount(espchat::Type::Ack), 2u);
}
TEST_F(EspBackendTest, FiltersOtherRoomsAndMismatchedSourceMac) {
  ASSERT_EQ(backend->join("123"), AddResult::Accepted);
  inject(textPacket(124));
  inject(textPacket(123), 0x334455667788ULL);
  runUntil(0, 500);
  EXPECT_TRUE(session.receivedMessages.empty());
  inject(textPacket(123));
  runUntil(510, 1000);
  ASSERT_EQ(session.receivedMessages.size(), 1u);
  EXPECT_STREQ(session.receivedMessages[0].peer, "G000123");
}
TEST_F(EspBackendTest, GroupTimeoutFailsAfterThreeAttemptsAndCanRetry) {
  ASSERT_EQ(backend->join("123"), AddResult::Accepted);
  ASSERT_TRUE(backend->send("G000123", "hello", 9));
  EXPECT_FALSE(backend->send("*", "busy", 10));
  runUntil(0, 6000);
  EXPECT_EQ(sentCount(espchat::Type::Text), 3u);
  ASSERT_FALSE(session.deliveries.empty());
  EXPECT_EQ(session.deliveries.back(), Delivery::Failed);
  EXPECT_FALSE(backend->busy());
  EXPECT_TRUE(backend->send("G000123", "hello", 9));
}
TEST_F(EspBackendTest, PublicSentDoesNotClaimReceipt) {
  ASSERT_TRUE(backend->send("*", "hello", 9));
  runUntil(0, 2000);
  EXPECT_EQ(sentCount(espchat::Type::Text), 1u);
  ASSERT_EQ(session.deliveries.size(), 1u);
  EXPECT_EQ(session.deliveries.back(), Delivery::Sent);
  EXPECT_EQ(session.receipts, 0);
}
TEST_F(EspBackendTest, ReceiptsMatchOriginalSessionRoomRecipientAndDeduplicateDevices) {
  ASSERT_EQ(backend->join("123"), AddResult::Accepted);
  ASSERT_TRUE(backend->send("G000123", "hello", 9));
  backend->tick(0);
  espchat::Packet sent;
  ASSERT_TRUE(espchat::decode(HalEspNow::outgoing[0].data(), HalEspNow::outgoing[0].size(), sent));
  espchat::Packet ack;
  ack.type = espchat::Type::Ack;
  ack.room = sent.room;
  ack.sender = 0x223344556677ULL;
  ack.recipient = sent.sender;
  ack.session = sent.session;
  ack.sequence = sent.sequence;
  auto wrong = ack;
  ++wrong.session;
  inject(wrong);
  wrong = ack;
  ++wrong.room;
  inject(wrong);
  wrong = ack;
  ++wrong.recipient;
  inject(wrong);
  wrong = ack;
  ++wrong.sequence;
  inject(wrong);
  backend->tick(10);
  EXPECT_EQ(session.receipts, 0);
  inject(ack);
  inject(ack);
  ++ack.sender;
  inject(ack);
  backend->tick(20);
  EXPECT_EQ(session.receipts, 2);
  runUntil(30, 6000);
  ASSERT_EQ(session.deliveries.size(), 1u);
  EXPECT_EQ(session.deliveries.back(), Delivery::Delivered);
}
TEST_F(EspBackendTest, ChannelLossFailsPendingAndRecoversTransport) {
  ASSERT_TRUE(backend->send("*", "hello", 9));
  backend->tick(0);
  HalEspNow::testChannel = 0;
  backend->tick(10);
  EXPECT_FALSE(backend->busy());
  EXPECT_EQ(session.state.status, Status::Connecting);
  EXPECT_EQ(session.deliveries.back(), Delivery::Failed);
  HalEspNow::testChannel = 11;
  backend->tick(20);
  EXPECT_EQ(session.state.status, Status::Online);
  EXPECT_EQ(session.state.channel, 11);
  EXPECT_EQ(HalEspNow::begins, 2u);
}
TEST_F(EspBackendTest, MissingMacCallbackDoesNotStallWorker) {
  HalEspNow::nextResult = HalEspNow::TxResult::Pending;
  ASSERT_TRUE(backend->send("*", "hello", 9));
  runUntil(0, 2100);
  EXPECT_FALSE(backend->busy());
  EXPECT_EQ(session.deliveries.back(), Delivery::Failed);
  EXPECT_GT(HalEspNow::ends, 0u);
}
TEST_F(EspBackendTest, GroupMembershipPersistsAtomicallyAndIsBounded) {
  EXPECT_EQ(backend->join("0"), AddResult::InvalidId);
  EXPECT_EQ(backend->join("123"), AddResult::Accepted);
  EXPECT_EQ(backend->join("000123"), AddResult::Existing);
  Storage.failWrites = true;
  EXPECT_EQ(backend->join("124"), AddResult::StorageError);
  Storage.failWrites = false;
  backend.reset();
  session = {};
  backend = std::make_unique<EspNowBackend>(session);
  ASSERT_TRUE(backend->begin());
  ASSERT_EQ(session.state.contactCount, 1);
  EXPECT_STREQ(session.state.contacts[0].id, "G000123");
  EXPECT_FALSE(backend->send("G000124", "not joined", 1));
  for (unsigned i = 1; i <= 7; ++i) {
    char room[8];
    std::snprintf(room, sizeof(room), "%u", i);
    EXPECT_EQ(backend->join(room), AddResult::Accepted);
  }
  EXPECT_EQ(backend->join("999"), AddResult::Limit);
}
